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

// Road engine — direct C++ include, no N-API bridge
#include "road_engine.hpp"

// Core services
#include "core/ApplicationContext.hpp"
#include "core/project/ProjectManager.hpp"
#include "core/workspace/WorkspaceManager.hpp"

// UI
#include "ui/home/HomeWidget.hpp"
#include "ui/roadstudio/RoadStudioWidget.hpp"
#include "ui/roadstudio/widgets/RoadInspector.hpp"
#include "ui/trainstudio/TrainStudioWidget.hpp"
#include "ui/terrain/TerrainStudioWidget.hpp"

// Phase 2c: MapLibre Native Qt map viewport
#if defined(HAVE_MAPLIBRE)
#include "app/MapViewportWidget.hpp"
#endif

// ═══════════════════════════════════════════════════════════
// MainWindow — the application shell
// ═══════════════════════════════════════════════════════════

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(ApplicationContext* ctx, QWidget* parent = nullptr)
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
                this, &MainWindow::onWorkspaceActivated);

        // Wire project changes
        connect(&m_ctx->projects(), &ProjectManager::projectChanged,
                this, &MainWindow::onProjectChanged);
        connect(&m_ctx->projects(), &ProjectManager::projectOpened,
                this, &MainWindow::onProjectOpened);

        updateStatusBar();
    }

private:
    ApplicationContext* m_ctx;
    QLabel* m_statusLabel = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    HomeWidget* m_homeWidget = nullptr;
    RoadStudioWidget* m_roadStudioWidget = nullptr;
    TrainStudioWidget* m_trainStudioWidget = nullptr;
    TerrainStudioWidget* m_terrainStudioWidget = nullptr;
    QDockWidget* m_rightDock = nullptr;
    QWidget* m_rightDockPlaceholder = nullptr;
    RoadInspector* m_roadInspector = nullptr;
#if defined(HAVE_MAPLIBRE)
    MapViewportWidget* m_mapWidget = nullptr;
#endif

    void setupMenuBar() {
        // File menu
        QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

        QAction* newProjectAct = fileMenu->addAction(tr("&New Project..."));
        newProjectAct->setShortcut(QKeySequence::New);
        connect(newProjectAct, &QAction::triggered, this, &MainWindow::onNewProject);

        QAction* openProjectAct = fileMenu->addAction(tr("&Open Project..."));
        openProjectAct->setShortcut(QKeySequence::Open);
        connect(openProjectAct, &QAction::triggered, this, &MainWindow::onOpenProject);

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
    }

    void setupToolBar() {
        QToolBar* toolbar = addToolBar(tr("Main"));
        toolbar->setMovable(false);

        QAction* newAct = toolbar->addAction(tr("New"));
        newAct->setShortcut(QKeySequence::New);
        connect(newAct, &QAction::triggered, this, &MainWindow::onNewProject);

        QAction* openAct = toolbar->addAction(tr("Open"));
        openAct->setShortcut(QKeySequence::Open);
        connect(openAct, &QAction::triggered, this, &MainWindow::onOpenProject);

        toolbar->addSeparator();

        // Workspace switching actions
        for (const auto& ws : m_ctx->workspaces().workspaces()) {
            auto* act = toolbar->addAction(ws.name);
            connect(act, &QAction::triggered, this, [this, id = ws.id]() {
                m_ctx->workspaces().activate(id);
            });
        }
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
                this, &MainWindow::onNewProjectFromTemplate);
        connect(m_homeWidget, &HomeWidget::openProjectRequested,
                this, &MainWindow::onOpenProjectPath);
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
        }
#endif

        // Page 2: Road Studio (2D map with road overlay)
        m_roadStudioWidget = new RoadStudioWidget(m_ctx);
        m_centerStack->addWidget(m_roadStudioWidget);
#if defined(HAVE_MAPLIBRE)
        if (m_roadStudioWidget->mapWidget()) {
            connect(m_roadStudioWidget->mapWidget(), &MapViewportWidget::mapClicked,
                    this, [this](double lat, double lon) {
                        m_statusLabel->setText(
                            QStringLiteral("Road Studio — Clicked: %1, %2")
                                .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
                    });
        }
#endif

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
        }
#endif

        setCentralWidget(m_centerStack);
        m_centerStack->setCurrentIndex(0); // Home
    }

    void setupDockWidgets() {
        // Left dock — project tree / explorer
        QDockWidget* leftDock = new QDockWidget(tr("Project"), this);
        leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        auto* leftContent = new QLabel(tr("Project tree\n(Phase 3)"));
        leftContent->setAlignment(Qt::AlignCenter);
        leftDock->setWidget(leftContent);
        addDockWidget(Qt::LeftDockWidgetArea, leftDock);

        // Right dock — inspector / properties
        m_rightDock = new QDockWidget(tr("Inspector"), this);
        m_rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        auto* placeholder = new QLabel(tr("Properties\n(Select a road)"));
        placeholder->setAlignment(Qt::AlignCenter);
        m_rightDockPlaceholder = placeholder;
        m_rightDock->setWidget(m_rightDockPlaceholder);
        addDockWidget(Qt::RightDockWidgetArea, m_rightDock);
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
        } else if (ws.id == "terrain") {
            m_centerStack->setCurrentIndex(1); // Terrain Studio
            if (m_rightDockPlaceholder) {
                m_rightDock->setWidget(m_rightDockPlaceholder);
                m_rightDock->setWindowTitle("Inspector");
            }
        } else if (ws.id == "road-studio") {
            m_centerStack->setCurrentIndex(2); // Road Studio
            // Swap inspector to RoadInspector
            if (m_roadStudioWidget && !m_roadInspector) {
                m_roadInspector = new RoadInspector(m_roadStudioWidget->store(), this);
            }
            if (m_roadInspector) {
                m_rightDock->setWidget(m_roadInspector);
                m_rightDock->setWindowTitle("Road Inspector");
            }
        } else if (ws.id == "train-studio") {
            m_centerStack->setCurrentIndex(3); // Train placeholder
        }

        // Reset inspector for non-road-studio workspaces
        if (ws.id != "road-studio" && m_rightDockPlaceholder) {
            m_rightDock->setWidget(m_rightDockPlaceholder);
            m_rightDock->setWindowTitle("Inspector");
        }

        setWindowTitle(QStringLiteral("OpenGeoStudio — %1").arg(ws.name));
        updateStatusBar();
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
    app.setApplicationName(QStringLiteral("OpenGeoStudio"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("OpenGeoStudio"));

    // Set dark theme (matching the Electron app's dark theme)
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(43, 43, 43));
    darkPalette.setColor(QPalette::WindowText, QColor(208, 208, 208));
    darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
    darkPalette.setColor(QPalette::AlternateBase, QColor(43, 43, 43));
    darkPalette.setColor(QPalette::Text, QColor(208, 208, 208));
    darkPalette.setColor(QPalette::Button, QColor(55, 55, 55));
    darkPalette.setColor(QPalette::ButtonText, QColor(208, 208, 208));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    darkPalette.setColor(QPalette::ToolTipBase, QColor(43, 43, 43));
    darkPalette.setColor(QPalette::ToolTipText, QColor(208, 208, 208));
    app.setPalette(darkPalette);

    // Create application context with all services
    ApplicationContext ctx;

    MainWindow window(&ctx);
    window.show();

    qDebug() << "OpenGeoStudio started — Road Engine v"
             << QString::fromLatin1(road_engine::versionString());

    return app.exec();
}

#include "main.moc"
