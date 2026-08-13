#pragma once

// ============================================================
// EditSession — Abstract base class for editing sessions
// ============================================================
//
// Mirrors LaneMaker's RoadDrawingSession pattern.
//
// Each editing mode (road creation, lane creation, destroy, modify)
// is implemented as a subclass of EditSession. The EditorController
// owns the active session and forwards input to it.
//
// Session lifecycle:
//   1. EditorController creates session when tool is activated
//   2. Input events are forwarded via update()
//   3. update() returns SessionStatus:
//      - Running: session continues
//      - Completed: session succeeded, controller commits via QUndoStack
//      - Cancelled: session aborted, controller discards
//   4. Space key confirms (triggers complete())
//   5. Escape key cancels (triggers cancel())
//
// Sessions do NOT own road data — they operate on the store via
// the EditorController. Sessions generate preview state that the
// viewport renders, but do not commit changes until completion.
//

#include "EditorInput.hpp"
#include "../RoadTypes.hpp"

#include <QString>

namespace editor {

class EditSession {
public:
    virtual ~EditSession() = default;

    // Process an input event. Returns the session status after processing.
    virtual SessionStatus update(const EditorInput& input) = 0;

    // Called when the user explicitly confirms (Space key).
    // Default implementation returns Completed.
    virtual SessionStatus complete() { return SessionStatus::Completed; }

    // Called when the user explicitly cancels (Escape key).
    // Default implementation returns Cancelled.
    virtual SessionStatus cancel() { return SessionStatus::Cancelled; }

    // The tool type this session implements
    virtual roads::Tool toolType() const = 0;

    // Human-readable status message for the status bar
    virtual QString statusMessage() const = 0;

    // Whether the session has a preview to render
    virtual bool hasPreview() const { return false; }
};

} // namespace editor
