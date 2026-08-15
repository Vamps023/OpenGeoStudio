#pragma once

// ============================================================
// ProjectManager — Project CRUD and persistence
// ============================================================
//
// Replaces core/project/project-manager.ts.
// Handles .ogproj file loading/saving, project folder creation,
// and recent projects tracking.
//
// All IPC channels (PROJECT_CREATE, PROJECT_OPEN, PROJECT_SAVE, etc.)
// become direct C++ method calls.
//

#include "Project.hpp"
#include "../events/EventBus.hpp"
#include "../logger/Logger.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <vector>

class ProjectManager : public QObject {
    Q_OBJECT

public:
    // Project subfolders (mirrors project-manager.ts lines 79-95)
    static const QStringList kSubfolders;

    explicit ProjectManager(EventBus* bus, QObject* parent = nullptr);

    // --- Project CRUD ---

    // Create a new project (no folder)
    Project create(const QString& name, const QString& workspaceId = "home");

    // Create a new project with a folder structure
    Project createWithFolder(const QString& name, const QString& folderPath,
                              const QString& workspaceId = "home");

    // Open an existing .ogproj file
    std::optional<Project> open(const QString& filePath);

    // Save the current project to its filePath
    bool save();

    // Save the current project to a new filePath
    bool saveAs(const QString& filePath);

    // Close the current project
    void close();

    // --- Accessors ---

    const Project& current() const { return m_current; }
    bool hasProject() const { return !m_current.isNull(); }

    // Mark the current project as dirty (modified)
    void markDirty();

    // --- Recent projects ---

    struct RecentEntry {
        QString id;
        QString name;
        QString filePath;
        QString modifiedAt;
        bool pinned = false;
    };

    const std::vector<RecentEntry>& recent() const { return m_recent; }
    void addRecent(const Project& project);
    void removeRecent(const QString& filePath);
    void clearRecent();
    void togglePin(const QString& filePath);

    // Delete a project: removes from recent list and deletes the project
    // folder + .ogproj file from disk. Returns true if disk deletion succeeded.
    bool deleteProject(const QString& filePath, bool deleteFolder = true);

    // --- Autosave ---

    void setAutosaveInterval(int seconds); // 0 = disabled
    int autosaveInterval() const { return m_autosaveTimer.interval() / 1000; }

    // --- Project folder helpers ---

    QString getSubfolder(const QString& name) const;
    QString getExportPath() const;

signals:
    void projectCreated(const Project& project);
    void projectOpened(const Project& project);
    void projectSaved(const Project& project);
    void projectClosed();
    void projectChanged(const Project& project);
    void recentChanged();

private slots:
    void onAutosave();

private:
    void loadRecent();
    void saveRecent();
    void createFolderStructure(const QString& basePath);
    void updateTimestamp();

    EventBus* m_bus;
    Logger m_log;
    Project m_current;
    std::vector<RecentEntry> m_recent;
    QTimer m_autosaveTimer;
    QString m_recentFilePath;
};
