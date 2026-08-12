#pragma once

// ============================================================
// TrainViewport2D — 2D track editing viewport
// ============================================================
//
// Replaces TrainViewport.tsx. MapLibre background + Canvas overlay
// for track rendering. Simpler than Road Studio — no engine mesh,
// no debug layers, no 3D.
//

#include "TrainStudioStore.hpp"
#include "../../core/ApplicationContext.hpp"
#include "../app/MapViewportWidget.hpp"

#include <QWidget>
#include <QPainter>
#include <QPointF>

class TrainOverlayWidget;

class TrainViewport2D : public QWidget {
    Q_OBJECT
public:
    explicit TrainViewport2D(ApplicationContext* ctx, TrainStudioStore* store,
                              QWidget* parent = nullptr);
    MapViewportWidget* mapWidget() { return m_mapWidget; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();
    ApplicationContext* m_ctx;
    TrainStudioStore* m_store;
    MapViewportWidget* m_mapWidget = nullptr;
    TrainOverlayWidget* m_overlay = nullptr;
};

class TrainOverlayWidget : public QWidget {
    Q_OBJECT
public:
    explicit TrainOverlayWidget(TrainStudioStore* store, MapViewportWidget* map,
                                QWidget* parent = nullptr);
    void refreshMapState();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    QPointF localToScreen(double localX, double localY) const;
    void screenToLocal(const QPointF& screen, double& outX, double& outY) const;
    void screenToGeo(const QPointF& screen, double& outLat, double& outLon) const;
    void geoToScreen(double lat, double lon, QPointF& outScreen) const;

    void drawTracks(QPainter& p);
    void drawTrack(QPainter& p, const trains::Track& track);
    void drawControlPoints(QPainter& p, const trains::Track& track);
    void drawArcPreview(QPainter& p);
    void handleClick(const QPointF& pos);
    trains::ControlPoint* hitTestControlPoint(const QPointF& pos, QString& outTrackId);

    TrainStudioStore* m_store;
    MapViewportWidget* m_map;

    double m_mapLat = 18.52, m_mapLon = 73.85, m_mapZoom = 15.0;
    double m_mpp = 1.0, m_ppm = 1.0;

    bool m_panning = false;
    QPointF m_lastPanPos;
    bool m_dragging = false;
};
