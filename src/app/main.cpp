// ═══════════════════════════════════════════════════════════
// OpenGeoStudio — Qt Application Entry Point
// ═══════════════════════════════════════════════════════════
//
// Initializes QApplication, creates the ApplicationContext,
// and starts the Qt event loop. This is the native C++ replacement
// for the Electron main process (app/main.ts).
//
// The road engine is called directly via RoadEngineService —
// no IPC, no N-API, no Node.js.
// ═══════════════════════════════════════════════════════════

#include <QApplication>
#include <QMainWindow>
#include <QIcon>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QAction>
#include <QLabel>
#include <QDockWidget>
#include <QMessageBox>
#include <QStackedWidget>
#include <QFileDialog>
#include <QInputDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QShortcut>
#include <QKeySequence>
#include <QCompleter>

// Road engine — direct C++ include, no N-API bridge
#include "road_engine.hpp"

// Core services
#include "core/ApplicationContext.hpp"
#include "core/project/ProjectManager.hpp"
#include "core/workspace/WorkspaceManager.hpp"

// UI
#include "ui/home/HomeWidget.hpp"
#include "ui/roadstudio/RoadStudioWidget.hpp"
#include "ui/trainstudio/TrainStudioWidget.hpp"
#include "ui/terrain/TerrainStudioWidget.hpp"
#include "main_window.h"

// Phase 2c: MapLibre Native Qt map viewport
#if defined(HAVE_MAPLIBRE)
#include "app/MapViewportWidget.hpp"
#endif

// ═══════════════════════════════════════════════════════════
// SettingsDialog — API keys and project settings
// ═══════════════════════════════════════════════════════════

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Settings");
        setMinimumWidth(450);
        setStyleSheet("QDialog { background: #0d1117; }"
            "QLabel { color: #e6edf3; }"
            "QLineEdit { background: #21262d; border: 1px solid #30363d; border-radius: 4px; padding: 6px; color: #e6edf3; }"
            "QPushButton { background: #21262d; border: 1px solid #30363d; border-radius: 6px; padding: 8px 20px; color: #e6edf3; }"
            "QPushButton:hover { background: #30363d; }"
            "QGroupBox { border: 1px solid #30363d; border-radius: 6px; margin-top: 12px; padding-top: 8px; color: #e6edf3; }"
            "QGroupBox::title { color: #7d8590; }");

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(12);
        layout->setContentsMargins(16, 16, 16, 16);

        // API Keys section
        auto* keysGroup = new QGroupBox("API Keys");
        auto* keysForm = new QFormLayout(keysGroup);

        auto* openTopoKey = new QLineEdit();
        openTopoKey->setPlaceholderText("OpenTopography API key");
        openTopoKey->setEchoMode(QLineEdit::Password);
        keysForm->addRow("OpenTopography:", openTopoKey);

        auto* mapboxKey = new QLineEdit();
        mapboxKey->setPlaceholderText("Mapbox access token");
        mapboxKey->setEchoMode(QLineEdit::Password);
        keysForm->addRow("Mapbox:", mapboxKey);

        auto* maptilerKey = new QLineEdit();
        maptilerKey->setPlaceholderText("MapTiler API key");
        maptilerKey->setEchoMode(QLineEdit::Password);
        keysForm->addRow("MapTiler:", maptilerKey);

        layout->addWidget(keysGroup);

        // Project defaults section
        auto* defaultsGroup = new QGroupBox("Project Defaults");
        auto* defaultsForm = new QFormLayout(defaultsGroup);

        auto* defaultWorkspace = new QComboBox();
        defaultWorkspace->addItems({"Home", "Terrain", "Road Studio", "Train Studio"});
        defaultsForm->addRow("Default workspace:", defaultWorkspace);

        auto* defaultRoadWidth = new QLineEdit("8.0");
        defaultsForm->addRow("Default road width (m):", defaultRoadWidth);

        auto* defaultLanes = new QLineEdit("2");
        defaultsForm->addRow("Default lane count:", defaultLanes);

        layout->addWidget(defaultsGroup);

        // Buttons
        auto* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();

        auto* closeBtn = new QPushButton("Close");
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
        btnLayout->addWidget(closeBtn);

        layout->addLayout(btnLayout);
    }
};

