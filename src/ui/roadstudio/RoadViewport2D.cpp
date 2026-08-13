// RoadViewport2D — 2D road viewport implementation

#include "RoadViewport2D.hpp"
#include "GeoConvert.hpp"

#include <QVBoxLayout>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QPainterPath>
#include <QFont>
#include <cmath>

#ifdef ENABLE_LANEMAKER
#include "curve_fitting.h"
#include "Geometries/RoadGeometry.h"
#endif

// ============================================================
// RoadViewport2D
// ============================================================

RoadViewport2D::RoadViewport2D(ApplicationContext* ctx, RoadStudioStore* store,
                                 RoadEngineService* engine, QWidget* parent)
    : QWidget(parent), m_ctx(ctx), m_store(store), m_engine(engine) {
    setupUi();

    // Connect store signals to trigger overlay repaint
    connect(m_store, &RoadStudioStore::roadsChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &RoadStudioStore::selectionChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &RoadStudioStore::toolChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &RoadStudioStore::lmRoadStateChanged, m_overlay, qOverload<>(&QWidget::update));
    connect(m_store, &RoadStudioStore::debugModeChanged, m_overlay, qOverload<>(&QWidget::update));
}

void RoadViewport2D::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_mapWidget = new MapViewportWidget();
    layout->addWidget(m_mapWidget, 1);

    // Create transparent overlay on top of the map
    m_overlay = new RoadOverlayWidget(m_store, m_engine, m_mapWidget, this);
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    m_overlay->setAttribute(Qt::WA_NoSystemBackground, true);
    m_overlay->setAttribute(Qt::WA_TranslucentBackground, true);
    m_overlay->setMouseTracking(true);
}

void RoadViewport2D::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_overlay) {
        m_overlay->setGeometry(0, 0, width(), height());
        m_overlay->raise();
        m_overlay->update();
    }
}

void RoadViewport2D::onRoadsChanged() {
    if (m_overlay) m_overlay->update();
}

void RoadViewport2D::onSelectionChanged(const roads::Selection&) {
    if (m_overlay) m_overlay->update();
}

void RoadViewport2D::onToolChanged(roads::Tool) {
    if (m_overlay) m_overlay->update();
}

void RoadViewport2D::onLmRoadStateChanged() {
    if (m_overlay) m_overlay->update();
}

// ============================================================
// RoadOverlayWidget
// ============================================================

RoadOverlayWidget::RoadOverlayWidget(RoadStudioStore* store, RoadEngineService* engine,
                                      MapViewportWidget* map, QWidget* parent)
    : QWidget(parent), m_store(store), m_map(map), m_engine(engine) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setFocusPolicy(Qt::StrongFocus);
}

void RoadOverlayWidget::refreshMapState() {
    if (!m_map || !m_map->map()) return;
    auto* mapObj = m_map->map();
    const auto coord = mapObj->coordinate();
    m_mapLat = coord.first;
    m_mapLon = coord.second;
    m_mapZoom = mapObj->zoom();
    m_mpp = roads::metersPerPixel(m_mapZoom, m_mapLat);
    m_ppm = 1.0 / m_mpp;
}

QPointF RoadOverlayWidget::localToScreen(double localX, double localY) const {
    // Convert local meters to screen coordinates
    // The map center is at the widget center
    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    const double refLat = m_store->refLat();
    const double refLon = m_store->refLon();

    // Convert ref origin to screen
    double refScreenX, refScreenY;
    // ref origin offset from map center in pixels
    double refLocalX, refLocalY;
    roads::geoToLocal(refLat, refLon, m_mapLat, m_mapLon, refLocalX, refLocalY);
    refScreenX = cx - refLocalX * m_ppm;
    refScreenY = cy + refLocalY * m_ppm; // Y-down screen

    return QPointF(refScreenX + localX * m_ppm,
                   refScreenY - localY * m_ppm);
}

void RoadOverlayWidget::screenToLocal(const QPointF& screen, double& outX, double& outY) const {
    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    const double refLat = m_store->refLat();
    const double refLon = m_store->refLon();

    double refLocalX, refLocalY;
    roads::geoToLocal(refLat, refLon, m_mapLat, m_mapLon, refLocalX, refLocalY);
    double refScreenX = cx - refLocalX * m_ppm;
    double refScreenY = cy + refLocalY * m_ppm;

    outX = (screen.x() - refScreenX) / m_ppm;
    outY = (refScreenY - screen.y()) / m_ppm;
}

void RoadOverlayWidget::screenToGeo(const QPointF& screen, double& outLat, double& outLon) const {
    double localX, localY;
    screenToLocal(screen, localX, localY);
    roads::localToGeo(localX, localY, m_store->refLat(), m_store->refLon(), outLat, outLon);
}

void RoadOverlayWidget::geoToScreen(double lat, double lon, QPointF& outScreen) const {
    double localX, localY;
    roads::geoToLocal(lat, lon, m_store->refLat(), m_store->refLon(), localX, localY);
    outScreen = localToScreen(localX, localY);
}

// ============================================================
// Rendering
// ============================================================

void RoadOverlayWidget::paintEvent(QPaintEvent*) {
    refreshMapState();

    if (!m_engine || !m_store) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Draw roads
    drawRoads(p);

    const auto tool = m_store->tool();

    // Draw LaneMaker preview if active (road creation mode)
    if (tool == roads::Tool::Road) {
        drawStagedPreview(p);
        drawFlexPreview(p);
        drawDirectionHandle(p);
        drawLaneMakerPreview(p);
        drawSnapIndicator(p);
    }

    // Draw destroy preview
    if (tool == roads::Tool::Destroy) {
        drawDestroyPreview(p);
    }

    // Draw debug layers if enabled
    if (m_store->debugMode()) {
        drawDebugLayers(p);
    }

    // Draw mode hint
    drawLaneConfigOverlay(p);
}

void RoadOverlayWidget::drawRoads(QPainter& p) {
    for (const auto& road : m_store->roads()) {
        drawRoad(p, road);
        drawControlPoints(p, road);
    }
}

