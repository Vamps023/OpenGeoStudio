// ═══════════════════════════════════════════════════════════
// OpenGeoStudio — Qt Application Entry Point
// ═══════════════════════════════════════════════════════════
//
// Initializes QApplication, creates the MainWindow shell, and
// starts the Qt event loop. This is the native C++ replacement
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
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

// Road engine — direct C++ include, no N-API bridge
#include "road_engine.hpp"

// Phase 2c: MapLibre Native Qt map viewport
#if defined(HAVE_MAPLIBRE)
#include "MapViewportWidget.hpp"
#endif

// ═══════════════════════════════════════════════════════════
// MainWindow — the application shell
// ═══════════════════════════════════════════════════════════

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr)
        : QMainWindow(parent)
    {
        setWindowTitle(QStringLiteral("OpenGeoStudio"));
        resize(1400, 900);

        setupMenuBar();
        setupToolBar();
        setupStatusBar();
        setupDockWidgets();

        // Verify road engine is linked and working
        const QString engineVersion = QString::fromLatin1(road_engine::versionString());
        m_statusLabel->setText(QStringLiteral("Road Engine v%1  |  Ready").arg(engineVersion));
    }

private:
    QLabel* m_statusLabel = nullptr;

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

        QAction* exitAct = fileMenu->addAction(tr("E&xit"));
        exitAct->setShortcut(QKeySequence::Quit);
        connect(exitAct, &QAction::triggered, qApp, &QApplication::quit);

        // View menu
        QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

        // Workspace switching (placeholder — Phase 3 will implement full workspace system)
        viewMenu->addAction(tr("Home Workspace"), this, [this]() {
            setWindowTitle(QStringLiteral("OpenGeoStudio — Home"));
        });
        viewMenu->addAction(tr("Terrain Workspace"), this, [this]() {
            setWindowTitle(QStringLiteral("OpenGeoStudio — Terrain"));
        });
        viewMenu->addAction(tr("Road Studio Workspace"), this, [this]() {
            setWindowTitle(QStringLiteral("OpenGeoStudio — Road Studio"));
        });
        viewMenu->addAction(tr("Train Studio Workspace"), this, [this]() {
            setWindowTitle(QStringLiteral("OpenGeoStudio — Train Studio"));
        });

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
    }

    void setupStatusBar() {
        m_statusLabel = new QLabel(QStringLiteral("Initializing..."));
        statusBar()->addWidget(m_statusLabel);
    }

    void setupDockWidgets() {
        // Left dock — placeholder for workspace navigator / project tree
        QDockWidget* leftDock = new QDockWidget(tr("Project"), this);
        leftDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        QLabel* leftPlaceholder = new QLabel(tr("Project tree will appear here\n(Phase 3 implementation)"));
        leftPlaceholder->setAlignment(Qt::AlignCenter);
        leftDock->setWidget(leftPlaceholder);
        addDockWidget(Qt::LeftDockWidgetArea, leftDock);

        // Right dock — placeholder for inspector / properties panel
        QDockWidget* rightDock = new QDockWidget(tr("Inspector"), this);
        rightDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        QLabel* rightPlaceholder = new QLabel(tr("Properties panel will appear here\n(Phase 3 implementation)"));
        rightPlaceholder->setAlignment(Qt::AlignCenter);
        rightDock->setWidget(rightPlaceholder);
        addDockWidget(Qt::RightDockWidgetArea, rightDock);

        // Central widget — MapLibre map viewport (Phase 2c rendering spike)
        // Uses Esri World Imagery raster tiles, same as the reference
        // Electron app's SkiaViewport.tsx
#if defined(HAVE_MAPLIBRE)
        auto* mapViewport = new MapViewportWidget(this);
        setCentralWidget(mapViewport);

        // Wire map click → status bar (demonstrates coordinate conversion works)
        connect(mapViewport, &MapViewportWidget::mapClicked,
                this, [this](double lat, double lon) {
                    m_statusLabel->setText(
                        QStringLiteral("Clicked: %1, %2  |  Road Engine v%3")
                            .arg(lat, 0, 'f', 6)
                            .arg(lon, 0, 'f', 6)
                            .arg(QString::fromLatin1(road_engine::versionString())));
                });
#else
        // Fallback: placeholder when MapLibre is not available
        QLabel* centerPlaceholder = new QLabel(
            tr("OpenGeoStudio\n\n"
               "Road Engine v%1 loaded successfully.\n\n"
               "MapLibre not found — build maplibre-native-qt to enable map viewport.")
            .arg(QString::fromLatin1(road_engine::versionString())));
        centerPlaceholder->setAlignment(Qt::AlignCenter);
        centerPlaceholder->setStyleSheet(QStringLiteral(
            "QLabel { font-size: 18px; color: #888; background-color: #2b2b2b; }"));
        setCentralWidget(centerPlaceholder);
#endif
    }

private slots:
    void onNewProject() {
        m_statusLabel->setText(QStringLiteral("New Project (not yet implemented — Phase 3)"));
    }

    void onOpenProject() {
        m_statusLabel->setText(QStringLiteral("Open Project (not yet implemented — Phase 3)"));
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

    MainWindow window;
    window.show();

    qDebug() << "OpenGeoStudio started — Road Engine v"
             << QString::fromLatin1(road_engine::versionString());

    return app.exec();
}

#include "main.moc"