// ═══════════════════════════════════════════════════════════
// CommandPalette — Ctrl+Shift+P quick command execution
// ═══════════════════════════════════════════════════════════

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Command Palette");
        setMinimumWidth(500);
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(4);

        m_input = new QLineEdit(this);
        m_input->setPlaceholderText("Type a command...");
        m_input->setStyleSheet(
            "QLineEdit { background: #21262d; border: 1px solid #06b6d4; border-radius: 6px;"
            "padding: 10px 14px; color: #e6edf3; font-size: 14px; }");
        layout->addWidget(m_input);

        m_list = new QListWidget(this);
        m_list->setStyleSheet(
            "QListWidget { background: #0d1117; border: 1px solid #30363d; border-radius: 6px; }"
            "QListWidget::item { padding: 8px 12px; color: #e6edf3; }"
            "QListWidget::item:hover { background: #161b22; }"
            "QListWidget::item:selected { background: rgba(6,182,212,0.15); color: #06b6d4; }");
        layout->addWidget(m_list);

        // Populate commands
        addCommand("File: New Project", "file.new");
        addCommand("File: Open Project", "file.open");
        addCommand("File: Save Project", "file.save");
        addCommand("View: Home", "ws.home");
        addCommand("View: Terrain", "ws.terrain");
        addCommand("View: Road Studio", "ws.road");
        addCommand("View: Train Studio", "ws.train");
        addCommand("Settings: Open Settings", "settings.open");
        addCommand("Help: About", "help.about");

        m_input->setFocus();

        connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::filterCommands);
        connect(m_input, &QLineEdit::returnPressed, this, &CommandPalette::executeSelected);
        connect(m_list, &QListWidget::itemDoubleClicked, this, &CommandPalette::executeSelected);
    }

    QString selectedCommand() const { return m_selectedCommand; }

private:
    QLineEdit* m_input = nullptr;
    QListWidget* m_list = nullptr;
    QString m_selectedCommand;
    QStringList m_allCommands;

    void addCommand(const QString& label, const QString& id) {
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, id);
        m_allCommands.append(label + "|" + id);
    }

    void filterCommands(const QString& text) {
        m_list->clear();
        for (const auto& cmd : m_allCommands) {
            auto parts = cmd.split('|');
            if (parts.size() != 2) continue;
            if (text.isEmpty() || parts[0].contains(text, Qt::CaseInsensitive)) {
                auto* item = new QListWidgetItem(parts[0], m_list);
                item->setData(Qt::UserRole, parts[1]);
            }
        }
        if (m_list->count() > 0) m_list->setCurrentRow(0);
    }

    void executeSelected() {
        auto* item = m_list->currentItem();
        if (item) {
            m_selectedCommand = item->data(Qt::UserRole).toString();
            accept();
        }
    }
};

// ═══════════════════════════════════════════════════════════
// AppMainWindow — the application shell
// ═══════════════════════════════════════════════════════════

class AppMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit AppMainWindow(ApplicationContext* ctx, QWidget* parent = nullptr)
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
                this, &AppMainWindow::onWorkspaceActivated);

        // Wire project changes
        connect(&m_ctx->projects(), &ProjectManager::projectChanged,
                this, &AppMainWindow::onProjectChanged);
        connect(&m_ctx->projects(), &ProjectManager::projectOpened,
                this, &AppMainWindow::onProjectOpened);

        updateStatusBar();
    }

private:
    ApplicationContext* m_ctx;
    QLabel* m_statusLabel = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    QMap<QString, QAction*> m_workspaceActions;
    HomeWidget* m_homeWidget = nullptr;
    RoadStudioWidget* m_roadStudioWidget = nullptr;
    TrainStudioWidget* m_trainStudioWidget = nullptr;
    TerrainStudioWidget* m_terrainStudioWidget = nullptr;
    QDockWidget* m_leftDock = nullptr;
    QDockWidget* m_rightDock = nullptr;
    QWidget* m_rightDockPlaceholder = nullptr;
    // RoadInspector removed — LaneMaker's MainWindow has its own UI
#if defined(HAVE_MAPLIBRE)
    MapViewportWidget* m_mapWidget = nullptr;