void RoadOverlayWidget::drawRoad(QPainter& p, const roads::Road& road) {
    if (road.points.size() < 2) return;

    // Use the C++ engine to sample the centerline
    auto centerline = m_engine->sampleCenterline(
        road, m_store->refLat(), m_store->refLon(), 64);

    if (centerline.size() < 2) {
        // Fallback: draw straight line between control points
        QPointF start, end;
        geoToScreen(road.points[0].lat, road.points[0].lon, start);
        geoToScreen(road.points[1].lat, road.points[1].lon, end);
        QPen pen(QColor(road.color), std::max(2.0, road.width * m_ppm));
        p.setPen(pen);
        p.drawLine(start, end);
        return;
    }

    // Build centerline path from engine samples
    QPainterPath path;
    path.moveTo(localToScreen(centerline[0].x, centerline[0].y));
    for (size_t i = 1; i < centerline.size(); ++i) {
        path.lineTo(localToScreen(centerline[i].x, centerline[i].y));
    }

    // Draw road surface (width-scaled)
    const double roadWidthPx = road.width * m_ppm;
    QColor roadColor(road.color);
    roadColor.setAlpha(180);
    QPen pen(roadColor, std::max(2.0, roadWidthPx));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawPath(path);

    // Draw left and right edges (engine-computed)
    auto leftEdge = m_engine->sampleLeftEdge(
        road, m_store->refLat(), m_store->refLon(), 64);
    auto rightEdge = m_engine->sampleRightEdge(
        road, m_store->refLat(), m_store->refLon(), 64);

    if (leftEdge.size() >= 2) {
        QPainterPath leftPath;
        leftPath.moveTo(localToScreen(leftEdge[0].x, leftEdge[0].y));
        for (size_t i = 1; i < leftEdge.size(); ++i) {
            leftPath.lineTo(localToScreen(leftEdge[i].x, leftEdge[i].y));
        }
        p.setPen(QPen(QColor(255, 100, 100, 120), 1.5));
        p.drawPath(leftPath);
    }

    if (rightEdge.size() >= 2) {
        QPainterPath rightPath;
        rightPath.moveTo(localToScreen(rightEdge[0].x, rightEdge[0].y));
        for (size_t i = 1; i < rightEdge.size(); ++i) {
            rightPath.lineTo(localToScreen(rightEdge[i].x, rightEdge[i].y));
        }
        p.setPen(QPen(QColor(100, 100, 255, 120), 1.5));
        p.drawPath(rightPath);
    }

    // Draw centerline (thin yellow line on top)
    QPen centerPen(QColor(255, 255, 0, 200), 1.5);
    p.setPen(centerPen);
    p.drawPath(path);

    // Draw lane boundaries if multi-lane
    if (road.laneCount > 1) {
        auto boundaries = m_engine->generateLaneBoundaries(
            road, m_store->refLat(), m_store->refLon(), 64);
        p.setPen(QPen(QColor(255, 255, 255, 100), 1, Qt::DashLine));
        for (const auto& boundary : boundaries) {
            if (boundary.size() < 2) continue;
            QPainterPath bPath;
            bPath.moveTo(localToScreen(boundary[0].x, boundary[0].y));
            for (size_t i = 1; i < boundary.size(); ++i) {
                bPath.lineTo(localToScreen(boundary[i].x, boundary[i].y));
            }
            p.drawPath(bPath);
        }
    }
}

void RoadOverlayWidget::drawControlPoints(QPainter& p, const roads::Road& road) {
    const double ptRadius = 5.0;
    const bool isSelected = m_store->selection().roadId == road.id;

    for (int i = 0; i < road.points.size(); ++i) {
        QPointF screen;
        geoToScreen(road.points[i].lat, road.points[i].lon, screen);

        bool pointSelected = isSelected && m_store->selection().pointIndices.contains(i);

        // Draw control point circle
        QColor color = pointSelected ? QColor(255, 220, 0) : QColor(100, 200, 255);
        p.setBrush(color);
        p.setPen(QPen(Qt::white, 1.5));
        p.drawEllipse(screen, ptRadius, ptRadius);

        // Draw bezier handles if present
        if (road.points[i].handleIn || road.points[i].handleOut) {
            double localX, localY;
            roads::geoToLocal(road.points[i].lat, road.points[i].lon,
                              m_store->refLat(), m_store->refLon(), localX, localY);

            if (road.points[i].handleIn) {
                QPointF handleScreen = localToScreen(
                    localX + road.points[i].handleIn->x,
                    localY + road.points[i].handleIn->y);
                p.setPen(QPen(QColor(100, 200, 255, 150), 1));
                p.drawLine(screen, handleScreen);
                p.setBrush(QColor(100, 200, 255, 150));
                p.drawEllipse(handleScreen, 3, 3);
            }
            if (road.points[i].handleOut) {
                QPointF handleScreen = localToScreen(
                    localX + road.points[i].handleOut->x,
                    localY + road.points[i].handleOut->y);
                p.setPen(QPen(QColor(100, 200, 255, 150), 1));
                p.drawLine(screen, handleScreen);
                p.setBrush(QColor(100, 200, 255, 150));
                p.drawEllipse(handleScreen, 3, 3);
            }
        }
    }
}

void RoadOverlayWidget::drawLaneMakerPreview(QPainter& p) {
    auto start = m_store->lmRoadStart();
    auto end = m_store->lmRoadEnd();
    auto preview = m_store->previewPoint();

    if (!start) return;

    QPointF startScreen = localToScreen(start->x, start->y);

    // Draw start point
    p.setBrush(QColor(0, 255, 0, 200));
    p.setPen(QPen(Qt::white, 2));
    p.drawEllipse(startScreen, 8, 8);

    // Draw start direction arrow
    auto dir = m_store->lmRoadStartDir();
    if (dir) {
        QPointF dirScreen = localToScreen(start->x + dir->x * 20, start->y + dir->y * 20);
        p.setPen(QPen(QColor(0, 255, 0, 200), 2));
        p.drawLine(startScreen, dirScreen);
        // Arrowhead
        double angle = std::atan2(dirScreen.y() - startScreen.y(),
                                   dirScreen.x() - startScreen.x());
        QPointF arrow1(dirScreen.x() - 8 * std::cos(angle - 0.4),
                       dirScreen.y() - 8 * std::sin(angle - 0.4));
        QPointF arrow2(dirScreen.x() - 8 * std::cos(angle + 0.4),
                       dirScreen.y() - 8 * std::sin(angle + 0.4));
        p.drawLine(dirScreen, arrow1);
        p.drawLine(dirScreen, arrow2);
    }

    // Draw end point or preview
    QPointF endScreen;
    if (end) {
        endScreen = localToScreen(end->x, end->y);
        p.setBrush(QColor(255, 0, 0, 200));
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(endScreen, 8, 8);
    } else if (preview) {
        endScreen = localToScreen(preview->x, preview->y);
        p.setBrush(QColor(255, 200, 0, 150));
        p.setPen(QPen(QColor(255, 200, 0), 1, Qt::DashLine));
        p.drawEllipse(endScreen, 6, 6);
    } else {
        return;
    }

    // Draw line from start to end/preview
    p.setPen(QPen(QColor(255, 255, 0, 150), 2, Qt::DashLine));
    p.drawLine(startScreen, endScreen);

    // Draw distance hint
    if (end || preview) {
        roads::Point2D endPt = end ? *end : *preview;
        double dx = endPt.x - start->x;
        double dy = endPt.y - start->y;
        double dist = std::sqrt(dx * dx + dy * dy);

        QPointF mid = (startScreen + endScreen) / 2.0;
        p.setPen(QColor(255, 255, 255, 220));
        QFont font = p.font();
        font.setPointSize(10);
        font.setBold(true);
        p.setFont(font);
        p.drawText(mid + QPointF(5, -5), QString::number(dist, 'f', 1) + " m");
    }
}

