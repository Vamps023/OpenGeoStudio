#include "Studio3DWidget.hpp"
#include "OgreWidget.hpp"
#include "EditorPanels.hpp"
#include "PropertiesEditor.hpp"
#include "NPanel.hpp"

#include "core/ApplicationContext.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QScrollArea>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QButtonGroup>
#include <QTabWidget>
#include <QTextEdit>
#include <QSlider>
#include <QProgressBar>
#include <QMessageBox>
#include <QFileDialog>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QAction>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QTime>
#include <QPair>
#include <QCoreApplication>
#include <QKeyEvent>
#include <cmath>
#include <cstdlib>

Studio3DWidget::Studio3DWidget(ApplicationContext* ctx, QWidget* parent)
    : QMainWindow(parent), m_ctx(ctx)
{
    setWindowFlags(Qt::Widget); // embed inside parent, not as a separate window
    setupUI();
}

Studio3DWidget::~Studio3DWidget() = default;

void Studio3DWidget::applyBlenderStyle()
{
    setStyleSheet(QStringLiteral(
        // Blender-like dark theme
        "QMainWindow { background: #1a1a1a; }"
        "QMenuBar { background: #303030; color: #e0e0e0; padding: 2px; }"
        "QMenuBar::item { background: transparent; padding: 4px 10px; }"
        "QMenuBar::item:selected { background: #3d5742; }"
        "QMenu { background: #303030; color: #e0e0e0; border: 1px solid #1a1a1a; }"
        "QMenu::item { padding: 4px 20px; }"
        "QMenu::item:selected { background: #3d5742; }"
        "QToolBar { background: #303030; border: none; spacing: 2px; padding: 3px; }"
        "QToolBar::separator { width: 1px; background: #1a1a1a; margin: 4px 2px; }"
        "QToolButton { background: transparent; color: #e0e0e0; padding: 6px 10px;"
        "  border-radius: 3px; font-size: 11px; }"
        "QToolButton:hover { background: #3d5742; }"
        "QToolButton:pressed { background: #4a6d50; }"
        "QToolButton:checked { background: #4a6d50; color: #ffffff; }"
        "QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }"
        "QDockWidget::title { background: #303030; padding: 4px 8px;"
        "  border-bottom: 1px solid #1a1a1a; }"
        "QDockWidget::title-text { color: #a0a0a0; font-size: 10px;"
        "  font-weight: bold; text-transform: uppercase; letter-spacing: 1px; }"
        "QLabel#panelHeader { background: #303030; color: #a0a0a0; font-size: 10px;"
        "  font-weight: bold; text-transform: uppercase; letter-spacing: 1px;"
        "  padding: 4px 8px; border-bottom: 1px solid #1a1a1a; }"
        "QLabel#npanelTitle { color: #e0e0e0; font-size: 12px; font-weight: bold;"
        "  padding: 4px; }"
        "QWidget#npanel { background: #262626; border-left: 1px solid #1a1a1a; }"
        "QTabWidget::pane { border: 1px solid #1a1a1a; background: #262626; }"
        "QTabBar::tab { background: #303030; color: #a0a0a0; padding: 6px 12px;"
        "  border: 1px solid #1a1a1a; }"
        "QTabBar::tab:selected { background: #3d5742; color: #ffffff; }"
        "QTabBar::tab:hover { background: #3a3a3a; }"
        "QGroupBox { border: 1px solid #3a3a3a; border-radius: 3px;"
        "  margin-top: 8px; padding-top: 8px; color: #c0c0c0; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 6px; padding: 0 4px; }"
        "QLineEdit, QDoubleSpinBox, QComboBox, QSpinBox {"
        "  background: #1a1a1a; color: #e0e0e0; border: 1px solid #3a3a3a;"
        "  border-radius: 2px; padding: 2px 4px; font-size: 11px; }"
        "QLineEdit:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid #4a6d50; }"
        "QCheckBox { color: #e0e0e0; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator:unchecked { background: #1a1a1a; border: 1px solid #3a3a3a; }"
        "QCheckBox::indicator:checked { background: #4a6d50; border: 1px solid #4a6d50; }"
        "QPushButton { background: #3d5742; color: #ffffff; border: none;"
        "  padding: 6px 12px; border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background: #4a6d50; }"
        "QPushButton:pressed { background: #2d4032; }"
        "QSlider::groove:horizontal { background: #1a1a1a; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #4a6d50; width: 12px;"
        "  margin: -4px 0; border-radius: 6px; }"
        "QSlider::handle:horizontal:hover { background: #5a7d60; }"
        "QStatusBar { background: #303030; color: #a0a0a0; }"
        "QStatusBar::item { border: none; }"
        "QSplitter::handle { background: #1a1a1a; }"
        "QSplitter::handle:horizontal { width: 2px; }"
        "QSplitter::handle:vertical { height: 2px; }"
        "QScrollArea { border: none; }"
        "QTextEdit { background-color: #1a1a1a; color: #c0c0c0;"
        "  font-family: Consolas; font-size: 11px; border: none; }"
        "QTreeWidget { background: #262626; color: #e0e0e0; border: none;"
        "  font-size: 11px; }"
        "QTreeWidget::item { padding: 2px; }"
        "QTreeWidget::item:selected { background: #3d5742; }"
        "QHeaderView::section { background: #303030; color: #a0a0a0;"
        "  border: none; padding: 3px; font-size: 10px; }"
        "QListWidget { background: #262626; color: #e0e0e0; border: none; }"
        "QListWidget::item { padding: 4px; }"
        "QListWidget::item:selected { background: #3d5742; }"
    ));
}

