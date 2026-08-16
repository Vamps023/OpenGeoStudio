#include <QVBoxLayout>
#include <QMenuBar>
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QStatusBar>
#include <QApplication>
#include <QScreen>
#include <filesystem>
#include <fstream>
#include "../../../core/PathHelper.hpp"
#include <sstream>

#include "main_window.h"
#include "main_widget.h"
#include "change_tracker.h"
#include "action_manager.h"
#include "vehicle_manager.h"
#include "test/validation.h"
#include "util.h"
#include "replay_window.h"
#include "preference.h"

#include "spdlog/spdlog.h"

#include "map_view_gl.h"

MainWindow* g_mainWindow;

extern UserPreference g_preference;

MainWindow::MainWindow(QWidget* parent): QWidget(parent)
{
    setWindowTitle(tr("Road Studio"));
    setMinimumWidth(MinWidth);
    setMinimumHeight(MinHeight);
    resize(PreferredSize());

    g_mainWindow = this;

    // Menu bar is now integrated into the main app's menu bar (AppMainWindow)
    // We still need the QAction objects for state tracking
    toggleSimAction = new QAction("Toggle simulation", this);
    toggleSimAction->setCheckable(true);
    toggleSimAction->setChecked(false);
    pauseResumeSimulation = new QAction("Paused", this);
    pauseResumeSimulation->setCheckable(true);
    pauseResumeSimulation->setChecked(false);
    pauseResumeSimulation->setEnabled(false);

#ifdef __linux__
    replayWindow = std::make_unique<ReplayWindow>();
#else
    replayWindow = std::make_unique<ReplayWindow>(this);
#endif
    preferenceWindow = std::make_unique<PreferenceWindow>(this);

    vehicleManager = std::make_unique<VehicleManager>(this);

    mainWidget = std::make_unique<MainWidget>();
    mainWidget->toggleAntialiasing(g_preference.antiAlias);

    // Status bar — styled dark panel at bottom
    auto* statusBar = new QWidget;
    statusBar->setObjectName("roadStatusBar");
    statusBar->setFixedHeight(26);
    statusBar->setStyleSheet(
        "QWidget#roadStatusBar { background-color: #161b22; border-top: 1px solid #30363d; }"
        "QStatusBar { background: transparent; color: #7d8590; font-size: 11px; }"
        "QStatusBar QLabel { color: #7d8590; padding: 0 8px; }");
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(0);

    hintStatus = std::make_unique<QStatusBar>();
    hintStatus->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fpsStatus = std::make_unique<QStatusBar>();
    fpsStatus->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    statusLayout->addWidget(hintStatus.get());
    statusLayout->addStretch();
    statusLayout->addWidget(fpsStatus.get());

    auto mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(mainWidget.get());
    mainLayout->addWidget(statusBar);

    setLayout(mainLayout);

    // Internal connections for simulation state
    connect(toggleSimAction, &QAction::toggled, this, &MainWindow::toggleSimulation);
    connect(pauseResumeSimulation, &QAction::toggled, vehicleManager.get(), &VehicleManager::TogglePause);
    connect(replayWindow.get(), &ReplayWindow::Restart, this, &MainWindow::reset);
    connect(mainWidget.get(), &MainWidget::FPSChanged, this, &MainWindow::setFPS);
    connect(preferenceWindow.get(), &PreferenceWindow::ToggleAA, mainWidget.get(), &MainWidget::toggleAntialiasing);

    connect(mainWidget->mapViewGL, &LM::MapViewGL::MousePerformedAction, this, &MainWindow::updateHint);

    if (g_preference.showWelcome)
        preferenceWindow->open();
    srand(std::time(0));
}

MainWindow::~MainWindow() = default;

QSize MainWindow::PreferredSize() const
{
    auto available = QApplication::primaryScreen()->geometry();
    int preferredHeight = available.height() * 0.6;
    int preferredWidth = available.width() * 0.5;
    return QSize(std::max(preferredWidth, MinWidth), std::max(preferredHeight, MinHeight));
}

void MainWindow::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (recordResize)
        LM::ActionManager::Instance()->Record(e->oldSize(), e->size());
}

void MainWindow::newMap()
{
    auto oldsize = size();
    reset();
    LM::ActionManager::Instance()->Record(oldsize, size());
}