void RoadOverlayWidget::drawSelection(QPainter& p) {
    // Selection highlighting is done in drawControlPoints
}

void RoadOverlayWidget::drawDirectionHandle(QPainter& p) {
    if (!m_store->isDirectionHandleActive() || !m_store->directionHandlePos()) return;

    QPointF center = localToScreen(m_store->directionHandlePos()->x,
                                    m_store->directionHandlePos()->y);
    double angle = m_store->directionHandleAngle();

    const double innerR = 4.0 * m_ppm;
    const double outerR = 6.0 * m_ppm;

    // Draw direction handle ring (green)
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 200, 0, 200), 2));
    p.drawEllipse(center, outerR, outerR);
    p.setPen(QPen(QColor(0, 150, 0, 150), 1));
    p.drawEllipse(center, innerR, innerR);

    // Draw direction arrow
    QPointF arrowEnd(center.x() + std::cos(angle) * outerR * 1.5,
                     center.y() - std::sin(angle) * outerR * 1.5);
    p.setPen(QPen(QColor(0, 255, 0, 220), 2));
    p.drawLine(center, arrowEnd);

    // Arrowhead
    QPointF arrow1(arrowEnd.x() - 8 * std::cos(angle - 0.4),
                   arrowEnd.y() + 8 * std::sin(angle - 0.4));
    QPointF arrow2(arrowEnd.x() - 8 * std::cos(angle + 0.4),
                   arrowEnd.y() + 8 * std::sin(angle + 0.4));
    p.drawLine(arrowEnd, arrow1);
    p.drawLine(arrowEnd, arrow2);
}

