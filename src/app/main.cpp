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

        // Page 1: Map (Terrain + Road Studio 2D)
#if defined(HAVE_MAPLIBRE)
        m_mapWidget = new MapViewportWidget();
        m_centerStack->addWidget(m_mapWidget);
        connect(m_mapWidget, &MapViewportWidget::mapClicked,
                this, [this](double lat, double lon) {
                    m_statusLabel->setText(
                        QStringLiteral("Clicked: %1, %2").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6));
                });
#else
        auto* mapPlaceholder = new QLabel("MapLibre not available — build maplibre-native-qt");
        mapPlaceholder->setAlignment(Qt::AlignCenter);
        m_centerStack->addWidget(mapPlaceholder);
#endif

        // Page 2: Road Studio placeholder
        auto* roadPlaceholder = new QLabel("Road Studio — Phase 4 implementation");
        roadPlaceholder->setAlignment(Qt::AlignCenter);
        roadPlaceholder->setStyleSheet("font-size: 18px; color: #888;");
        m_centerStack->addWidget(roadPlaceholder);

        // Page 3: Train Studio placeholder
        auto* trainPlaceholder = new QLabel("Train Studio — Phase 5 implementation");
        trainPlaceholder->setAlignment(Qt::AlignCenter);
        trainPlaceholder->setStyleSheet("font-size: 18px; color: #888;");
        m_centerStack->addWidget(trainPlaceholder);

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
        QDockWidget* rightDock = new QDockWidget(tr("Inspector"), this);
        rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        auto* rightContent = new QLabel(tr("Properties\n(Phase 3)"));
        rightContent->setAlignment(Qt::AlignCenter);
        rightDock->setWidget(rightContent);
        addDockWidget(Qt::RightDockWidgetArea, rightDock);
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
            m_centerStack->setCurrentIndex(1); // Map
        } else if (ws.id == "road-studio") {
            m_centerStack->setCurrentIndex(1); // Map (2D road overlay will be added in Phase 4)
        } else if (ws.id == "train-studio") {
            m_centerStack->setCurrentIndex(3); // Train placeholder
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
