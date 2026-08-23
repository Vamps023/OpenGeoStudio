// MapViewportWidget - MapLibre Native Qt map widget implementation

#include "MapViewportWidget.hpp"

#include <QVBoxLayout>
#include <QSettings>
#include "../core/logger/Logger.hpp"
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
        appLog().info("[StyleHttpServer] Listening on port", m_port);
        connect(&m_server, &QTcpServer::newConnection,
                this, &StyleHttpServer::onNewConnection);
    } else {
        appLog().warn("[StyleHttpServer] Failed to listen:", m_server.errorString());
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
    appLog().info("[MapViewport] Style server URL:", styleUrl);

    // Configure MapLibre settings with Esri imagery style.
    // Default coordinate/zoom are persisted via QSettings so the map reopens
    // at the user's last position. First launch falls back to a neutral
    // world overview (lat 0, lon 0, zoom 2).
    QMapLibre::Styles styles;
    styles.emplace_back(styleUrl, "Esri World Imagery");

    QSettings mapSettings;
    const double defaultLat = mapSettings.value("map/default_lat", 0.0).toDouble();
    const double defaultLon = mapSettings.value("map/default_lon", 0.0).toDouble();
    const double defaultZoom = mapSettings.value("map/default_zoom", 2.0).toDouble();

    QMapLibre::Settings settings;
    settings.setStyles(styles);
    settings.setDefaultCoordinate(QMapLibre::Coordinate(defaultLat, defaultLon));
    settings.setDefaultZoom(defaultZoom);

    m_mapWidget = new QMapLibre::MapWidget(settings);
    m_mapWidget->setParent(this);
    m_mapWidget->setMinimumSize(100, 100);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_mapWidget, 1);

    appLog().info("[MapViewport] MapWidget created with Esri style URL in Settings");

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

    // Emit cursor coordinates on mouse move
    connect(m_mapWidget, &QMapLibre::MapWidget::onMouseMoveEvent,
            this, [this](QMapLibre::Coordinate coord) {
                double zoom = 15.0;
                if (auto* m = map()) zoom = m->zoom();
                emit cursorMoved(coord.first, coord.second, zoom);
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
            appLog().info("[MapViewport] Map loaded successfully");
            break;
        case QMapLibre::Map::MapChangeDidFailLoadingMap:
            appLog().warn("[MapViewport] Map failed to load");
            break;
        case QMapLibre::Map::MapChangeDidFinishLoadingStyle:
            appLog().info("[MapViewport] Style loaded successfully");
            break;
        case QMapLibre::Map::MapChangeRegionDidChange:
            emit mapMoved();
            // Persist the current view so the map reopens at the same place.
            if (auto* m = map()) {
                const auto coord = m->coordinate();
                QSettings s;
                s.setValue("map/default_lat", coord.first);
                s.setValue("map/default_lon", coord.second);
                s.setValue("map/default_zoom", m->zoom());
            }
            break;
        default:
            break;
    }
}