void RoadOverlayWidget::drawStagedPreview(QPainter& p) {
    const auto& staged = m_store->stagedGeometries();
    if (staged.isEmpty()) return;

    // Draw all staged geometry segments
    for (const auto& geo : staged) {
        if (geo.samples.size() < 2) continue;

        // Draw centerline (solid green)
        QPainterPath path;
        path.moveTo(localToScreen(geo.samples[0].x, geo.samples[0].y));
        for (int i = 1; i < geo.samples.size(); ++i) {
            path.lineTo(localToScreen(geo.samples[i].x, geo.samples[i].y));
        }
        p.setPen(QPen(QColor(0, 255, 0, 200), 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);

        // Draw road boundaries (lighter green)
        double halfWidth = m_store->defaultWidth() / 2.0;
        QPainterPath leftPath, rightPath;
        for (int i = 0; i < geo.samples.size(); ++i) {
            // Compute normal at each sample point
            double dx, dy;
            if (i < geo.samples.size() - 1) {
                dx = geo.samples[i+1].x - geo.samples[i].x;
                dy = geo.samples[i+1].y - geo.samples[i].y;
            } else {
                dx = geo.samples[i].x - geo.samples[i-1].x;
                dy = geo.samples[i].y - geo.samples[i-1].y;
            }
            double len = std::sqrt(dx*dx + dy*dy);
            if (len < 1e-9) continue;
            double nx = -dy / len * halfWidth;
            double ny = dx / len * halfWidth;

            QPointF left = localToScreen(geo.samples[i].x + nx, geo.samples[i].y + ny);
            QPointF right = localToScreen(geo.samples[i].x - nx, geo.samples[i].y - ny);
            if (i == 0) {
                leftPath.moveTo(left);
                rightPath.moveTo(right);
            } else {
                leftPath.lineTo(left);
                rightPath.lineTo(right);
            }
        }
        p.setPen(QPen(QColor(100, 255, 100, 120), 1));
        p.drawPath(leftPath);
        p.drawPath(rightPath);
    }
}

void RoadOverlayWidget::drawFlexPreview(QPainter& p) {
    if (!m_flexValid || m_flexPreview.samples.size() < 2) return;

    // Draw flex preview as dashed yellow line
    QPainterPath path;
    path.moveTo(localToScreen(m_flexPreview.samples[0].x, m_flexPreview.samples[0].y));
    for (int i = 1; i < m_flexPreview.samples.size(); ++i) {
        path.lineTo(localToScreen(m_flexPreview.samples[i].x, m_flexPreview.samples[i].y));
    }
    p.setPen(QPen(QColor(255, 255, 0, 180), 2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Draw flex preview boundaries
    double halfWidth = m_store->defaultWidth() / 2.0;
    QPainterPath leftPath, rightPath;
    for (int i = 0; i < m_flexPreview.samples.size(); ++i) {
        double dx, dy;
        if (i < m_flexPreview.samples.size() - 1) {
            dx = m_flexPreview.samples[i+1].x - m_flexPreview.samples[i].x;
            dy = m_flexPreview.samples[i+1].y - m_flexPreview.samples[i].y;
        } else {
            dx = m_flexPreview.samples[i].x - m_flexPreview.samples[i-1].x;
            dy = m_flexPreview.samples[i].y - m_flexPreview.samples[i-1].y;
        }
        double len = std::sqrt(dx*dx + dy*dy);
        if (len < 1e-9) continue;
        double nx = -dy / len * halfWidth;
        double ny = dx / len * halfWidth;

        QPointF left = localToScreen(m_flexPreview.samples[i].x + nx, m_flexPreview.samples[i].y + ny);
        QPointF right = localToScreen(m_flexPreview.samples[i].x - nx, m_flexPreview.samples[i].y - ny);
        if (i == 0) {
            leftPath.moveTo(left);
            rightPath.moveTo(right);
        } else {
            leftPath.lineTo(left);
            rightPath.lineTo(right);
        }
    }
    p.setPen(QPen(QColor(255, 255, 100, 80), 1, Qt::DashLine));
    p.drawPath(leftPath);
    p.drawPath(rightPath);

    // Draw distance hint
    if (m_flexPreview.length > 0) {
        QPointF mid = localToScreen(
            (m_flexPreview.startPos.x + m_flexPreview.endPos.x) / 2.0,
            (m_flexPreview.startPos.y + m_flexPreview.endPos.y) / 2.0);
        p.setPen(QColor(255, 255, 255, 220));
        QFont font = p.font();
        font.setPointSize(10);
        font.setBold(true);
        p.setFont(font);
        p.drawText(mid + QPointF(5, -5), QString::number(m_flexPreview.length, 'f', 1) + " m");
    }
}

void RoadOverlayWidget::drawSnapIndicator(QPainter& p) {
    if (!m_store->isSnappingToRoad()) return;

    // Draw a green highlight at the snap point
    // (The snap state is set during click handling)
    p.setPen(QPen(QColor(0, 255, 0, 200), 2));
    p.setBrush(QColor(0, 255, 0, 80));
    p.drawEllipse(QPointF(width()/2.0, height()/2.0), 10, 10);
}

void RoadOverlayWidget::drawDestroyPreview(QPainter& p) {
    if (!m_destroyTarget) return;

    // Highlight the road segment that will be destroyed
    auto samples = m_engine->sampleCenterline(*m_destroyTarget, m_store->refLat(), m_store->refLon(), 64);

    double s1 = std::min(m_destroyS1, m_destroyS2);
    double s2 = std::max(m_destroyS1, m_destroyS2);

    // Find the sample range that corresponds to s1..s2
    QPainterPath path;
    bool started = false;
    double totalS = 0;
    for (size_t i = 0; i < samples.size(); ++i) {
        if (i > 0) {
            totalS += std::hypot(samples[i].x - samples[i-1].x, samples[i].y - samples[i-1].y);
        }
        if (totalS >= s1 && totalS <= s2) {
            QPointF screen = localToScreen(samples[i].x, samples[i].y);
            if (!started) {
                path.moveTo(screen);
                started = true;
            } else {
                path.lineTo(screen);
            }
        }
    }

    p.setPen(QPen(QColor(255, 0, 0, 200), 4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // Draw instruction
    p.setPen(QColor(255, 255, 255, 220));
    QFont font = p.font();
    font.setPointSize(10);
    font.setBold(true);
    p.setFont(font);
    if (m_destroyS1 == m_destroyS2) {
        p.drawText(10, height() - 20, "Click second point to select destroy range");
    } else {
        p.drawText(10, height() - 20, "Click again to confirm destroy");
    }
}

void RoadOverlayWidget::drawLaneConfigOverlay(QPainter& p) {
    const auto tool = m_store->tool();
    if (tool == roads::Tool::Select) return;

    // Draw mode hint at bottom
    QString hint;
    switch (tool) {
    case roads::Tool::Road:
        if (!m_store->isLmRoadActive()) {
            hint = "Road Mode: Click to set start point";
        } else if (!m_store->isDirectionHandleActive()) {
            hint = "Click to set first segment endpoint | Esc: cancel";
        } else {
            hint = "Click to add segment | Drag green handle to change direction | Space: finish | Esc: undo";
        }
        break;
    case roads::Tool::Lane:
        hint = "Lane Mode: Click on a road to create lanes/ramps";
        break;
    case roads::Tool::Destroy:
        hint = "Destroy Mode: Click two points on a road to destroy that segment";
        break;
    case roads::Tool::Modify:
        hint = "Modify Mode: Click on a road to modify its lane profile";
        break;
    default:
        break;
    }

    if (!hint.isEmpty()) {
        p.setPen(QColor(255, 255, 255, 200));
        QFont font = p.font();
        font.setPointSize(9);
        p.setFont(font);
        p.drawText(10, height() - 5, hint);
    }
}

void RoadOverlayWidget::drawDebugLayers(QPainter& p) {
    if (!m_store->debugMode()) return;

    // Debug indicator
    p.setPen(QColor(255, 100, 100, 200));
    QFont font = p.font();
    font.setPointSize(9);
    font.setBold(true);
    p.setFont(font);
    p.drawText(10, 20, "DEBUG MODE");

    // Draw debug layers for each road
    for (const auto& road : m_store->roads()) {
        if (road.points.size() < 2) continue;

        const double refLat = m_store->refLat();
        const double refLon = m_store->refLon();

        // Centerline (green, thicker)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::Centerline)) {
            auto samples = m_engine->sampleCenterline(road, refLat, refLon, 64);
            if (samples.size() >= 2) {
                QPainterPath path;
                path.moveTo(localToScreen(samples[0].x, samples[0].y));
                for (size_t i = 1; i < samples.size(); ++i) {
                    path.lineTo(localToScreen(samples[i].x, samples[i].y));
                }
                p.setPen(QPen(QColor(0, 255, 0, 200), 2));
                p.drawPath(path);
            }
        }

        // Left edge (red)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::LeftEdge)) {
            auto edge = m_engine->sampleLeftEdge(road, refLat, refLon, 64);
            if (edge.size() >= 2) {
                QPainterPath path;
                path.moveTo(localToScreen(edge[0].x, edge[0].y));
                for (size_t i = 1; i < edge.size(); ++i) {
                    path.lineTo(localToScreen(edge[i].x, edge[i].y));
                }
                p.setPen(QPen(QColor(255, 50, 50, 200), 1.5));
                p.drawPath(path);
            }
        }

        // Right edge (blue)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::RightEdge)) {
            auto edge = m_engine->sampleRightEdge(road, refLat, refLon, 64);
            if (edge.size() >= 2) {
                QPainterPath path;
                path.moveTo(localToScreen(edge[0].x, edge[0].y));
                for (size_t i = 1; i < edge.size(); ++i) {
                    path.lineTo(localToScreen(edge[i].x, edge[i].y));
                }
                p.setPen(QPen(QColor(50, 50, 255, 200), 1.5));
                p.drawPath(path);
            }
        }

        // Lane boundaries (white dashed)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::LaneBoundaries) ||
            m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::LaneBoundaryLines)) {
            auto boundaries = m_engine->generateLaneBoundaries(road, refLat, refLon, 64);
            p.setPen(QPen(QColor(255, 255, 255, 180), 1, Qt::DashLine));
            for (const auto& boundary : boundaries) {
                if (boundary.size() < 2) continue;
                QPainterPath path;
                path.moveTo(localToScreen(boundary[0].x, boundary[0].y));
                for (size_t i = 1; i < boundary.size(); ++i) {
                    path.lineTo(localToScreen(boundary[i].x, boundary[i].y));
                }
                p.drawPath(path);
            }
        }

        // Sample points (small dots)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::SamplePoints)) {
            auto samples = m_engine->sampleCenterline(road, refLat, refLon, 64);
            p.setBrush(QColor(255, 200, 0, 200));
            p.setPen(QPen(Qt::black, 0.5));
            for (const auto& s : samples) {
                QPointF screen = localToScreen(s.x, s.y);
                p.drawEllipse(screen, 2, 2);
            }
        }

        // Lane centers (cyan lines)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::LaneCenters)) {
            auto boundaries = m_engine->generateLaneBoundaries(road, refLat, refLon, 64);
            p.setPen(QPen(QColor(0, 255, 255, 150), 1));
            for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
                const auto& b1 = boundaries[i];
                const auto& b2 = boundaries[i + 1];
                size_t n = std::min(b1.size(), b2.size());
                if (n < 2) continue;
                QPainterPath path;
                path.moveTo(localToScreen((b1[0].x + b2[0].x) / 2, (b1[0].y + b2[0].y) / 2));
                for (size_t j = 1; j < n; ++j) {
                    path.lineTo(localToScreen((b1[j].x + b2[j].x) / 2, (b1[j].y + b2[j].y) / 2));
                }
                p.drawPath(path);
            }
        }

        // Road polygon outline (magenta)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::RoadPolygon)) {
            auto leftEdge = m_engine->sampleLeftEdge(road, refLat, refLon, 64);
            auto rightEdge = m_engine->sampleRightEdge(road, refLat, refLon, 64);
            if (leftEdge.size() >= 2 && rightEdge.size() >= 2) {
                QPainterPath path;
                path.moveTo(localToScreen(leftEdge[0].x, leftEdge[0].y));
                for (size_t i = 1; i < leftEdge.size(); ++i) {
                    path.lineTo(localToScreen(leftEdge[i].x, leftEdge[i].y));
                }
                for (int i = rightEdge.size() - 1; i >= 0; --i) {
                    path.lineTo(localToScreen(rightEdge[i].x, rightEdge[i].y));
                }
                path.closeSubpath();
                p.setPen(QPen(QColor(255, 0, 255, 150), 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
        }

        // Mesh wireframe (orange)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::MeshWireframe)) {
            auto mesh = m_engine->generateMesh(road, refLat, refLon, 32);
            if (!mesh.isEmpty() && mesh.indices.size() >= 2) {
                p.setPen(QPen(QColor(255, 165, 0, 100), 0.5));
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    unsigned int i0 = mesh.indices[i];
                    unsigned int i1 = mesh.indices[i + 1];
                    unsigned int i2 = mesh.indices[i + 2];
                    if (i0 * 3 + 2 < static_cast<unsigned int>(mesh.positions.size()) &&
                        i1 * 3 + 2 < static_cast<unsigned int>(mesh.positions.size()) &&
                        i2 * 3 + 2 < static_cast<unsigned int>(mesh.positions.size())) {
                        QPointF p0 = localToScreen(mesh.positions[i0 * 3], mesh.positions[i0 * 3 + 1]);
                        QPointF p1 = localToScreen(mesh.positions[i1 * 3], mesh.positions[i1 * 3 + 1]);
                        QPointF p2 = localToScreen(mesh.positions[i2 * 3], mesh.positions[i2 * 3 + 1]);
                        p.drawLine(p0, p1);
                        p.drawLine(p1, p2);
                        p.drawLine(p2, p0);
                    }
                }
            }
        }

        // Triangulation (filled triangles with alternating colors)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::Triangulation)) {
            auto mesh = m_engine->generateMesh(road, refLat, refLon, 32);
            if (!mesh.isEmpty() && mesh.indices.size() >= 3) {
                for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    unsigned int i0 = mesh.indices[i];
                    unsigned int i1 = mesh.indices[i + 1];
                    unsigned int i2 = mesh.indices[i + 2];
                    if (i0 * 3 + 2 < static_cast<unsigned int>(mesh.positions.size()) &&
                        i1 * 3 + 2 < static_cast<unsigned int>(mesh.positions.size()) &&
                        i2 * 3 + 2 < static_cast<unsigned int>(mesh.positions.size())) {
                        QPointF p0 = localToScreen(mesh.positions[i0 * 3], mesh.positions[i0 * 3 + 1]);
                        QPointF p1 = localToScreen(mesh.positions[i1 * 3], mesh.positions[i1 * 3 + 1]);
                        QPointF p2 = localToScreen(mesh.positions[i2 * 3], mesh.positions[i2 * 3 + 1]);
                        QColor fill = (i / 3) % 2 ? QColor(100, 200, 100, 60) : QColor(200, 100, 100, 60);
                        p.setBrush(fill);
                        p.setPen(QPen(QColor(255, 255, 255, 80), 0.5));
                        QPolygonF tri;
                        tri << p0 << p1 << p2;
                        p.drawPolygon(tri);
                    }
                }
            }
        }

        // Vertex normals (green arrows)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::VertexNormals)) {
            auto mesh = m_engine->generateMesh(road, refLat, refLon, 32);
            if (mesh.positions.size() >= 3 && mesh.normals.size() >= 3) {
                p.setPen(QPen(QColor(0, 255, 100, 150), 1));
                int vCount = mesh.positions.size() / 3;
                for (int i = 0; i < vCount; ++i) {
                    QPointF base = localToScreen(mesh.positions[i * 3], mesh.positions[i * 3 + 1]);
                    QPointF tip = localToScreen(
                        mesh.positions[i * 3] + mesh.normals[i * 3] * 5,
                        mesh.positions[i * 3 + 1] + mesh.normals[i * 3 + 1] * 5);
                    p.drawLine(base, tip);
                }
            }
        }

        // UV grid (colored grid based on UV coordinates)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::UVGrid)) {
            auto mesh = m_engine->generateMesh(road, refLat, refLon, 32);
            if (mesh.positions.size() >= 3 && mesh.uvs.size() >= 2) {
                int vCount = mesh.positions.size() / 3;
                for (int i = 0; i < vCount; ++i) {
                    QPointF pt = localToScreen(mesh.positions[i * 3], mesh.positions[i * 3 + 1]);
                    float u = mesh.uvs[i * 2];
                    float v = mesh.uvs[i * 2 + 1];
                    QColor color = QColor::fromHsvF(u, 0.8, v);
                    p.setBrush(color);
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(pt, 3, 3);
                }
            }
        }

        // Offset curves (parallel curves at lane offsets)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::OffsetCurves)) {
            auto centerline = m_engine->sampleCenterline(road, refLat, refLon, 64);
            if (centerline.size() >= 2) {
                double laneWidth = road.width / road.laneCount;
                for (int lane = -road.laneCount; lane <= road.laneCount; ++lane) {
                    if (lane == 0) continue;
                    double offset = lane * laneWidth;
                    p.setPen(QPen(QColor(100, 200, 255, 120), 1, Qt::DotLine));
                    QPainterPath path;
                    for (size_t i = 0; i < centerline.size(); ++i) {
                        // Simple perpendicular offset
                        if (i == 0 || i == centerline.size() - 1) continue;
                        double dx = centerline[i+1].x - centerline[i-1].x;
                        double dy = centerline[i+1].y - centerline[i-1].y;
                        double len = std::sqrt(dx*dx + dy*dy);
                        if (len < 1e-9) continue;
                        double nx = -dy / len * offset;
                        double ny = dx / len * offset;
                        QPointF pt = localToScreen(centerline[i].x + nx, centerline[i].y + ny);
                        if (i == 1) path.moveTo(pt);
                        else path.lineTo(pt);
                    }
                    p.drawPath(path);
                }
            }
        }

        // Lane IDs (text labels at lane centers)
        if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::LaneIds)) {
            auto boundaries = m_engine->generateLaneBoundaries(road, refLat, refLon, 64);
            p.setPen(QColor(255, 255, 100, 200));
            QFont idFont = p.font();
            idFont.setPointSize(8);
            idFont.setBold(true);
            p.setFont(idFont);
            for (int i = 0; i + 1 < static_cast<int>(boundaries.size()); ++i) {
                const auto& b1 = boundaries[i];
                const auto& b2 = boundaries[i + 1];
                if (b1.size() < 2 || b2.size() < 2) continue;
                size_t mid = b1.size() / 2;
                QPointF center = localToScreen(
                    (b1[mid].x + b2[mid].x) / 2,
                    (b1[mid].y + b2[mid].y) / 2);
                p.drawText(center + QPointF(-10, 5), QString("L%1").arg(i));
            }
        }
    }

    // Intersection debug layers (between pairs of roads)
    auto allRoads = m_store->roads();
    for (int i = 0; i < allRoads.size(); ++i) {
        for (int j = i + 1; j < allRoads.size(); ++j) {
            if (allRoads[i].points.size() < 2 || allRoads[j].points.size() < 2) continue;

            auto ix = m_engine->generateIntersection(allRoads[i], allRoads[j],
                                                      m_store->refLat(), m_store->refLon());
            if (!ix.valid) continue;

            // Intersection polygon (yellow filled)
            if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::IntersectionPolygon)) {
                if (ix.polygon.size() >= 3) {
                    QPainterPath path;
                    path.moveTo(localToScreen(ix.polygon[0].x, ix.polygon[0].y));
                    for (int k = 1; k < ix.polygon.size(); ++k) {
                        path.lineTo(localToScreen(ix.polygon[k].x, ix.polygon[k].y));
                    }
                    path.closeSubpath();
                    p.setBrush(QColor(255, 255, 0, 60));
                    p.setPen(QPen(QColor(255, 255, 0, 200), 1.5));
                    p.drawPath(path);
                }
            }

            // Fillet arcs (orange)
            if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::FilletArcs)) {
                if (ix.filletArcPoints.size() >= 2) {
                    p.setPen(QPen(QColor(255, 165, 0, 200), 2));
                    QPainterPath path;
                    path.moveTo(localToScreen(ix.filletArcPoints[0].x, ix.filletArcPoints[0].y));
                    for (int k = 1; k < ix.filletArcPoints.size(); ++k) {
                        path.lineTo(localToScreen(ix.filletArcPoints[k].x, ix.filletArcPoints[k].y));
                    }
                    p.drawPath(path);
                }
            }

            // Trim points (purple dots)
            if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::TrimPoints)) {
                p.setBrush(QColor(200, 0, 255, 200));
                p.setPen(QPen(Qt::black, 0.5));
                for (const auto& pt : ix.trimPoints) {
                    QPointF screen = localToScreen(pt.x, pt.y);
                    p.drawEllipse(screen, 4, 4);
                }
            }

            // Tangent points (red dots at fillet tangent points)
            if (m_store->debugLayerEnabled(RoadStudioStore::DebugLayer::TangentPoints)) {
                p.setBrush(QColor(255, 50, 50, 200));
                p.setPen(QPen(Qt::black, 0.5));
                if (ix.filletArcPoints.size() >= 2) {
                    QPointF s1 = localToScreen(ix.filletArcPoints[0].x, ix.filletArcPoints[0].y);
                    QPointF s2 = localToScreen(ix.filletArcPoints.last().x, ix.filletArcPoints.last().y);
                    p.drawEllipse(s1, 3, 3);
                    p.drawEllipse(s2, 3, 3);
                }
            }
        }
    }
}

