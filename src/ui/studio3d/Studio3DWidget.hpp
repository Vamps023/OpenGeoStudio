#pragma once

// ============================================================
// Studio3DWidget — 3D Studio workspace with OGRE-Next
// ============================================================
//
// OGRE-Next is embedded directly in Qt (no external process).
// This widget provides:
//   - Embedded 3D viewport using OgreWidget
//   - Load terrain heightmap + albedo from project
//   - Orbit/zoom camera controls
//   - Export terrain data for external use
//

#include <QWidget>
#include <QString>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QProgressBar>
#include <QGroupBox>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QJsonObject>
#include <QDir>

class ApplicationContext;
class OgreWidget;
class WorldOutliner;
class Inspector;
class LayerPanel;
class ContentBrowser;

class Studio3DWidget : public QWidget {
    Q_OBJECT

public:
    explicit Studio3DWidget(ApplicationContext* ctx, QWidget* parent = nullptr);
    ~Studio3DWidget() override;

private slots:
    void onLoadTerrain();
    void onClearTerrain();
    void onLoadRoads();
    void onResetCamera();
    void onExportTerrain();
    void onExportRoads();
    void onHeightScaleChanged(int value);
    void onAddBuilding();
    void onAddTree();
    void onAddBox();
    void onClearObjects();
    void onSaveScene();
    void onLoadScene();
    void onSaveWorld();
    void onLoadWorld();
    void onActorSelected(const QString& id);

public:
    // Called by main window when project is opened/closed
    void onProjectOpened();
    void onProjectClosed();

private:
    void setupUI();
    void appendLog(const QString& msg);
    QString findHeightmapInProject();
    QString findAlbedoInProject();
    QString findXodrInProject();
    QString sceneFilePath() const;
    QString worldFilePath() const;
    QJsonObject resolveScenePaths(const QJsonObject& scene);

    ApplicationContext* m_ctx;

    // OGRE viewport
    OgreWidget* m_ogreWidget = nullptr;

    // Editor panels
    WorldOutliner* m_outliner = nullptr;
    Inspector* m_inspector = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    ContentBrowser* m_contentBrowser = nullptr;

    // Controls
    QPushButton* m_loadTerrainBtn = nullptr;
    QPushButton* m_clearTerrainBtn = nullptr;
    QPushButton* m_loadRoadsBtn = nullptr;
    QPushButton* m_resetCameraBtn = nullptr;
    QPushButton* m_exportTerrainBtn = nullptr;
    QPushButton* m_exportRoadsBtn = nullptr;
    QPushButton* m_addBuildingBtn = nullptr;
    QPushButton* m_addTreeBtn = nullptr;
    QPushButton* m_addBoxBtn = nullptr;
    QPushButton* m_clearObjectsBtn = nullptr;
    QPushButton* m_saveSceneBtn = nullptr;
    QPushButton* m_loadSceneBtn = nullptr;
    QPushButton* m_saveWorldBtn = nullptr;
    QPushButton* m_loadWorldBtn = nullptr;
    QSlider* m_heightScaleSlider = nullptr;
    QLabel* m_heightScaleLabel = nullptr;

    // Status
    QLabel* m_statusLabel = nullptr;
    QTextEdit* m_logEdit = nullptr;

    // Track whether scene has been auto-loaded for current project
    bool m_sceneAutoLoaded = false;
};
