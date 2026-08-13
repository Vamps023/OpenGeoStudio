// RoadDestroySession — Implementation

#include "RoadDestroySession.hpp"
#include "RoadGeometryHelper.hpp"
#include "../RoadEngineService.hpp"

#include <cmath>
#include <algorithm>

namespace editor {

RoadDestroySession::RoadDestroySession(RoadStudioStore* store, RoadEngineService* engine)
    : m_store(store), m_engine(engine) {
}

SessionStatus RoadDestroySession::update(const EditorInput& input) {
    if (input.type != InputType::MousePress || input.button != Qt::LeftButton)
        return SessionStatus::Running;

    // Hit test: find the nearest road at the click position
    roads::Point2D clickPos = {input.localX, input.localY};
    double bestDist = 15.0;  // 15px-ish tolerance in meters
    QString bestRoadId;
    double bestS = 0;

    for (const auto& road : m_store->roads()) {
        if (road.points.size() < 2) continue;
        auto samples = m_engine->sampleCenterline(road, m_store->refLat(), m_store->refLon(), 64);
        double s, dist;
        roads::Point2D nearest;
        RoadGeometryHelper::nearestPointOnRoad(samples, clickPos, s, dist, nearest);
        if (dist < bestDist) {
            bestDist = dist;
            bestRoadId = road.id;
            bestS = s;
        }
    }

    if (bestRoadId.isEmpty()) {
        // Clicked empty space — reset
        m_targetRoadId.clear();
        m_hasFirstClick = false;
        return SessionStatus::Running;
    }

    if (!m_hasFirstClick || m_targetRoadId != bestRoadId) {
        // First click on a road (or different road)
        m_targetRoadId = bestRoadId;
        m_s1 = bestS;
        m_s2 = bestS;
        m_hasFirstClick = true;
    } else {
        // Second click on same road — define s-range
        m_s2 = bestS;
        if (m_s1 > m_s2) std::swap(m_s1, m_s2);

        // If most of the road is selected, delete the entire road
        auto* road = m_store->getRoad(m_targetRoadId);
        if (road) {
            auto samples = m_engine->sampleCenterline(*road, m_store->refLat(), m_store->refLon(), 64);
            double totalLen = 0;
            for (int i = 1; i < samples.size(); ++i) {
                totalLen += std::hypot(samples[i].x - samples[i-1].x,
                                       samples[i].y - samples[i-1].y);
            }
            if ((m_s2 - m_s1) > totalLen * 0.5) {
                m_store->deleteRoad(m_targetRoadId);
            }
        }
        m_targetRoadId.clear();
        m_hasFirstClick = false;
    }
    return SessionStatus::Running;
}

SessionStatus RoadDestroySession::complete() {
    // Space — if a road is targeted, delete it entirely
    if (!m_targetRoadId.isEmpty()) {
        m_store->deleteRoad(m_targetRoadId);
        m_targetRoadId.clear();
        m_hasFirstClick = false;
    }
    return SessionStatus::Running;  // Stay active for more deletions
}

SessionStatus RoadDestroySession::cancel() {
    m_targetRoadId.clear();
    m_hasFirstClick = false;
    return SessionStatus::Cancelled;
}

QString RoadDestroySession::statusMessage() const {
    if (!m_hasFirstClick) {
        return "Destroy Tool: Click on a road to select it for deletion";
    }
    return "Destroy Tool: Click again to define deletion range, or Space to delete entire road";
}

} // namespace editor