// ============================================================
// Interaction
// ============================================================

void RoadOverlayWidget::mousePressEvent(QMouseEvent* event) {
    const QPointF pos = event->position();

    // Pan with middle button or shift+left
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

void RoadOverlayWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPointF pos = event->position();

    if (m_panning) {
        // Pan the map by dragging
        QPointF delta = pos - m_lastPanPos;
        if (m_map && m_map->map()) {
            m_map->map()->moveBy(delta);
        }
        m_lastPanPos = pos;
        update();
        return;
    }

    // Direction handle dragging
    if (m_draggingDirHandle && m_store->isDirectionHandleActive() && m_store->directionHandlePos()) {
        double localX, localY;
        screenToLocal(pos, localX, localY);
        double dx = localX - m_store->directionHandlePos()->x;
        double dy = localY - m_store->directionHandlePos()->y;
        double angle = std::atan2(dy, dx);
        m_store->updateDirectionHandleAngle(angle);

        // Re-fit the last staged geometry with the new end direction
        if (!m_store->stagedGeometries().isEmpty()) {
            const auto& lastGeo = m_store->stagedGeometries().last();
            roads::Vec2 newEndDir = {std::cos(angle), std::sin(angle)};
            auto refitted = generateFlexGeometry(lastGeo.startPos, lastGeo.startDir,
                                                  lastGeo.endPos, newEndDir);
            // Replace the last staged geometry
            m_store->popStagedGeometry();
            m_store->stageGeometry(refitted);
        }
        return;
    }

    // Update flex preview for LaneMaker road creation
    if (m_store->tool() == roads::Tool::Road && m_store->isDirectionHandleActive()) {
        double localX, localY;
        screenToLocal(pos, localX, localY);

        // Generate flex preview from last staged end to cursor
        auto lastEnd = m_store->directionHandlePos().value();
        auto lastDir = roads::Vec2{std::cos(m_store->directionHandleAngle()),
                                    std::sin(m_store->directionHandleAngle())};
        roads::Point2D cursor = {localX, localY};

        double dx = cursor.x - lastEnd.x;
        double dy = cursor.y - lastEnd.y;
        double len = std::sqrt(dx*dx + dy*dy);
        roads::Vec2 cursorDir = len > 1e-6 ? roads::Vec2{dx/len, dy/len} : lastDir;

        m_flexPreview = generateFlexGeometry(lastEnd, lastDir, cursor, cursorDir);
        m_flexValid = m_flexPreview.length > 1.0;
        update();
        return;
    }

    // Update preview point for LaneMaker (before first segment)
    if (m_store->tool() == roads::Tool::Road && m_store->isLmRoadActive() && !m_store->isDirectionHandleActive()) {
        double localX, localY;
        screenToLocal(pos, localX, localY);
        m_store->setPreviewPoint({localX, localY});

        // Generate flex preview from start to cursor
        auto start = m_store->lmRoadStart().value();
        auto startDir = m_store->lmRoadStartDir().value_or(roads::Vec2{1.0, 0.0});
        roads::Point2D cursor = {localX, localY};
        double dx = cursor.x - start.x;
        double dy = cursor.y - start.y;
        double len = std::sqrt(dx*dx + dy*dy);
        roads::Vec2 endDir = len > 1e-6 ? roads::Vec2{dx/len, dy/len} : startDir;

        m_flexPreview = generateFlexGeometry(start, startDir, cursor, endDir);
        m_flexValid = m_flexPreview.length > 1.0;
        update();
        return;
    }

    // Dragging control point
    if (m_draggingPoint) {
        double lat, lon;
        screenToGeo(pos, lat, lon);
        m_draggingPoint->lat = lat;
        m_draggingPoint->lon = lon;
        update();
    }
}

void RoadOverlayWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
    }
    m_draggingPoint = nullptr;
    m_draggingDirHandle = false;
}

void RoadOverlayWidget::wheelEvent(QWheelEvent* event) {
    // Forward wheel event to the MapLibre widget for zoom
    if (m_map && m_map->map()) {
        // Scale zoom by wheel delta
        const double zoomStep = event->angleDelta().y() > 0 ? 0.5 : -0.5;
        double newZoom = m_map->map()->zoom() + zoomStep;
        m_map->map()->setZoom(newZoom);
        event->accept();
    } else {
        event->ignore();
    }
    update();
}

void RoadOverlayWidget::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        // Cancel: pop last staged geometry, or cancel entire session
        if (!m_store->stagedGeometries().isEmpty()) {
            m_store->popStagedGeometry();
            if (m_store->stagedGeometries().isEmpty()) {
                m_store->clearDirectionHandle();
            } else {
                // Reset direction handle to end of last staged
                const auto& last = m_store->stagedGeometries().last();
                double angle = std::atan2(last.endDir.y, last.endDir.x);
                m_store->setDirectionHandle(last.endPos, angle);
            }
        } else if (m_store->isLmRoadActive()) {
            m_store->cancelLmRoad();
        } else {
            QWidget::keyPressEvent(event);
        }
        break;
    case Qt::Key_Space:
    case Qt::Key_Return:
        // Confirm: finish the road
        if (m_store->isLmRoadActive()) {
            m_store->finishLmRoad();
        }
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
    update();
}

