#pragma once

// ============================================================
// RoadCreationSession — LaneMaker-style road creation
// ============================================================
//
// Implements the multi-click staged road creation workflow
// mirroring LaneMaker's RoadCreationSession.
//
// Workflow:
//   1. Click 1: Set start point + initial direction
//      (snaps to existing road endpoints if close)
//   2. Click 2: Set end of first segment, generate geometry
//      via ConnectRays, show direction handle at end
//   3. Click 3+: Stage additional segments from direction handle
//   4. Space/Enter: Confirm — build road from staged geometries
//   5. Escape: Cancel (or pop last staged segment if any exist)
//
// The session updates the store's LaneMaker workflow state
// (startLmRoad, stageGeometry, directionHandle, etc.) which
// the viewport renders as preview graphics.
//

#include "EditSession.hpp"
#include "EditorInput.hpp"
#include "../RoadTypes.hpp"
#include "../RoadStudioStore.hpp"

class RoadEngineService;

namespace editor {

class RoadCreationSession : public EditSession {
public:
    explicit RoadCreationSession(RoadStudioStore* store, RoadEngineService* engine);

    SessionStatus update(const EditorInput& input) override;
    SessionStatus complete() override;
    SessionStatus cancel() override;

    roads::Tool toolType() const override { return roads::Tool::Road; }

    QString statusMessage() const override;
    bool hasPreview() const override { return true; }

private:
    // Handle mouse press in the road creation workflow
    SessionStatus handleMousePress(const EditorInput& input);

    // Handle mouse move — updates preview point
    void handleMouseMove(const EditorInput& input);

    // Try to snap to an existing road endpoint
    bool trySnapToRoad(roads::Point2D query, roads::Point2D& outPoint,
                       roads::Vec2& outDir, QString& outRoadId, bool& outIsStart);

    RoadStudioStore* m_store;
    RoadEngineService* m_engine;

    // Snap threshold in meters
    static constexpr double SnapThreshold = 8.0;
};

} // namespace editor