void MainWindow::reset()
{
    auto prevLevel = spdlog::get_level();
    /*Road destruction order be random, which could cause temporary invalid state.*/
    //spdlog::set_level(spdlog::level::critical);

    stopSimulation();
    mainWidget->Reset();
    LM::ChangeTracker::Instance()->Clear();
    LM::ActionManager::Instance()->Reset();
    LM::g_mapViewGL->ResetCamera();
    LM::g_createRoadElevationOption = 0;
    resizeDontRecord(PreferredSize().width(), PreferredSize().height());
    loadedFileName.clear();
    LM::g_mapViewGL->update();
    //spdlog::set_level(prevLevel);
}

void MainWindow::resizeDontRecord(int w, int h)
{
    recordResize = false;
    resize(w, h);
    recordResize = true;
}

void MainWindow::saveToFile()
{
    auto saveLoc = loadedFileName.empty() ? LM::DefaultSaveFolder().string() : loadedFileName;
    QString s = QFileDialog::getSaveFileName(
        this,
        "Choose save location",
        saveLoc.c_str(),
        "OpenDrive (*.xodr)", nullptr
#ifdef __linux__
        ,QFileDialog::DontUseNativeDialog
#endif
        );
    if (!s.isEmpty())
    {
        saveToPath(s);
    }
}

void MainWindow::saveToPath(const QString& path)
{
    QString s = path;
    if (!s.endsWith(".xodr")) {
        s.append(".xodr");
    }

    // Ensure the parent directory exists (Qt handles Unicode paths)
    QDir().mkpath(QFileInfo(s).absolutePath());

    // pugixml's save_file uses fopen() which can't handle Unicode paths
    // on Windows. Always save to a temp file first (ASCII-safe location),
    // then copy to the final destination using QFile::copy (Qt handles
    // Unicode paths natively). This is more reliable than trying to detect
    // whether the path is ASCII or not.
    QString tempFile = QDir::tempPath() + "/road_save_temp.xodr";
    if (QFile::exists(tempFile)) QFile::remove(tempFile);

    auto tempLoc = tempFile.toStdString();
    LM::ChangeTracker::Instance()->Save(tempLoc);

    if (!QFile::exists(tempFile)) {
        spdlog::error("saveToPath: failed to save temp file: {}", tempFile.toStdString());
        return;
    }

    // Copy from temp to final destination (Qt handles Unicode)
    if (QFile::exists(s)) QFile::remove(s);
    if (QFile::copy(tempFile, s)) {
        spdlog::info("saveToPath: saved to: {} (size: {})", s.toStdString(), QFileInfo(s).size());
    } else {
        spdlog::error("saveToPath: failed to copy to final destination: {}", s.toStdString());
    }
    QFile::remove(tempFile);

    loadedFileName = s.toStdString();
    setProperty("lastRoadFile", s);
}

void MainWindow::loadFromFile()
{
    QString s = QFileDialog::getOpenFileName(
        this,
        "Choose File to Open",
        LM::DefaultSaveFolder().string().c_str(),
        "OpenDrive (*.xodr)", nullptr
#ifdef __linux__
        ,QFileDialog::DontUseNativeDialog
#endif
    );
    if (s.size() != 0)
    {
        loadFromPath(s);
    }
}

void MainWindow::loadFromPath(const QString& path)
{
    QString s = path;
    if (s.isEmpty()) return;
    if (!QFile::exists(s)) {
        spdlog::warn("loadFromPath: file does not exist: {}", s.toStdString());
        return;
    }

    // Check file is not empty
    QFileInfo fi(s);
    if (fi.size() == 0) {
        spdlog::warn("loadFromPath: file is empty, skipping: {}", s.toStdString());
        return;
    }

    reset();

    // std::ifstream can't handle Unicode paths on Windows.
    // Always copy to a temp file in an ASCII-safe location first,
    // then load from there. This is simpler and more reliable than
    // trying to detect whether the path is ASCII or not.
    QString tempFile = QDir::tempPath() + "/road_load_temp.xodr";
    if (QFile::exists(tempFile)) QFile::remove(tempFile);
    if (!QFile::copy(s, tempFile)) {
        spdlog::error("loadFromPath: failed to copy to temp file: {}", s.toStdString());
        return;
    }

    auto tempLoc = tempFile.toStdString();
    bool loaded = LM::ChangeTracker::Instance()->Load(tempLoc);
    QFile::remove(tempFile);

    if (!loaded) {
        spdlog::error("loadFromPath: ChangeTracker::Load failed for: {}", s.toStdString());
        return;
    }

    spdlog::info("loadFromPath: loaded successfully from: {}", s.toStdString());

    // Read the file content for action history recording
    // Use QFile (handles Unicode) then convert to std::string
    QFile qfile(s);
    if (qfile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray content = qfile.readAll();
        qfile.close();
        LM::ActionManager::Instance()->Record(content.toStdString());
    }

    loadedFileName = s.toStdString();
    setProperty("lastRoadFile", s);

    if (LM::g_mapViewGL) {
        LM::g_mapViewGL->update();
    }
}