void RoadOverlayWidget::handleClick(const QPointF& pos) {
    const auto tool = m_store->tool();

    if (tool == roads::Tool::Road) {
        handleLmRoadClick(pos);
    } else if (tool == roads::Tool::Destroy) {
        handleDestroyClick(pos);
    } else if (tool == roads::Tool::Modify) {
        handleModifyClick(pos);
    } else {
        // Select tool — hit test control points
        QString roadId;
        auto* cp = hitTestControlPoint(pos, roadId);
        if (cp) {
            auto* road = m_store->getRoad(roadId);
            int pointIdx = -1;
            if (road) {
                for (int i = 0; i < road->points.size(); ++i) {
                    if (&road->points[i] == cp) {
                        pointIdx = i;
                        break;
                    }
                }
            }
            roads::Selection sel;
            sel.roadId = roadId;
            if (pointIdx >= 0) sel.pointIndices.append(pointIdx);
            m_store->setSelection(sel);
            m_draggingPoint = cp;
            m_draggingRoadId = roadId;
        } else {
            m_store->clearSelection();
        }
    }
    update();
}

void RoadOverlayWidget::handleLmRoadClick(const QPointF& pos) {
    double localX, localY;
    screenToLocal(pos, localX, localY);

    // Check if clicking on direction handle
    if (m_store->isDirectionHandleActive() && hitTestDirectionHandle(pos)) {
        m_draggingDirHandle = true;
        return;
    }

    if (!m_store->isLmRoadActive()) {
        // First click — set start point
        // Default direction: toward the cursor (or east if cursor is at same point)
        roads::Point2D start = {localX, localY};
        roads::Vec2 dir = {1.0, 0.0};

        // Check snap to existing road
        double snapS;
        auto* snapRoad = hitTestRoad(pos, snapS);
        if (snapRoad && m_store->snapEnabled()) {
            // Snap to road endpoint
            if (snapS < 5.0 && snapRoad->points.size() >= 2) {
                // Snap to start
                start = {
                    (snapRoad->points[0].lat - m_store->refLat()) * 111320.0,
                    (snapRoad->points[0].lon - m_store->refLon()) * 111320.0 * std::cos(m_store->refLat() * M_PI / 180.0)
                };
                // Direction from first to second point
                if (snapRoad->points.size() >= 2) {
                    double dx = (snapRoad->points[1].lon - snapRoad->points[0].lon) * 111320.0 * std::cos(m_store->refLat() * M_PI / 180.0);
                    double dy = (snapRoad->points[1].lat - snapRoad->points[0].lat) * 111320.0;
                    double len = std::sqrt(dx*dx + dy*dy);
                    if (len > 1e-6) dir = {dx/len, dy/len};
                }
                m_store->setSnapToRoad(true, snapRoad->id, 0, true);
            } else if (snapS > 0 && snapRoad->points.size() >= 2) {
                // Snap to end — compute direction from last segment
                int n = snapRoad->points.size();
                start = {
                    (snapRoad->points[n-1].lat - m_store->refLat()) * 111320.0,
                    (snapRoad->points[n-1].lon - m_store->refLon()) * 111320.0 * std::cos(m_store->refLat() * M_PI / 180.0)
                };
                double dx = (snapRoad->points[n-1].lon - snapRoad->points[n-2].lon) * 111320.0 * std::cos(m_store->refLat() * M_PI / 180.0);
                double dy = (snapRoad->points[n-1].lat - snapRoad->points[n-2].lat) * 111320.0;
                double len = std::sqrt(dx*dx + dy*dy);
                if (len > 1e-6) dir = {dx/len, dy/len};
                m_store->setSnapToRoad(true, snapRoad->id, snapS, true);
            }
        }

        m_store->startLmRoad(start, dir);
    } else if (!m_store->isDirectionHandleActive()) {
        // No staged geometry yet — this click sets the first segment
        auto start = m_store->lmRoadStart().value();
        auto startDir = m_store->lmRoadStartDir().value_or(roads::Vec2{1.0, 0.0});

        roads::Point2D end = {localX, localY};
        // End direction: from start to end
        double dx = end.x - start.x;
        double dy = end.y - start.y;
        double len = std::sqrt(dx*dx + dy*dy);
        roads::Vec2 endDir = len > 1e-6 ? roads::Vec2{dx/len, dy/len} : roads::Vec2{1.0, 0.0};

        // Generate geometry using ConnectRays
        auto geo = generateFlexGeometry(start, startDir, end, endDir);

        if (geo.length > 1.0) {
            m_store->stageGeometry(geo);

            // Set direction handle at the end of this segment
            double endAngle = std::atan2(geo.endDir.y, geo.endDir.x);
            m_store->setDirectionHandle(geo.endPos, endAngle);
        }
    } else {
        // Direction handle is active — this click stages another segment
        // The flex preview shows where the next segment will go
        if (m_flexValid && m_flexPreview.length > 1.0) {
            m_store->stageGeometry(m_flexPreview);

            // Update direction handle to end of new segment
            double endAngle = std::atan2(m_flexPreview.endDir.y, m_flexPreview.endDir.x);
            m_store->setDirectionHandle(m_flexPreview.endPos, endAngle);
        }
    }
}

