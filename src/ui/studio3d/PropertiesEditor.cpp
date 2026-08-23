#include "PropertiesEditor.hpp"
#include "OgreWidget.hpp"

#include <QLineEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <cmath>

PropertiesEditor::PropertiesEditor(OgreWidget* ogre, QWidget* parent)
    : QWidget(parent), m_ogre(ogre)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::East);

    buildObjectTab();
    buildMaterialTab();
    buildWorldTab();
    buildSceneTab();
    buildRenderTab();

    m_tabs->addTab(m_objectTab, tr("Object"));
    m_tabs->addTab(m_materialTab, tr("Material"));
    m_tabs->addTab(m_worldTab, tr("World"));
    m_tabs->addTab(m_sceneTab, tr("Scene"));
    m_tabs->addTab(m_renderTab, tr("Render"));

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        m_context = static_cast<Context>(idx);
        refreshCurrent();
    });

    lay->addWidget(m_tabs);
    refreshCurrent();
}

// Helper to make a compact spinbox
static QDoubleSpinBox* makeSpinBox(float step, float minVal, float maxVal, int decimals)
{
    auto* sb = new QDoubleSpinBox();
    sb->setSingleStep(step);
    sb->setRange(minVal, maxVal);
    sb->setDecimals(decimals);
    sb->setButtonSymbols(QDoubleSpinBox::NoButtons);
    return sb;
}

