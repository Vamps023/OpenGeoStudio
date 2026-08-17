#include "Studio3DWidget.hpp"
#include "OgreWidget.hpp"
#include "EditorPanels.hpp"

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
#include <cmath>
#include <cstdlib>

Studio3DWidget::Studio3DWidget(ApplicationContext* ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx)
{
    setupUI();
}

Studio3DWidget::~Studio3DWidget() = default;

namespace {
QLabel* makePanelHeader(const QString& title, QWidget* parent)
{
    auto* lbl = new QLabel(title, parent);
    lbl->setObjectName("panelHeader");
    return lbl;
}

QFrame* makeVSeparator(QWidget* parent)
{
    auto* sep = new QFrame(parent);
    sep->setObjectName("vsep");
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(2);
    return sep;
}
} // namespace

void Studio3DWidget::setupUI()
{
    setStyleSheet(QStringLiteral(
        "QLabel#panelHeader { background: #161b22; color: #7d8590; font-size: 10px;"
        "  font-weight: bold; text-transform: uppercase; letter-spacing: 1px;"
        "  padding: 6px 8px; border: 1px solid #30363d; }"
        "QWidget#studioToolbar { background: #0d1117; border: 1px solid #30363d; }"
        "QToolButton { background: transparent; color: #e6edf3; padding: 6px 12px;"
        "  border-radius: 4px; font-size: 12px; }"
        "QToolButton:hover { background: #21262d; }"
        "QToolButton:pressed { background: #30363d; }"
        "QToolButton:checked { background: #1f6feb; color: #ffffff; }"
        "QFrame#vsep { background: #30363d; }"
        "QWidget#studioStatusBar { background: #161b22; border: 1px solid #30363d; }"
        "QSplitter::handle { background: #21262d; }"
        "QSplitter::handle:horizontal { width: 3px; }"
        "QSplitter::handle:vertical { height: 3px; }"
        "QTabWidget::pane { border: 1px solid #30363d; }"
        "QTabBar::tab { background: #0d1117; color: #7d8590; padding: 6px 16px;"
        "  border: 1px solid #30363d; border-bottom: none; }"
        "QTabBar::tab:selected { background: #161b22; color: #e6edf3;"
        "  border-bottom: 2px solid #1f6feb; }"
    ));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // ─── Viewport (created first; panels attach to it) ───
    m_ogreWidget = new OgreWidget(this);

    // ─── Toolbar ───
    mainLayout->addWidget(setupToolbar());

    // ─── Editor row: outliner | viewport | details ───
    auto* editorRow = new QSplitter(Qt::Horizontal, this);
    editorRow->setChildrenCollapsible(false);
    editorRow->addWidget(setupLeftPanel());

    auto* viewportWrap = new QWidget(this);
    auto* viewportLayout = new QVBoxLayout(viewportWrap);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->addWidget(m_ogreWidget->containerWidget());
    editorRow->addWidget(viewportWrap);

    editorRow->addWidget(setupRightPanel());
    editorRow->setStretchFactor(0, 0);
    editorRow->setStretchFactor(1, 1);
    editorRow->setStretchFactor(2, 0);
    editorRow->setSizes({250, 900, 340});

    // ─── Bottom: content browser + output log ───
    m_bottomTabs = new QTabWidget(this);
    m_bottomTabs->setMinimumHeight(190);

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
    logLayout->setContentsMargins(4, 4, 4, 4);
    m_logEdit = new QTextEdit(logWrap);
    m_logEdit->setReadOnly(true);
    m_logEdit->setStyleSheet(
        "QTextEdit { background-color: #1e1e2e; color: #cdd6f4; "
        "font-family: Consolas; font-size: 11px; }");
    logLayout->addWidget(m_logEdit);
    m_bottomTabs->addTab(logWrap, "Output Log");

    // ─── Main vertical splitter: editor row over bottom tabs ───
    auto* mainSplit = new QSplitter(Qt::Vertical, this);
    mainSplit->setChildrenCollapsible(false);
    mainSplit->addWidget(editorRow);
    mainSplit->addWidget(m_bottomTabs);
    mainSplit->setStretchFactor(0, 1);
    mainSplit->setStretchFactor(1, 0);
    mainSplit->setSizes({620, 230});
    mainLayout->addWidget(mainSplit, 1);

    // ─── Status bar ───
    mainLayout->addWidget(setupStatusBar());

    setLayout(mainLayout);

    // ─── Signal wiring ───
    connect(m_outliner, &WorldOutliner::actorSelected, this, &Studio3DWidget::onActorSelected);
    connect(m_ogreWidget, &OgreWidget::actorSelected, this, &Studio3DWidget::onActorSelected);
    connect(m_ogreWidget, &OgreWidget::actorAdded, [this](const QString&) { m_outliner->refresh(); refreshStats(); });
    connect(m_ogreWidget, &OgreWidget::actorRemoved, [this](const QString&) { m_outliner->refresh(); refreshStats(); });
    connect(m_ogreWidget, &OgreWidget::sceneChanged, [this]() { m_outliner->refresh(); m_layerPanel->refresh(); refreshStats(); });
    connect(m_ogreWidget, &OgreWidget::transformModeChanged, this, &Studio3DWidget::onTransformModeChanged);

    setStatus("Ready");
    refreshStats();
    appendLog("3D Studio ready (Unreal-style editor layout)");
}