void Studio3DWidget::setupUI()
{
    applyBlenderStyle();

    // ─── Viewport (created first; panels attach to it) ───
    m_ogreWidget = new OgreWidget(this);

    // Central widget = viewport with optional N-panel overlay
    auto* centralWidget = new QWidget(this);
    auto* centralLayout = new QHBoxLayout(centralWidget);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_ogreWidget->containerWidget(), 1);

    // N-panel overlay (hidden by default, toggled with N)
    m_nPanel = new NPanel(m_ogreWidget, centralWidget);
    m_nPanel->setVisible(false);
    centralLayout->addWidget(m_nPanel);

    setCentralWidget(centralWidget);

    // Build panels
    setupMenuBar();
    setupToolBar();
    setupDockPanels();
    setupStatusBar();

    // ─── Signal wiring ───
    connect(m_outliner, &WorldOutliner::actorSelected, this, &Studio3DWidget::onActorSelected);
    connect(m_ogreWidget, &OgreWidget::actorSelected, this, &Studio3DWidget::onActorSelected);
    connect(m_ogreWidget, &OgreWidget::actorAdded, [this](const QString&) { m_outliner->refresh(); refreshStats(); });
    connect(m_ogreWidget, &OgreWidget::actorRemoved, [this](const QString&) { m_outliner->refresh(); refreshStats(); });
    connect(m_ogreWidget, &OgreWidget::sceneChanged, [this]() { m_outliner->refresh(); m_layerPanel->refresh(); refreshStats(); });
    connect(m_ogreWidget, &OgreWidget::transformModeChanged, this, &Studio3DWidget::onTransformModeChanged);
    connect(m_ogreWidget, &OgreWidget::actorTransformed, [this](const QString& id) {
        m_propertiesEditor->setActor(id);
        m_nPanel->setActor(id);
    });
    connect(m_propertiesEditor, &PropertiesEditor::actorModified, [this](const QString& id) {
        m_outliner->refresh();
        m_nPanel->setActor(id);
    });
    connect(m_nPanel, &NPanel::actorModified, [this](const QString& id) {
        m_outliner->refresh();
        m_propertiesEditor->setActor(id);
    });

    // Wire PropertiesEditor World tab buttons
    connect(m_propertiesEditor->m_loadTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadTerrain);
    connect(m_propertiesEditor->m_clearTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onClearTerrain);
    connect(m_propertiesEditor->m_loadRoadsBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadRoads);
    connect(m_propertiesEditor->m_genBuildingsBtn, &QPushButton::clicked, this, &Studio3DWidget::onGenerateBuildings);
    connect(m_propertiesEditor->m_genVegetationBtn, &QPushButton::clicked, this, &Studio3DWidget::onGenerateVegetation);
    connect(m_propertiesEditor->m_heightScaleSlider, &QSlider::valueChanged, this, &Studio3DWidget::onHeightScaleChanged);

    // Wire PropertiesEditor Scene tab buttons
    connect(m_propertiesEditor->m_saveSceneBtn, &QPushButton::clicked, this, &Studio3DWidget::onSaveScene);
    connect(m_propertiesEditor->m_loadSceneBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadScene);
    connect(m_propertiesEditor->m_saveWorldBtn, &QPushButton::clicked, this, &Studio3DWidget::onSaveWorld);
    connect(m_propertiesEditor->m_loadWorldBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadWorld);
    connect(m_propertiesEditor->m_clearObjectsBtn, &QPushButton::clicked, this, &Studio3DWidget::onClearObjects);

    setStatus("Ready");
    refreshStats();
    appendLog("3D Studio ready (Blender-style editor layout)");
}

