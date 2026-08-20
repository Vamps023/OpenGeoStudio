// TerrainViewport — Map with area selection overlay implementation

#include "TerrainViewport.hpp"

#include <QVBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainterPath>
#include <cmath>
#include <limits>

// ============================================================
// TerrainViewport
// ============================================================

TerrainViewport::TerrainViewport(ApplicationContext* ctx, TerrainStore* store,
                                  QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_store(store) {
    setupUi();
    if (m_overlay) {
        connect(m_store, &TerrainStore::boundsChanged, m_overlay, qOverload<>(&QWidget::update));
        connect(m_store, &TerrainStore::tileGridChanged, m_overlay, qOverload<>(&QWidget::update));
        connect(m_store, &TerrainStore::tileSelectionChanged, m_overlay, qOverload<>(&QWidget::update));
        connect(m_store, &TerrainStore::selectingChanged, m_overlay, qOverload<>(&QWidget::update));
    }
}

void TerrainViewport::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_mapWidget = new MapViewportWidget();
    layout->addWidget(m_mapWidget, 1);

    m_overlay = new TerrainOverlayWidget(m_store, m_mapWidget, this);
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_overlay->setAttribute(Qt::WA_NoSystemBackground, true);
    m_overlay->setAttribute(Qt::WA_TranslucentBackground, true);
    m_overlay->setMouseTracking(true);
}

void TerrainViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_overlay) {
        m_overlay->setGeometry(0, 0, width(), height());
        m_overlay->raise();
        m_overlay->update();
    }
}

// ============================================================
// TerrainOverlayWidget
// ============================================================

TerrainOverlayWidget::TerrainOverlayWidget(TerrainStore* store, MapViewportWidget* map,
                                            QWidget* parent)
    : QWidget(parent), m_store(store), m_map(map) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::StrongFocus);
}

void TerrainOverlayWidget::refreshMapState() {
    if (!m_map || !m_map->map()) return;
    auto* mapObj = m_map->map();
    const auto coord = mapObj->coordinate();
    m_mapLat = coord.first;
    m_mapLon = coord.second;
    m_mapZoom = mapObj->zoom();
}

QPointF TerrainOverlayWidget::geoToScreen(double lat, double lon) const {
    if (!m_map || !m_map->map()) return QPointF();
    return m_map->map()->pixelForCoordinate({lat, lon});
}

void TerrainOverlayWidget::screenToGeo(const QPointF& screen, double& outLat, double& outLon) const {
    outLat = std::numeric_limits<double>::quiet_NaN();
    outLon = std::numeric_limits<double>::quiet_NaN();
    if (!m_map || !m_map->map()) return;
    const auto coord = m_map->map()->coordinateForPixel(screen);
    outLat = coord.first;
    outLon = coord.second;
}

void TerrainOverlayWidget::paintEvent(QPaintEvent*) {
    refreshMapState();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_showSelection) drawSelectionBox(p);
    if (m_showGrid) drawTileGrid(p);
    if (m_showLabels) drawTileLabels(p);
}

void TerrainOverlayWidget::drawSelectionBox(QPainter& p) {
    // Draw live selection preview
    if (m_selecting) {
        QRectF rect(QRectF(m_selectStart, m_selectEnd).normalized());
        p.setPen(QPen(QColor(0, 255, 255, 200), 2));
        p.setBrush(QColor(0, 255, 255, 40));
        p.drawRect(rect);
    }

    // Ctrl+drag rectangle tile selection
    drawRectSelection(p);

    // Draw saved bounds
    const auto& bounds = m_store->selectedBounds();
    if (bounds.isValid() && !m_selecting) {
        QPointF tl = geoToScreen(bounds.north, bounds.west);
        QPointF br = geoToScreen(bounds.south, bounds.east);
        QRectF rect(tl, br);
        p.setPen(QPen(QColor(0, 255, 255, 200), 2));
        p.setBrush(QColor(0, 255, 255, 30));
        p.drawRect(rect.normalized());
    }
}

