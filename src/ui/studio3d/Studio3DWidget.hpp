#pragma once

// ============================================================
// Studio3DWidget — Unreal-style 3D Studio workspace
// ============================================================
//
// Layout follows the UE editor:
//   - Toolbar: save/load, transform tools (Q/W/E/R), snap + grid
//   - Left:    World Outliner + Layers
//   - Center:  OGRE-Next viewport
//   - Right:   Inspector (Details) + Place Actors / World tabs
//   - Bottom:  Content Browser (asset library) + Output Log tabs
//   - Status bar with live hints
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
#include <QToolButton>
#include <QJsonObject>
#include <QDir>

#include "../../core/world/World.hpp"

class ApplicationContext;
class OgreWidget;
class WorldOutliner;
class Inspector;
class LayerPanel;
class ContentBrowser;
class QTabWidget;
class QButtonGroup;

class Studio3DWidget : public QWidget {
    Q_OBJECT

public:
    explicit Studio3DWidget(ApplicationContext* ctx, QWidget* parent = nullptr);
    ~Studio3DWidget() override;

private slots:
    void onLoadTerrain();
    void onClearTerrain();
    void onLoadRoads();
    void onExportTerrain();
    void onExportRoads();
    void onHeightScaleChanged(int value);
    void onAddBuilding();
    void onAddTree();
    void onAddBox();
    void onClearObjects();
    void onGenerateBuildings();
    void onGenerateVegetation();
    void onAddLake();
    void onAddSunLight();
    void onAddSkyLight();
    void onSaveScene();
    void onLoadScene();
    void onSaveWorld();
    void onLoadWorld();
    void onActorSelected(const QString& id);

    // Toolbar-driven editor state
    void onTransformModeChanged(int mode);
    void onSnapToggled(bool enabled);
    void onSnapSizeChanged(int index);
    void onGridToggled(bool visible);

    // Asset library placement (ContentBrowser::assetRequested)
    void onPlaceAsset(const QString& pathOrType, const QString& type);

public:
    // Called by main window when project is opened/closed
    void onProjectOpened();
    void onProjectClosed();

private:
    void setupUI();
    QWidget* setupToolbar();
    QWidget* setupLeftPanel();
    QWidget* setupRightPanel();
    QWidget* setupStatusBar();
    void appendLog(const QString& msg);
    void setStatus(const QString& text);
    void refreshStats();
    QString findHeightmapInProject();
    QString findAlbedoInProject();
    QString findXodrInProject();
    QString sceneFilePath() const;
    QString worldFilePath() const;
    QJsonObject resolveScenePaths(const QJsonObject& scene);
    void loadMissingProjectAssets();

    // Places an actor at the camera focus, snapped to the terrain surface.
    // Returns the new actor's id.
    QString placeActorAtFocus(world::ActorType type, float sx, float sy, float sz,
                              const QString& layerId, const QString& name = QString());
    // Size/layer presets for the built-in actor palette
    void placePreset(world::ActorType type);

    ApplicationContext* m_ctx;

    // OGRE viewport
    OgreWidget* m_ogreWidget = nullptr;

    // Editor panels
    WorldOutliner* m_outliner = nullptr;
    Inspector* m_inspector = nullptr;
    LayerPanel* m_layerPanel = nullptr;
    ContentBrowser* m_contentBrowser = nullptr;
    QTabWidget* m_bottomTabs = nullptr;

    // Toolbar
    QToolButton* m_selectToolBtn = nullptr;
    QToolButton* m_moveToolBtn = nullptr;
    QToolButton* m_rotateToolBtn = nullptr;
    QToolButton* m_scaleToolBtn = nullptr;
    QButtonGroup* m_toolGroup = nullptr;
    QToolButton* m_snapBtn = nullptr;
    QToolButton* m_gridBtn = nullptr;
    QComboBox* m_snapSizeCombo = nullptr;

    // Terrain controls (World tab)
    QSlider* m_heightScaleSlider = nullptr;
    QLabel* m_heightScaleLabel = nullptr;

    // Status bar
    QLabel* m_statusLabel = nullptr;
    QLabel* m_statsLabel = nullptr;

    // Output log
    QTextEdit* m_logEdit = nullptr;

    // Track whether scene has been auto-loaded for current project
    bool m_sceneAutoLoaded = false;
};