void Studio3DWidget::setupMenuBar()
{
    auto* mb = menuBar();
    mb->setStyleSheet("QMenuBar { background: #303030; color: #e0e0e0; }");

    // File
    auto* fileMenu = mb->addMenu("File");
    fileMenu->addAction("Save Scene", this, &Studio3DWidget::onSaveScene, QKeySequence("Ctrl+S"));
    fileMenu->addAction("Load Scene", this, &Studio3DWidget::onLoadScene, QKeySequence("Ctrl+O"));
    fileMenu->addSeparator();
    fileMenu->addAction("Save World", this, &Studio3DWidget::onSaveWorld);
    fileMenu->addAction("Load World", this, &Studio3DWidget::onLoadWorld);
    fileMenu->addSeparator();
    fileMenu->addAction("Export Terrain", this, &Studio3DWidget::onExportTerrain);
    fileMenu->addAction("Export Roads", this, &Studio3DWidget::onExportRoads);

    // Edit
    auto* editMenu = mb->addMenu("Edit");
    editMenu->addAction("Clear All Objects", this, &Studio3DWidget::onClearObjects);
    editMenu->addSeparator();
    auto* snapAct = editMenu->addAction("Toggle Snap", this, &Studio3DWidget::onSnapToggled);
    snapAct->setCheckable(true);
    auto* gridAct = editMenu->addAction("Toggle Grid", this, &Studio3DWidget::onGridToggled);
    gridAct->setCheckable(true);

    // View
    auto* viewMenu = mb->addMenu("View");
    viewMenu->addAction("Toggle Toolbar (T)", this, &Studio3DWidget::onToggleToolbar, QKeySequence("T"));
    viewMenu->addAction("Toggle N-Panel (N)", this, &Studio3DWidget::onToggleNPanel, QKeySequence("N"));
    viewMenu->addSeparator();
    viewMenu->addAction("Toggle Outliner", this, &Studio3DWidget::onToggleOutliner);
    viewMenu->addAction("Toggle Properties", this, &Studio3DWidget::onToggleProperties);
    viewMenu->addAction("Toggle Bottom Panel", this, &Studio3DWidget::onToggleBottom);
    viewMenu->addSeparator();
    viewMenu->addAction("Reset Camera", this, [this]() { m_ogreWidget->resetCamera(); });

    // Add
    auto* addMenu = mb->addMenu("Add");
    addMenu->addAction("Building", this, [this]() { placePreset(world::ActorType::Building); });
    addMenu->addAction("Tree", this, [this]() { placePreset(world::ActorType::Tree); });
    addMenu->addAction("Vegetation", this, [this]() { placePreset(world::ActorType::Vegetation); });
    addMenu->addAction("Grass", this, [this]() { placePreset(world::ActorType::Grass); });
    addMenu->addAction("Rock", this, [this]() { placePreset(world::ActorType::Rock); });
    addMenu->addAction("Prop/Cube", this, [this]() { placePreset(world::ActorType::Prop); });
    addMenu->addAction("Empty", this, [this]() { placePreset(world::ActorType::Empty); });
    addMenu->addSeparator();
    addMenu->addAction("Sun Light", this, &Studio3DWidget::onAddSunLight);
    addMenu->addAction("Sky Light", this, &Studio3DWidget::onAddSkyLight);
    addMenu->addAction("Lake", this, &Studio3DWidget::onAddLake);
    addMenu->addSeparator();
    addMenu->addAction("Load Terrain", this, &Studio3DWidget::onLoadTerrain);
    addMenu->addAction("Load Roads", this, &Studio3DWidget::onLoadRoads);
    addMenu->addSeparator();
    addMenu->addAction("Generate Buildings (100)", this, &Studio3DWidget::onGenerateBuildings);
    addMenu->addAction("Generate Vegetation (PCG)", this, &Studio3DWidget::onGenerateVegetation);

    // Help
    auto* helpMenu = mb->addMenu("Help");
    helpMenu->addAction("Controls...", this, [this]() {
        QMessageBox::information(this, "3D Studio Controls",
            "LMB: Select / Gizmo drag\n"
            "RMB: Orbit camera\n"
            "MMB: Pan camera\n"
            "Wheel: Zoom\n\n"
            "Q: Select tool\n"
            "W: Move tool\n"
            "E: Rotate tool\n"
            "R: Scale tool\n"
            "F: Frame selected\n"
            "Del: Delete selected\n\n"
            "T: Toggle toolbar\n"
            "N: Toggle N-panel\n"
            "Ctrl+S: Save scene\n"
            "Ctrl+O: Load scene");
    });
}

void Studio3DWidget::setupToolBar()
{
    m_toolBar = new QToolBar(this);
    m_toolBar->setOrientation(Qt::Vertical);
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(24, 24));
    addToolBar(Qt::LeftToolBarArea, m_toolBar);

    auto makeBtn = [this](const QString& text, const QString& tip, bool checkable) {
        auto* b = new QToolButton(m_toolBar);
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setMinimumWidth(32);
        b->setMinimumHeight(32);
        return b;
    };

    // Transform tools
    m_selectToolBtn = makeBtn("Q", "Select tool (Q)", true);
    m_moveToolBtn = makeBtn("W", "Move tool (W)", true);
    m_rotateToolBtn = makeBtn("E", "Rotate tool (E)", true);
    m_scaleToolBtn = makeBtn("R", "Scale tool (R)", true);
    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);
    m_toolGroup->addButton(m_selectToolBtn);
    m_toolGroup->addButton(m_moveToolBtn);
    m_toolGroup->addButton(m_rotateToolBtn);
    m_toolGroup->addButton(m_scaleToolBtn);
    m_selectToolBtn->setChecked(true);
    connect(m_selectToolBtn, &QToolButton::clicked, this, [this]() {
        m_ogreWidget->setTransformMode(TransformMode::None); });
    connect(m_moveToolBtn, &QToolButton::clicked, this, [this]() {
        m_ogreWidget->setTransformMode(TransformMode::Move); });
    connect(m_rotateToolBtn, &QToolButton::clicked, this, [this]() {
        m_ogreWidget->setTransformMode(TransformMode::Rotate); });
    connect(m_scaleToolBtn, &QToolButton::clicked, this, [this]() {
        m_ogreWidget->setTransformMode(TransformMode::Scale); });
    m_toolBar->addWidget(m_selectToolBtn);
    m_toolBar->addWidget(m_moveToolBtn);
    m_toolBar->addWidget(m_rotateToolBtn);
    m_toolBar->addWidget(m_scaleToolBtn);

    m_toolBar->addSeparator();

    // Snap + Grid
    m_snapBtn = makeBtn("S", "Toggle snapping", true);
    m_snapBtn->setChecked(m_ogreWidget->isSnapEnabled());
    connect(m_snapBtn, &QToolButton::toggled, this, &Studio3DWidget::onSnapToggled);
    m_toolBar->addWidget(m_snapBtn);

    m_gridBtn = makeBtn("#", "Toggle grid", true);
    m_gridBtn->setChecked(m_ogreWidget->isGridVisible());
    connect(m_gridBtn, &QToolButton::toggled, this, &Studio3DWidget::onGridToggled);
    m_toolBar->addWidget(m_gridBtn);

    m_toolBar->addSeparator();

    // Camera reset
    auto* camBtn = makeBtn("C", "Reset camera", false);
    connect(camBtn, &QToolButton::clicked, this, [this]() {
        m_ogreWidget->resetCamera();
        appendLog("Camera reset.");
    });
    m_toolBar->addWidget(camBtn);
}

