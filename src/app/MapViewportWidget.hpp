// ============================================================
// MapViewportWidget - MapLibre Native Qt map widget wrapper
// ============================================================
//
// Phase 2c rendering spike - Option A prototype.
//
// Wraps QMapLibre::MapWidget with the Esri World Imagery raster
// tile source (same as the reference Electron app's SkiaViewport).
// Provides pan/zoom and exposes the underlying QMapLibre::Map for
// coordinate conversion (geo <-> screen) needed by road overlay.
//

#pragma once

#include <QMapLibre/Map>
#include <QMapLibre/Settings>
#include <QMapLibre/Types>
#include <QMapLibreWidgets/MapWidget>

#include <QWidget>
#include <QString>

class MapViewportWidget : public QWidget {
    Q_OBJECT

public:
    explicit MapViewportWidget(QWidget* parent = nullptr);
    ~MapViewportWidget() override = default;

    // Access the underlying MapLibre map for coordinate conversion,
    // style manipulation, camera control, etc.
    [[nodiscard]] QMapLibre::Map* map() const;

    // Convenience camera helpers
    void setCenter(double lat, double lon);
    void setZoom(double zoom);
    void fitBounds(double minLat, double minLon, double maxLat, double maxLon);

signals:
    void mapClicked(double lat, double lon);
    void mapMoved();

private slots:
    void onMapChanged(QMapLibre::Map::MapChange change);

private:
    void setupMap();

    QMapLibre::MapWidget* m_mapWidget = nullptr;
};