void TerrainOverlayWidget::drawTileGrid(QPainter& p) {
    const auto& grid = m_store->tileGrid();
    if (grid.tiles.isEmpty()) return;

    const auto& selected = m_store->selectedTiles();

    for (const auto& tile : grid.tiles) {
        QPointF tl = geoToScreen(tile.bounds.north, tile.bounds.west);
        QPointF br = geoToScreen(tile.bounds.south, tile.bounds.east);
        QRectF rect(tl, br);

        const bool isSelected = selected.contains(tile.id());
        const bool isHovered = m_showGrid && tile.id() == m_hoverTile;
        if (isSelected) {
            p.setPen(QPen(QColor(0, 255, 100, 200), 2));
            p.setBrush(QColor(0, 255, 100, 60));
        } else if (isHovered) {
            p.setPen(QPen(QColor(255, 255, 255, 220), 2));
            p.setBrush(QColor(255, 255, 255, 25));
        } else {
            p.setPen(QPen(QColor(255, 255, 255, 100), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
        }
        p.drawRect(rect.normalized());
    }
}

// Tile under a screen position, or empty string when over no tile
QString TerrainOverlayWidget::tileAt(const QPointF& screen) const {
    if (!m_showGrid) return QString();
    double lat, lon;
    screenToGeo(screen, lat, lon);
    if (std::isnan(lat) || std::isnan(lon)) return QString();
    const auto& grid = m_store->tileGrid();
    for (const auto& tile : grid.tiles) {
        if (lat <= tile.bounds.north && lat >= tile.bounds.south &&
            lon >= tile.bounds.west && lon <= tile.bounds.east) {
            return tile.id();
        }
    }
    return QString();
}

// Select (or deselect) every tile intersecting the Ctrl+drag rectangle
void TerrainOverlayWidget::applyRectSelection(bool select) {
    double lat1, lon1, lat2, lon2;
    screenToGeo(m_rectStart, lat1, lon1);
    screenToGeo(m_rectEnd, lat2, lon2);
    if (std::isnan(lat1) || std::isnan(lon1)) return;

    const double rNorth = std::max(lat1, lat2);
    const double rSouth = std::min(lat1, lat2);
    const double rEast = std::max(lon1, lon2);
    const double rWest = std::min(lon1, lon2);

    const auto& grid = m_store->tileGrid();
    const auto& selected = m_store->selectedTiles();
    for (const auto& tile : grid.tiles) {
        const bool intersects = tile.bounds.north >= rSouth && tile.bounds.south <= rNorth &&
                                tile.bounds.east >= rWest && tile.bounds.west <= rEast;
        if (!intersects) continue;
        const bool isSelected = selected.contains(tile.id());
        if (select != isSelected)
            m_store->toggleTile(tile.id());
    }
}

void TerrainOverlayWidget::drawRectSelection(QPainter& p) {
    if (!m_rectSelecting) return;
    QRectF rect(QRectF(m_rectStart, m_rectEnd).normalized());
    if (m_rectSelectMode) {
        p.setPen(QPen(QColor(0, 255, 100, 220), 2, Qt::DashLine));
        p.setBrush(QColor(0, 255, 100, 40));
    } else {
        p.setPen(QPen(QColor(255, 120, 120, 220), 2, Qt::DashLine));
        p.setBrush(QColor(255, 120, 120, 40));
    }
    p.drawRect(rect);
}

void TerrainOverlayWidget::drawTileLabels(QPainter& p) {
    const auto& grid = m_store->tileGrid();
    if (grid.tiles.isEmpty()) return;

    QFont font = p.font();
    font.setPointSize(8);
    p.setFont(font);

    for (const auto& tile : grid.tiles) {
        QPointF tl = geoToScreen(tile.bounds.north, tile.bounds.west);
        QPointF br = geoToScreen(tile.bounds.south, tile.bounds.east);
        QRectF rect(tl, br);
        p.setPen(QColor(255, 255, 255, 200));
        p.drawText(rect, Qt::AlignTop | Qt::AlignLeft,
                   QString("%1,%2").arg(tile.row).arg(tile.col));
    }
}

void TerrainOverlayWidget::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();

    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPanPos = pos;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    // Shift+drag (no Ctrl) draws the export area bounding box (1:1 square)
    if (event->button() == Qt::LeftButton &&
        (event->modifiers() & Qt::ShiftModifier) &&
        !(event->modifiers() & Qt::ControlModifier)) {
        m_selecting = true;
        m_selectStart = pos;
        m_selectEnd = pos;
        m_store->setSelecting(true);
        update();
        return;
    }

    // Ctrl+drag selects all tiles intersecting the rectangle;
    // Ctrl+Alt+drag deselects them
    if (event->button() == Qt::LeftButton &&
        (event->modifiers() & Qt::ControlModifier)) {
        m_rectSelecting = true;
        m_rectSelectMode = !(event->modifiers() & Qt::AltModifier);
        m_rectStart = pos;
        m_rectEnd = pos;
        update();
        return;
    }

    // Plain left press: click toggles the tile under the cursor,
    // drag pans the map (disambiguated in mouseMoveEvent)
    if (event->button() == Qt::LeftButton) {
        m_maybeClick = true;
        m_pressPos = pos;
        return;
    }
}

void TerrainOverlayWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();

    if (m_panning) {
        QPointF delta = pos - m_lastPanPos;
        if (m_map && m_map->map()) m_map->map()->moveBy(delta);
        m_lastPanPos = pos;
        update();
        return;
    }

    if (m_selecting) {
        m_selectEnd = pos;
        // Constrain to 1:1 square
        double dx = m_selectEnd.x() - m_selectStart.x();
        double dy = m_selectEnd.y() - m_selectStart.y();
        double size = std::max(std::abs(dx), std::abs(dy));
        m_selectEnd.setX(m_selectStart.x() + (dx >= 0 ? size : -size));
        m_selectEnd.setY(m_selectStart.y() + (dy >= 0 ? size : -size));
        update();
        return;
    }

    if (m_rectSelecting) {
        m_rectEnd = pos;
        update();
        return;
    }

    // Promote a plain-left press to panning once it becomes a drag
    if (m_maybeClick) {
        if ((pos - m_pressPos).manhattanLength() > 4.0) {
            m_maybeClick = false;
            m_panning = true;
            m_lastPanPos = m_pressPos;
            setCursor(Qt::ClosedHandCursor);
        }
        return;
    }

    // Hover affordance: highlight the tile under the cursor
    const QString hover = tileAt(pos);
    if (hover != m_hoverTile) {
        m_hoverTile = hover;
        update();
    }
    setCursor(hover.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
}

void TerrainOverlayWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    }

    if (m_selecting && event->button() == Qt::LeftButton) {
        m_selecting = false;
        m_store->setSelecting(false);

        // Convert screen rect to geo bounds
        double lat1, lon1, lat2, lon2;
        screenToGeo(m_selectStart, lat1, lon1);
        screenToGeo(m_selectEnd, lat2, lon2);

        terrain::GeoBounds bounds;
        bounds.north = std::max(lat1, lat2);
        bounds.south = std::min(lat1, lat2);
        bounds.east = std::max(lon1, lon2);
        bounds.west = std::min(lon1, lon2);

        if (bounds.isValid()) {
            m_store->setBounds(bounds);
        }
        update();
        return;
    }

    if (m_rectSelecting && event->button() == Qt::LeftButton) {
        m_rectSelecting = false;
        applyRectSelection(m_rectSelectMode);
        update();
        return;
    }

    // Plain click (never became a drag) toggles the tile under the cursor
    if (m_maybeClick && event->button() == Qt::LeftButton) {
        m_maybeClick = false;
        const QString id = tileAt(event->position());
        if (!id.isEmpty()) {
            m_store->toggleTile(id);
        }
        update();
    }
}

void TerrainOverlayWidget::wheelEvent(QWheelEvent* event) {
    if (!m_map || !m_map->map()) return;

    // MapLibre zoom: scale by factor based on wheel delta
    // QGIS/MapLibre pattern: zoom toward cursor position
    const QPointF pos = event->position();
    const double zoomFactor = (event->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    double currentZoom = m_map->map()->zoom();
    double newZoom = currentZoom + std::log(zoomFactor) / std::log(2.0);

    // Clamp zoom to reasonable range
    newZoom = std::max(0.0, std::min(20.0, newZoom));

    // Get geo coordinate under cursor before zoom
    double lat, lon;
    screenToGeo(pos, lat, lon);

    // Apply zoom
    m_map->map()->setZoom(newZoom);

    // Adjust center so the cursor stays over the same geo point
    // (zoom-toward-cursor like QGIS/MapLibre)
    if (!std::isnan(lat) && !std::isnan(lon)) {
        // After zoom, find where the cursor geo point is now,
        // and shift the map to keep it under the cursor
        QPointF newScreen = geoToScreen(lat, lon);
        QPointF delta = pos - newScreen;
        m_map->map()->moveBy(delta);
    }

    update();
}