void Studio3DWidget::setupDockPanels()
{
    // ─── Left dock: Outliner + Layers ───
    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    auto* outlinerHeader = new QLabel("Outliner", leftPanel);
    outlinerHeader->setObjectName("panelHeader");
    leftLayout->addWidget(outlinerHeader);
    m_outliner = new WorldOutliner(m_ogreWidget, leftPanel);
    leftLayout->addWidget(m_outliner, 3);

    auto* layerHeader = new QLabel("Layers", leftPanel);
    layerHeader->setObjectName("panelHeader");
    leftLayout->addWidget(layerHeader);
    m_layerPanel = new LayerPanel(m_ogreWidget, leftPanel);
    leftLayout->addWidget(m_layerPanel, 1);

    m_outlinerDock = new QDockWidget("Outliner", this);
    m_outlinerDock->setWidget(leftPanel);
    m_outlinerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_outlinerDock->setMinimumWidth(220);
    addDockWidget(Qt::LeftDockWidgetArea, m_outlinerDock);

    // ─── Right dock: Properties editor ───
    m_propertiesEditor = new PropertiesEditor(m_ogreWidget, this);
    m_propertiesDock = new QDockWidget("Properties", this);
    m_propertiesDock->setWidget(m_propertiesEditor);
    m_propertiesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_propertiesDock->setMinimumWidth(280);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    // ─── Bottom dock: Content Browser + Output Log ───
    m_bottomTabs = new QTabWidget(this);
    m_bottomTabs->setMinimumHeight(180);

    m_contentBrowser = new ContentBrowser(m_ogreWidget, this);
    QString assetDir;
    if (m_ctx && m_ctx->projects().hasProject()) {
        assetDir = QDir(m_ctx->projects().current().basePath).filePath("Assets");
    }
    if (assetDir.isEmpty() || !QDir(assetDir).exists()) {
        assetDir = QCoreApplication::applicationDirPath();
    }
    m_contentBrowser->setAssetDirectory(assetDir);
    connect(m_contentBrowser, &ContentBrowser::assetRequested,
            this, &Studio3DWidget::onPlaceAsset);
    m_bottomTabs->addTab(m_contentBrowser, "Content Browser");

    auto* logWrap = new QWidget(this);
    auto* logLayout = new QVBoxLayout(logWrap);
    logLayout->setContentsMargins(0, 0, 0, 0);
    m_logEdit = new QTextEdit(logWrap);
    m_logEdit->setReadOnly(true);
    logLayout->addWidget(m_logEdit);
    m_bottomTabs->addTab(logWrap, "Output Log");

    m_bottomDock = new QDockWidget("Content / Log", this);
    m_bottomDock->setWidget(m_bottomTabs);
    m_bottomDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);
    addDockWidget(Qt::BottomDockWidgetArea, m_bottomDock);

    // Keep the height slider reference for the World tab
    m_heightScaleSlider = m_propertiesEditor->m_heightScaleSlider;
    m_heightScaleLabel = m_propertiesEditor->m_heightScaleLabel;
}

void Studio3DWidget::setupStatusBar()
{
    auto* sb = statusBar();
    sb->setStyleSheet("QStatusBar { background: #303030; color: #a0a0a0; }");

    m_statusLabel = new QLabel("Ready");
    sb->addWidget(m_statusLabel);

    auto* hintLabel = new QLabel(
        "LMB Select · RMB Orbit · MMB Pan · Wheel Zoom · Q/W/E/R Tools · F Frame · T Toolbar · N Panel · Del Delete");
    hintLabel->setStyleSheet("QLabel { color: #707070; font-size: 10px; }");
    sb->addPermanentWidget(hintLabel);

    m_statsLabel = new QLabel("0 actors");
    sb->addPermanentWidget(m_statsLabel);
}

void Studio3DWidget::keyPressEvent(QKeyEvent* event)
{
    // T and N are handled by the menu actions' shortcuts, but also
    // forward to the OgreWidget for viewport-specific keys.
    QMainWindow::keyPressEvent(event);
}

void Studio3DWidget::appendLog(const QString& msg)
{
    m_logEdit->append("[" + QTime::currentTime().toString("HH:mm:ss") + "] " + msg);
}

void Studio3DWidget::setStatus(const QString& text)
{
    m_statusLabel->setText(text);
}

void Studio3DWidget::refreshStats()
{
    m_statsLabel->setText(QString("%1 actors").arg(m_ogreWidget->actorCount()));
}

QString Studio3DWidget::placeActorAtFocus(world::ActorType type, float sx, float sy, float sz,
                                          const QString& layerId, const QString& name)
{
    const OgreWidget::WorldPos target = m_ogreWidget->cameraTarget();
    const float x = target.x + static_cast<float>(rand() % 40 - 20) * 0.5f;
    const float z = target.z + static_cast<float>(rand() % 40 - 20) * 0.5f;
    const float groundY = m_ogreWidget->hasTerrain()
        ? m_ogreWidget->sampleTerrainHeight(x, z) : 0.0f;

    const QString id = m_ogreWidget->addActor(type, x, groundY + sy * 0.5f, z, 0,
                                               sx, sy, sz, layerId);
    if (!name.isEmpty())
        m_ogreWidget->renameActor(id, name);
    m_outliner->refresh();
    refreshStats();
    setStatus(QString("Placed %1").arg(name.isEmpty() ? "actor" : name));
    return id;
}

