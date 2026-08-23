#pragma once

// ============================================================
// TerrainViewport — Map with area selection overlay
// ============================================================
//
// Replaces modules/terrain/client/MapViewport/MapViewport.tsx.
// Shift+drag draws a bounding box (constrained to 1:1 square).
// Tile grid overlay shows export tiles.
//

#include "TerrainStore.hpp"
#include "../../core/ApplicationContext.hpp"
#include "../app/MapViewportWidget.hpp"

#include <QWidget>
#include <QPainter>
#include <QPointF>

class TerrainOverlayWidget;

class TerrainViewport : public QWidget {
    Q_OBJECT
public:
    explicit TerrainViewport(ApplicationContext* ctx, TerrainStore* store,
                              QWidget* parent = nullptr);
    MapViewportWidget* mapWidget() { return m_mapWidget; }
    TerrainOverlayWidget* overlay() { return m_overlay; }

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUi();
    ApplicationContext* m_ctx;
    TerrainStore* m_store;
    MapViewportWidget* m_mapWidget = nullptr;
    TerrainOverlayWidget* m_overlay = nullptr;
};

class TerrainOverlayWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerrainOverlayWidget(TerrainStore* store, MapViewportWidget* map,
                                   QWidget* parent = nullptr);
    void refreshMapState();

    void setShowGrid(bool v) { m_showGrid = v; update(); }
    void setShowLabels(bool v) { m_showLabels = v; update(); }
    void setShowSelection(bool v) { m_showSelection = v; update(); }

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    QPointF geoToScreen(double lat, double lon) const;
    void screenToGeo(const QPointF& screen, double& outLat, double& outLon) const;
    void drawSelectionBox(QPainter& p);
    void drawTileGrid(QPainter& p);
    void drawTileLabels(QPainter& p);
    QString tileAt(const QPointF& screen) const;
    void applyRectSelection(bool select);
    void drawRectSelection(QPainter& p);

    TerrainStore* m_store;
    MapViewportWidget* m_map;

    double m_mapLat = 0.0, m_mapLon = 0.0, m_mapZoom = 2.0;

    bool m_selecting = false;
    QPointF m_selectStart;
    QPointF m_selectEnd;
    bool m_panning = false;
    QPointF m_lastPanPos;

    // Click-to-toggle vs drag-to-pan disambiguation
    bool m_maybeClick = false;
    QPointF m_pressPos;

    // Ctrl+drag multi tile select / Ctrl+Alt+drag deselect
    bool m_rectSelecting = false;
    bool m_rectSelectMode = true;  // true = select, false = deselect
    QPointF m_rectStart;
    QPointF m_rectEnd;

    // Hovered tile for visual affordance
    QString m_hoverTile;

    bool m_showGrid = true;
    bool m_showLabels = true;
    bool m_showSelection = true;
};
