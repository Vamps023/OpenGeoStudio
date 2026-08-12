// ApplicationContext — Central service container implementation

#include "ApplicationContext.hpp"

ApplicationContext::ApplicationContext(QObject* parent)
    : QObject(parent) {
    m_eventBus = std::make_unique<EventBus>(this);
    m_projectManager = std::make_unique<ProjectManager>(m_eventBus.get(), this);
    m_workspaceManager = std::make_unique<WorkspaceManager>(m_eventBus.get(), this);
}
