// ============================================================
// AppMainWindow.cpp — application shell implementation
// ============================================================
//
// NOTE: deliberately do NOT include road_engine.hpp here. Its public
// headers define geo:: types that collide with the parallel copies in
// src/engine/road/*.hpp when combined with the OSM import chain inside
// AUTOMOC's single translation unit. We only need the version string,
// which main.cpp bridges via ogs::appdetail::roadEngineVersion().

#include "AppMainWindow.hpp"

#include "../../theme/Theme.hpp"

#include "SettingsDialog.hpp"
#include "CommandPalette.hpp"

#include "core/ApplicationContext.hpp"
#include "core/project/ProjectManager.hpp"
#include "core/project/Project.hpp"
#include "core/logger/Logger.hpp"
#include "ui/terrain/TerrainTypes.hpp"
#include "core/workspace/WorkspaceManager.hpp"
#include "ui/home/HomeWidget.hpp"
#include "ui/roadstudio/RoadStudioWidget.hpp"
#include "ui/trainstudio/TrainStudioWidget.hpp"
#include "ui/terrain/TerrainStudioWidget.hpp"
#include "ui/studio3d/Studio3DWidget.hpp"
#include "main_window.h"
#include "gis/ui/CrsSelectorDialog.hpp"
#include "gis/crs/CRSManager.hpp"

#if defined(HAVE_MAPLIBRE)
#include "app/MapViewportWidget.hpp"
#endif

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QShortcut>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>

#include <cmath>

namespace th = ogs::theme;

// ─── Construction ────────────────────────────────────────────────
AppMainWindow::AppMainWindow(ApplicationContext* ctx, QWidget* parent)
    : QMainWindow(parent), m_ctx(ctx)
{
    setWindowTitle(QStringLiteral("OpenGeoStudio"));
    resize(1400, 900);

    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCenterWidget();
    setupDockWidgets();

    // Wire workspace switching
    connect(&m_ctx->workspaces(), &WorkspaceManager::workspaceActivated,
            this, [this](const Workspace& ws) { onWorkspaceActivated(ws); });

    // Wire project changes
    connect(&m_ctx->projects(), &ProjectManager::projectChanged,
            this, [this](const Project& p) { onProjectChanged(p); });
    connect(&m_ctx->projects(), &ProjectManager::projectOpened,
            this, [this](const Project& p) { onProjectOpened(p); });

    updateStatusBar();
}

void AppMainWindow::openProjectPath(const QString& path) {
    m_ctx->projects().open(path);
}

void AppMainWindow::activate3DStudio() {
    m_ctx->workspaces().activate("3d-studio");
}