void PropertiesEditor::buildObjectTab()
{
    m_objectTab = new QWidget();
    auto* scroll = new QScrollArea(m_objectTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* form = new QVBoxLayout(content);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(6);

    // Name
    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel("Name:"));
    m_nameEdit = new QLineEdit();
    nameRow->addWidget(m_nameEdit);
    form->addLayout(nameRow);

    // Visibility + Layer
    m_visibleCheck = new QCheckBox("Visible");
    form->addWidget(m_visibleCheck);
    auto* layerRow = new QHBoxLayout();
    layerRow->addWidget(new QLabel("Layer:"));
    m_layerCombo = new QComboBox();
    layerRow->addWidget(m_layerCombo);
    form->addLayout(layerRow);

    // Transform group
    auto* transformGroup = new QGroupBox("Transform");
    auto* tForm = new QFormLayout(transformGroup);
    auto* posRow = new QHBoxLayout();
    m_posX = makeSpinBox(0.1f, -100000, 100000, 2);
    m_posY = makeSpinBox(0.1f, -100000, 100000, 2);
    m_posZ = makeSpinBox(0.1f, -100000, 100000, 2);
    posRow->addWidget(new QLabel("X")); posRow->addWidget(m_posX);
    posRow->addWidget(new QLabel("Y")); posRow->addWidget(m_posY);
    posRow->addWidget(new QLabel("Z")); posRow->addWidget(m_posZ);
    tForm->addRow("Location:", posRow);

    auto* rotRow = new QHBoxLayout();
    m_rotX = makeSpinBox(1.0f, -360, 360, 1);
    m_rotY = makeSpinBox(1.0f, -360, 360, 1);
    m_rotZ = makeSpinBox(1.0f, -360, 360, 1);
    rotRow->addWidget(new QLabel("X")); rotRow->addWidget(m_rotX);
    rotRow->addWidget(new QLabel("Y")); rotRow->addWidget(m_rotY);
    rotRow->addWidget(new QLabel("Z")); rotRow->addWidget(m_rotZ);
    tForm->addRow("Rotation:", rotRow);

    auto* scaleRow = new QHBoxLayout();
    m_scaleX = makeSpinBox(0.1f, 0.01f, 1000, 3);
    m_scaleY = makeSpinBox(0.1f, 0.01f, 1000, 3);
    m_scaleZ = makeSpinBox(0.1f, 0.01f, 1000, 3);
    scaleRow->addWidget(new QLabel("X")); scaleRow->addWidget(m_scaleX);
    scaleRow->addWidget(new QLabel("Y")); scaleRow->addWidget(m_scaleY);
    scaleRow->addWidget(new QLabel("Z")); scaleRow->addWidget(m_scaleZ);
    tForm->addRow("Scale:", scaleRow);
    form->addWidget(transformGroup);

    form->addStretch();
    scroll->setWidget(content);

    auto* outer = new QVBoxLayout(m_objectTab);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(m_nameEdit, &QLineEdit::editingFinished, this, &PropertiesEditor::onNameChanged);
    connect(m_visibleCheck, &QCheckBox::toggled, this, &PropertiesEditor::onVisibilityToggled);
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertiesEditor::onLayerChanged);
    connect(m_posX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
    connect(m_posY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
    connect(m_posZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
    connect(m_rotX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
    connect(m_rotY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
    connect(m_rotZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
    connect(m_scaleX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
    connect(m_scaleY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
    connect(m_scaleZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onTransformChanged);
}

void PropertiesEditor::buildMaterialTab()
{
    m_materialTab = new QWidget();
    auto* scroll = new QScrollArea(m_materialTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* form = new QVBoxLayout(content);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(6);

    auto* colorGroup = new QGroupBox("Base Color");
    auto* cForm = new QFormLayout(colorGroup);
    auto* colorRow = new QHBoxLayout();
    m_colorR = makeSpinBox(0.01f, 0, 1, 3);
    m_colorG = makeSpinBox(0.01f, 0, 1, 3);
    m_colorB = makeSpinBox(0.01f, 0, 1, 3);
    colorRow->addWidget(new QLabel("R")); colorRow->addWidget(m_colorR);
    colorRow->addWidget(new QLabel("G")); colorRow->addWidget(m_colorG);
    colorRow->addWidget(new QLabel("B")); colorRow->addWidget(m_colorB);
    cForm->addRow("Color:", colorRow);
    form->addWidget(colorGroup);

    auto* pbrGroup = new QGroupBox("Surface");
    auto* pForm = new QFormLayout(pbrGroup);
    auto* roughRow = new QHBoxLayout();
    m_roughnessSlider = new QSlider(Qt::Horizontal);
    m_roughnessSlider->setRange(0, 100);
    m_roughnessLabel = new QLabel("0.60");
    m_roughnessLabel->setMinimumWidth(40);
    roughRow->addWidget(m_roughnessSlider);
    roughRow->addWidget(m_roughnessLabel);
    pForm->addRow("Roughness:", roughRow);

    auto* metalRow = new QHBoxLayout();
    m_metalnessSlider = new QSlider(Qt::Horizontal);
    m_metalnessSlider->setRange(0, 100);
    m_metalnessLabel = new QLabel("0.00");
    m_metalnessLabel->setMinimumWidth(40);
    metalRow->addWidget(m_metalnessSlider);
    metalRow->addWidget(m_metalnessLabel);
    pForm->addRow("Metallic:", metalRow);
    form->addWidget(pbrGroup);

    auto* assetGroup = new QGroupBox("Mesh Asset");
    auto* aForm = new QVBoxLayout(assetGroup);
    auto* assetRow = new QHBoxLayout();
    m_assetPathEdit = new QLineEdit();
    m_assetPathEdit->setReadOnly(true);
    m_assetPathEdit->setPlaceholderText("No mesh asset (procedural)");
    m_browseAssetBtn = new QPushButton("Browse");
    assetRow->addWidget(m_assetPathEdit);
    assetRow->addWidget(m_browseAssetBtn);
    aForm->addLayout(assetRow);
    form->addWidget(assetGroup);

    form->addStretch();
    scroll->setWidget(content);

    auto* outer = new QVBoxLayout(m_materialTab);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(m_colorR, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onColorChanged);
    connect(m_colorG, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onColorChanged);
    connect(m_colorB, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &PropertiesEditor::onColorChanged);
}

void PropertiesEditor::buildWorldTab()
{
    m_worldTab = new QWidget();
    auto* scroll = new QScrollArea(m_worldTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* form = new QVBoxLayout(content);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(6);

    // Lighting
    auto* lightGroup = new QGroupBox("Lighting");
    auto* lForm = new QFormLayout(lightGroup);
    auto* yawRow = new QHBoxLayout();
    m_sunYawSlider = new QSlider(Qt::Horizontal);
    m_sunYawSlider->setRange(0, 360);
    m_sunYawLabel = new QLabel("45");
    m_sunYawLabel->setMinimumWidth(40);
    yawRow->addWidget(m_sunYawSlider);
    yawRow->addWidget(m_sunYawLabel);
    lForm->addRow("Sun Yaw:", yawRow);

    auto* pitchRow = new QHBoxLayout();
    m_sunPitchSlider = new QSlider(Qt::Horizontal);
    m_sunPitchSlider->setRange(0, 90);
    m_sunPitchLabel = new QLabel("60");
    m_sunPitchLabel->setMinimumWidth(40);
    pitchRow->addWidget(m_sunPitchSlider);
    pitchRow->addWidget(m_sunPitchLabel);
    lForm->addRow("Sun Pitch:", pitchRow);

    auto* sunIntRow = new QHBoxLayout();
    m_sunIntensitySlider = new QSlider(Qt::Horizontal);
    m_sunIntensitySlider->setRange(0, 100);
    m_sunIntensityLabel = new QLabel("3.0");
    m_sunIntensityLabel->setMinimumWidth(40);
    sunIntRow->addWidget(m_sunIntensitySlider);
    sunIntRow->addWidget(m_sunIntensityLabel);
    lForm->addRow("Sun Power:", sunIntRow);

    auto* skyIntRow = new QHBoxLayout();
    m_skyIntensitySlider = new QSlider(Qt::Horizontal);
    m_skyIntensitySlider->setRange(0, 100);
    m_skyIntensityLabel = new QLabel("1.0");
    m_skyIntensityLabel->setMinimumWidth(40);
    skyIntRow->addWidget(m_skyIntensitySlider);
    skyIntRow->addWidget(m_skyIntensityLabel);
    lForm->addRow("Sky Power:", skyIntRow);
    form->addWidget(lightGroup);

    // Terrain
    auto* terrainGroup = new QGroupBox("Terrain");
    auto* tForm = new QVBoxLayout(terrainGroup);
    m_loadTerrainBtn = new QPushButton("Load Terrain from Project");
    tForm->addWidget(m_loadTerrainBtn);
    m_clearTerrainBtn = new QPushButton("Clear Terrain");
    tForm->addWidget(m_clearTerrainBtn);
    auto* hsRow = new QHBoxLayout();
    m_heightScaleSlider = new QSlider(Qt::Horizontal);
    m_heightScaleSlider->setRange(1, 1000);
    m_heightScaleSlider->setValue(100);
    m_heightScaleLabel = new QLabel("100m");
    m_heightScaleLabel->setMinimumWidth(50);
    hsRow->addWidget(new QLabel("Height Scale:"));
    hsRow->addWidget(m_heightScaleSlider);
    hsRow->addWidget(m_heightScaleLabel);
    tForm->addLayout(hsRow);
    form->addWidget(terrainGroup);

    // Roads
    auto* roadGroup = new QGroupBox("Roads");
    auto* rForm = new QVBoxLayout(roadGroup);
    m_loadRoadsBtn = new QPushButton("Load Roads from Project");
    rForm->addWidget(m_loadRoadsBtn);
    form->addWidget(roadGroup);

    // Generation
    auto* genGroup = new QGroupBox("World Generation");
    auto* gForm = new QVBoxLayout(genGroup);
    m_genBuildingsBtn = new QPushButton("Generate Buildings (100)");
    gForm->addWidget(m_genBuildingsBtn);
    m_genVegetationBtn = new QPushButton("Generate Vegetation (PCG)");
    gForm->addWidget(m_genVegetationBtn);
    form->addWidget(genGroup);

    form->addStretch();
    scroll->setWidget(content);

    auto* outer = new QVBoxLayout(m_worldTab);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(m_sunYawSlider, &QSlider::valueChanged, this, &PropertiesEditor::onSunChanged);
    connect(m_sunPitchSlider, &QSlider::valueChanged, this, &PropertiesEditor::onSunChanged);
    connect(m_sunIntensitySlider, &QSlider::valueChanged, this, &PropertiesEditor::onSunChanged);
    connect(m_skyIntensitySlider, &QSlider::valueChanged, this, &PropertiesEditor::onSkyChanged);
    connect(m_heightScaleSlider, &QSlider::valueChanged, this, [this](int v) {
        m_heightScaleLabel->setText(QString("%1m").arg(v));
    });
}

void PropertiesEditor::buildSceneTab()
{
    m_sceneTab = new QWidget();
    auto* scroll = new QScrollArea(m_sceneTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* form = new QVBoxLayout(content);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(6);

    auto* sceneGroup = new QGroupBox("Scene Persistence");
    auto* sForm = new QVBoxLayout(sceneGroup);
    m_saveSceneBtn = new QPushButton("Save Scene");
    sForm->addWidget(m_saveSceneBtn);
    m_loadSceneBtn = new QPushButton("Load Scene");
    sForm->addWidget(m_loadSceneBtn);
    form->addWidget(sceneGroup);

    auto* worldGroup = new QGroupBox("World Persistence");
    auto* wForm = new QVBoxLayout(worldGroup);
    m_saveWorldBtn = new QPushButton("Save World");
    wForm->addWidget(m_saveWorldBtn);
    m_loadWorldBtn = new QPushButton("Load World");
    wForm->addWidget(m_loadWorldBtn);
    form->addWidget(worldGroup);

    auto* objGroup = new QGroupBox("Scene Objects");
    auto* oForm = new QVBoxLayout(objGroup);
    m_clearObjectsBtn = new QPushButton("Clear All Objects");
    oForm->addWidget(m_clearObjectsBtn);
    form->addWidget(objGroup);

    form->addStretch();
    scroll->setWidget(content);

    auto* outer = new QVBoxLayout(m_sceneTab);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

void PropertiesEditor::buildRenderTab()
{
    m_renderTab = new QWidget();
    auto* scroll = new QScrollArea(m_renderTab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* form = new QVBoxLayout(content);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(6);

    auto* displayGroup = new QGroupBox("Display");
    auto* dForm = new QVBoxLayout(displayGroup);
    m_gridCheck = new QCheckBox("Show Grid");
    dForm->addWidget(m_gridCheck);
    m_snapCheck = new QCheckBox("Snap to Grid");
    dForm->addWidget(m_snapCheck);
    auto* snapRow = new QHBoxLayout();
    snapRow->addWidget(new QLabel("Snap Size:"));
    m_snapSizeCombo = new QComboBox();
    m_snapSizeCombo->addItems({"0.1 m", "0.5 m", "1 m", "2 m", "5 m", "10 m"});
    m_snapSizeCombo->setCurrentIndex(2);
    snapRow->addWidget(m_snapSizeCombo);
    dForm->addLayout(snapRow);
    form->addWidget(displayGroup);

    auto* camGroup = new QGroupBox("Camera");
    auto* cForm = new QVBoxLayout(camGroup);
    m_resetCameraBtn = new QPushButton("Reset Camera");
    cForm->addWidget(m_resetCameraBtn);
    form->addWidget(camGroup);

    form->addStretch();
    scroll->setWidget(content);

    auto* outer = new QVBoxLayout(m_renderTab);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(m_gridCheck, &QCheckBox::toggled, this, [this](bool v) {
        m_ogre->setGridVisible(v);
    });
    connect(m_snapCheck, &QCheckBox::toggled, this, [this](bool v) {
        m_ogre->setSnapEnabled(v);
    });
    connect(m_snapSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        static const float sizes[] = {0.1f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
        if (idx >= 0 && idx < 6) m_ogre->setSnapSize(sizes[idx]);
    });
    connect(m_resetCameraBtn, &QPushButton::clicked, this, [this]() {
        m_ogre->resetCamera();
    });
}

void PropertiesEditor::setActor(const QString& actorId)
{
    m_currentActorId = actorId;
    if (m_context == Context::Object || m_context == Context::Material)
        refreshCurrent();
}

void PropertiesEditor::clear()
{
    m_currentActorId.clear();
    m_nameEdit->clear();
    m_visibleCheck->setChecked(false);
    m_posX->setValue(0); m_posY->setValue(0); m_posZ->setValue(0);
    m_rotX->setValue(0); m_rotY->setValue(0); m_rotZ->setValue(0);
    m_scaleX->setValue(1); m_scaleY->setValue(1); m_scaleZ->setValue(1);
}

void PropertiesEditor::setContext(Context ctx)
{
    m_context = ctx;
    m_tabs->setCurrentIndex(static_cast<int>(ctx));
}

void PropertiesEditor::refreshCurrent()
{
    switch (m_context) {
    case Context::Object:   refreshObjectTab(); break;
    case Context::Material: refreshMaterialTab(); break;
    case Context::World:    refreshWorldTab(); break;
    case Context::Scene:    refreshSceneTab(); break;
    case Context::Render:   refreshRenderTab(); break;
    }
}

void PropertiesEditor::refreshObjectTab()
{
    if (m_currentActorId.isEmpty()) { clear(); return; }
    world::Actor* a = m_ogre->world()->findActor(m_currentActorId);
    if (!a) { clear(); return; }

    m_updating = true;
    m_nameEdit->setText(a->name);
    m_visibleCheck->setChecked(a->visible);

    // Layer combo
    m_layerCombo->clear();
    for (const auto& l : m_ogre->getLayers())
        m_layerCombo->addItem(l.name, l.id);
    int idx = m_layerCombo->findData(a->layerId);
    if (idx >= 0) m_layerCombo->setCurrentIndex(idx);

    m_posX->setValue(a->transform.posX);
    m_posY->setValue(a->transform.posY);
    m_posZ->setValue(a->transform.posZ);
    m_rotX->setValue(a->transform.rotX);
    m_rotY->setValue(a->transform.rotY);
    m_rotZ->setValue(a->transform.rotZ);
    m_scaleX->setValue(a->transform.scaleX);
    m_scaleY->setValue(a->transform.scaleY);
    m_scaleZ->setValue(a->transform.scaleZ);
    m_updating = false;
}

void PropertiesEditor::refreshMaterialTab()
{
    if (m_currentActorId.isEmpty()) return;
    world::Actor* a = m_ogre->world()->findActor(m_currentActorId);
    if (!a) return;

    m_updating = true;
    m_colorR->setValue(a->colorR);
    m_colorG->setValue(a->colorG);
    m_colorB->setValue(a->colorB);
    m_assetPathEdit->setText(a->assetPath);
    m_updating = false;
}

void PropertiesEditor::refreshWorldTab()
{
    m_updating = true;
    m_sunYawSlider->setValue(static_cast<int>(m_ogre->getSunYaw()));
    m_sunPitchSlider->setValue(static_cast<int>(m_ogre->getSunPitch()));
    m_sunIntensitySlider->setValue(static_cast<int>(m_ogre->getSunIntensity() * 10));
    m_skyIntensitySlider->setValue(static_cast<int>(m_ogre->getSkyIntensity() * 10));
    m_sunYawLabel->setText(QString::number(m_ogre->getSunYaw()));
    m_sunPitchLabel->setText(QString::number(m_ogre->getSunPitch()));
    m_sunIntensityLabel->setText(QString::number(m_ogre->getSunIntensity(), 'f', 1));
    m_skyIntensityLabel->setText(QString::number(m_ogre->getSkyIntensity(), 'f', 1));
    m_updating = false;
}

void PropertiesEditor::refreshSceneTab()
{
    // The scene tab contains only action buttons (Save/Load/Clear),
    // so there is no widget state to refresh.
}

void PropertiesEditor::refreshRenderTab()
{
    m_updating = true;
    m_gridCheck->setChecked(m_ogre->isGridVisible());
    m_snapCheck->setChecked(m_ogre->isSnapEnabled());
    m_updating = false;
}

void PropertiesEditor::onTransformChanged()
{
    if (m_updating || m_currentActorId.isEmpty()) return;
    m_ogre->updateActorTransform(m_currentActorId,
        float(m_posX->value()), float(m_posY->value()), float(m_posZ->value()),
        float(m_rotY->value()),
        float(m_scaleX->value()), float(m_scaleY->value()), float(m_scaleZ->value()));
    emit actorModified(m_currentActorId);
}

void PropertiesEditor::onNameChanged()
{
    if (m_updating || m_currentActorId.isEmpty()) return;
    m_ogre->renameActor(m_currentActorId, m_nameEdit->text());
    emit actorModified(m_currentActorId);
}

void PropertiesEditor::onVisibilityToggled(bool visible)
{
    if (m_updating || m_currentActorId.isEmpty()) return;
    m_ogre->updateActorVisibility(m_currentActorId, visible);
    emit actorModified(m_currentActorId);
}

void PropertiesEditor::onLayerChanged(int index)
{
    if (m_updating || m_currentActorId.isEmpty() || index < 0) return;
    QString layerId = m_layerCombo->itemData(index).toString();
    m_ogre->updateActorLayer(m_currentActorId, layerId);
    emit actorModified(m_currentActorId);
}

void PropertiesEditor::onColorChanged()
{
    if (m_updating || m_currentActorId.isEmpty()) return;
    world::Actor* a = m_ogre->world()->findActor(m_currentActorId);
    if (!a) return;
    a->colorR = float(m_colorR->value());
    a->colorG = float(m_colorG->value());
    a->colorB = float(m_colorB->value());
    a->touch();
    emit actorModified(m_currentActorId);
}

void PropertiesEditor::onSunChanged()
{
    if (m_updating) return;
    float yaw = float(m_sunYawSlider->value());
    float pitch = float(m_sunPitchSlider->value());
    float intensity = float(m_sunIntensitySlider->value()) * 0.1f;
    m_sunYawLabel->setText(QString::number(yaw));
    m_sunPitchLabel->setText(QString::number(pitch));
    m_sunIntensityLabel->setText(QString::number(intensity, 'f', 1));
    m_ogre->setSunDirection(yaw, pitch);
    m_ogre->setSunIntensity(intensity);
}

void PropertiesEditor::onSkyChanged()
{
    if (m_updating) return;
    float intensity = float(m_skyIntensitySlider->value()) * 0.1f;
    m_skyIntensityLabel->setText(QString::number(intensity, 'f', 1));
    m_ogre->setSkyIntensity(intensity);
}

void PropertiesEditor::onTerrainChanged()
{
    // Terrain changes are handled by OgreWidget directly.
    // This slot exists for future terrain property bindings.
    refreshCurrent();
}

void PropertiesEditor::onRenderChanged()
{
    // Render settings changed — refresh the render tab to reflect
    // the current state of the OgreWidget.
    refreshRenderTab();
}