QWidget* Studio3DWidget::setupToolbar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName("studioToolbar");
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(2);

    auto makeBtn = [bar](const QString& text, const QString& tip, bool checkable) {
        auto* b = new QToolButton(bar);
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        return b;
    };

    // Scene save/load
    auto* saveBtn = makeBtn("Save Scene",
        "Save the 3D scene (terrain, roads, actors, camera) to the project", false);
    connect(saveBtn, &QToolButton::clicked, this, &Studio3DWidget::onSaveScene);
    lay->addWidget(saveBtn);

    auto* loadBtn = makeBtn("Load Scene",
        "Load the saved 3D scene from the project", false);
    connect(loadBtn, &QToolButton::clicked, this, &Studio3DWidget::onLoadScene);
    lay->addWidget(loadBtn);

    lay->addWidget(makeVSeparator(bar));

    // Transform tools (mirrored by Q/W/E/R in the viewport)
    m_selectToolBtn = makeBtn("Select", "Select tool (Q)", true);
    m_moveToolBtn = makeBtn("Move", "Move tool (W)", true);
    m_rotateToolBtn = makeBtn("Rotate", "Rotate tool (E)", true);
    m_scaleToolBtn = makeBtn("Scale", "Scale tool (R)", true);
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
    lay->addWidget(m_selectToolBtn);
    lay->addWidget(m_moveToolBtn);
    lay->addWidget(m_rotateToolBtn);
    lay->addWidget(m_scaleToolBtn);

    lay->addWidget(makeVSeparator(bar));

    // Snapping
    m_snapBtn = makeBtn("Snap", "Snap actor transforms to the grid size", true);
    m_snapBtn->setChecked(m_ogreWidget->isSnapEnabled());
    connect(m_snapBtn, &QToolButton::toggled, this, &Studio3DWidget::onSnapToggled);
    lay->addWidget(m_snapBtn);

    m_snapSizeCombo = new QComboBox(bar);
    m_snapSizeCombo->addItems({"0.1 m", "0.5 m", "1 m", "2 m", "5 m", "10 m"});
    m_snapSizeCombo->setCurrentIndex(2); // 1 m — matches OgreWidget default
    m_snapSizeCombo->setToolTip("Grid snap size");
    connect(m_snapSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Studio3DWidget::onSnapSizeChanged);
    lay->addWidget(m_snapSizeCombo);

    m_gridBtn = makeBtn("Grid", "Show/hide the viewport grid", true);
    m_gridBtn->setChecked(m_ogreWidget->isGridVisible());
    connect(m_gridBtn, &QToolButton::toggled, this, &Studio3DWidget::onGridToggled);
    lay->addWidget(m_gridBtn);

    lay->addStretch();

    auto* resetCamBtn = makeBtn("Reset Camera", "Reset the camera to the world origin", false);
    connect(resetCamBtn, &QToolButton::clicked, this, [this]() {
        m_ogreWidget->resetCamera();
        appendLog("Camera reset.");
    });
    lay->addWidget(resetCamBtn);

    return bar;
}

QWidget* Studio3DWidget::setupLeftPanel()
{
    auto* leftPanel = new QWidget(this);
    leftPanel->setMinimumWidth(220);
    leftPanel->setMaximumWidth(300);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(3);

    leftLayout->addWidget(makePanelHeader("World Outliner", leftPanel));
    m_outliner = new WorldOutliner(m_ogreWidget, leftPanel);
    leftLayout->addWidget(m_outliner, 3);

    leftLayout->addWidget(makePanelHeader("Layers", leftPanel));
    m_layerPanel = new LayerPanel(m_ogreWidget, leftPanel);
    leftLayout->addWidget(m_layerPanel, 1);

    return leftPanel;
}

