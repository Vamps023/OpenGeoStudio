#pragma once

// ============================================================
// RoadModificationSession — LaneMaker-style road modification
// ============================================================
//
// Click on a road to select it, then use the lane config widget
// to change its lane profile. Space confirms, Esc cancels.
//

#include "EditSession.hpp"
#include "EditorInput.hpp"
#include "../RoadTypes.hpp"
#include "../RoadStudioStore.hpp"

class RoadEngineService;

namespace editor {

class RoadModificationSession : public EditSession {
public:
    explicit RoadModificationSession(RoadStudioStore* store, RoadEngineService* engine);

    SessionStatus update(const EditorInput& input) override;
    SessionStatus complete() override;
    SessionStatus cancel() override;

    roads::Tool toolType() const override { return roads::Tool::Modify; }
    QString statusMessage() const override;

private:
    RoadStudioStore* m_store;
    RoadEngineService* m_engine;
};

} // namespace editor