void MainWindow::undo()
{
    LM::ActionManager::Instance()->Record(LM::ActionType::Action_Undo);
    if (!LM::ChangeTracker::Instance()->Undo())
    {
        spdlog::warn("Cannot undo");
    }
    else
    {
        LM::g_mapViewGL->update();
    }
}

void MainWindow::redo()
{
    LM::ActionManager::Instance()->Record(LM::ActionType::Action_Redo);
    if (!LM::ChangeTracker::Instance()->Redo())
    {
        spdlog::warn("Cannot redo");
    }
    else
    {
        LM::g_mapViewGL->update();
    }
}

void MainWindow::verifyMap()
{
    LTest::Validation::ValidateMap();
    spdlog::info("Done map verification.");
}

void MainWindow::saveActionHistory()
{
    QString s = QFileDialog::getSaveFileName(
        this,
        "Choose save location",
        LM::DefaultSaveFolder().string().c_str(),
        "ActionHistory (*.dat)", nullptr
#ifdef __linux__
        ,QFileDialog::DontUseNativeDialog
#endif
        );
    if (s.size() != 0)
    {
        auto loc = s.toStdString();
        LM::ActionManager::Instance()->Save(loc);
    }
}

void MainWindow::debugActionHistory()
{
    openReplayWindow(true);
}

void MainWindow::playActionHistory()
{
    openReplayWindow(false);
}

void MainWindow::openReplayWindow(bool playImmediate)
{
    QString s = QFileDialog::getOpenFileName(
        this,
        "Choose File to Open",
        LM::DefaultSaveFolder().string().c_str(),
        "ActionHistory (*.dat)", nullptr
#ifdef __linux__
        ,QFileDialog::DontUseNativeDialog
#endif
        );
    if (!s.isEmpty())
    {
        reset();
        QScreen* screen = QGuiApplication::primaryScreen();
        QRect  screenGeometry = screen->geometry();
        replayWindow->LoadHistory(s.toStdString(), playImmediate);
        const int replayWindowWidth = 300;
        replayWindow->setGeometry(
            std::min(screenGeometry.width() - replayWindowWidth, geometry().right()), geometry().top(), 
            replayWindowWidth, geometry().height());
        replayWindow->open();
    }
}

void MainWindow::toggleSimulation(bool enable)
{
    pauseResumeSimulation->setChecked(false);
    if (enable)
    {
        vehicleManager->Begin();
    }
    else
    {
        vehicleManager->End();
    }
    pauseResumeSimulation->setEnabled(enable);
    mainWidget->GoToSimulationMode(enable);
}

void MainWindow::stopSimulation()
{
    if (toggleSimAction->isChecked())
    {
        vehicleManager->End();
        toggleSimAction->setChecked(false);
    }
}

void MainWindow::setFPS(QString msg)
{
    fpsStatus->showMessage(msg);
}

void MainWindow::updateHint()
{
    auto groundInfo = QString("(%1, %2) ")
        .arg(LM::g_PointerOnGround[0])
        .arg(LM::g_PointerOnGround[1]);
    auto roadInfo = LM::g_PointerRoadID.empty() ?
        QString("VBuffer: %1%")
        .arg(mainWidget->mapViewGL->VBufferUseage_pct()) :
        QString("Road %1 @%2 Lane %3")
        .arg(LM::g_PointerRoadID.c_str())
        .arg(LM::g_PointerRoadS, 6, 'f', 3)
        .arg(LM::g_PointerLane);
    groundInfo.append(roadInfo);
    if (LM::g_PointerVehicle != -1)
    {
        groundInfo.append(QString("  Vehicle: %1").arg(LM::g_PointerVehicle));
    }

    hintStatus->showMessage(groundInfo);
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    testReplay();
    reset();
    mainWidget->mapViewGL->CleanupResources();
    QWidget::closeEvent(e);
}

