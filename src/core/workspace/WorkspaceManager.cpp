// WorkspaceManager — Workspace definitions and switching implementation

#include "WorkspaceManager.hpp"

WorkspaceManager::WorkspaceManager(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus), m_log("WorkspaceManager") {
    registerDefaults();
    m_log.info("Registered", m_workspaces.size(), "workspaces");
}

void WorkspaceManager::registerDefaults() {
    // Home — start screen with recent projects and templates
    m_workspaces.push_back(Workspace{
        "home", "Home", "Start screen with recent projects and templates",
        "", {}, {"project-explorer", "recent-projects"}, "home"
    });

    // Terrain — map area selection and export
    m_workspaces.push_back(Workspace{
        "terrain", "Terrain", "Map area selection and terrain export",
        "", {"terrain", "export"}, {"export-panel", "job-queue"}, "map"
    });

    // Road Studio — road network design
    m_workspaces.push_back(Workspace{
        "road-studio", "Road Studio", "Road network design with C++ geometry engine",
        "", {"road-studio"}, {"road-inspector", "road-toolbar"}, "road-2d"
    });

    // Train Studio — railway design
    m_workspaces.push_back(Workspace{
        "train-studio", "Train Studio", "Railway design and simulation",
        "", {"train-studio"}, {"train-inspector"}, "train"
    });
}

const Workspace* WorkspaceManager::active() const {
    return getById(m_activeId);
}

const Workspace* WorkspaceManager::getById(const QString& id) const {
    for (const auto& ws : m_workspaces) {
        if (ws.id == id) return &ws;
    }
    return nullptr;
}

void WorkspaceManager::activate(const QString& id) {
    if (id == m_activeId) return;
    const Workspace* ws = getById(id);
    if (!ws) {
        m_log.warn("Unknown workspace id:", id);
        return;
    }

    m_activeId = id;
    m_log.info("Workspace activated:", ws->name);
    m_bus->publish("workspace:activated", {{"id", id}, {"name", ws->name}});
    emit workspaceActivated(*ws);
}
