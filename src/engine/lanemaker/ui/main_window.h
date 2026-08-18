#pragma once
#include <qwidget.h>

QT_BEGIN_NAMESPACE
class QGraphicsScene;
class QStatusBar;
class ReplayWindow;
class PreferenceWindow;
QT_END_NAMESPACE

class MainWidget;
class VehicleManager;

namespace LM
{
    class MapViewGL;
}

class MainWindow : public QWidget
{
public:
    MainWindow(QWidget* parent = nullptr);

    ~MainWindow();

    void resizeDontRecord(int w, int h);

    void runReplay(std::string replay);

    // Exposed for external menu bar integration
    void newMap();
    void verifyMap();
    void saveActionHistory();
    void debugActionHistory();
    void playActionHistory();
    void toggleSimulation(bool enabled);
    void stopSimulation();
    bool isSimulationPaused() const;
    void togglePauseSimulation(bool paused);
    bool isSimulationActive() const;
    void openPreferences();

    // Switch to rail mode (Train Studio) — changes profile catalog to rail profiles
    // and cross-section visual to draw rails instead of road lanes.
    void setRailMode(bool railMode);
    bool isRailMode() const;

    // Match Terrain Studio's Esri imagery center and slippy-map zoom.
    void useSharedSatelliteView(double lat, double lon, double zoom);

    // Access the internal MainWidget (for GL initialization triggers)
    MainWidget* getMainWidget() const { return mainWidget.get(); }

    // Force the OpenGL widget to render — call when Road Studio becomes visible
    // so that initializeGL() runs and road drawing is possible.
    void triggerGLInitialization();

protected:
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void closeEvent(QCloseEvent* event) override;

#ifdef __linux__
    void keyPressEvent(QKeyEvent*) override;
#endif

private:
    const int MinWidth = 640;
    const int MinHeight = 480;

    QSize PreferredSize() const;

    std::unique_ptr<QStatusBar> hintStatus;
    std::unique_ptr<QStatusBar> fpsStatus;

    std::unique_ptr<MainWidget> mainWidget;
    std::unique_ptr<VehicleManager> vehicleManager;

    std::unique_ptr<ReplayWindow> replayWindow;
    std::unique_ptr<PreferenceWindow> preferenceWindow;

    QAction* toggleSimAction;
    QAction* pauseResumeSimulation;

    bool quitReplayComplete;

    bool recordResize = true;

    std::string loadedFileName;
    QString m_pendingLoadPath;  // deferred until GL is initialized

public slots:
    void ReplaySingleStep();

    void undo();

    void redo();

    void saveToFile();

    void loadFromFile();

    // Save/load to a specific path without showing a file dialog.
    // Used by the project system to auto-save/load road data.
    void saveToPath(const QString& path);
    void loadFromPath(const QString& path);

    // Returns the path of the currently loaded road file, or empty.
    QString loadedFilePath() const { return QString::fromStdString(loadedFileName); }

private slots:
    void updateHint();
    void setFPS(QString);

    void onReplayDone(bool);

private:
    void openReplayWindow(bool playImmediate);

    void testReplay();

    void reset();
};

extern MainWindow* g_mainWindow;