#endif

    // Road Studio menu bar integration
    QMenu* m_roadFileMenu = nullptr;
    QMenu* m_roadEditMenu = nullptr;
    QMenu* m_roadReplayMenu = nullptr;
    QMenu* m_roadSimulationMenu = nullptr;
    QAction* m_roadSimToggleAct = nullptr;
    QAction* m_roadSimPauseAct = nullptr;
    QAction* m_roadUndoAct = nullptr;
    QAction* m_roadRedoAct = nullptr;

    void setupMenuBar() {
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
            m_ctx->projects().save();
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
            const QString version = QString::fromLatin1(road_engine::versionString());
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
                else if (cmd == "file.save") m_ctx->projects().save();
                else if (cmd == "ws.home") m_ctx->workspaces().activate("home");
                else if (cmd == "ws.terrain") m_ctx->workspaces().activate("terrain");
                else if (cmd == "ws.road") m_ctx->workspaces().activate("road-studio");
                else if (cmd == "ws.train") m_ctx->workspaces().activate("train-studio");
                else if (cmd == "settings.open") openSettings();
                else if (cmd == "help.about") {
                    const QString version = QString::fromLatin1(road_engine::versionString());
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

    void openSettings() {
        SettingsDialog dialog(this);
        dialog.exec();
    }

    void setupRoadStudioMenus() {
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

    void showRoadStudioMenus(bool show) {
        if (m_roadFileMenu) m_roadFileMenu->menuAction()->setVisible(show);
        if (m_roadEditMenu) m_roadEditMenu->menuAction()->setVisible(show);
        if (m_roadReplayMenu) m_roadReplayMenu->menuAction()->setVisible(show);
        if (m_roadSimulationMenu) m_roadSimulationMenu->menuAction()->setVisible(show);
    }

    void setupToolBar() {
        QToolBar* toolbar = addToolBar(tr("Main"));
        toolbar->setMovable(false);
        toolbar->setIconSize(QSize(16, 16));

        // Logo / app name (left)
        auto* logoLabel = new QLabel(QStringLiteral("  OpenGeoStudio  "));
        logoLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #e6edf3; padding: 0 8px;");
        toolbar->addWidget(logoLabel);

        // Workspace tabs (center-left) — checkable, like the Electron app
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
            m_ctx->projects().save();
        });

        QAction* openAct = toolbar->addAction(tr("Open"));
        openAct->setShortcut(QKeySequence::Open);
        connect(openAct, &QAction::triggered, this, &AppMainWindow::onOpenProject);

        QAction* newAct = toolbar->addAction(tr("New"));
        newAct->setShortcut(QKeySequence::New);
        connect(newAct, &QAction::triggered, this, &AppMainWindow::onNewProject);
    }

    void setupStatusBar() {
        m_statusLabel = new QLabel(QStringLiteral("Ready"));
        statusBar()->addWidget(m_statusLabel);
    }

    void setupCenterWidget() {
        m_centerStack = new QStackedWidget(this);

        // Page 0: Home
        m_homeWidget = new HomeWidget(m_ctx);
        connect(m_homeWidget, &HomeWidget::newProjectRequested,
                this, &AppMainWindow::onNewProjectFromTemplate);
        connect(m_homeWidget, &HomeWidget::openProjectRequested,
                this, &AppMainWindow::onOpenProjectPath);
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
        m_roadStudioWidget = new RoadStudioWidget();
        m_centerStack->addWidget(m_roadStudioWidget);

        // Page 3: Train Studio (2D track editing)
        m_trainStudioWidget = new TrainStudioWidget(m_ctx);
        m_centerStack->addWidget(m_trainStudioWidget);
#if defined(HAVE_MAPLIBRE)
        if (m_trainStudioWidget->mapWidget()) {
            connect(m_trainStudioWidget->mapWidget(), &MapViewportWidget::mapClicked,
                    this, [this](double lat, double lon) {
                        m_statusLabel->setText(
                            QStringLiteral("Train Studio — Clicked: %1, %2")
                                .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
                    });
            connect(m_trainStudioWidget->mapWidget(), &MapViewportWidget::cursorMoved,
                    this, [this](double lat, double lon, double zoom) {
                        m_statusLabel->setText(
                            QStringLiteral("Lat: %1  Lon: %2  Zoom: %3  |  Train Studio")
                                .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(zoom, 0, 'f', 1));
                    });
        }
#endif

        setCentralWidget(m_centerStack);
        m_centerStack->setCurrentIndex(0); // Home
    }

    void setupDockWidgets() {
        // Left dock — project tree / explorer
        m_leftDock = new QDockWidget(tr("Project"), this);
        m_leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        m_leftDock->setStyleSheet(
            "QDockWidget::title { background: #161b22; border-bottom: 1px solid #30363d; padding: 6px 12px; color: #e6edf3; }");
        auto* leftContent = new QLabel(tr("No project open"));
        leftContent->setAlignment(Qt::AlignCenter);
        leftContent->setStyleSheet("color: #7d8590; font-size: 13px; padding: 20px;");
        m_leftDock->setWidget(leftContent);
        addDockWidget(Qt::LeftDockWidgetArea, m_leftDock);

        // Right dock — inspector / properties
        m_rightDock = new QDockWidget(tr("Inspector"), this);
        m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        m_rightDock->setStyleSheet(
            "QDockWidget::title { background: #161b22; border-bottom: 1px solid #30363d; padding: 6px 12px; color: #e6edf3; }");
        auto* placeholder = new QLabel(tr("Select a road to\nview its properties"));
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet("color: #7d8590; font-size: 13px; padding: 20px;");
        m_rightDockPlaceholder = placeholder;
        m_rightDock->setWidget(m_rightDockPlaceholder);
        addDockWidget(Qt::RightDockWidgetArea, m_rightDock);

        // Hide docks on Home (no side panels in Home workspace)
        m_leftDock->setVisible(false);
        m_rightDock->setVisible(false);
    }

    void updateStatusBar() {
        const QString engineVersion = QString::fromLatin1(road_engine::versionString());
        if (m_ctx->projects().hasProject()) {
            const auto& p = m_ctx->projects().current();
            m_statusLabel->setText(
                QStringLiteral("%1 | Road Engine v%2 | Workspace: %3")
                    .arg(p.name, engineVersion, m_ctx->workspaces().activeId()));
        } else {
            m_statusLabel->setText(
                QStringLiteral("No project open | Road Engine v%1 | Workspace: %2")
                    .arg(engineVersion, m_ctx->workspaces().activeId()));
        }
    }

private slots:
    void onWorkspaceActivated(const Workspace& ws) {
        // Switch center widget based on workspace
        if (ws.id == "home") {
            m_centerStack->setCurrentIndex(0);
            // Home: no docks (matching reference)
            m_leftDock->setVisible(false);
            m_rightDock->setVisible(false);
            showRoadStudioMenus(false);
        } else if (ws.id == "terrain") {
            m_centerStack->setCurrentIndex(1); // Terrain Studio
            // Terrain: right dock has export panel (built into widget), hide docks
            m_leftDock->setVisible(false);
            m_rightDock->setVisible(false);
            showRoadStudioMenus(false);
        } else if (ws.id == "road-studio") {
            m_centerStack->setCurrentIndex(2); // Road Studio (LaneMaker)
            // LaneMaker's MainWindow has its own toolbar, lane config, etc.
            m_leftDock->setVisible(false);
            m_rightDock->setVisible(false);
            // Add Road Studio menus to the main app menu bar
            setupRoadStudioMenus();
            showRoadStudioMenus(true);
        } else if (ws.id == "train-studio") {
            m_centerStack->setCurrentIndex(3); // Train Studio
            // Train Studio: no docks (matching reference)
            m_leftDock->setVisible(false);
            m_rightDock->setVisible(false);
            showRoadStudioMenus(false);
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

    void onProjectChanged(const Project&) {
        updateStatusBar();
    }

    void onProjectOpened(const Project& p) {
        // Switch to the project's workspace
        m_ctx->workspaces().activate(p.workspaceId);
        updateStatusBar();
    }

    void onNewProject() {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New Project"), tr("Project name:"),
            QLineEdit::Normal, "Untitled Project", &ok);
        if (!ok || name.isEmpty()) return;

        const QString defaultDir = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation) + "/OpenGeoStudio";
        const QString folder = QFileDialog::getExistingDirectory(
            this, tr("Select Project Folder"), defaultDir);
        if (folder.isEmpty()) return;

        const QString projectDir = folder + "/" + name;
        m_ctx->projects().createWithFolder(name, projectDir, "home");
        m_ctx->workspaces().activate("home");
    }

    void onNewProjectFromTemplate(const QString& templateId) {
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("New %1 Project").arg(templateId), tr("Project name:"),
            QLineEdit::Normal, "Untitled " + templateId, &ok);
        if (!ok || name.isEmpty()) return;

        const QString defaultDir = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation) + "/OpenGeoStudio";
        const QString folder = QFileDialog::getExistingDirectory(
            this, tr("Select Project Folder"), defaultDir);
        if (folder.isEmpty()) return;

        const QString projectDir = folder + "/" + name;
        m_ctx->projects().createWithFolder(name, projectDir, templateId);
        m_ctx->workspaces().activate(templateId);
    }

    void onOpenProject() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open Project"), {},
            tr("OpenGeoStudio Projects (*.ogproj)"));
        if (!path.isEmpty()) {
            m_ctx->projects().open(path);
        }
    }

    void onOpenProjectPath(const QString& path) {
        m_ctx->projects().open(path);
    }
};

