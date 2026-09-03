#pragma once

// ============================================================
// AppMainWindow — the application shell
// ============================================================
// Menu bar, toolbar, status bar, workspace stack and docks.
// Styling comes from the global theme (ogs::theme).
// Implementation lives in AppMainWindow.cpp.
// ============================================================

#include <QMainWindow>
#include <QMap>

class QAction;
class QDockWidget;
class QLabel;
class QMenu;
class QStackedWidget;
class QWidget;
class QString;

struct Workspace;
class Project;
class ApplicationContext;
class HomeWidget;
class RoadStudioWidget;
class TrainStudioWidget;
class TerrainStudioWidget;
class Studio3DWidget;

// Bridge for road_engine::versionString(). The engine's public headers
// define geo:: types that collide with the parallel copies in
// src/engine/road/*.hpp inside AUTOMOC's single translation unit, so the
// version function is defined in main.cpp instead of pulling the headers.
namespace ogs::appdetail { const char* roadEngineVersion(); }

class AppMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AppMainWindow(ApplicationContext* ctx, QWidget* parent = nullptr);

    // Entry points for command-line open / scripting (used by main.cpp)
    void openProjectPath(const QString& path);
    void activate3DStudio();

private:
    // ── UI construction ─────────────────────────────────────────
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupCenterWidget();
    void setupDockWidgets();

    // Road Studio menu-bar integration (LaneMaker actions hoisted
    // into the app menu bar while Road Studio is active)
    void setupRoadStudioMenus();
    void showRoadStudioMenus(bool show);

    // ── State / actions ─────────────────────────────────────────
    void openSettings();
    void updateStatusBar();
    void onWorkspaceActivated(const Workspace& ws);
    void onProjectChanged(const Project& proj);
    void onProjectOpened(const Project& proj);
    void propagateProjectCrs();

    void onNewProject();
    void onNewProjectFromTemplate(const QString& templateId);
    void onOpenProject();
    void createProjectNamed(const QString& name, const QString& templateId);

    // Serializes TerrainStore + road .xodr into the project folder
    // and .ogproj moduleState so all studios share the same project.
    void saveProjectState();
    void loadProjectState();

    // Write the current Road Studio network to {project}/Roads/road.xodr
    QString exportRoadToProjectFolder();

    // Focus a LaneMaker window on the currently selected terrain area
    void syncLaneMakerViewToTerrain(class MainWindow* laneMaker);

    // ── Members ─────────────────────────────────────────────────
    ApplicationContext* m_ctx = nullptr;
    QLabel* m_statusLabel = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    QMap<QString, QAction*> m_workspaceActions;
    HomeWidget* m_homeWidget = nullptr;
    RoadStudioWidget* m_roadStudioWidget = nullptr;
    TrainStudioWidget* m_trainStudioWidget = nullptr;
    TerrainStudioWidget* m_terrainStudioWidget = nullptr;
    Studio3DWidget* m_studio3DWidget = nullptr;
    QString m_pendingRoadFile;  // deferred road file to load when Road Studio is activated
    QDockWidget* m_leftDock = nullptr;
    QDockWidget* m_rightDock = nullptr;
    QWidget* m_rightDockPlaceholder = nullptr;

    // Road Studio menu bar integration
    QMenu* m_roadFileMenu = nullptr;
    QMenu* m_roadEditMenu = nullptr;
    QMenu* m_roadReplayMenu = nullptr;
    QMenu* m_roadSimulationMenu = nullptr;
    QAction* m_roadSimToggleAct = nullptr;
    QAction* m_roadSimPauseAct = nullptr;
    QAction* m_roadUndoAct = nullptr;
    QAction* m_roadRedoAct = nullptr;
};