void Studio3DWidget::placePreset(world::ActorType type)
{
    switch (type) {
    case world::ActorType::Building:
        placeActorAtFocus(type, 10, 15, 10, "buildings", "Building"); break;
    case world::ActorType::Tree:
        placeActorAtFocus(type, 3, 8, 3, "vegetation", "Tree"); break;
    case world::ActorType::Vegetation:
        placeActorAtFocus(type, 2, 2, 2, "vegetation", "Vegetation"); break;
    case world::ActorType::Grass:
        placeActorAtFocus(type, 1, 0.5f, 1, "vegetation", "Grass"); break;
    case world::ActorType::Rock:
        placeActorAtFocus(type, 2, 1.5f, 2, "default", "Rock"); break;
    case world::ActorType::Empty:
        placeActorAtFocus(type, 1, 1, 1, "default", "Empty"); break;
    default:
        placeActorAtFocus(world::ActorType::Prop, 4, 4, 4, "default", "Prop"); break;
    }
}

void Studio3DWidget::onPlaceAsset(const QString& pathOrType, const QString& type)
{
    if (type == "actor") {
        const auto actorType = static_cast<world::ActorType>(pathOrType.toInt());
        switch (actorType) {
        case world::ActorType::SunLight:
            onAddSunLight();
            return;
        case world::ActorType::SkyLight:
            onAddSkyLight();
            return;
        case world::ActorType::Water:
            onAddLake();
            return;
        default:
            placePreset(actorType);
            return;
        }
    }

    const QFileInfo fi(pathOrType);
    if (!fi.exists()) return;
    const QString id = placeActorAtFocus(world::ActorType::Prop, 4, 4, 4, "default",
                                         fi.completeBaseName());
    if (world::Actor* a = m_ogreWidget->world()->findActor(id))
        a->assetPath = fi.absoluteFilePath();
    appendLog(QString("Placed %1 asset: %2").arg(type, fi.fileName()));
}

QString Studio3DWidget::findHeightmapInProject()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();
    const QString projectDir = QDir::cleanPath(m_ctx->projects().current().basePath);
    const QString terrainDir = QDir(projectDir).filePath("Terrain");

    const QStringList roots = {
        terrainDir,
        QDir(projectDir).filePath("Exports"),
        projectDir
    };
    // Prefer GeoTIFF over PNG — the DemDecoder path handles large merged
    // heightmaps without Qt's 256 MB QImage allocation limit.
    const QStringList preferred = {"heightmap_merged.tif", "heightmap_merged.tiff",
                                   "heightmap_merged.png"};
    for (const QString& root : roots) {
        if (!QDir(root).exists()) continue;
        for (const QString& name : preferred) {
            QDirIterator it(root, {name}, QDir::Files, QDirIterator::Subdirectories);
            if (it.hasNext()) return it.next();
        }
    }
    for (const QString& root : roots) {
        if (!QDir(root).exists()) continue;
        QDirIterator it(root, {"*.png", "*.tif", "*.tiff"},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString candidate = it.next();
            const QFileInfo info(candidate);
            if (info.dir().dirName().compare("albedo", Qt::CaseInsensitive) == 0) continue;
            if (info.fileName().startsWith("albedo", Qt::CaseInsensitive)) continue;
            return candidate;
        }
    }
    return QString();
}

QString Studio3DWidget::findAlbedoInProject()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();
    const QString projectDir = QDir::cleanPath(m_ctx->projects().current().basePath);
    const QString terrainDir = QDir(projectDir).filePath("Terrain");
    const QStringList roots = {
        terrainDir,
        QDir(projectDir).filePath("Exports"),
        projectDir
    };
    const QStringList preferred = {"albedo_merged.png", "albedo_merged.tif",
                                   "albedo_merged.tiff", "satellite.png",
                                   "imagery.png"};
    for (const QString& root : roots) {
        if (!QDir(root).exists()) continue;
        for (const QString& name : preferred) {
            QDirIterator it(root, {name}, QDir::Files, QDirIterator::Subdirectories);
            if (it.hasNext()) return it.next();
        }
    }
    for (const QStringList& patterns : {QStringList{"*.png", "*.jpg", "*.jpeg"},
                                        QStringList{"*.tif", "*.tiff"}}) {
        for (const QString& root : roots) {
            if (!QDir(root).exists()) continue;
            QDirIterator it(root, patterns, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString candidate = it.next();
                const QFileInfo info(candidate);
                if (info.dir().dirName().compare("heightmaps", Qt::CaseInsensitive) == 0) continue;
                if (info.fileName().startsWith("heightmap", Qt::CaseInsensitive)) continue;
                return candidate;
            }
        }
    }
    return QString();
}