// ═══════════════════════════════════════════════════════════
// Main — application entry point
// ═══════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Set application icon (from original Electron app assets)
    app.setWindowIcon(QIcon(":/icons/app.png"));

    // Initialize LaneMaker's Qt resources (shaders, models, icons)
    // Required because lanemaker is a static library — Qt doesn't auto-register
    // resources from static libraries in Qt 6.
    Q_INIT_RESOURCE(shaders);
    Q_INIT_RESOURCE(images);
    app.setApplicationName(QStringLiteral("OpenGeoStudio"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("OpenGeoStudio"));

    // Set dark theme matching the Electron app's GitHub-inspired dark palette
    // surface-base: #0d1117, surface-panel: #161b22, surface-elevated: #1c2128
    // edge: #30363d, fg-primary: #e6edf3, fg-secondary: #7d8590
    // accent: #06b6d4 (cyan), ok: #3fb950, warn: #d29922, err: #f85149
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(0x0d, 0x11, 0x17));
    darkPalette.setColor(QPalette::WindowText, QColor(0xe6, 0xed, 0xf3));
    darkPalette.setColor(QPalette::Base, QColor(0x16, 0x1b, 0x22));
    darkPalette.setColor(QPalette::AlternateBase, QColor(0x1c, 0x21, 0x28));
    darkPalette.setColor(QPalette::Text, QColor(0xe6, 0xed, 0xf3));
    darkPalette.setColor(QPalette::Button, QColor(0x1c, 0x21, 0x28));
    darkPalette.setColor(QPalette::ButtonText, QColor(0xe6, 0xed, 0xf3));
    darkPalette.setColor(QPalette::Highlight, QColor(0x06, 0xb6, 0xd4));
    darkPalette.setColor(QPalette::HighlightedText, QColor(0x0d, 0x11, 0x17));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(0x1c, 0x21, 0x28));
    darkPalette.setColor(QPalette::ToolTipText, QColor(0xe6, 0xed, 0xf3));
    darkPalette.setColor(QPalette::PlaceholderText, QColor(0x7d, 0x85, 0x90));
    darkPalette.setColor(QPalette::Light, QColor(0x21, 0x26, 0x2d));
    darkPalette.setColor(QPalette::Midlight, QColor(0x1c, 0x21, 0x28));
    darkPalette.setColor(QPalette::Mid, QColor(0x16, 0x1b, 0x22));
    darkPalette.setColor(QPalette::Dark, QColor(0x0d, 0x11, 0x17));
    darkPalette.setColor(QPalette::Shadow, QColor(0x0d, 0x11, 0x17));
    darkPalette.setColor(QPalette::Link, QColor(0x06, 0xb6, 0xd4));
    darkPalette.setColor(QPalette::LinkVisited, QColor(0x0e, 0x74, 0x90));
    app.setPalette(darkPalette);

    // Global stylesheet for GitHub-dark look
    app.setStyleSheet(QStringLiteral(
        // Scrollbar styling
        "QScrollBar:vertical { width: 8px; background: transparent; }"
        "QScrollBar:horizontal { height: 8px; background: transparent; }"
        "QScrollBar::handle { background: #30363d; border-radius: 4px; min-height: 20px; }"
        "QScrollBar::handle:hover { background: #484f58; }"
        "QScrollBar::add-line, QScrollBar::sub-line { border: none; height: 0; width: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"

        // Toolbars — top bar like reference app
        "QToolBar { background: #0d1117; border: none; border-bottom: 1px solid #30363d; spacing: 2px; padding: 4px 6px; }"
        "QToolBar::separator { width: 1px; height: 20px; background: #30363d; margin: 4px 8px; }"
        "QToolBar QToolButton { padding: 5px 12px; border-radius: 6px; color: #7d8590; font-size: 13px; }"
        "QToolBar QToolButton:hover { background: #21262d; color: #e6edf3; }"
        "QToolBar QToolButton:checked { background: rgba(6,182,212,0.15); color: #06b6d4; border: 1px solid rgba(6,182,212,0.4); }"

        // Menu bar
        "QMenuBar { background: #0d1117; color: #e6edf3; border-bottom: 1px solid #30363d; padding: 2px; }"
        "QMenuBar::item { padding: 4px 12px; background: transparent; border-radius: 4px; }"
        "QMenuBar::item:selected { background: #21262d; }"
        "QMenu { background: #161b22; border: 1px solid #30363d; color: #e6edf3; }"
        "QMenu::item { padding: 6px 24px; border-radius: 4px; }"
        "QMenu::item:selected { background: #21262d; }"
        "QMenu::separator { height: 1px; background: #30363d; margin: 4px 8px; }"

        // Status bar
        "QStatusBar { background: #0d1117; color: #7d8590; border-top: 1px solid #30363d; font-size: 12px; }"
        "QStatusBar::item { border: none; }"
        "QStatusBar QLabel { color: #7d8590; padding: 0 8px; }"

        // Dock widgets
        "QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; background: #0d1117; }"
        "QDockWidget::title { background: #161b22; border-bottom: 1px solid #30363d; padding: 6px 12px; color: #e6edf3; font-weight: bold; }"

        // Group boxes
        "QGroupBox { border: 1px solid #30363d; border-radius: 6px; margin-top: 12px; padding-top: 8px; color: #e6edf3; font-weight: bold; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: #7d8590; }"

        // Inputs
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: #21262d; border: 1px solid #30363d; border-radius: 4px; padding: 4px 8px; color: #e6edf3; selection-background-color: rgba(6,182,212,0.3); }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid #06b6d4; }"
        "QLineEdit::placeholder { color: #484f58; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background: #1c2128; border: 1px solid #30363d; color: #e6edf3; selection-background-color: #21262d; }"

        // Buttons
        "QPushButton { background: #21262d; border: 1px solid #30363d; border-radius: 6px; padding: 6px 16px; color: #e6edf3; }"
        "QPushButton:hover { background: #30363d; border-color: #484f58; }"
        "QPushButton:pressed { background: #1c2128; }"
        "QPushButton:disabled { color: #484f58; background: #161b22; }"

        // List widgets
        "QListWidget { background: #0d1117; border: 1px solid #30363d; border-radius: 6px; color: #e6edf3; }"
        "QListWidget::item { padding: 8px 12px; border-bottom: 1px solid #21262d; }"
        "QListWidget::item:hover { background: #161b22; }"
        "QListWidget::item:selected { background: rgba(6,182,212,0.15); color: #06b6d4; border-left: 3px solid #06b6d4; }"

        // Labels
        "QLabel { color: #e6edf3; }"

        // Checkboxes
        "QCheckBox { color: #e6edf3; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 3px; border: 1px solid #30363d; background: #21262d; }"
        "QCheckBox::indicator:checked { background: #06b6d4; border-color: #06b6d4; }"

        // Progress bar
        "QProgressBar { background: #21262d; border: 1px solid #30363d; border-radius: 4px; text-align: center; color: #e6edf3; }"
        "QProgressBar::chunk { background: #06b6d4; border-radius: 3px; }"

        // Tab widget
        "QTabWidget::pane { border: 1px solid #30363d; background: #0d1117; }"
        "QTabBar::tab { background: #161b22; color: #7d8590; padding: 6px 16px; border: 1px solid #30363d; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; }"
        "QTabBar::tab:selected { background: #0d1117; color: #06b6d4; border-bottom: 2px solid #06b6d4; }"
        "QTabBar::tab:hover:!selected { background: #21262d; }"

        // Tool buttons in toolbars (small)
        "QToolButton { padding: 4px 8px; border-radius: 4px; }"
    ));

    // Create application context with all services
    ApplicationContext ctx;

    AppMainWindow window(&ctx);
    window.show();

    qDebug() << "OpenGeoStudio started — Road Engine v"
             << QString::fromLatin1(road_engine::versionString());

    return app.exec();
}

#include "main.moc"