QWidget* Studio3DWidget::setupRightPanel()
{
    auto* rightPanel = new QSplitter(Qt::Vertical, this);
    rightPanel->setChildrenCollapsible(false);
    rightPanel->setMinimumWidth(300);
    rightPanel->setMaximumWidth(400);

    // Details (Inspector) on top
    m_inspector = new Inspector(m_ogreWidget, this);
    rightPanel->addWidget(m_inspector);

    // Place Actors / World tabs below
    auto* tabs = new QTabWidget(this);

    // ─── Place Actors tab ───
    auto* placeScroll = new QScrollArea(this);
    placeScroll->setWidgetResizable(true);
    placeScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    placeScroll->setFrameShape(QFrame::NoFrame);
    auto* placePanel = new QWidget(this);
    auto* placeLayout = new QVBoxLayout(placePanel);
    placeLayout->setContentsMargins(6, 6, 6, 6);
    placeLayout->setSpacing(8);

    auto addPlaceSection = [this, placeLayout, placePanel](const QString& title,
                                                           std::initializer_list<QPair<QString, world::ActorType>> entries) {
        placeLayout->addWidget(makePanelHeader(title, placePanel));
        auto* grid = new QGridLayout();
        grid->setSpacing(4);
        int col = 0, row = 0;
        for (const auto& entry : entries) {
            auto* b = new QToolButton(placePanel);
            b->setText(entry.first);
            b->setToolButtonStyle(Qt::ToolButtonTextOnly);
            b->setToolTip(QString("Place a %1 at the camera focus").arg(entry.first));
            const world::ActorType t = entry.second;
            connect(b, &QToolButton::clicked, this, [this, t]() {
                onPlaceAsset(QString::number(static_cast<int>(t)), "actor");
            });
            grid->addWidget(b, row, col);
            if (++col == 3) { col = 0; ++row; }
        }
        grid->setColumnStretch(2, 1);
        placeLayout->addLayout(grid);
    };

    addPlaceSection("Basics", {
        {"Cube", world::ActorType::Prop},
        {"Empty", world::ActorType::Empty},
        {"Prop", world::ActorType::Prop},
    });
    addPlaceSection("Nature", {
        {"Tree", world::ActorType::Tree},
        {"Vegetation", world::ActorType::Vegetation},
        {"Grass", world::ActorType::Grass},
        {"Rock", world::ActorType::Rock},
        {"Lake", world::ActorType::Water},
    });
    addPlaceSection("City", {
        {"Building", world::ActorType::Building},
    });
    addPlaceSection("Lights", {
        {"Sun Light", world::ActorType::SunLight},
        {"Sky Light", world::ActorType::SkyLight},
    });

    auto* placeHint = new QLabel("Actors are placed at the camera focus and snap to the terrain surface.", placePanel);
    placeHint->setWordWrap(true);
    placeHint->setStyleSheet("QLabel { color: #7d8590; font-size: 10px; }");
    placeLayout->addWidget(placeHint);
    placeLayout->addStretch();
    placeScroll->setWidget(placePanel);
    tabs->addTab(placeScroll, "Place Actors");

    // ─── World tab: terrain, roads, generation, project data ───
    auto* worldScroll = new QScrollArea(this);
    worldScroll->setWidgetResizable(true);
    worldScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    worldScroll->setFrameShape(QFrame::NoFrame);
    auto* worldPanel = new QWidget(this);
    auto* worldLayout = new QVBoxLayout(worldPanel);
    worldLayout->setContentsMargins(6, 6, 6, 6);
    worldLayout->setSpacing(8);

    // Terrain
    auto* terrainGroup = new QGroupBox("Terrain", worldPanel);
    auto* terrainLayout = new QVBoxLayout(terrainGroup);
    auto* loadTerrainBtn = new QPushButton("Load Terrain from Project", terrainGroup);
    loadTerrainBtn->setStyleSheet(
        "QPushButton { background-color: #89b4fa; color: #1e1e2e; "
        "font-weight: bold; padding: 8px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #b4befe; }");
    loadTerrainBtn->setToolTip("Load heightmap + albedo from the current project's Terrain folder");
    connect(loadTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadTerrain);
    terrainLayout->addWidget(loadTerrainBtn);

    auto* clearTerrainBtn = new QPushButton("Clear Terrain", terrainGroup);
    connect(clearTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onClearTerrain);
    terrainLayout->addWidget(clearTerrainBtn);

    auto* hsLayout = new QHBoxLayout();
    auto* hsLabel = new QLabel("Height Scale:", terrainGroup);
    m_heightScaleLabel = new QLabel("100m", terrainGroup);
    m_heightScaleLabel->setMinimumWidth(50);
    m_heightScaleSlider = new QSlider(Qt::Horizontal, terrainGroup);
    m_heightScaleSlider->setRange(1, 1000);
    m_heightScaleSlider->setValue(100);
    connect(m_heightScaleSlider, &QSlider::valueChanged, this, &Studio3DWidget::onHeightScaleChanged);
    hsLayout->addWidget(hsLabel);
    hsLayout->addWidget(m_heightScaleSlider);
    hsLayout->addWidget(m_heightScaleLabel);
    terrainLayout->addLayout(hsLayout);
    worldLayout->addWidget(terrainGroup);

    // Roads
    auto* roadsGroup = new QGroupBox("Roads", worldPanel);
    auto* roadsLayout = new QVBoxLayout(roadsGroup);
    auto* loadRoadsBtn = new QPushButton("Load Roads from Project", roadsGroup);
    loadRoadsBtn->setStyleSheet(
        "QPushButton { background-color: #a6e3a1; color: #1e1e2e; "
        "font-weight: bold; padding: 8px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #b4e3b4; }");
    loadRoadsBtn->setToolTip("Load road network (XODR) from the current project's Roads folder");
    connect(loadRoadsBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadRoads);
    roadsLayout->addWidget(loadRoadsBtn);

    auto* exportRoadsBtn = new QPushButton("Export Road Network", roadsGroup);
    exportRoadsBtn->setToolTip("Copy road network files to the project Exports folder");
    connect(exportRoadsBtn, &QPushButton::clicked, this, &Studio3DWidget::onExportRoads);
    roadsLayout->addWidget(exportRoadsBtn);
    worldLayout->addWidget(roadsGroup);

    // Procedural generation
    auto* genGroup = new QGroupBox("World Generation", worldPanel);
    auto* genLayout = new QVBoxLayout(genGroup);
    auto* genHint = new QLabel("Procedural authoring via WorldBuilder:", genGroup);
    genHint->setStyleSheet("QLabel { color: #7d8590; font-size: 10px; }");
    genLayout->addWidget(genHint);

    auto* genBuildingsBtn = new QPushButton("Generate Buildings (100)", genGroup);
    genBuildingsBtn->setToolTip("Generate 100 buildings at terrain height via WorldBuilder");
    connect(genBuildingsBtn, &QPushButton::clicked, this, &Studio3DWidget::onGenerateBuildings);
    genLayout->addWidget(genBuildingsBtn);

    auto* genVegBtn = new QPushButton("Generate Vegetation (PCG)", genGroup);
    genVegBtn->setToolTip("Run the PCG vegetation graph (scatter, height/slope filters)");
    connect(genVegBtn, &QPushButton::clicked, this, &Studio3DWidget::onGenerateVegetation);
    genLayout->addWidget(genVegBtn);
    worldLayout->addWidget(genGroup);

    // Project data
    auto* projectGroup = new QGroupBox("Project Data", worldPanel);
    auto* projectLayout = new QVBoxLayout(projectGroup);

    auto* saveWorldBtn = new QPushButton("Save World", projectGroup);
    saveWorldBtn->setStyleSheet(
        "QPushButton { background-color: #89b4fa; color: #1e1e2e; font-weight: bold; padding: 8px; }");
    saveWorldBtn->setToolTip("Save the complete World (actors, layers, splines, PCG, terrain) to the project");
    connect(saveWorldBtn, &QPushButton::clicked, this, &Studio3DWidget::onSaveWorld);
    projectLayout->addWidget(saveWorldBtn);

    auto* loadWorldBtn = new QPushButton("Load World", projectGroup);
    loadWorldBtn->setStyleSheet(
        "QPushButton { background-color: #a6e3a1; color: #1e1e2e; font-weight: bold; padding: 8px; }");
    loadWorldBtn->setToolTip("Load the complete World from the project");
    connect(loadWorldBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadWorld);
    projectLayout->addWidget(loadWorldBtn);

    auto* exportTerrainBtn = new QPushButton("Export Terrain Data", projectGroup);
    exportTerrainBtn->setToolTip("Copy terrain heightmap + albedo to the project Exports folder");
    connect(exportTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onExportTerrain);
    projectLayout->addWidget(exportTerrainBtn);

    auto* clearObjectsBtn = new QPushButton("Clear All Objects", projectGroup);
    connect(clearObjectsBtn, &QPushButton::clicked, this, &Studio3DWidget::onClearObjects);
    projectLayout->addWidget(clearObjectsBtn);
    worldLayout->addWidget(projectGroup);

    worldLayout->addStretch();
    worldScroll->setWidget(worldPanel);
    tabs->addTab(worldScroll, "World");

    rightPanel->addWidget(tabs);
    rightPanel->setStretchFactor(0, 3);
    rightPanel->setStretchFactor(1, 2);
    rightPanel->setSizes({420, 380});

    return rightPanel;
}