void Studio3DWidget::onLoadTerrain()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    QString heightmapPath = findHeightmapInProject();
    if (heightmapPath.isEmpty()) {
        const QString projectPath = m_ctx->projects().current().basePath;
        QMessageBox::information(this, "Select Terrain Heightmap",
            QString("No heightmap was found automatically in:\n%1\n\n"
                    "Select an exported PNG or GeoTIFF manually.").arg(projectPath));
        heightmapPath = QFileDialog::getOpenFileName(
            this, "Select Terrain Heightmap", projectPath,
            "Terrain files (*.png *.tif *.tiff);;All files (*.*)");
        if (heightmapPath.isEmpty()) return;
    }

    QString albedoPath = findAlbedoInProject();
    if (albedoPath.isEmpty()) {
        albedoPath = QFileDialog::getOpenFileName(
            this, "Select Terrain Albedo (Optional)",
            QFileInfo(heightmapPath).absolutePath(),
            "Imagery files (*.png *.jpg *.jpeg *.tif *.tiff);;All files (*.*)");
    }

    float heightScale = static_cast<float>(m_heightScaleSlider->value());
    float terrainSize = 4000.0f;
    if (m_ctx && m_ctx->projects().hasProject() && m_ctx->projects().current().bounds.valid) {
        auto& b = m_ctx->projects().current().bounds;
        double latMid = (b.minLat + b.maxLat) / 2.0;
        double latM = (b.maxLat - b.minLat) * 111320.0;
        double lonM = (b.maxLon - b.minLon) * 111320.0 * cos(latMid * 3.14159265358979 / 180.0);
        terrainSize = static_cast<float>(std::max(latM, lonM));
        if (terrainSize < 10.0f) terrainSize = 4000.0f;
    }

    appendLog("Loading terrain...");
    appendLog("  Heightmap: " + heightmapPath);
    if (!albedoPath.isEmpty())
        appendLog("  Albedo: " + albedoPath);
    appendLog(QString("  Height scale: %1m").arg(heightScale));

    setStatus("Loading terrain...");
    m_ogreWidget->loadTerrain(heightmapPath, albedoPath, terrainSize, heightScale);

    if (!m_ogreWidget->hasTerrain()) {
        setStatus("Terrain load failed");
        appendLog("Terrain load failed. Check the selected heightmap and OGRE log.");
        QMessageBox::warning(this, "Terrain Load Failed",
            QString("The selected heightmap could not be loaded:\n%1").arg(heightmapPath));
        return;
    }
    setStatus("Terrain loaded");
    appendLog("Terrain loaded successfully.");
}

void Studio3DWidget::onClearTerrain()
{
    m_ogreWidget->clearTerrain();
    setStatus("Terrain cleared");
    appendLog("Terrain cleared.");
}

QString Studio3DWidget::findXodrInProject()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();

    QString roadDir = m_ctx->projects().current().basePath + "/Roads";

    if (QDir(roadDir).exists()) {
        QStringList filters;
        filters << "*.xodr";
        QStringList files = QDir(roadDir).entryList(filters, QDir::Files);
        if (!files.isEmpty()) return roadDir + "/" + files.first();
    }

    return QString();
}

void Studio3DWidget::onLoadRoads()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    QString xodrPath = findXodrInProject();
    if (xodrPath.isEmpty()) {
        QMessageBox::warning(this, "No Road Data",
            "No XODR file found in project.\n"
            "Create roads in Road Studio first.");
        return;
    }

    appendLog("Loading roads...");
    appendLog("  XODR: " + xodrPath);

    setStatus("Loading roads...");
    m_ogreWidget->loadRoads(xodrPath);

    setStatus("Roads loaded");
    appendLog("Roads loaded successfully.");
}

void Studio3DWidget::onAddBuilding()
{
    placeActorAtFocus(world::ActorType::Building, 10, 15, 10, "buildings", "Building");
}

void Studio3DWidget::onAddTree()
{
    placeActorAtFocus(world::ActorType::Tree, 3, 8, 3, "vegetation", "Tree");
}

void Studio3DWidget::onAddBox()
{
    placeActorAtFocus(world::ActorType::Prop, 5, 5, 5, "default", "Cube");
}

void Studio3DWidget::onClearObjects()
{
    m_ogreWidget->clearActors();
    appendLog("All objects cleared.");
    setStatus("Objects cleared");
}

void Studio3DWidget::onGenerateBuildings()
{
    m_ogreWidget->generateBuildings(100);
    appendLog("Generated 100 buildings via WorldBuilder.");
    setStatus("Buildings generated");
    m_outliner->refresh();
    refreshStats();
}

void Studio3DWidget::onGenerateVegetation()
{
    m_ogreWidget->generateVegetation(500, 0.01f);
    appendLog("Generated vegetation via PCG (scatter + height/slope filters).");
    setStatus("Vegetation generated");
    m_outliner->refresh();
    refreshStats();
}

void Studio3DWidget::onAddLake()
{
    float terrainSize = m_ogreWidget->world()->settings.terrainSize;
    if (terrainSize <= 0) terrainSize = 1000.0f;
    m_ogreWidget->addLake("Lake", 0, 0, terrainSize * 0.3f, terrainSize * 0.3f, 5.0f);
    appendLog("Added lake at world center.");
    setStatus("Lake added");
    m_outliner->refresh();
    refreshStats();
}

void Studio3DWidget::onAddSunLight()
{
    m_ogreWidget->addSunLight(45.0f, 60.0f, 3.0f);
    appendLog("Added sun light (yaw=45, pitch=60, intensity=3).");
    setStatus("Sun light added");
    refreshStats();
}

void Studio3DWidget::onAddSkyLight()
{
    m_ogreWidget->addSkyLight(1.0f);
    appendLog("Added sky light.");
    setStatus("Sky light added");
    refreshStats();
}

void Studio3DWidget::onTransformModeChanged(int mode)
{
    const auto m = static_cast<TransformMode>(mode);
    m_selectToolBtn->setChecked(m == TransformMode::None);
    m_moveToolBtn->setChecked(m == TransformMode::Move);
    m_rotateToolBtn->setChecked(m == TransformMode::Rotate);
    m_scaleToolBtn->setChecked(m == TransformMode::Scale);
}

void Studio3DWidget::onSnapToggled(bool enabled)
{
    m_ogreWidget->setSnapEnabled(enabled);
    setStatus(enabled ? "Snapping enabled" : "Snapping disabled");
}

void Studio3DWidget::onSnapSizeChanged(int index)
{
    static const float sizes[] = {0.1f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f};
    if (index < 0 || index >= 6) return;
    m_ogreWidget->setSnapSize(sizes[index]);
}

