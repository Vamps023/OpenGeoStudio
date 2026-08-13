// RoadModificationSession — Implementation

#include "RoadModificationSession.hpp"
#include "RoadGeometryHelper.hpp"
#include "../RoadEngineService.hpp"

#include <cmath>

namespace editor {

RoadModificationSession::RoadModificationSession(RoadStudioStore* store, RoadEngineService* engine)
    : m_store(store), m_engine(engine) {
}

SessionStatus RoadModificationSession::update(const EditorInput& input) {
    if (input.type != InputType::MousePress || input.button != Qt::LeftButton)
        return SessionStatus::Running;

    // Hit test: find the nearest road
    roads::Point2D clickPos = {input.localX, input.localY};
    double bestDist = 15.0;
    QString bestRoadId;

    for (const auto& road : m_store->roads()) {
        if (road.points.size() < 2) continue;
        auto samples = m_engine->sampleCenterline(road, m_store->refLat(), m_store->refLon(), 64);
        double s, dist;
        roads::Point2D nearest;
        RoadGeometryHelper::nearestPointOnRoad(samples, clickPos, s, dist, nearest);
        if (dist < bestDist) {
            bestDist = dist;
            bestRoadId = road.id;
        }
    }

    if (!bestRoadId.isEmpty()) {
        roads::Selection sel;
        sel.roadId = bestRoadId;
        m_store->setSelection(sel);
    }
    return SessionStatus::Running;
}

SessionStatus RoadModificationSession::complete() {
    return SessionStatus::Running;  // Stay active
}

SessionStatus RoadModificationSession::cancel() {
    return SessionStatus::Cancelled;
}

QString RoadModificationSession::statusMessage() const {
    return "Modify Tool: Click on a road to select it, then use the lane config panel";
}

} // namespace editor
