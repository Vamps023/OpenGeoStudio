// RoadCreationSession — LaneMaker-style road creation implementation

#include "RoadCreationSession.hpp"
#include "RoadGeometryHelper.hpp"
#include "../RoadEngineService.hpp"

#include <cmath>

namespace editor {

RoadCreationSession::RoadCreationSession(RoadStudioStore* store, RoadEngineService* engine)
    : m_store(store), m_engine(engine) {
}

SessionStatus RoadCreationSession::update(const EditorInput& input) {
    switch (input.type) {
        case InputType::MousePress:
            if (input.button == Qt::LeftButton) {
                return handleMousePress(input);
            }
            break;
        case InputType::MouseMove:
            handleMouseMove(input);
            break;
        default:
            break;
    }
    return SessionStatus::Running;
}

SessionStatus RoadCreationSession::complete() {
    // Space/Enter — finish the road from staged geometries
    if (m_store->isLmRoadActive()) {
        m_store->finishLmRoad();
    }
    return SessionStatus::Completed;
}

SessionStatus RoadCreationSession::cancel() {
    // Escape — pop last staged geometry, or cancel entire session
    if (!m_store->stagedGeometries().isEmpty()) {
        m_store->popStagedGeometry();
        if (m_store->stagedGeometries().isEmpty()) {
            m_store->clearDirectionHandle();
        } else {
            const auto& last = m_store->stagedGeometries().last();
            double angle = std::atan2(last.endDir.y, last.endDir.x);
            m_store->setDirectionHandle(last.endPos, angle);
        }
        return SessionStatus::Running;  // Still active, just popped a segment
    }
    if (m_store->isLmRoadActive()) {
        m_store->cancelLmRoad();
    }
    return SessionStatus::Cancelled;
}

QString RoadCreationSession::statusMessage() const {
    if (!m_store->isLmRoadActive()) {
        return "Road Tool: Click to place start point";
    }
    if (!m_store->isDirectionHandleActive()) {
        return "Road Tool: Move mouse to preview, click to place first segment";
    }
    if (m_store->stagedGeometries().isEmpty()) {
        return "Road Tool: Click to place first segment";
    }
    return QString("Road Tool: %1 segment(s) staged. Click to add more, Space to finish, Esc to undo")
        .arg(m_store->stagedGeometries().size());
}

SessionStatus RoadCreationSession::handleMousePress(const EditorInput& input) {
    roads::Point2D clickPos = {input.localX, input.localY};

    // If direction handle is active and we click on it, that's handled
    // by the viewport's drag logic (not the session).
    // The session only processes clicks that aren't on the handle.

    if (!m_store->isLmRoadActive()) {
        // First click — set start point
        roads::Point2D start = clickPos;
        roads::Vec2 dir = {1.0, 0.0};

        // Try snapping to existing road endpoint
        roads::Point2D snapPoint;
        roads::Vec2 snapDir;
        QString snapRoadId;
        bool snapIsStart;
        if (m_store->snapEnabled() &&
            trySnapToRoad(clickPos, snapPoint, snapDir, snapRoadId, snapIsStart)) {
            start = snapPoint;
            dir = snapDir;
            m_store->setSnapToRoad(true, snapRoadId, 0, true);
        } else {
            m_store->setSnapToRoad(false);
        }

        m_store->startLmRoad(start, dir);
        return SessionStatus::Running;
    }

    if (!m_store->isDirectionHandleActive()) {
        // No staged geometry yet — this click sets the first segment
        auto start = m_store->lmRoadStart().value();
        auto startDir = m_store->lmRoadStartDir().value_or(roads::Vec2{1.0, 0.0});

        roads::Point2D end = clickPos;
        roads::Vec2 endDir = RoadGeometryHelper::directionBetween(start, end);

        auto geo = RoadGeometryHelper::connectRays(start, startDir, end, endDir);

        if (geo.length > 1.0) {
            m_store->stageGeometry(geo);
            double endAngle = std::atan2(geo.endDir.y, geo.endDir.x);
            m_store->setDirectionHandle(geo.endPos, endAngle);
        }
        return SessionStatus::Running;
    }

    // Direction handle is active — stage another segment from the flex preview
    // The flex preview is computed by the viewport on mouse move and stored
    // in the store's preview state. We use the preview point to generate geometry.
    if (m_store->previewPoint()) {
        auto dirHandlePos = m_store->directionHandlePos().value();
        double dirAngle = m_store->directionHandleAngle();
        roads::Vec2 dirVec = {std::cos(dirAngle), std::sin(dirAngle)};

        roads::Point2D end = m_store->previewPoint().value();
        roads::Vec2 endDir = RoadGeometryHelper::directionBetween(dirHandlePos, end);

        auto geo = RoadGeometryHelper::connectRays(dirHandlePos, dirVec, end, endDir);

        if (geo.length > 1.0) {
            m_store->stageGeometry(geo);
            double endAngle = std::atan2(geo.endDir.y, geo.endDir.x);
            m_store->setDirectionHandle(geo.endPos, endAngle);
        }
    }
    return SessionStatus::Running;
}

void RoadCreationSession::handleMouseMove(const EditorInput& input) {
    if (!m_store->isLmRoadActive()) return;

    roads::Point2D mousePos = {input.localX, input.localY};
    m_store->setPreviewPoint(mousePos);
}

bool RoadCreationSession::trySnapToRoad(
    roads::Point2D query,
    roads::Point2D& outPoint, roads::Vec2& outDir,
    QString& outRoadId, bool& outIsStart) {

    for (const auto& road : m_store->roads()) {
        if (road.points.size() < 2) continue;

        if (RoadGeometryHelper::snapToEndpoints(
                road, query, m_store->refLat(), m_store->refLon(),
                SnapThreshold, outPoint, outDir, outIsStart)) {
            outRoadId = road.id;
            return true;
        }
    }
    return false;
}

} // namespace editor
