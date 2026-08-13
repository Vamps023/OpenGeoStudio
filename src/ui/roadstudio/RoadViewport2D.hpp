#pragma once

// ============================================================
// RoadViewport2D — 2D road viewport with MapLibre + road overlay
// ============================================================
//
// Replaces SkiaViewport.tsx (modules/road-studio/client/SkiaViewport.tsx).
//
// Architecture:
// - MapLibre map widget as background (Esri satellite imagery)
// - Transparent QWidget overlay on top for road rendering
// - Coordinate conversion between geographic and screen/local
// - Mouse interaction for pan, zoom, click, road drawing
//

#include "RoadStudioStore.hpp"
#include "RoadEngineService.hpp"
#include "../../core/ApplicationContext.hpp"
#include "../app/MapViewportWidget.hpp"

#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPointF>

class RoadOverlayWidget;

class RoadViewport2D : public QWidget {
    Q_OBJECT

public:
    explicit RoadViewport2D(ApplicationContext* ctx, RoadStudioStore* store,
                             RoadEngineService* engine, QWidget* parent = nullptr);

    // Get the map widget (for coordinate conversion)
    MapViewportWidget* mapWidget() { return m_mapWidget; }

private slots:
    void onRoadsChanged();
    void onSelectionChanged(const roads::Selection& sel);
    void onToolChanged(roads::Tool tool);
    void onLmRoadStateChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();

    ApplicationContext* m_ctx;
    RoadStudioStore* m_store;
    RoadEngineService* m_engine;
    MapViewportWidget* m_mapWidget = nullptr;
    RoadOverlayWidget* m_overlay = nullptr;
};

// ============================================================
// RoadOverlayWidget — transparent overlay for road rendering
// ============================================================

class RoadOverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit RoadOverlayWidget(RoadStudioStore* store, RoadEngineService* engine,
                                MapViewportWidget* map, QWidget* parent = nullptr);

    void refreshMapState();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    // Coordinate conversion
    QPointF localToScreen(double localX, double localY) const;
    void screenToLocal(const QPointF& screen, double& outX, double& outY) const;
    void screenToGeo(const QPointF& screen, double& outLat, double& outLon) const;
    void geoToScreen(double lat, double lon, QPointF& outScreen) const;

    // Rendering
    void drawRoads(QPainter& p);
    void drawRoad(QPainter& p, const roads::Road& road);
    void drawControlPoints(QPainter& p, const roads::Road& road);
    void drawLaneMakerPreview(QPainter& p);
    void drawDirectionHandle(QPainter& p);
    void drawStagedPreview(QPainter& p);
    void drawFlexPreview(QPainter& p);
    void drawSnapIndicator(QPainter& p);
    void drawDestroyPreview(QPainter& p);
    void drawSelection(QPainter& p);
    void drawDebugLayers(QPainter& p);
    void drawLaneConfigOverlay(QPainter& p);

    // Interaction
    void handleClick(const QPointF& pos);
    void handleLmRoadClick(const QPointF& pos);
    void handleDestroyClick(const QPointF& pos);
    void handleModifyClick(const QPointF& pos);
    roads::ControlPoint* hitTestControlPoint(const QPointF& pos, QString& outRoadId);
    bool hitTestDirectionHandle(const QPointF& pos);
    roads::Road* hitTestRoad(const QPointF& pos, double& outS);

    // Geometry generation for flex preview
    roads::StagedGeometry generateFlexGeometry(roads::Point2D start, roads::Vec2 startDir,
                                                roads::Point2D end, roads::Vec2 endDir);

    RoadStudioStore* m_store;
    MapViewportWidget* m_map;
    RoadEngineService* m_engine;

    // Map state (refreshed from MapLibre)
    double m_mapLat = 18.52;
    double m_mapLon = 73.85;
    double m_mapZoom = 15.0;
    double m_mpp = 1.0;  // meters per pixel
    double m_ppm = 1.0;  // pixels per meter

    // Interaction state
    bool m_panning = false;
    QPointF m_lastPanPos;
    roads::ControlPoint* m_draggingPoint = nullptr;
    QString m_draggingRoadId;

    // Direction handle dragging
    bool m_draggingDirHandle = false;
    double m_dirHandleDragOffset = 0;

    // Destroy mode state
    roads::Road* m_destroyTarget = nullptr;
    double m_destroyS1 = 0, m_destroyS2 = 0;

    // Flex preview cache
    roads::StagedGeometry m_flexPreview;
    bool m_flexValid = false;
};
