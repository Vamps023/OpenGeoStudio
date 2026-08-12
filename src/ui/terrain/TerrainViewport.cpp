// TerrainViewport — Map with area selection overlay implementation

#include "TerrainViewport.hpp"

#include <QVBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <cmath>

// ============================================================
// TerrainViewport
// ============================================================

TerrainViewport::TerrainViewport(ApplicationContext* ctx, TerrainStore* store,
                                  QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_store(store) {
    setupUi();
    connect(m_store, &TerrainStore::boundsChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &TerrainStore::tileGridChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &TerrainStore::tileSelectionChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &TerrainStore::selectingChanged, m_overlay, qOverload<>(&QWidget::update));
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
    if (!m_map || !m_map->map()) return;
    const auto coord = m_map->map()->coordinateForPixel(screen);
    outLat = coord.first;
    outLon = coord.second;
}

void TerrainOverlayWidget::paintEvent(QPaintEvent*) {
    refreshMapState();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    drawSelectionBox(p);
    drawTileGrid(p);
    drawTileLabels(p);
}

void TerrainOverlayWidget::drawSelectionBox(QPainter& p) {
    // Draw live selection preview
    if (m_selecting) {
        QRectF rect(QRectF(m_selectStart, m_selectEnd).normalized());
        p.setPen(QPen(QColor(0, 255, 255, 200), 2));
        p.setBrush(QColor(0, 255, 255, 40));
        p.drawRect(rect);
    }

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

        bool isSelected = selected.contains(tile.id());
        if (isSelected) {
            p.setPen(QPen(QColor(0, 255, 100, 200), 2));
            p.setBrush(QColor(0, 255, 100, 60));
        } else {
            p.setPen(QPen(QColor(255, 255, 255, 100), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
        }
        p.drawRect(rect.normalized());
    }
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

    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ShiftModifier))) {
        m_panning = true;
        m_lastPanPos = pos;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier)) {
        m_selecting = true;
        m_selectStart = pos;
        m_selectEnd = pos;
        m_store->setSelecting(true);
        update();
        return;
    }

    // Click on tile toggles selection
    if (event->button() == Qt::LeftButton) {
        double lat, lon;
        screenToGeo(pos, lat, lon);
        const auto& grid = m_store->tileGrid();
        for (const auto& tile : grid.tiles) {
            if (lat <= tile.bounds.north && lat >= tile.bounds.south &&
                lon >= tile.bounds.west && lon <= tile.bounds.east) {
                m_store->toggleTile(tile.id());
                update();
                return;
            }
        }
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
    }
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
    }
}
