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
#include <QTcpServer>
#include <QTcpSocket>

// Minimal HTTP server to serve the Esri style JSON to MapLibre.
// MapLibre's network stack can't load file:// URLs, so we serve
// the inline style JSON via a local HTTP endpoint.
class StyleHttpServer : public QObject {
    Q_OBJECT
public:
    explicit StyleHttpServer(const QByteArray& styleJson, QObject* parent = nullptr);
    [[nodiscard]] QString styleUrl() const;

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    QTcpServer m_server;
    QByteArray m_styleJson;
    quint16 m_port = 0;
};

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
    void cursorMoved(double lat, double lon, double zoom);

private slots:
    void onMapChanged(QMapLibre::Map::MapChange change);

private:
    void setupMap();

    QMapLibre::MapWidget* m_mapWidget = nullptr;
    StyleHttpServer* m_styleServer = nullptr;
};
