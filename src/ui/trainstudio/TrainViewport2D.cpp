// TrainViewport2D — 2D track editing viewport implementation

#include "TrainViewport2D.hpp"
#include "../roadstudio/GeoConvert.hpp"

#include <QVBoxLayout>
#include <QResizeEvent>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>

// ============================================================
// TrainViewport2D
// ============================================================

TrainViewport2D::TrainViewport2D(ApplicationContext* ctx, TrainStudioStore* store,
                                  QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_store(store) {
    setupUi();
    connect(m_store, &TrainStudioStore::tracksChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &TrainStudioStore::selectionChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &TrainStudioStore::toolChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &TrainStudioStore::arcStateChanged, m_overlay, qOverload<>(&QWidget::update));
}

void TrainViewport2D::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_mapWidget = new MapViewportWidget();
    layout->addWidget(m_mapWidget, 1);

    m_overlay = new TrainOverlayWidget(m_store, m_mapWidget, this);
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_overlay->setAttribute(Qt::WA_NoSystemBackground, true);
    m_overlay->setAttribute(Qt::WA_TranslucentBackground, true);
    m_overlay->setMouseTracking(true);
}

void TrainViewport2D::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_overlay) {
        m_overlay->setGeometry(0, 0, width(), height());
        m_overlay->raise();
        m_overlay->update();
    }
}

// ============================================================
// TrainOverlayWidget
// ============================================================

TrainOverlayWidget::TrainOverlayWidget(TrainStudioStore* store, MapViewportWidget* map,
                                        QWidget* parent)
    : QWidget(parent), m_store(store), m_map(map) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::StrongFocus);
}

void TrainOverlayWidget::refreshMapState() {
    if (!m_map || !m_map->map()) return;
    auto* mapObj = m_map->map();
    const auto coord = mapObj->coordinate();
    m_mapLat = coord.first;
    m_mapLon = coord.second;
    m_mapZoom = mapObj->zoom();
    m_mpp = roads::metersPerPixel(m_mapZoom, m_mapLat);
    m_ppm = 1.0 / m_mpp;
}

QPointF TrainOverlayWidget::localToScreen(double localX, double localY) const {
    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    double refLocalX, refLocalY;
    roads::geoToLocal(m_store->refLat(), m_store->refLon(), m_mapLat, m_mapLon, refLocalX, refLocalY);
    double refScreenX = cx - refLocalX * m_ppm;
    double refScreenY = cy + refLocalY * m_ppm;
    return QPointF(refScreenX + localX * m_ppm, refScreenY - localY * m_ppm);
}

void TrainOverlayWidget::screenToLocal(const QPointF& screen, double& outX, double& outY) const {
    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    double refLocalX, refLocalY;
    roads::geoToLocal(m_store->refLat(), m_store->refLon(), m_mapLat, m_mapLon, refLocalX, refLocalY);
    double refScreenX = cx - refLocalX * m_ppm;
    double refScreenY = cy + refLocalY * m_ppm;
    outX = (screen.x() - refScreenX) / m_ppm;
    outY = (refScreenY - screen.y()) / m_ppm;
}

void TrainOverlayWidget::screenToGeo(const QPointF& screen, double& outLat, double& outLon) const {
    double localX, localY;
    screenToLocal(screen, localX, localY);
    roads::localToGeo(localX, localY, m_store->refLat(), m_store->refLon(), outLat, outLon);
}

void TrainOverlayWidget::geoToScreen(double lat, double lon, QPointF& outScreen) const {
    double localX, localY;
    roads::geoToLocal(lat, lon, m_store->refLat(), m_store->refLon(), localX, localY);
    outScreen = localToScreen(localX, localY);
}

void TrainOverlayWidget::paintEvent(QPaintEvent*) {
    refreshMapState();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    drawTracks(p);
    if (m_store->isArcDrawing()) drawArcPreview(p);
}

void TrainOverlayWidget::drawTracks(QPainter& p) {
    for (const auto& track : m_store->tracks()) {
        drawTrack(p, track);
        drawControlPoints(p, track);
    }
}

void TrainOverlayWidget::drawTrack(QPainter& p, const trains::Track& track) {
    if (track.points.size() < 2) return;

    QPainterPath path;
    QPointF screen;
    geoToScreen(track.points[0].lat, track.points[0].lon, screen);
    path.moveTo(screen);
    for (int i = 1; i < track.points.size(); ++i) {
        geoToScreen(track.points[i].lat, track.points[i].lon, screen);
        path.lineTo(screen);
    }

    // Draw track line (brown, gauge-scaled width)
    const double trackWidthPx = std::max(2.0, track.gauge * m_ppm * 2);
    QColor trackColor(track.color);
    trackColor.setAlpha(200);
    QPen pen(trackColor, trackWidthPx);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawPath(path);

    // Draw rails (two thin lines offset by gauge/2)
    if (track.points.size() >= 2 && m_ppm > 0.5) {
        const double railOffset = track.gauge * m_ppm / 2.0;
        // Compute offset paths (simplified — just offset the polyline)
        for (int side = -1; side <= 1; side += 2) {
            QPainterPath railPath;
            for (int i = 0; i < track.points.size(); ++i) {
                geoToScreen(track.points[i].lat, track.points[i].lon, screen);
                if (i == 0) {
                    railPath.moveTo(screen.x() + side * railOffset, screen.y());
                } else {
                    railPath.lineTo(screen.x() + side * railOffset, screen.y());
                }
            }
            p.setPen(QPen(QColor(200, 200, 200, 180), 1.5));
            p.drawPath(railPath);
        }
    }
}