#ifdef __linux__
void MainWindow::keyPressEvent(QKeyEvent* e)
{
    mainWidget->mapViewGL->keyPressEvent(e);
}
#endif

void MainWindow::testReplay()
{
    auto recordPath = LM::ActionManager::Instance()->AutosavePath();
    if (g_preference.alwaysVerify
        && std::filesystem::exists(recordPath))
    {
        g_preference.alwaysVerify = false; // No verification during replay
        auto saveFolder = LM::DefaultSaveFolder();
        auto originalPath = saveFolder / (std::string("compare_a_") + LM::RunTimestamp() + std::string(".xodr"));
        auto originalPathStr = originalPath.string();
        LM::ChangeTracker::Instance()->Save(originalPathStr);

        reset();

        quitReplayComplete = false;
        connect(replayWindow.get(), &ReplayWindow::DoneReplay, this, &MainWindow::onReplayDone);
        replayWindow->LoadHistory(recordPath, true);
        replayWindow->exec();

        if (quitReplayComplete)
        {
            auto replayPath = saveFolder / (std::string("compare_b_") + LM::RunTimestamp() + std::string(".xodr"));
            auto replayPathStr = replayPath.string();
            LM::ChangeTracker::Instance()->Save(replayPathStr);

            if (!LTest::Validation::CompareFiles(originalPathStr, replayPathStr))
            {
                LM::ActionManager::Instance()->MarkException();
                spdlog::error("Replay result is different from original map! Check {} for details.", recordPath);
            }
            else
            {
                // On success, clean up temporary saves
                std::remove(originalPathStr.c_str());
                std::remove(replayPathStr.c_str());
                spdlog::info("Action replay test: OK");
            }
        }
        else
        {
            // cancelled by user
            std::remove(originalPathStr.c_str());
            spdlog::info("Action replay test: Cancelled");
        }
    }
    
    if (LM::ActionManager::Instance()->CleanAutoSave())
    {
        std::remove(recordPath.c_str());
    }
}

void MainWindow::onReplayDone(bool completed)
{
    quitReplayComplete = completed;
    replayWindow->close();
}

void MainWindow::runReplay(std::string replay)
{
    if (!std::filesystem::exists(replay))
    {
        spdlog::warn("Unable to run speficied replay(s): File does not exist");
        return;
    }
    // Always run verify during replay
    g_preference.alwaysVerify = true;
    connect(replayWindow.get(), &ReplayWindow::DoneReplay, this, &MainWindow::onReplayDone);

    if (std::filesystem::is_regular_file(replay) && std::filesystem::path(replay).extension() == ".dat")
    {
        spdlog::info(">> Running {}", replay);
        replayWindow->LoadHistory(replay, true);
        replayWindow->exec();
    }
    else if (std::filesystem::is_directory(replay))
    {
        for (std::filesystem::recursive_directory_iterator i(replay), end; i != end; ++i)
        {
            auto path = i->path();
            if (std::filesystem::is_regular_file(path) && std::filesystem::path(path).extension() == ".dat")
            {
                spdlog::info(">> Running {}", path.string());
                replayWindow->LoadHistory(path.string(), true);
                replayWindow->exec();
                reset();
            }
        }
    }
}

// Helper methods for external menu bar integration
bool MainWindow::isSimulationActive() const
{
    return toggleSimAction && toggleSimAction->isChecked();
}

bool MainWindow::isSimulationPaused() const
{
    return pauseResumeSimulation && pauseResumeSimulation->isChecked();
}

void MainWindow::togglePauseSimulation(bool paused)
{
    if (pauseResumeSimulation)
    {
        pauseResumeSimulation->setChecked(paused);
        if (paused)
            vehicleManager->TogglePause();
    }
}

void MainWindow::openPreferences()
{
    if (preferenceWindow)
        preferenceWindow->open();
}

void MainWindow::setRailMode(bool railMode)
{
    if (railMode)
        setWindowTitle(tr("Train Studio"));
    else
        setWindowTitle(tr("Road Studio"));

    if (mainWidget)
        mainWidget->SetRailMode(railMode);
}

bool MainWindow::isRailMode() const
{
    return mainWidget && mainWidget->IsRailMode();
}

void MainWindow::useSharedSatelliteView(double lat, double lon, double zoom)
{
    if (mainWidget)
        mainWidget->UseSharedSatelliteView(lat, lon, zoom);
}