void Studio3DWidget::onGridToggled(bool visible)
{
    m_ogreWidget->setGridVisible(visible);
}

// Panel toggles
void Studio3DWidget::onToggleToolbar()    { m_toolBar->setVisible(!m_toolBar->isVisible()); }
void Studio3DWidget::onToggleNPanel()     { m_nPanel->setVisible(!m_nPanel->isVisible()); }
void Studio3DWidget::onToggleOutliner()   { m_outlinerDock->setVisible(!m_outlinerDock->isVisible()); }
void Studio3DWidget::onToggleProperties() { m_propertiesDock->setVisible(!m_propertiesDock->isVisible()); }
void Studio3DWidget::onToggleBottom()     { m_bottomDock->setVisible(!m_bottomDock->isVisible()); }

QString Studio3DWidget::sceneFilePath() const
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();
    return m_ctx->projects().current().basePath + "/Scene/scene3d.json";
}

QJsonObject Studio3DWidget::resolveScenePaths(const QJsonObject& scene)
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return scene;
    QString basePath = m_ctx->projects().current().basePath;
    QDir baseDir(basePath);

    QJsonObject resolved = scene;
    if (resolved.contains("terrain")) {
        QJsonObject terrain = resolved["terrain"].toObject();
        if (terrain.contains("heightmapPath")) {
            QString hmPath = terrain["heightmapPath"].toString();
            if (QDir::isRelativePath(hmPath)) {
                terrain["heightmapPath"] = baseDir.absoluteFilePath(hmPath);
            }
        }
        if (terrain.contains("albedoPath")) {
            QString albPath = terrain["albedoPath"].toString();
            if (QDir::isRelativePath(albPath)) {
                terrain["albedoPath"] = baseDir.absoluteFilePath(albPath);
            }
        }
        resolved["terrain"] = terrain;
    }
    if (resolved.contains("xodrPath")) {
        QString xodrPath = resolved["xodrPath"].toString();
        if (QDir::isRelativePath(xodrPath)) {
            resolved["xodrPath"] = baseDir.absoluteFilePath(xodrPath);
        }
    }
    return resolved;
}

void Studio3DWidget::onSaveScene()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    QString path = sceneFilePath();
    if (path.isEmpty()) return;

    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject scene = m_ogreWidget->saveScene();

    QString basePath = m_ctx->projects().current().basePath;
    QDir baseDir(basePath);
    if (scene.contains("terrain")) {
        QJsonObject terrain = scene["terrain"].toObject();
        if (terrain.contains("heightmapPath")) {
            QString hmPath = terrain["heightmapPath"].toString();
            terrain["heightmapPath"] = baseDir.relativeFilePath(hmPath);
        }
        if (terrain.contains("albedoPath")) {
            QString albPath = terrain["albedoPath"].toString();
            terrain["albedoPath"] = baseDir.relativeFilePath(albPath);
        }
        scene["terrain"] = terrain;
    }
    if (scene.contains("xodrPath")) {
        QString xodrPath = scene["xodrPath"].toString();
        scene["xodrPath"] = baseDir.relativeFilePath(xodrPath);
    }

    QJsonDocument doc(scene);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Save Failed",
            QString("Could not write to:\n%1").arg(path));
        return;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    appendLog("Scene saved to: " + path);
    setStatus("Scene saved");
    QMessageBox::information(this, "Scene Saved",
        QString("3D scene saved to:\n%1").arg(path));
}

void Studio3DWidget::onLoadScene()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    QString path = sceneFilePath();
    if (!QFile::exists(path)) {
        QMessageBox::warning(this, "No Scene",
            "No saved 3D scene found.\nSave the scene first.");
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Load Failed",
            QString("Could not read:\n%1").arg(path));
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, "Load Failed",
            QString("JSON parse error: %1").arg(err.errorString()));
        return;
    }

    m_ogreWidget->loadScene(resolveScenePaths(doc.object()));
    appendLog("Scene loaded from: " + path);
    setStatus("Scene loaded");
    refreshStats();
}

void Studio3DWidget::loadMissingProjectAssets()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return;

    if (!m_ogreWidget->hasTerrain()) {
        const QString hmPath = findHeightmapInProject();
        if (!hmPath.isEmpty()) {
            const QString albPath = findAlbedoInProject();
            float heightScale = static_cast<float>(m_heightScaleSlider->value());
            float terrainSize = 4000.0f;
            if (m_ctx->projects().current().bounds.valid) {
                const auto& b = m_ctx->projects().current().bounds;
                const double latMid = (b.minLat + b.maxLat) / 2.0;
                const double latM = (b.maxLat - b.minLat) * 111320.0;
                const double lonM = (b.maxLon - b.minLon) * 111320.0 *
                    cos(latMid * 3.14159265358979 / 180.0);
                terrainSize = static_cast<float>(std::max(latM, lonM));
                if (terrainSize < 10.0f) terrainSize = 4000.0f;
            }
            m_ogreWidget->loadTerrain(hmPath, albPath, terrainSize, heightScale);
            appendLog("Terrain auto-loaded from project: " + hmPath);
        } else {
            appendLog("No exported heightmap found under project/Terrain.");
        }
    }

    if (!m_ogreWidget->hasRoads()) {
        const QString xodrPath = findXodrInProject();
        if (!xodrPath.isEmpty()) {
            m_ogreWidget->loadRoads(xodrPath);
            appendLog("Roads auto-loaded from project: " + xodrPath);
        }
    }
}