void TrainOverlayWidget::drawControlPoints(QPainter& p, const trains::Track& track) {
    const double ptRadius = 5.0;
    const bool isSelected = m_store->selection().trackId == track.id;

    for (int i = 0; i < track.points.size(); ++i) {
        QPointF screen;
        geoToScreen(track.points[i].lat, track.points[i].lon, screen);

        bool pointSelected = isSelected && m_store->selection().pointIndices.contains(i);
        QColor color = pointSelected ? QColor(255, 220, 0) : QColor(200, 160, 80);
        p.setBrush(color);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawEllipse(screen, ptRadius, ptRadius);
    }
}

void TrainOverlayWidget::drawArcPreview(QPainter& p) {
    auto start = m_store->arcStart();
    if (!start) return;

    QPointF startScreen;
    geoToScreen(start->lat, start->lon, startScreen);

    // Draw start point
    p.setBrush(QColor(0, 255, 0, 200));
    p.setPen(QPen(Qt::white, 2));
    p.drawEllipse(startScreen, 8, 8);

    // Draw direction arrow
    auto dir = m_store->arcStartDir();
    if (dir) {
        double startLocalX, startLocalY;
        roads::geoToLocal(start->lat, start->lon, m_store->refLat(), m_store->refLon(),
                          startLocalX, startLocalY);
        QPointF dirScreen = localToScreen(startLocalX + dir->x() * 20, startLocalY + dir->y() * 20);
        p.setPen(QPen(QColor(0, 255, 0, 200), 2));
        p.drawLine(startScreen, dirScreen);
    }

    // Draw hint text
    p.setPen(QColor(255, 255, 255, 220));
    QFont font = p.font();
    font.setPointSize(10);
    font.setBold(true);
    p.setFont(font);
    p.drawText(startScreen + QPointF(12, -12), "Click end point");
}

void TrainOverlayWidget::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();

    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier))) {
        m_panning = true;
        m_lastPanPos = pos;
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        handleClick(pos);
    }
}

void TrainOverlayWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();

    if (m_panning) {
        QPointF delta = pos - m_lastPanPos;
        if (m_map && m_map->map()) m_map->map()->moveBy(delta);
        m_lastPanPos = pos;
        update();
        return;
    }

    // Note: drag-to-move is not implemented for Train Studio because
    // the store doesn't expose mutable track access. Selection works.
    // This will be improved in a future iteration.
}

void TrainOverlayWidget::mouseReleaseEvent(QMouseEvent*) {
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    }
    m_draggingPoint = nullptr;
}

void TrainOverlayWidget::handleClick(const QPointF& pos) {
    const auto tool = m_store->tool();

    if (tool == trains::Tool::Select) {
        QString trackId;
        hitTestControlPoint(pos, trackId);
        if (!trackId.isEmpty()) {
            trains::Selection sel;
            sel.trackId = trackId;
            // Find the closest point index
            const auto& tracks = m_store->tracks();
            for (const auto& t : tracks) {
                if (t.id == trackId) {
                    double minDist = 1e18;
                    int closestIdx = 0;
                    for (int i = 0; i < t.points.size(); ++i) {
                        QPointF screen;
                        geoToScreen(t.points[i].lat, t.points[i].lon, screen);
                        double d = std::hypot(screen.x() - pos.x(), screen.y() - pos.y());
                        if (d < minDist) { minDist = d; closestIdx = i; }
                    }
                    sel.pointIndices.append(closestIdx);
                    break;
                }
            }
            m_store->setSelection(sel);
        } else {
            m_store->clearSelection();
        }
    } else if (tool == trains::Tool::Line) {
        double lat, lon;
        screenToGeo(pos, lat, lon);
        // Add to current drawing track or start new one
        // For simplicity: if no drawing track, start one; otherwise add point
        // (The reference app uses a more complex workflow)
        m_store->startNewTrack(lat, lon);
    } else if (tool == trains::Tool::Arc) {
        double lat, lon;
        screenToGeo(pos, lat, lon);

        if (!m_store->isArcDrawing()) {
            // First click — set start point with default direction (east)
            trains::ControlPoint start;
            start.id = "";
            start.lat = lat;
            start.lon = lon;
            m_store->startArc(start, QPointF(1.0, 0.0));
        } else {
            // Second click — finish arc
            m_store->finishArc(lat, lon);
        }
    }
    update();
}

trains::ControlPoint* TrainOverlayWidget::hitTestControlPoint(const QPointF& pos,
                                                               QString& outTrackId) {
    const double tolerance = 12.0;
    const auto& tracks = m_store->tracks();
    for (int t = 0; t < tracks.size(); ++t) {
        const auto& track = tracks[t];
        for (int i = 0; i < track.points.size(); ++i) {
            QPointF screen;
            geoToScreen(track.points[i].lat, track.points[i].lon, screen);
            if (std::hypot(screen.x() - pos.x(), screen.y() - pos.y()) < tolerance) {
                outTrackId = track.id;
                // Return mutable pointer — need non-const access
                // The store doesn't expose mutable tracks, so we search by ID
                // For now, return nullptr and handle selection differently
                return nullptr;
            }
        }
    }
    return nullptr;
}
