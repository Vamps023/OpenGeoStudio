#pragma once

// ============================================================
// WorkspaceManager — Workspace definitions and switching
// ============================================================
//
// Replaces core/workspace/workspace-manager.ts.
// Manages the 4 workspaces: home, terrain, road-studio, train-studio.
// Each workspace defines which panels/docks are visible and which
// viewport widget is shown in the center.
//

#include "../events/EventBus.hpp"
#include "../logger/Logger.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>

struct Workspace {
    QString id;
    QString name;
    QString description;
    QString icon;       // icon name or path
    QStringList modules;
    QStringList panels; // panel ids for left/right docks
    QString centerViewport; // "home", "map", "road-2d", "road-3d", "train"
};

class WorkspaceManager : public QObject {
    Q_OBJECT

public:
    explicit WorkspaceManager(EventBus* bus, QObject* parent = nullptr);

    const std::vector<Workspace>& workspaces() const { return m_workspaces; }

    const Workspace* active() const;
    QString activeId() const { return m_activeId; }

    void activate(const QString& id);

    const Workspace* getById(const QString& id) const;

signals:
    void workspaceActivated(const Workspace& workspace);

private:
    void registerDefaults();

    EventBus* m_bus;
    Logger m_log;
    std::vector<Workspace> m_workspaces;
    QString m_activeId = "home";
};
