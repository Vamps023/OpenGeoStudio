#pragma once

// ============================================================
// EditorInput — Input types for the editor session framework
// ============================================================
//
// Mirrors LaneMaker's MouseAction / KeyPressAction pattern.
// The EditorController translates Qt mouse/key events into
// EditorInput structs and forwards them to the active EditSession.
//
// This keeps Qt event types out of the session classes, making
// sessions testable without a QWidget.
//

#include <QPointF>
#include <Qt>
#include <QString>

namespace editor {

// Type of editor input event
enum class InputType {
    MousePress,
    MouseMove,
    MouseRelease,
    MouseDoubleClick,
    KeyPress
};

// A single editor input event (mouse or keyboard)
struct EditorInput {
    InputType type = InputType::MousePress;

    // Screen coordinates (pixels, relative to viewport)
    QPointF screenPos;

    // Local meters (relative to reference origin)
    double localX = 0;
    double localY = 0;

    // Geographic coordinates (lat/lon degrees)
    double lat = 0;
    double lon = 0;

    // Mouse button (for mouse events)
    Qt::MouseButton button = Qt::NoButton;

    // Keyboard modifiers
    Qt::KeyboardModifiers modifiers;

    // Key code (for key events)
    int key = 0;

    // Key text (for key events)
    QString keyText;

    // Convenience constructors
    static EditorInput mousePress(QPointF screen, double lx, double ly,
                                   double lat, double lon,
                                   Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        EditorInput e;
        e.type = InputType::MousePress;
        e.screenPos = screen;
        e.localX = lx; e.localY = ly;
        e.lat = lat; e.lon = lon;
        e.button = btn;
        e.modifiers = mods;
        return e;
    }

    static EditorInput mouseMove(QPointF screen, double lx, double ly,
                                  double lat, double lon,
                                  Qt::KeyboardModifiers mods) {
        EditorInput e;
        e.type = InputType::MouseMove;
        e.screenPos = screen;
        e.localX = lx; e.localY = ly;
        e.lat = lat; e.lon = lon;
        e.modifiers = mods;
        return e;
    }

    static EditorInput mouseRelease(QPointF screen, double lx, double ly,
                                     double lat, double lon,
                                     Qt::MouseButton btn, Qt::KeyboardModifiers mods) {
        EditorInput e;
        e.type = InputType::MouseRelease;
        e.screenPos = screen;
        e.localX = lx; e.localY = ly;
        e.lat = lat; e.lon = lon;
        e.button = btn;
        e.modifiers = mods;
        return e;
    }

    static EditorInput keyPress(int key, const QString& text, Qt::KeyboardModifiers mods) {
        EditorInput e;
        e.type = InputType::KeyPress;
        e.key = key;
        e.keyText = text;
        e.modifiers = mods;
        return e;
    }
};

// Session status returned from EditSession::update()
enum class SessionStatus {
    Running,    // Session is active, waiting for more input
    Completed,  // Session finished successfully, commit the result
    Cancelled   // Session was cancelled, discard the result
};

} // namespace editor
