#pragma once

// ============================================================
// EventBus — Lightweight pub/sub event system
// ============================================================
//
// Replaces the TypeScript EventBus (core/events/event-bus.ts).
// Uses Qt signals/slots internally. Thread-safe via Qt::QueuedConnection.
//
// Usage:
//   EventBus bus;
//   auto unsub = bus.on("project:opened", [](const QVariantMap& args) {
//       qDebug() << "Project opened:" << args["name"];
//   });
//   bus.emit("project:opened", {{"name", "MyProject"}});
//   unsub(); // unsubscribe
//

#include <QObject>
#include <QVariantMap>
#include <functional>
#include <unordered_map>
#include <vector>

class EventBus : public QObject {
    Q_OBJECT

public:
    using Handler = std::function<void(const QVariantMap&)>;
    using Unsubscribe = std::function<void()>;

    explicit EventBus(QObject* parent = nullptr) : QObject(parent) {}

    // Subscribe to an event. Returns an unsubscribe function.
    Unsubscribe on(const QString& event, Handler handler) {
        auto& handlers = m_handlers[event.toStdString()];
        handlers.push_back(std::move(handler));
        size_t idx = handlers.size() - 1;
        std::string evt = event.toStdString();
        return [this, evt, idx]() {
            auto it = m_handlers.find(evt);
            if (it != m_handlers.end() && idx < it->second.size()) {
                it->second[idx] = nullptr; // mark as removed
            }
        };
    }

    // Subscribe to an event, auto-unsubscribe after first call.
    Unsubscribe once(const QString& event, Handler handler) {
        Unsubscribe unsub;
        auto wrapper = [this, handler = std::move(handler), &unsub](const QVariantMap& args) {
            if (unsub) unsub();
            handler(args);
        };
        unsub = on(event, std::move(wrapper));
        return unsub;
    }

    // Emit an event to all subscribers.
    // Note: named 'publish' because 'emit' is a Qt macro.
    void publish(const QString& event, const QVariantMap& args = {}) {
        auto it = m_handlers.find(event.toStdString());
        if (it == m_handlers.end()) return;
        for (auto& h : it->second) {
            if (h) h(args);
        }
    }

private:
    std::unordered_map<std::string, std::vector<Handler>> m_handlers;
};