QWidget* Studio3DWidget::setupStatusBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName("studioStatusBar");
    auto* lay = new QHBoxLayout(bar);
    lay->setContentsMargins(10, 4, 10, 4);
    lay->setSpacing(12);

    m_statusLabel = new QLabel("Ready", bar);
    lay->addWidget(m_statusLabel);

    lay->addStretch();

    auto* hintLabel = new QLabel(
        "LMB Select · Drag Orbit · MMB Pan · Wheel Zoom · Q/W/E/R Tools · F Frame · Del Delete",
        bar);
    hintLabel->setStyleSheet("QLabel { color: #7d8590; font-size: 10px; }");
    lay->addWidget(hintLabel);

    m_statsLabel = new QLabel("0 actors", bar);
    lay->addWidget(m_statsLabel);

    return bar;
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
    // Small offset so repeated placements don't stack exactly on one spot
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

    // File asset — place a prop carrying the asset path (mesh support pending;
    // the path is preserved on the actor for when real mesh loading lands)
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

    // Prefer merged products, then exported tiles. Support both the normal
    // Terrain layout and projects where the export created Terrain/Terrain.
    const QStringList roots = {
        terrainDir,
        QDir(projectDir).filePath("Exports"),
        projectDir
    };
    const QStringList preferred = {"heightmap_merged.png", "heightmap_merged.tif",
                                   "heightmap_merged.tiff"};
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
            // Never fall back to an albedo image as a heightmap
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
    // PNG/JPEG first (displayable directly), then TIFF
    for (const QStringList& patterns : {QStringList{"*.png", "*.jpg", "*.jpeg"},
                                        QStringList{"*.tif", "*.tiff"}}) {
        for (const QString& root : roots) {
            if (!QDir(root).exists()) continue;
            QDirIterator it(root, patterns, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString candidate = it.next();
                const QFileInfo info(candidate);
                // Never fall back to a heightmap as albedo
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
    // Use 4000m for 4km terrain areas, or derive from project bounds if available
    float terrainSize = 4000.0f; // 4km default for Houston-scale terrain
    if (m_ctx && m_ctx->projects().hasProject() && m_ctx->projects().current().bounds.valid) {
        auto& b = m_ctx->projects().current().bounds;
        // Calculate approximate meters from lat/lon bounds
        double latMid = (b.minLat + b.maxLat) / 2.0;
        double latM = (b.maxLat - b.minLat) * 111320.0;  // meters per degree lat
        double lonM = (b.maxLon - b.minLon) * 111320.0 * cos(latMid * 3.14159265358979 / 180.0);
        terrainSize = static_cast<float>(std::max(latM, lonM));
        if (terrainSize < 10.0f) terrainSize = 4000.0f;  // fallback
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

QString Studio3DWidget::sceneFilePath() const
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();
    return m_ctx->projects().current().basePath + "/Scene/scene3d.json";
}

// Resolve relative paths in a scene JSON to absolute paths using project base path
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

    // Convert absolute paths to relative (relative to project base path)
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

    // A previously saved scene may exist without terrain because the scene
    // was saved before Terrain Studio finished exporting. Always discover
    // project assets that are still missing instead of treating that scene
    // file as authoritative for all resources.
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
    // Auto-load the saved scene if it exists (only once per project open),
    // then independently load any terrain/road artifacts missing from it.
    if (!m_ctx || !m_ctx->projects().hasProject()) return;
    if (m_sceneAutoLoaded) {
        // This supports the workflow: export terrain in Terrain Studio,
        // then return to an already-open 3D Studio workspace.
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

    // No usable saved scene — load terrain and roads independently.
    QTimer::singleShot(500, this, [this]() { loadMissingProjectAssets(); });
}

void Studio3DWidget::onProjectClosed()
{
    m_ogreWidget->clearActors();
    m_ogreWidget->clearRoads();
    m_ogreWidget->clearTerrain();
    m_sceneAutoLoaded = false;  // Reset for next project
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

    // Copy all terrain data to Exports
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

// ============================================================
// World save/load
// ============================================================

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
    if (m_inspector)
        m_inspector->setActor(id);
}
