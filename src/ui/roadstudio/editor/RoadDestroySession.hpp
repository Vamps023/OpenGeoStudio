#pragma once

// ============================================================
// RoadDestroySession — LaneMaker-style road destruction
// ============================================================
//
// Click on a road to select it, then click again (or drag) to
// select an s-range for deletion. Space confirms, Esc cancels.
// If the entire road is selected, the road is removed.
//

#include "EditSession.hpp"
#include "EditorInput.hpp"
#include "../RoadTypes.hpp"
#include "../RoadStudioStore.hpp"

class RoadEngineService;

namespace editor {

class RoadDestroySession : public EditSession {
public:
    explicit RoadDestroySession(RoadStudioStore* store, RoadEngineService* engine);

    SessionStatus update(const EditorInput& input) override;
    SessionStatus complete() override;
    SessionStatus cancel() override;

    roads::Tool toolType() const override { return roads::Tool::Destroy; }
    QString statusMessage() const override;
    bool hasPreview() const override { return m_targetRoadId != nullptr; }

    // Current destroy target (for viewport preview rendering)
    const QString& targetRoadId() const { return m_targetRoadId; }
    double sStart() const { return m_s1; }
    double sEnd() const { return m_s2; }

private:
    RoadStudioStore* m_store;
    RoadEngineService* m_engine;

    QString m_targetRoadId;
    double m_s1 = 0, m_s2 = 0;
    bool m_hasFirstClick = false;
};

} // namespace editor
