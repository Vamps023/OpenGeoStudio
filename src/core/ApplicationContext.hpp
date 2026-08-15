#pragma once

// ============================================================
// ApplicationContext — Central service container
// ============================================================
//
// Replaces the TypeScript AppContext + ServiceRegistry.
// Owns all core services and provides access to them.
//
// In the native app, IPC channels are replaced by direct
// method calls on services accessed through this context.
//

#include "../events/EventBus.hpp"
#include "../project/ProjectManager.hpp"
#include "../workspace/WorkspaceManager.hpp"
#include "../../ui/terrain/TerrainStore.hpp"

#include <QObject>
#include <memory>

class ApplicationContext : public QObject {
    Q_OBJECT

public:
    explicit ApplicationContext(QObject* parent = nullptr);

    // Service accessors
    EventBus& events() { return *m_eventBus; }
    ProjectManager& projects() { return *m_projectManager; }
    WorkspaceManager& workspaces() { return *m_workspaceManager; }
    TerrainStore& terrain() { return *m_terrainStore; }

    const EventBus& events() const { return *m_eventBus; }
    const ProjectManager& projects() const { return *m_projectManager; }
    const WorkspaceManager& workspaces() const { return *m_workspaceManager; }
    const TerrainStore& terrain() const { return *m_terrainStore; }

private:
    std::unique_ptr<EventBus> m_eventBus;
    std::unique_ptr<ProjectManager> m_projectManager;
    std::unique_ptr<WorkspaceManager> m_workspaceManager;
    std::unique_ptr<TerrainStore> m_terrainStore;
};