// ─── Menu bar ────────────────────────────────────────────────────
void AppMainWindow::setupMenuBar() {
    // File menu
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    QAction* newProjectAct = fileMenu->addAction(tr("&New Project..."));
    newProjectAct->setShortcut(QKeySequence::New);
    connect(newProjectAct, &QAction::triggered, this, &AppMainWindow::onNewProject);

    QAction* openProjectAct = fileMenu->addAction(tr("&Open Project..."));
    openProjectAct->setShortcut(QKeySequence::Open);
    connect(openProjectAct, &QAction::triggered, this, &AppMainWindow::onOpenProject);

    fileMenu->addSeparator();

    QAction* saveAct = fileMenu->addAction(tr("&Save Project"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, [this]() {
        saveProjectState();
    });

    fileMenu->addSeparator();

    QAction* setCrsAct = fileMenu->addAction(tr("Set Project &CRS..."));
    connect(setCrsAct, &QAction::triggered, this, [this]() {
        gis::CrsSelectorDialog dlg(this);
        // Pre-select current project CRS if set
        auto& proj = m_ctx->projects().current();
        if (proj.hasProjectCRS()) {
            auto def = gis::CRSManager::instance().fromAuthId(
                proj.projectCrsAuthId.toStdString());
            if (def) dlg.setSelectedCRS(*def);
        }

        connect(&dlg, &gis::CrsSelectorDialog::crsSelected, this,
            [this](const gis::CRSDefinition& crs) {
                auto& p = m_ctx->projects().current();
                p.projectCrsAuthId = QString::fromStdString(
                    crs.authority + ":" + std::to_string(crs.code));
                p.projectCrsName = QString::fromStdString(crs.name);
                p.projectCrsWkt2 = QString::fromStdString(crs.wkt2);
                m_ctx->projects().markDirty();
                propagateProjectCrs();
                updateStatusBar();
            });
        dlg.exec();
    });

    fileMenu->addSeparator();

    QAction* exitAct = fileMenu->addAction(tr("E&xit"));
    exitAct->setShortcut(QKeySequence::Quit);
    connect(exitAct, &QAction::triggered, qApp, &QApplication::quit);

    // View menu — workspace switching
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    for (const auto& ws : m_ctx->workspaces().workspaces()) {
        auto* act = viewMenu->addAction(ws.name);
        connect(act, &QAction::triggered, this, [this, id = ws.id]() {
            m_ctx->workspaces().activate(id);
        });
    }

    // Help menu
    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* aboutAct = helpMenu->addAction(tr("&About OpenGeoStudio"));
    connect(aboutAct, &QAction::triggered, this, [this]() {
        const QString version = QString::fromLatin1(ogs::appdetail::roadEngineVersion());
        QMessageBox::information(this, tr("About OpenGeoStudio"),
            tr("<h3>OpenGeoStudio</h3>"
               "<p>Native C++/Qt 6 desktop application</p>"
               "<p>Road Engine: v%1</p>"
               "<p>Licensed under MIT + Apache-2.0</p>")
            .arg(version));
    });

    // Command palette shortcut (Ctrl+Shift+P)
    auto* cmdPaletteShortcut = new QShortcut(QKeySequence("Ctrl+Shift+P"), this);
    connect(cmdPaletteShortcut, &QShortcut::activated, this, [this]() {
        CommandPalette palette(this);
        if (palette.exec() == QDialog::Accepted) {
            QString cmd = palette.selectedCommand();
            if (cmd == "file.new") onNewProject();
            else if (cmd == "file.open") onOpenProject();
            else if (cmd == "file.save") saveProjectState();
            else if (cmd == "ws.home") m_ctx->workspaces().activate("home");
            else if (cmd == "ws.terrain") m_ctx->workspaces().activate("terrain");
            else if (cmd == "ws.road") m_ctx->workspaces().activate("road-studio");
            else if (cmd == "ws.train") m_ctx->workspaces().activate("train-studio");
            else if (cmd == "ws.3d") m_ctx->workspaces().activate("3d-studio");
            else if (cmd == "settings.open") openSettings();
            else if (cmd == "help.about") {
                const QString version = QString::fromLatin1(ogs::appdetail::roadEngineVersion());
                QMessageBox::information(this, tr("About OpenGeoStudio"),
                    tr("<h3>OpenGeoStudio</h3>"
                       "<p>Native C++/Qt 6 desktop application</p>"
                       "<p>Road Engine: v%1</p>")
                    .arg(version));
            }
        }
    });

    // Settings shortcut (Ctrl+,)
    auto* settingsShortcut = new QShortcut(QKeySequence("Ctrl+,"), this);
    connect(settingsShortcut, &QShortcut::activated, this, [this]() { openSettings(); });

    // Workspace switching shortcuts (Alt+1 through Alt+4)
    const QStringList wsIds = {"home", "terrain", "road-studio", "train-studio"};
    for (int i = 0; i < wsIds.size(); ++i) {
        auto* sc = new QShortcut(QKeySequence(QString("Alt+%1").arg(i + 1)), this);
        connect(sc, &QShortcut::activated, this, [this, id = wsIds[i]]() {
            m_ctx->workspaces().activate(id);
        });
    }
}

void AppMainWindow::openSettings() {
    SettingsDialog dialog(this);
    dialog.exec();
}

// ─── Road Studio menu integration ────────────────────────────────
void AppMainWindow::setupRoadStudioMenus() {
    if (m_roadFileMenu) return;  // Already set up

    MainWindow* lmw = m_roadStudioWidget->laneMakerWindow();

    // File menu
    m_roadFileMenu = menuBar()->addMenu(tr("&Road File"));
    auto* newAct = m_roadFileMenu->addAction(tr("New"));
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, lmw, &MainWindow::newMap);

    auto* openAct = m_roadFileMenu->addAction(tr("Open"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, lmw, &MainWindow::loadFromFile);

    auto* saveAct = m_roadFileMenu->addAction(tr("Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, lmw, &MainWindow::saveToFile);

    m_roadFileMenu->addSeparator();

    auto* prefAct = m_roadFileMenu->addAction(tr("Preferences"));
    connect(prefAct, &QAction::triggered, lmw, &MainWindow::openPreferences);

    // Edit menu
    m_roadEditMenu = menuBar()->addMenu(tr("&Edit"));
    m_roadUndoAct = m_roadEditMenu->addAction(tr("Undo"));
    m_roadUndoAct->setShortcut(QKeySequence::Undo);
    connect(m_roadUndoAct, &QAction::triggered, lmw, &MainWindow::undo);

    m_roadRedoAct = m_roadEditMenu->addAction(tr("Redo"));
    m_roadRedoAct->setShortcut(QKeySequence::Redo);
    connect(m_roadRedoAct, &QAction::triggered, lmw, &MainWindow::redo);

    m_roadEditMenu->addSeparator();

    auto* verifyAct = m_roadEditMenu->addAction(tr("Verify Now"));
    connect(verifyAct, &QAction::triggered, lmw, &MainWindow::verifyMap);

    // Replay menu
    m_roadReplayMenu = menuBar()->addMenu(tr("&Replay"));
    auto* saveReplayAct = m_roadReplayMenu->addAction(tr("Save"));
    connect(saveReplayAct, &QAction::triggered, lmw, &MainWindow::saveActionHistory);

    auto* debugReplayAct = m_roadReplayMenu->addAction(tr("Debug"));
    connect(debugReplayAct, &QAction::triggered, lmw, &MainWindow::debugActionHistory);

    auto* watchReplayAct = m_roadReplayMenu->addAction(tr("Watch"));
    connect(watchReplayAct, &QAction::triggered, lmw, &MainWindow::playActionHistory);

    // Simulation menu
    m_roadSimulationMenu = menuBar()->addMenu(tr("&Simulation"));
    m_roadSimToggleAct = m_roadSimulationMenu->addAction(tr("Toggle simulation"));
    m_roadSimToggleAct->setCheckable(true);
    connect(m_roadSimToggleAct, &QAction::triggered, lmw, [this, lmw]() {
        bool enabled = m_roadSimToggleAct->isChecked();
        lmw->toggleSimulation(enabled);
        if (m_roadSimPauseAct) {
            m_roadSimPauseAct->setEnabled(enabled);
            if (!enabled) m_roadSimPauseAct->setChecked(false);
        }
        if (m_roadUndoAct) m_roadUndoAct->setEnabled(!enabled);
        if (m_roadRedoAct) m_roadRedoAct->setEnabled(!enabled);
    });

    m_roadSimPauseAct = m_roadSimulationMenu->addAction(tr("Paused"));
    m_roadSimPauseAct->setCheckable(true);
    m_roadSimPauseAct->setEnabled(false);
    connect(m_roadSimPauseAct, &QAction::triggered, lmw, [this, lmw]() {
        lmw->togglePauseSimulation(m_roadSimPauseAct->isChecked());
    });
}

void AppMainWindow::showRoadStudioMenus(bool show) {
    if (m_roadFileMenu) m_roadFileMenu->menuAction()->setVisible(show);
    if (m_roadEditMenu) m_roadEditMenu->menuAction()->setVisible(show);
    if (m_roadReplayMenu) m_roadReplayMenu->menuAction()->setVisible(show);
    if (m_roadSimulationMenu) m_roadSimulationMenu->menuAction()->setVisible(show);
}

// ─── Toolbar / status bar ────────────────────────────────────────
void AppMainWindow::setupToolBar() {
    QToolBar* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->setIconSize(QSize(16, 16));

    // Logo / app name (left)
    auto* logoLabel = new QLabel(QStringLiteral("  OpenGeoStudio  "));
    logoLabel->setStyleSheet(
        QStringLiteral("font-size: %1px; font-weight: bold; color: %2; padding: 0 8px;")
            .arg(th::FontTitle).arg(th::c::Text));
    toolbar->addWidget(logoLabel);

    // Workspace tabs (center-left) — checkable
    auto* wsGroup = new QActionGroup(toolbar);
    wsGroup->setExclusive(true);
    for (const auto& ws : m_ctx->workspaces().workspaces()) {
        auto* act = new QAction(ws.name, toolbar);
        act->setCheckable(true);
        act->setChecked(ws.id == "home");
        wsGroup->addAction(act);
        toolbar->addAction(act);
        connect(act, &QAction::triggered, this, [this, id = ws.id]() {
            m_ctx->workspaces().activate(id);
        });
        m_workspaceActions[ws.id] = act;
    }

    // Spacer to push global actions to the right
    auto* spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // Global actions (right)
    QAction* saveAct = toolbar->addAction(tr("Save"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, [this]() {
        saveProjectState();
    });

    QAction* openAct = toolbar->addAction(tr("Open"));
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &AppMainWindow::onOpenProject);

    QAction* newAct = toolbar->addAction(tr("New"));
    newAct->setShortcut(QKeySequence::New);
    connect(newAct, &QAction::triggered, this, &AppMainWindow::onNewProject);
}

void AppMainWindow::setupStatusBar() {
    m_statusLabel = new QLabel(QStringLiteral("Ready"));
    statusBar()->addWidget(m_statusLabel);
}

// ─── Center widget / docks ───────────────────────────────────────
void AppMainWindow::setupCenterWidget() {
    m_centerStack = new QStackedWidget(this);

    // Page 0: Home
    m_homeWidget = new HomeWidget(m_ctx);
    connect(m_homeWidget, &HomeWidget::newProjectRequested,
            this, &AppMainWindow::onNewProjectFromTemplate);
    connect(m_homeWidget, &HomeWidget::openProjectRequested,
            this, &AppMainWindow::openProjectPath);
    m_centerStack->addWidget(m_homeWidget);

    // Page 1: Terrain Studio (area selection + export)
    m_terrainStudioWidget = new TerrainStudioWidget(m_ctx);
    m_centerStack->addWidget(m_terrainStudioWidget);
#if defined(HAVE_MAPLIBRE)
    if (m_terrainStudioWidget->mapWidget()) {
        connect(m_terrainStudioWidget->mapWidget(), &MapViewportWidget::mapClicked,
                this, [this](double lat, double lon) {
                    m_statusLabel->setText(
                        QStringLiteral("Terrain — Clicked: %1, %2")
                            .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
                });
        connect(m_terrainStudioWidget->mapWidget(), &MapViewportWidget::cursorMoved,
                this, [this](double lat, double lon, double zoom) {
                    m_statusLabel->setText(
                        QStringLiteral("Lat: %1  Lon: %2  Zoom: %3  |  Terrain Studio")
                            .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(zoom, 0, 'f', 1));
                });
    }
#endif

    // Page 2: Road Studio (LaneMaker's MainWindow — full road editor)
    m_roadStudioWidget = new RoadStudioWidget(m_ctx);
    m_centerStack->addWidget(m_roadStudioWidget);

    // Page 3: Train Studio (LaneMaker's MainWindow - full rail editor)
    m_trainStudioWidget = new TrainStudioWidget();
    m_centerStack->addWidget(m_trainStudioWidget);

    // Page 4: 3D Studio
    m_studio3DWidget = new Studio3DWidget(m_ctx);
    m_centerStack->addWidget(m_studio3DWidget);

    setCentralWidget(m_centerStack);
    m_centerStack->setCurrentIndex(0); // Home
}

void AppMainWindow::setupDockWidgets() {
    // Left dock — project tree / explorer
    m_leftDock = new QDockWidget(tr("Project"), this);
    m_leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* leftContent = new QLabel(tr("No project open"));
    leftContent->setAlignment(Qt::AlignCenter);
    leftContent->setObjectName(QStringLiteral("emptyPlaceholder"));
    m_leftDock->setWidget(leftContent);
    addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

    // Right dock — inspector / properties
    m_rightDock = new QDockWidget(tr("Inspector"), this);
    m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    auto* placeholder = new QLabel(tr("Select a road to\nview its properties"));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setObjectName(QStringLiteral("emptyPlaceholder"));
    m_rightDockPlaceholder = placeholder;
    m_rightDock->setWidget(m_rightDockPlaceholder);
    addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

    // Hide docks on Home (no side panels in Home workspace)
    m_leftDock->setVisible(false);
    m_rightDock->setVisible(false);
}

void AppMainWindow::updateStatusBar() {
    const QString engineVersion = QString::fromLatin1(ogs::appdetail::roadEngineVersion());
    if (m_ctx->projects().hasProject()) {
        const auto& p = m_ctx->projects().current();
        QString crsInfo = p.hasProjectCRS()
            ? QStringLiteral(" | CRS: %1").arg(p.projectCrsAuthId)
            : QStringLiteral(" | No CRS set");
        m_statusLabel->setText(
            QStringLiteral("%1 | Road Engine v%2 | Workspace: %3%4")
                .arg(p.name, engineVersion, m_ctx->workspaces().activeId(), crsInfo));
    } else {
        m_statusLabel->setText(
            QStringLiteral("No project open | Road Engine v%1 | Workspace: %2")
                .arg(engineVersion, m_ctx->workspaces().activeId()));
    }
}

// ─── Workspace switching ─────────────────────────────────────────
void AppMainWindow::onWorkspaceActivated(const Workspace& ws) {
    // Switch center widget based on workspace
    if (ws.id == "home") {
        m_centerStack->setCurrentIndex(0);
        // Home: no docks
        m_leftDock->setVisible(false);
        m_rightDock->setVisible(false);
        showRoadStudioMenus(false);
    } else if (ws.id == "terrain") {
        m_centerStack->setCurrentIndex(1); // Terrain Studio
        // Terrain: export panel is built into the widget, hide docks
        m_leftDock->setVisible(false);
        m_rightDock->setVisible(false);
        showRoadStudioMenus(false);
    } else if (ws.id == "road-studio") {
        appLog().info("Activating Road Studio: setCurrentIndex...");
        m_centerStack->setCurrentIndex(2); // Road Studio (LaneMaker)
        if (m_roadStudioWidget && m_roadStudioWidget->laneMakerWindow())
            m_roadStudioWidget->laneMakerWindow()->triggerGLInitialization();
        // Re-sync the map view to the current terrain selection so the
        // editor focuses on the area the user prepared in Terrain Studio
        // (terrain can change after the project was opened).
        syncLaneMakerViewToTerrain(m_roadStudioWidget->laneMakerWindow());
        appLog().info("Activating Road Studio: hiding docks...");
        // LaneMaker's MainWindow has its own toolbar, lane config, etc.
        m_leftDock->setVisible(false);
        m_rightDock->setVisible(false);
        appLog().info("Activating Road Studio: setupRoadStudioMenus...");
        // Add Road Studio menus to the main app menu bar
        setupRoadStudioMenus();
        appLog().info("Activating Road Studio: showRoadStudioMenus...");
        showRoadStudioMenus(true);
        appLog().info("Activating Road Studio: checking pending road file...");
        // Load pending road file if one was queued by project open
        if (!m_pendingRoadFile.isEmpty() &&
            m_roadStudioWidget && m_roadStudioWidget->laneMakerWindow()) {
            appLog().info("Activating Road Studio: pending file:", m_pendingRoadFile);
            // Defer to next event loop iteration so the OpenGL context is ready
            QString path = m_pendingRoadFile;
            m_pendingRoadFile.clear();
            QTimer::singleShot(2000, this, [this, path]() {
                appLog().info("Road Studio: timer fired, loading:", path);
                if (m_roadStudioWidget && m_roadStudioWidget->laneMakerWindow())
                    m_roadStudioWidget->laneMakerWindow()->loadFromPath(path);
            });
        }
        appLog().info("Activating Road Studio: done");
    } else if (ws.id == "train-studio") {
        m_centerStack->setCurrentIndex(3); // Train Studio
        if (m_trainStudioWidget && m_trainStudioWidget->laneMakerWindow()) {
            m_trainStudioWidget->laneMakerWindow()->triggerGLInitialization();
            syncLaneMakerViewToTerrain(m_trainStudioWidget->laneMakerWindow());
        }
        // Train Studio: no docks
        m_leftDock->setVisible(false);
        m_rightDock->setVisible(false);
        showRoadStudioMenus(false);
    } else if (ws.id == "3d-studio") {
        m_centerStack->setCurrentIndex(4); // 3D Studio
        m_leftDock->setVisible(false);
        m_rightDock->setVisible(false);
        showRoadStudioMenus(false);
        // Persist the current road network into the project's Roads folder
        // BEFORE entering 3D Studio, so auto-load can find the latest roads
        // even if the user never pressed "Save Project".
        exportRoadToProjectFolder();
        // Auto-load saved scene when entering 3D Studio
        if (m_studio3DWidget) m_studio3DWidget->onProjectOpened();
    }

    // Reset inspector for non-road-studio workspaces
    if (ws.id != "road-studio" && m_rightDockPlaceholder) {
        m_rightDock->setWidget(m_rightDockPlaceholder);
        m_rightDock->setWindowTitle("Inspector");
    }

    setWindowTitle(QStringLiteral("OpenGeoStudio — %1").arg(ws.name));
    updateStatusBar();

    // Sync toolbar tab
    if (m_workspaceActions.contains(ws.id)) {
        m_workspaceActions[ws.id]->setChecked(true);
    }
}

// ─── Project events / CRS ────────────────────────────────────────
void AppMainWindow::onProjectChanged(const Project&) {
    propagateProjectCrs();
    updateStatusBar();
}

void AppMainWindow::onProjectOpened(const Project&) {
    // Always switch to Terrain workspace after opening a project
    m_ctx->workspaces().activate("terrain");
    // Restore terrain and road state from the project file
    loadProjectState();
    propagateProjectCrs();
    updateStatusBar();
}

// Propagate the project CRS to all studios that need it
void AppMainWindow::propagateProjectCrs() {
    if (!m_ctx->projects().hasProject()) return;
    const auto& p = m_ctx->projects().current();
    if (!p.hasProjectCRS()) return;

    // Extract EPSG code from authId (e.g. "EPSG:32643" -> 32643)
    int epsg = 0;
    if (p.projectCrsAuthId.startsWith("EPSG:")) {
        epsg = p.projectCrsAuthId.mid(5).toInt();
    }
    if (epsg <= 0) return;

    // Propagate to terrain studio's export settings
    if (m_terrainStudioWidget) {
        auto& settings = m_terrainStudioWidget->store()->exportSettings();
        settings.projectCrsEpsg = epsg;
    }
}

// ─── Project state save/load ─────────────────────────────────────
// Serializes TerrainStore + road .xodr into the project folder
// and .ogproj moduleState so all studios share the same project.

// Write the current Road Studio network to {project}/Roads/road.xodr.
// Returns the written path, or empty when there is nothing to save.
QString AppMainWindow::exportRoadToProjectFolder() {
    if (!m_ctx->projects().hasProject()) return QString();
    if (!m_roadStudioWidget || !m_roadStudioWidget->laneMakerWindow())
        return QString();

    const QString basePath = m_ctx->projects().current().basePath;
    const QString roadsDir = basePath + "/Roads";
    QDir().mkpath(roadsDir);
    const QString roadFile = roadsDir + "/road.xodr";
    m_roadStudioWidget->laneMakerWindow()->saveToPath(roadFile);
    if (QFile::exists(roadFile)) {
        appLog().info("exportRoadToProjectFolder: saved", roadFile,
                      "size:", QFileInfo(roadFile).size());
        return roadFile;
    }
    appLog().warn("exportRoadToProjectFolder: road file was NOT created:", roadFile);
    return QString();
}

void AppMainWindow::saveProjectState() {
    if (!m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, tr("No Project"),
            tr("Create or open a project first."));
        return;
    }

    const QString basePath = m_ctx->projects().current().basePath;

    // ── Terrain state → moduleState ──
    QJsonObject moduleState;
    moduleState["terrain"] = m_ctx->terrain().toJson();

    // ── Road network → {project}/Roads/road.xodr ──
    const QString roadFile = exportRoadToProjectFolder();
    if (!roadFile.isEmpty()) {
        QJsonObject roadState;
        roadState["xodrFile"] = roadFile;
        moduleState["road-studio"] = roadState;
    } else {
        appLog().warn("saveProjectState: road studio widget not available");
    }

    // Update the project's moduleState
    const_cast<Project&>(m_ctx->projects().current()).moduleState = moduleState;

    // Save the project file
    if (m_ctx->projects().save()) {
        m_statusLabel->setText(QStringLiteral("Project saved: %1")
            .arg(m_ctx->projects().current().name));
    }
}

void AppMainWindow::loadProjectState() {
    if (!m_ctx->projects().hasProject()) return;

    const auto& proj = m_ctx->projects().current();
    const QJsonObject& ms = proj.moduleState;

    // Restore terrain state (bounds, tile grid, selected tiles, export settings)
    bool terrainRestored = false;
    if (ms.contains("terrain")) {
        m_ctx->terrain().fromJson(ms["terrain"].toObject());
        terrainRestored = true;
    }

    // ── Restore road network — defer loading until Road Studio is visible ──
    // Loading immediately crashes because LaneMaker's OpenGL context
    // isn't ready until the Road Studio page is actually shown.
    if (ms.contains("road-studio")) {
        QJsonObject roadState = ms["road-studio"].toObject();
        QString xodrFile = roadState["xodrFile"].toString();
        if (!xodrFile.isEmpty() && QFile::exists(xodrFile)) {
            m_pendingRoadFile = xodrFile;
            // If Road Studio is already active, load now.
            // Otherwise, load when the user switches to Road Studio.
            if (m_ctx->workspaces().activeId() == "road-studio" &&
                m_roadStudioWidget && m_roadStudioWidget->laneMakerWindow()) {
                m_roadStudioWidget->laneMakerWindow()->loadFromPath(xodrFile);
                m_pendingRoadFile.clear();
                m_statusLabel->setText(
                    QStringLiteral("Road network loaded: %1").arg(xodrFile));
            }
        }
    }

    // Update project bounds in terrain store — but only if the terrain
    // state was NOT already restored from JSON (which includes bounds +
    // selected tiles). Calling setBounds() again would recompute the tile
    // grid and wipe out the restored tile selection.
    if (!terrainRestored && proj.bounds.valid) {
        terrain::GeoBounds bounds;
        bounds.south = proj.bounds.minLat;
        bounds.north = proj.bounds.maxLat;
        bounds.west = proj.bounds.minLon;
        bounds.east = proj.bounds.maxLon;
        m_ctx->terrain().setBounds(bounds);
    }

    const auto& mapBounds = m_ctx->terrain().selectedBounds();
    if (mapBounds.isValid()) {
        const double centerLat = (mapBounds.south + mapBounds.north) * 0.5;
        const double centerLon = (mapBounds.west + mapBounds.east) * 0.5;
        const double span = std::max(mapBounds.widthDeg(), mapBounds.heightDeg());
        const double zoom = qBound(2.0,
            std::floor(std::log2(360.0 / std::max(span, 1e-9))) + 1.0, 18.0);
        if (m_roadStudioWidget && m_roadStudioWidget->laneMakerWindow())
            m_roadStudioWidget->laneMakerWindow()->useSharedSatelliteView(centerLat, centerLon, zoom);
        if (m_trainStudioWidget && m_trainStudioWidget->laneMakerWindow())
            m_trainStudioWidget->laneMakerWindow()->useSharedSatelliteView(centerLat, centerLon, zoom);
    }
}

// Focus a LaneMaker window on the currently selected terrain area.
// Called when activating Road/Train Studio so the editor always shows
// the region the user prepared in Terrain Studio, even if the terrain
// selection changed after the project was opened.
void AppMainWindow::syncLaneMakerViewToTerrain(MainWindow* laneMaker) {
    if (!laneMaker || !m_ctx->projects().hasProject()) return;
    const auto& mapBounds = m_ctx->terrain().selectedBounds();
    if (!mapBounds.isValid()) return;
    const double centerLat = (mapBounds.south + mapBounds.north) * 0.5;
    const double centerLon = (mapBounds.west + mapBounds.east) * 0.5;
    const double span = std::max(mapBounds.widthDeg(), mapBounds.heightDeg());
    const double zoom = qBound(2.0,
        std::floor(std::log2(360.0 / std::max(span, 1e-9))) + 1.0, 18.0);
    laneMaker->useSharedSatelliteView(centerLat, centerLon, zoom);
}

// ─── Project actions ─────────────────────────────────────────────
void AppMainWindow::onNewProject() {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New Project"), tr("Project name:"),
        QLineEdit::Normal, "Untitled Project", &ok);
    if (!ok || name.isEmpty()) return;

    createProjectNamed(name, "home");
}

void AppMainWindow::onNewProjectFromTemplate(const QString& templateId) {
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("New %1 Project").arg(templateId), tr("Project name:"),
        QLineEdit::Normal, "Untitled " + templateId, &ok);
    if (!ok || name.isEmpty()) return;

    createProjectNamed(name, templateId);
}

void AppMainWindow::onOpenProject() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), {},
        tr("OpenGeoStudio Projects (*.ogproj)"));
    if (!path.isEmpty()) {
        m_ctx->projects().open(path);
    }
}

// Shared project-creation path (name dialog already validated the name)
void AppMainWindow::createProjectNamed(const QString& name, const QString& templateId) {
    // Auto-create in C:/OpenGeoStudio/Projects/{name}
    // Keep projects off OneDrive/Unicode paths for safety
    const QString baseDir = "C:/OpenGeoStudio/Projects";
    QDir().mkpath(baseDir);
    const QString projectDir = baseDir + "/" + name;

    // Check if project already exists
    if (QDir(projectDir).exists()) {
        QMessageBox::warning(this, tr("Project Exists"),
            tr("A project named \"%1\" already exists at:\n%2\n\n"
               "Please choose a different name.")
                .arg(name, projectDir));
        return;
    }

    m_ctx->projects().createWithFolder(name, projectDir, templateId);
    // Always switch to Terrain workspace after creating a project
    m_ctx->workspaces().activate("terrain");
}






