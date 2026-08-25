#pragma once

// ============================================================
// PropertiesEditor — Blender-style tabbed properties panel
//
// Vertical icon tabs on the right edge switch between property
// contexts: Object, Material, World, Scene, Render. Each context
// shows a scrollable form of relevant controls. The editor reads
// and writes through OgreWidget (the world model remains the
// single source of truth).
// ============================================================

#include <QWidget>
#include <QTabWidget>
#include <QScrollArea>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>

#include "../../core/world/World.hpp"

class OgreWidget;

class PropertiesEditor : public QWidget {
    Q_OBJECT
public:
    enum class Context {
        Object,
        Material,
        World,
        Scene,
        Render
    };

    explicit PropertiesEditor(OgreWidget* ogre, QWidget* parent = nullptr);

    void setActor(const QString& actorId);
    void clear();
    void setContext(Context ctx);
    Context currentContext() const { return m_context; }
    // Default folder for .ogsmat save/assign (the project's Materials dir).
    void setMaterialDir(const QString& dir) { m_materialDir = dir; }

signals:
    void actorModified(const QString& id);

private slots:
    void onTransformChanged();
    void onNameChanged();
    void onVisibilityToggled(bool visible);
    void onLayerChanged(int index);
    void onColorButtonClicked();
    void onRoughnessChanged(int value);
    void onMetalnessChanged(int value);
    void onAlbedoBrowse();
    void onAlbedoClear();
    void onNormalBrowse();
    void onNormalClear();
    void onResetMaterial();
    void onAssignMaterialAsset();
    void onSaveMaterialAsset();
    void onBrowseAssetPath();
    void onSunChanged();
    void onSkyChanged();
    void onTerrainChanged();
    void onRenderChanged();

private:
    void buildObjectTab();
    void buildMaterialTab();
    void buildWorldTab();
    void buildSceneTab();
    void buildRenderTab();
    void refreshObjectTab();
    void refreshMaterialTab();
    void refreshWorldTab();
    void refreshSceneTab();
    void refreshRenderTab();
    void refreshCurrent();

    friend class Studio3DWidget;

    OgreWidget* m_ogre;
    QString m_currentActorId;
    Context m_context = Context::Object;
    bool m_updating = false;

    QTabWidget* m_tabs = nullptr;

    // Object tab
    QWidget* m_objectTab = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QCheckBox* m_visibleCheck = nullptr;
    QComboBox* m_layerCombo = nullptr;
    QDoubleSpinBox *m_posX, *m_posY, *m_posZ = nullptr;
    QDoubleSpinBox *m_rotX, *m_rotY, *m_rotZ = nullptr;
    QDoubleSpinBox *m_scaleX, *m_scaleY, *m_scaleZ = nullptr;

    // Material tab
    QWidget* m_materialTab = nullptr;
    QPushButton* m_colorSwatchBtn = nullptr;
    QSlider* m_roughnessSlider = nullptr;
    QSlider* m_metalnessSlider = nullptr;
    QLabel* m_roughnessLabel = nullptr;
    QLabel* m_metalnessLabel = nullptr;
    QLineEdit* m_albedoTexEdit = nullptr;
    QPushButton* m_albedoBrowseBtn = nullptr;
    QPushButton* m_albedoClearBtn = nullptr;
    QLineEdit* m_normalTexEdit = nullptr;
    QPushButton* m_normalBrowseBtn = nullptr;
    QPushButton* m_normalClearBtn = nullptr;
    QLineEdit* m_assetPathEdit = nullptr;
    QPushButton* m_browseAssetBtn = nullptr;
    QPushButton* m_resetMaterialBtn = nullptr;
    QPushButton* m_assignMaterialBtn = nullptr;
    QPushButton* m_saveMaterialBtn = nullptr;
    QLabel* m_materialAssetLabel = nullptr;
    QString m_materialDir;   // default folder for .ogsmat save/assign

    // World tab (lighting + terrain)
    QWidget* m_worldTab = nullptr;
    QSlider* m_sunYawSlider = nullptr;
    QSlider* m_sunPitchSlider = nullptr;
    QSlider* m_sunIntensitySlider = nullptr;
    QSlider* m_skyIntensitySlider = nullptr;
    QLabel* m_sunYawLabel = nullptr;
    QLabel* m_sunPitchLabel = nullptr;
    QLabel* m_sunIntensityLabel = nullptr;
    QLabel* m_skyIntensityLabel = nullptr;
    QSlider* m_heightScaleSlider = nullptr;
    QLabel* m_heightScaleLabel = nullptr;
    QPushButton* m_loadTerrainBtn = nullptr;
    QPushButton* m_clearTerrainBtn = nullptr;
    QPushButton* m_loadRoadsBtn = nullptr;
    QPushButton* m_genBuildingsBtn = nullptr;
    QPushButton* m_genVegetationBtn = nullptr;

    // Scene tab
    QWidget* m_sceneTab = nullptr;
    QPushButton* m_saveSceneBtn = nullptr;
    QPushButton* m_loadSceneBtn = nullptr;
    QPushButton* m_saveWorldBtn = nullptr;
    QPushButton* m_loadWorldBtn = nullptr;
    QPushButton* m_clearObjectsBtn = nullptr;

    // Render tab
    QWidget* m_renderTab = nullptr;
    QCheckBox* m_gridCheck = nullptr;
    QCheckBox* m_snapCheck = nullptr;
    QComboBox* m_snapSizeCombo = nullptr;
    QPushButton* m_resetCameraBtn = nullptr;
};