void Studio3DWidget::onProjectOpened()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return;
    if (m_sceneAutoLoaded) {
        QTimer::singleShot(100, this, [this]() { loadMissingProjectAssets(); });
        return;
    }
    m_sceneAutoLoaded = true;

    QString path = sceneFilePath();
    if (QFile::exists(path)) {
        appendLog("Auto-loading saved scene...");
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull()) {
                QTimer::singleShot(500, this, [this, doc]() {
                    m_ogreWidget->loadScene(resolveScenePaths(doc.object()));
                    appendLog("Scene auto-loaded from project.");
                    loadMissingProjectAssets();
                    refreshStats();
                });
                return;
            }
        }
    }

    QTimer::singleShot(500, this, [this]() { loadMissingProjectAssets(); });
}

void Studio3DWidget::onProjectClosed()
{
    m_ogreWidget->clearActors();
    m_ogreWidget->clearRoads();
    m_ogreWidget->clearTerrain();
    m_sceneAutoLoaded = false;
    appendLog("Project closed — 3D scene cleared.");
    setStatus("Ready");
    refreshStats();
}

void Studio3DWidget::onHeightScaleChanged(int value)
{
    m_heightScaleLabel->setText(QString("%1m").arg(value));
}

void Studio3DWidget::onExportTerrain()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    QString exportDir = m_ctx->projects().current().basePath + "/Exports";
    QDir().mkpath(exportDir);

    QString terrainDir = m_ctx->projects().current().basePath + "/Terrain";
    int count = 0;

    if (QDir(terrainDir).exists()) {
        QStringList filters;
        filters << "*.png" << "*.tif" << "*.tiff";
        QStringList files = QDir(terrainDir).entryList(filters, QDir::Files);
        for (const auto& f : files) {
            QString src = terrainDir + "/" + f;
            QString dst = exportDir + "/terrain_" + f;
            if (QFile::exists(dst)) QFile::remove(dst);
            if (QFile::copy(src, dst)) {
                count++;
                appendLog("Exported: " + f);
            }
        }
    }

    if (count > 0) {
        appendLog(QString("Exported %1 terrain files to: %2").arg(count).arg(exportDir));
        QMessageBox::information(this, "Export Complete",
            QString("Exported %1 file(s) to:\n%2").arg(count).arg(exportDir));
    } else {
        appendLog("No terrain data found to export.");
        QMessageBox::warning(this, "Export Failed",
            "No terrain data found.\nDownload terrain in Terrain Studio first.");
    }
}

void Studio3DWidget::onExportRoads()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    QString roadDir = m_ctx->projects().current().basePath + "/Roads";
    QString exportDir = m_ctx->projects().current().basePath + "/Exports";
    QDir().mkpath(exportDir);

    if (!QDir(roadDir).exists()) {
        QMessageBox::warning(this, "Export Failed",
            "No road data found.\nCreate roads in Road Studio first.");
        return;
    }

    QStringList filters;
    filters << "*.xodr" << "*.obj" << "*.fbx";
    QStringList files = QDir(roadDir).entryList(filters, QDir::Files);

    int count = 0;
    for (const auto& f : files) {
        QString src = roadDir + "/" + f;
        QString dst = exportDir + "/road_" + f;
        if (QFile::exists(dst)) QFile::remove(dst);
        if (QFile::copy(src, dst)) {
            count++;
            appendLog("Exported: " + f);
        }
    }

    if (count > 0) {
        appendLog(QString("Exported %1 road files to: %2").arg(count).arg(exportDir));
        QMessageBox::information(this, "Export Complete",
            QString("Exported %1 file(s) to:\n%2").arg(count).arg(exportDir));
    } else {
        QMessageBox::warning(this, "Export Failed",
            "No road files found in Roads folder.");
    }
}

QString Studio3DWidget::worldFilePath() const
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();
    return m_ctx->projects().current().basePath + "/World/world.json";
}

void Studio3DWidget::onSaveWorld()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    QString path = worldFilePath();
    if (path.isEmpty()) return;

    if (m_ogreWidget->saveWorld(path)) {
        appendLog("World saved to: " + path);
        setStatus("World saved");
        QMessageBox::information(this, "World Saved",
            QString("World saved to:\n%1\n\nActors: %2\nLayers: %3\nSplines: %4")
                .arg(path)
                .arg(m_ogreWidget->world()->actorCount())
                .arg(m_ogreWidget->world()->layerCount())
                .arg(m_ogreWidget->world()->splineCount()));
    } else {
        QMessageBox::warning(this, "Save Failed",
            QString("Could not write to:\n%1").arg(path));
    }
}

void Studio3DWidget::onLoadWorld()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) {
        QMessageBox::warning(this, "No Project", "Open an OpenGeoStudio project first.");
        return;
    }

    QString path = worldFilePath();
    if (!QFile::exists(path)) {
        QMessageBox::warning(this, "No World",
            "No saved world found.\nSave the world first.");
        return;
    }

    if (m_ogreWidget->loadWorld(path)) {
        appendLog("World loaded from: " + path);
        setStatus("World loaded");
        if (m_outliner) m_outliner->refresh();
        if (m_layerPanel) m_layerPanel->refresh();
        refreshStats();
        QMessageBox::information(this, "World Loaded",
            QString("World loaded from:\n%1\n\nActors: %2\nLayers: %3")
                .arg(path)
                .arg(m_ogreWidget->world()->actorCount())
                .arg(m_ogreWidget->world()->layerCount()));
    } else {
        QMessageBox::warning(this, "Load Failed",
            QString("Could not load world from:\n%1").arg(path));
    }
}

void Studio3DWidget::onActorSelected(const QString& id)
{
    m_propertiesEditor->setActor(id);
    m_nPanel->setActor(id);
}
