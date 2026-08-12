// MapViewportWidget - MapLibre Native Qt map widget implementation

#include "MapViewportWidget.hpp"

#include <QVBoxLayout>
#include <QDebug>

// Esri World Imagery raster style JSON (inline, no external style URL)
// Mirrors the reference app's SkiaViewport.tsx source configuration.
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
            "id": "esri-imagery",
            "type": "raster",
            "source": "esri-imagery",
            "minzoom": 0,
            "maxzoom": 22
        }
    ]
})";

MapViewportWidget::MapViewportWidget(QWidget* parent)
    : QWidget(parent) {
    setupMap();
}

void MapViewportWidget::setupMap() {
    // Configure MapLibre settings with Esri imagery as the default style.
    // The reference Electron app centers on lat=18.52, lon=73.85 (Pune, India)
    // at zoom 15. We replicate that here for parity.
    QMapLibre::Settings settings;
    settings.setContextMode(QMapLibre::Settings::UniqueGLContext);
    settings.setCacheDatabasePath(QString()); // in-memory cache for spike
    settings.setDefaultCoordinate(QMapLibre::Coordinate(18.52, 73.85));
    settings.setDefaultZoom(15.0);

    m_mapWidget = new QMapLibre::MapWidget(settings);
    m_mapWidget->setParent(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_mapWidget, 1);

    // Apply the Esri World Imagery raster style JSON
    if (auto* map = m_mapWidget->map()) {
        map->setStyleJson(QString::fromLatin1(kEsriImageryStyle));

        // Wire map change signals
        connect(map, &QMapLibre::Map::mapChanged,
                this, &MapViewportWidget::onMapChanged);

        // Wire mouse events from the map widget -> coordinate signals
        connect(m_mapWidget, &QMapLibre::MapWidget::onMousePressEvent,
                this, [this](QMapLibre::Coordinate coord) {
                    emit mapClicked(coord.first, coord.second);
                });
    }
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
        // QMapLibre: coordinateZoomForBounds(sw, ne) -> CoordinateZoom
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
        case QMapLibre::Map::MapChangeRegionDidChange:
            emit mapMoved();
            break;
        default:
            break;
    }
}