void RoadOverlayWidget::handleDestroyClick(const QPointF& pos) {
    double s;
    auto* road = hitTestRoad(pos, s);
    if (!road) return;

    if (m_destroyTarget != road) {
        m_destroyTarget = road;
        m_destroyS1 = s;
        m_destroyS2 = s;
    } else {
        m_destroyS2 = s;
        if (m_destroyS1 > m_destroyS2) std::swap(m_destroyS1, m_destroyS2);

        // Delete control points in the selected range
        // Simple approach: remove the road if most of it is selected
        if (m_destroyS2 - m_destroyS1 > road->points.size() * 5.0) {
            m_store->deleteRoad(road->id);
        }
        m_destroyTarget = nullptr;
    }
    update();
}

void RoadOverlayWidget::handleModifyClick(const QPointF& pos) {
    // Select a road to modify its lane profile
    double s;
    auto* road = hitTestRoad(pos, s);
    if (road) {
        roads::Selection sel;
        sel.roadId = road->id;
        m_store->setSelection(sel);
    }
    update();
}

bool RoadOverlayWidget::hitTestDirectionHandle(const QPointF& pos) {
    if (!m_store->isDirectionHandleActive() || !m_store->directionHandlePos()) return false;

    QPointF handleScreen = localToScreen(m_store->directionHandlePos()->x,
                                          m_store->directionHandlePos()->y);
    double dist = std::hypot(handleScreen.x() - pos.x(), handleScreen.y() - pos.y());

    // Inner radius ~4m, outer ~6m in screen pixels
    double innerPx = 4.0 * m_ppm;
    double outerPx = 6.0 * m_ppm;
    return dist >= innerPx && dist <= outerPx;
}

roads::Road* RoadOverlayWidget::hitTestRoad(const QPointF& pos, double& outS) {
    double localX, localY;
    screenToLocal(pos, localX, localY);

    double bestDist = 15.0; // 15px tolerance
    roads::Road* bestRoad = nullptr;
    double bestS = 0;

    for (auto& road : m_store->roads()) {
        if (road.points.size() < 2) continue;
        auto samples = m_engine->sampleCenterline(road, m_store->refLat(), m_store->refLon(), 64);
        for (size_t i = 0; i < samples.size(); ++i) {
            QPointF screen = localToScreen(samples[i].x, samples[i].y);
            double dist = std::hypot(screen.x() - pos.x(), screen.y() - pos.y());
            if (dist < bestDist) {
                bestDist = dist;
                bestRoad = m_store->getRoad(road.id);
                // Approximate s along the road
                double totalS = 0;
                for (size_t j = 1; j <= i && j < samples.size(); ++j) {
                    totalS += std::hypot(samples[j].x - samples[j-1].x, samples[j].y - samples[j-1].y);
                }
                bestS = totalS;
            }
        }
    }

    if (bestRoad) outS = bestS;
    return bestRoad;
}

roads::StagedGeometry RoadOverlayWidget::generateFlexGeometry(
    roads::Point2D start, roads::Vec2 startDir,
    roads::Point2D end, roads::Vec2 endDir) {

    roads::StagedGeometry geo;
    geo.startPos = start;
    geo.startDir = startDir;
    geo.endPos = end;
    geo.endDir = endDir;

#ifdef ENABLE_LANEMAKER
    // Use LaneMaker's ConnectRays
    odr::Vec2D startPos = {start.x, start.y};
    odr::Vec2D startHdg = {startDir.x, startDir.y};
    odr::Vec2D endPos = {end.x, end.y};
    odr::Vec2D endHdg = {endDir.x, endDir.y};

    try {
        auto geometry = LM::ConnectRays(startPos, startHdg, endPos, endHdg);
        if (geometry && geometry->length > 0) {
            geo.length = geometry->length;
            const int numSamples = 32;
            for (int i = 0; i <= numSamples; ++i) {
                double s = geo.length * i / numSamples;
                auto pt = geometry->get_point(s);
                geo.samples.append({pt[0], pt[1]});
            }
            // Get end direction from geometry
            auto grad = geometry->get_grad(geo.length);
            double len = std::sqrt(grad[0]*grad[0] + grad[1]*grad[1]);
            if (len > 1e-9) {
                geo.endDir = {grad[0]/len, grad[1]/len};
            }
            return geo;
        }
    } catch (...) {
        // Fall through to line fallback
    }
#endif

    // Fallback: straight line
    double dx = end.x - start.x;
    double dy = end.y - start.y;
    geo.length = std::sqrt(dx*dx + dy*dy);
    const int numSamples = 32;
    for (int i = 0; i <= numSamples; ++i) {
        double t = static_cast<double>(i) / numSamples;
        geo.samples.append({start.x + dx * t, start.y + dy * t});
    }
    if (geo.length > 1e-6) {
        geo.endDir = {dx/geo.length, dy/geo.length};
    }
    return geo;
}

roads::ControlPoint* RoadOverlayWidget::hitTestControlPoint(const QPointF& pos,
                                                             QString& outRoadId) {
    const double tolerance = 12.0; // 12px tolerance (matching reference)

    for (int r = 0; r < m_store->roads().size(); ++r) {
        const auto& road = m_store->roads()[r];
        for (int i = 0; i < road.points.size(); ++i) {
            QPointF screen;
            geoToScreen(road.points[i].lat, road.points[i].lon, screen);
            if (std::hypot(screen.x() - pos.x(), screen.y() - pos.y()) < tolerance) {
                outRoadId = road.id;
                // Return mutable pointer — store provides non-const access via getRoad
                auto* mutableRoad = m_store->getRoad(road.id);
                if (mutableRoad && i < mutableRoad->points.size()) {
                    return &mutableRoad->points[i];
                }
                return nullptr;
            }
        }
    }
    return nullptr;
}
