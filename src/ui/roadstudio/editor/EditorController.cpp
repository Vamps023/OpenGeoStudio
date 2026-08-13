// EditorController — Session management implementation

#include "EditorController.hpp"
#include "../RoadEngineService.hpp"

#include <QKeyEvent>

namespace editor {

EditorController::EditorController(RoadStudioStore* store, RoadEngineService* engine,
                                     QObject* parent)
    : QObject(parent), m_store(store), m_engine(engine) {
}

void EditorController::activateTool(roads::Tool tool) {
    // Cancel any active session
    if (m_session) {
        m_session->cancel();
        m_session.reset();
    }

    m_tool = tool;

    // Select tool doesn't create a session — it uses direct manipulation
    if (tool != roads::Tool::Select) {
        createSession(tool);
    }

    emit sessionChanged();
    emit statusChanged(statusMessage());
    emit stateChanged();
}

void EditorController::deactivateTool() {
    if (m_session) {
        m_session->cancel();
        m_session.reset();
    }
    m_tool = roads::Tool::Select;
    emit sessionChanged();
    emit statusChanged(statusMessage());
    emit stateChanged();
}

void EditorController::createSession(roads::Tool tool) {
    // Phase 24: Only the framework is in place.
    // Concrete sessions (RoadCreationSession, LaneCreationSession, etc.)
    // will be implemented in Phase 25+.
    //
    // For now, no session is created — the existing inline viewport logic
    // continues to handle road creation, destroy, and modify.
    //
    // The EditorController is ready to receive sessions once they are
    // implemented.
    (void)tool;
}

void EditorController::mousePress(QPointF screenPos, double localX, double localY,
                                    double lat, double lon,
                                    Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!m_session) return;

    auto input = EditorInput::mousePress(screenPos, localX, localY, lat, lon, button, mods);
    auto status = m_session->update(input);

    if (status != SessionStatus::Running) {
        endSession(status);
    } else {
        emit stateChanged();
    }
}

void EditorController::mouseMove(QPointF screenPos, double localX, double localY,
                                   double lat, double lon,
                                   Qt::KeyboardModifiers mods) {
    if (!m_session) return;

    auto input = EditorInput::mouseMove(screenPos, localX, localY, lat, lon, mods);
    auto status = m_session->update(input);

    if (status != SessionStatus::Running) {
        endSession(status);
    } else {
        emit stateChanged();
    }
}

void EditorController::mouseRelease(QPointF screenPos, double localX, double localY,
                                      double lat, double lon,
                                      Qt::MouseButton button, Qt::KeyboardModifiers mods) {
    if (!m_session) return;

    auto input = EditorInput::mouseRelease(screenPos, localX, localY, lat, lon, button, mods);
    auto status = m_session->update(input);

    if (status != SessionStatus::Running) {
        endSession(status);
    } else {
        emit stateChanged();
    }
}

void EditorController::keyPress(int key, const QString& text, Qt::KeyboardModifiers mods) {
    if (!m_session) return;

    // Space = confirm, Escape = cancel
    if (key == Qt::Key_Space) {
        endSession(m_session->complete());
        return;
    }
    if (key == Qt::Key_Escape) {
        endSession(m_session->cancel());
        return;
    }

    auto input = EditorInput::keyPress(key, text, mods);
    auto status = m_session->update(input);

    if (status != SessionStatus::Running) {
        endSession(status);
    } else {
        emit stateChanged();
    }
}

void EditorController::endSession(SessionStatus status) {
    if (!m_session) return;

    if (status == SessionStatus::Completed) {
        // The session's complete() has already been called.
        // In Phase 25+, the session will have produced a result
        // that gets committed via the QUndoStack.
        // For now, just clear the session.
    }

    // Cancelled or Completed — either way, the session is done
    m_session.reset();
    emit sessionChanged();
    emit statusChanged(statusMessage());
    emit stateChanged();
}

QString EditorController::statusMessage() const {
    if (!m_session) {
        switch (m_tool) {
            case roads::Tool::Select: return "Select mode — click to select, drag to move";
            case roads::Tool::Road:   return "Road tool — click to place points";
            case roads::Tool::Lane:   return "Lane tool — click source and destination roads";
            case roads::Tool::Destroy: return "Destroy tool — click road to remove";
            case roads::Tool::Modify:  return "Modify tool — click road to change profile";
        }
        return {};
    }
    return m_session->statusMessage();
}

} // namespace editor
