#pragma once

// ============================================================
// EditorController — Manages the active editing session
// ============================================================
//
// The EditorController sits between the viewport (which receives
// raw Qt mouse/key events) and the EditSession (which processes
// editor-level input).
//
// Responsibilities:
//   - Create the appropriate EditSession when a tool is activated
//   - Translate Qt events into EditorInput structs
//   - Forward input to the active session
//   - Handle session completion (commit via QUndoStack)
//   - Handle session cancellation (discard)
//   - Emit signals for UI updates (status bar, viewport repaint)
//
// The controller does NOT render anything — it only manages state.
// The viewport reads session state for preview rendering.
//

#include "EditSession.hpp"
#include "EditorInput.hpp"
#include "../RoadTypes.hpp"
#include "../RoadStudioStore.hpp"

#include <QObject>
#include <QPointF>
#include <memory>

class RoadEngineService;

namespace editor {

class EditorController : public QObject {
    Q_OBJECT

public:
    explicit EditorController(RoadStudioStore* store, RoadEngineService* engine,
                               QObject* parent = nullptr);

    // Activate a tool — creates the appropriate session
    void activateTool(roads::Tool tool);

    // Deactivate the current tool (cancels any active session)
    void deactivateTool();

    // Forward a Qt mouse press event to the active session
    void mousePress(QPointF screenPos, double localX, double localY,
                    double lat, double lon,
                    Qt::MouseButton button, Qt::KeyboardModifiers mods);

    // Forward a Qt mouse move event to the active session
    void mouseMove(QPointF screenPos, double localX, double localY,
                   double lat, double lon,
                   Qt::KeyboardModifiers mods);

    // Forward a Qt mouse release event to the active session
    void mouseRelease(QPointF screenPos, double localX, double localY,
                      double lat, double lon,
                      Qt::MouseButton button, Qt::KeyboardModifiers mods);

    // Forward a Qt key press event to the active session
    void keyPress(int key, const QString& text, Qt::KeyboardModifiers mods);

    // Whether a session is currently active
    bool hasActiveSession() const { return m_session != nullptr; }

    // The active session (nullptr if no session)
    EditSession* activeSession() const { return m_session.get(); }

    // The current tool
    roads::Tool activeTool() const { return m_tool; }

    // Status message for the status bar
    QString statusMessage() const;

signals:
    // Emitted when a session starts or ends
    void sessionChanged();

    // Emitted when the status message changes
    void statusChanged(const QString& message);

    // Emitted when the session state changes (triggers viewport repaint)
    void stateChanged();

private:
    void endSession(SessionStatus status);
    void createSession(roads::Tool tool);

    RoadStudioStore* m_store;
    RoadEngineService* m_engine;
    std::unique_ptr<EditSession> m_session;
    roads::Tool m_tool = roads::Tool::Select;
};

} // namespace editor
