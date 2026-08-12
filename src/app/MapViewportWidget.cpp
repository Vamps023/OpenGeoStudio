// MapViewportWidget - MapLibre Native Qt map widget implementation

#include "MapViewportWidget.hpp"

#include <QVBoxLayout>
#include <QDebug>
#include <QTimer>

// Esri World Imagery raster style JSON (inline, no external style URL)
// Mirrors the reference app's SkiaViewport.tsx source configuration.
// NOTE: A background layer is required for raster tiles to render in
// MapLibre Native Qt (see maplibre/maplibre-native-qt#279).
static constexpr const char* kEsriImageryStyle = R"({
    "version": 8,
    "sources": {
        "esri-imagery": {
            "type": "raster",
            "tiles": [
                "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
            ],
            "tileSize": 256,
            "maxzoom": 19,
            "attribution": "Esri"
        }
    },
    "layers": [
        {
            "id": "background",
            "type": "background",
            "paint": { "background-color": "#000000" }
        },
        {
            "id": "esri-imagery",
            "type": "raster",
            "source": "esri-imagery",
            "minzoom": 0,
            "maxzoom": 22
        }
    ]
})";

// ============================================================
// StyleHttpServer - minimal HTTP server for serving style JSON
// ============================================================

StyleHttpServer::StyleHttpServer(const QByteArray& styleJson, QObject* parent)
    : QObject(parent), m_styleJson(styleJson) {
    if (m_server.listen(QHostAddress::LocalHost)) {
        m_port = m_server.serverPort();
        qDebug() << "[StyleHttpServer] Listening on port" << m_port;
        connect(&m_server, &QTcpServer::newConnection,
                this, &StyleHttpServer::onNewConnection);
    } else {
        qWarning() << "[StyleHttpServer] Failed to listen:" << m_server.errorString();
    }
}

QString StyleHttpServer::styleUrl() const {
    return QString("http://127.0.0.1:%1/style.json").arg(m_port);
}

void StyleHttpServer::onNewConnection() {
    while (auto* sock = m_server.nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, &StyleHttpServer::onReadyRead);
    }
}

void StyleHttpServer::onReadyRead() {
    auto* sock = qobject_cast<QTcpSocket*>(sender());
    if (!sock) return;

    // Read and discard the HTTP request (we always serve the same style)
    sock->readAll();

    // Build HTTP response with CORS headers (MapLibre may check CORS)
    QByteArray response;
    response.append("HTTP/1.1 200 OK\r\n");
    response.append("Content-Type: application/json\r\n");
    response.append("Access-Control-Allow-Origin: *\r\n");
    response.append("Content-Length: " + QByteArray::number(m_styleJson.size()) + "\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(m_styleJson);

    sock->write(response);
    sock->disconnectFromHost();
}

// ============================================================
// MapViewportWidget
// ============================================================

MapViewportWidget::MapViewportWidget(QWidget* parent)
    : QWidget(parent) {
    setupMap();
}

void MapViewportWidget::setupMap() {
    // Start a local HTTP server to serve the Esri style JSON.
    // MapLibre's network stack can't load file:// URLs, so we serve
    // the inline style JSON via http://127.0.0.1:<port>/style.json.
    // The style URL must be set in Settings BEFORE the MapWidget is
    // created, because the Map loads the style during initialize()
    // (the QRhiWidget lifecycle), and setting styleJson after init
    // via a timer results in a black screen.
    // Start a local HTTP server to serve the Esri style JSON.
    // MapLibre's network stack can't load file:// URLs, so we serve
    // the inline style JSON via http://127.0.0.1:<port>/style.json.
    m_styleServer = new StyleHttpServer(
        QByteArray(kEsriImageryStyle), this);
    const QString styleUrl = m_styleServer->styleUrl();
    qDebug() << "[MapViewport] Style server URL:" << styleUrl;

    // Configure MapLibre settings with Esri imagery style.
    // The reference Electron app centers on lat=18.52, lon=73.85 (Pune, India)
    // at zoom 15. We replicate that here for parity.
    QMapLibre::Styles styles;
    styles.emplace_back(styleUrl, "Esri World Imagery");

    QMapLibre::Settings settings;
    settings.setStyles(styles);
    settings.setDefaultCoordinate(QMapLibre::Coordinate(18.52, 73.85));
    settings.setDefaultZoom(15.0);

    m_mapWidget = new QMapLibre::MapWidget(settings);
    m_mapWidget->setParent(this);
    m_mapWidget->setMinimumSize(100, 100);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_mapWidget, 1);

    qDebug() << "[MapViewport] MapWidget created with Esri style URL in Settings";

    // Connect to mapChanged after the map is created (lazily during initialize)
    QTimer::singleShot(100, this, [this]() {
        if (auto* map = m_mapWidget->map()) {
            connect(map, &QMapLibre::Map::mapChanged,
                    this, &MapViewportWidget::onMapChanged);
        }
    });

    // Wire mouse events from the map widget -> coordinate signals
    connect(m_mapWidget, &QMapLibre::MapWidget::onMousePressEvent,
            this, [this](QMapLibre::Coordinate coord) {
                emit mapClicked(coord.first, coord.second);
            });
}

QMapLibre::Map* MapViewportWidget::map() const {
    return m_mapWidget ? m_mapWidget->map() : nullptr;
}

void MapViewportWidget::setCenter(double lat, double lon) {
    if (auto* m = map()) {
        m->setCoordinate(QMapLibre::Coordinate(lat, lon));
    }
}

void MapViewportWidget::setZoom(double zoom) {
    if (auto* m = map()) {
        m->setZoom(zoom);
    }
}

void MapViewportWidget::fitBounds(double minLat, double minLon,
                                   double maxLat, double maxLon) {
    if (auto* m = map()) {
        const auto sw = QMapLibre::Coordinate(minLat, minLon);
        const auto ne = QMapLibre::Coordinate(maxLat, maxLon);
        const auto cz = m->coordinateZoomForBounds(sw, ne);
        m->setCoordinateZoom(cz.first, cz.second);
    }
}

void MapViewportWidget::onMapChanged(QMapLibre::Map::MapChange change) {
    switch (change) {
        case QMapLibre::Map::MapChangeDidFinishLoadingMap:
            qDebug() << "[MapViewport] Map loaded successfully";
            break;
        case QMapLibre::Map::MapChangeDidFailLoadingMap:
            qWarning() << "[MapViewport] Map failed to load";
            break;
        case QMapLibre::Map::MapChangeDidFinishLoadingStyle:
            qDebug() << "[MapViewport] Style loaded successfully";
            break;
        case QMapLibre::Map::MapChangeRegionDidChange:
            emit mapMoved();
            break;
        default:
            break;
    }
}
