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
#include <QCoreApplication>
#include <cmath>
#include <cstdlib>

Studio3DWidget::Studio3DWidget(ApplicationContext* ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx)
{
    setupUI();
}

Studio3DWidget::~Studio3DWidget() = default;

void Studio3DWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    // ─── Left: OGRE 3D Viewport ───
    auto* viewportGroup = new QGroupBox("3D Viewport (OGRE-Next)", this);
    auto* viewportLayout = new QVBoxLayout(viewportGroup);

    m_ogreWidget = new OgreWidget(viewportGroup);
    viewportLayout->addWidget(m_ogreWidget->containerWidget());

    // Camera controls
    auto* camLayout = new QHBoxLayout();
    m_resetCameraBtn = new QPushButton("Reset Camera", viewportGroup);
    m_resetCameraBtn->setStyleSheet("QPushButton { padding: 6px 12px; }");
    connect(m_resetCameraBtn, &QPushButton::clicked, this, &Studio3DWidget::onResetCamera);
    camLayout->addWidget(m_resetCameraBtn);
    camLayout->addStretch();
    viewportLayout->addLayout(camLayout);

    viewportGroup->setLayout(viewportLayout);

    // ─── Left: Unreal-style World Outliner + Layers ───
    auto* leftPanel = new QWidget(this);
    leftPanel->setMinimumWidth(220);
    leftPanel->setMaximumWidth(300);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    m_outliner = new WorldOutliner(m_ogreWidget, leftPanel);
    m_layerPanel = new LayerPanel(m_ogreWidget, leftPanel);
    leftLayout->addWidget(m_outliner, 3);
    leftLayout->addWidget(m_layerPanel, 1);

    m_inspector = new Inspector(m_ogreWidget, this);

    // Connect signals
    connect(m_outliner, &WorldOutliner::actorSelected, this, &Studio3DWidget::onActorSelected);
    connect(m_ogreWidget, &OgreWidget::actorSelected, this, &Studio3DWidget::onActorSelected);
    connect(m_ogreWidget, &OgreWidget::actorAdded, [this](const QString&) { m_outliner->refresh(); });
    connect(m_ogreWidget, &OgreWidget::actorRemoved, [this](const QString&) { m_outliner->refresh(); });
    connect(m_ogreWidget, &OgreWidget::sceneChanged, [this]() { m_outliner->refresh(); m_layerPanel->refresh(); });

    // ─── Right: Details + world controls ───
    auto* sidePanel = new QWidget(this);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sidePanel->setMaximumWidth(350);

    // ─── Terrain ───
    auto* terrainGroup = new QGroupBox("Terrain", sidePanel);
    auto* terrainLayout = new QVBoxLayout(terrainGroup);

    m_loadTerrainBtn = new QPushButton("Load Terrain from Project", terrainGroup);
    m_loadTerrainBtn->setStyleSheet(
        "QPushButton { background-color: #89b4fa; color: #1e1e2e; "
        "font-weight: bold; padding: 10px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #b4befe; }");
    m_loadTerrainBtn->setToolTip("Load heightmap + albedo from the current project's Terrain folder");
    connect(m_loadTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadTerrain);
    terrainLayout->addWidget(m_loadTerrainBtn);

    m_clearTerrainBtn = new QPushButton("Clear Terrain", terrainGroup);
    m_clearTerrainBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_clearTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onClearTerrain);
    terrainLayout->addWidget(m_clearTerrainBtn);

    // Roads section
    auto* roadsGroup = new QGroupBox("Roads", sidePanel);
    auto* roadsLayout = new QVBoxLayout(roadsGroup);
    m_loadRoadsBtn = new QPushButton("Load Roads from Project", roadsGroup);
    m_loadRoadsBtn->setStyleSheet(
        "QPushButton { background-color: #a6e3a1; color: #1e1e2e; "
        "font-weight: bold; padding: 10px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #b4e3b4; }");
    m_loadRoadsBtn->setToolTip("Load road network (XODR) from the current project's Roads folder");
    connect(m_loadRoadsBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadRoads);
    roadsLayout->addWidget(m_loadRoadsBtn);
    sideLayout->addWidget(roadsGroup);

    // Height scale slider
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

    sideLayout->addWidget(terrainGroup);

    // ─── Export ───
    auto* exportGroup = new QGroupBox("Export", sidePanel);
    auto* exportLayout = new QVBoxLayout(exportGroup);

    m_exportTerrainBtn = new QPushButton("Export Terrain Data", exportGroup);
    m_exportTerrainBtn->setToolTip("Export terrain heightmap + albedo to project Exports folder");
    m_exportTerrainBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_exportTerrainBtn, &QPushButton::clicked, this, &Studio3DWidget::onExportTerrain);
    exportLayout->addWidget(m_exportTerrainBtn);

    m_exportRoadsBtn = new QPushButton("Export Road Network", exportGroup);
    m_exportRoadsBtn->setToolTip("Export road network mesh for 3D rendering");
    m_exportRoadsBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_exportRoadsBtn, &QPushButton::clicked, this, &Studio3DWidget::onExportRoads);
    exportLayout->addWidget(m_exportRoadsBtn);

    sideLayout->addWidget(exportGroup);

    // ─── Objects ───
    auto* objGroup = new QGroupBox("3D Objects", sidePanel);
    auto* objLayout = new QVBoxLayout(objGroup);

    auto* objHint = new QLabel("Click an object type to place it at the terrain center:", objGroup);
    objHint->setWordWrap(true);
    objHint->setStyleSheet("QLabel { color: #888; font-size: 10px; }");
    objLayout->addWidget(objHint);

    auto* objBtnRow1 = new QHBoxLayout();
    m_addBuildingBtn = new QPushButton("Building", objGroup);
    m_addBuildingBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_addBuildingBtn, &QPushButton::clicked, this, &Studio3DWidget::onAddBuilding);
    objBtnRow1->addWidget(m_addBuildingBtn);

    m_addTreeBtn = new QPushButton("Tree", objGroup);
    m_addTreeBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_addTreeBtn, &QPushButton::clicked, this, &Studio3DWidget::onAddTree);
    objBtnRow1->addWidget(m_addTreeBtn);
    objLayout->addLayout(objBtnRow1);

    m_addBoxBtn = new QPushButton("Box", objGroup);
    m_addBoxBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_addBoxBtn, &QPushButton::clicked, this, &Studio3DWidget::onAddBox);
    objLayout->addWidget(m_addBoxBtn);

    m_clearObjectsBtn = new QPushButton("Clear All Objects", objGroup);
    m_clearObjectsBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_clearObjectsBtn, &QPushButton::clicked, this, &Studio3DWidget::onClearObjects);
    objLayout->addWidget(m_clearObjectsBtn);

    sideLayout->addWidget(objGroup);

    // ─── World Authoring (WorldBuilder procedural generation) ───
    auto* worldGroup = new QGroupBox("World Authoring", sidePanel);
    auto* worldLayout = new QVBoxLayout(worldGroup);

    auto* worldHint = new QLabel("Procedural world generation (WorldBuilder):", worldGroup);
    worldHint->setWordWrap(true);
    worldHint->setStyleSheet("QLabel { color: #888; font-size: 10px; }");
    worldLayout->addWidget(worldHint);

    m_genBuildingsBtn = new QPushButton("Generate Buildings (100)", worldGroup);
    m_genBuildingsBtn->setStyleSheet("QPushButton { padding: 8px; }");
    m_genBuildingsBtn->setToolTip("Generate 100 buildings at terrain height via WorldBuilder");
    connect(m_genBuildingsBtn, &QPushButton::clicked, this, &Studio3DWidget::onGenerateBuildings);
    worldLayout->addWidget(m_genBuildingsBtn);

    m_genVegetationBtn = new QPushButton("Generate Vegetation (PCG)", worldGroup);
    m_genVegetationBtn->setStyleSheet("QPushButton { padding: 8px; }");
    m_genVegetationBtn->setToolTip("Run the PCG vegetation graph (scatter, height/slope filters)");
    connect(m_genVegetationBtn, &QPushButton::clicked, this, &Studio3DWidget::onGenerateVegetation);
    worldLayout->addWidget(m_genVegetationBtn);

    auto* worldBtnRow = new QHBoxLayout();
    m_addLakeBtn = new QPushButton("Add Lake", worldGroup);
    m_addLakeBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_addLakeBtn, &QPushButton::clicked, this, &Studio3DWidget::onAddLake);
    worldBtnRow->addWidget(m_addLakeBtn);

    m_addSunBtn = new QPushButton("Add Sun", worldGroup);
    m_addSunBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_addSunBtn, &QPushButton::clicked, this, &Studio3DWidget::onAddSunLight);
    worldBtnRow->addWidget(m_addSunBtn);

    m_addSkyBtn = new QPushButton("Add Sky", worldGroup);
    m_addSkyBtn->setStyleSheet("QPushButton { padding: 8px; }");
    connect(m_addSkyBtn, &QPushButton::clicked, this, &Studio3DWidget::onAddSkyLight);
    worldBtnRow->addWidget(m_addSkyBtn);
    worldLayout->addLayout(worldBtnRow);

    sideLayout->addWidget(worldGroup);

    // ─── Scene ───
    auto* sceneGroup = new QGroupBox("Scene", sidePanel);
    auto* sceneLayout = new QVBoxLayout(sceneGroup);

    m_saveSceneBtn = new QPushButton("Save Scene", sceneGroup);
    m_saveSceneBtn->setStyleSheet("QPushButton { padding: 8px; }");
    m_saveSceneBtn->setToolTip("Save the current 3D scene (terrain, roads, objects, camera) to the project");
    connect(m_saveSceneBtn, &QPushButton::clicked, this, &Studio3DWidget::onSaveScene);
    sceneLayout->addWidget(m_saveSceneBtn);

    m_loadSceneBtn = new QPushButton("Load Scene", sceneGroup);
    m_loadSceneBtn->setStyleSheet("QPushButton { padding: 8px; }");
    m_loadSceneBtn->setToolTip("Load the saved 3D scene from the project");
    connect(m_loadSceneBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadScene);
    sceneLayout->addWidget(m_loadSceneBtn);

    m_saveWorldBtn = new QPushButton("Save World", sceneGroup);
    m_saveWorldBtn->setStyleSheet("QPushButton { background-color: #89b4fa; color: #1e1e2e; font-weight: bold; padding: 8px; }");
    m_saveWorldBtn->setToolTip("Save the complete World (actors, layers, splines, PCG, terrain) to the project");
    connect(m_saveWorldBtn, &QPushButton::clicked, this, &Studio3DWidget::onSaveWorld);
    sceneLayout->addWidget(m_saveWorldBtn);

    m_loadWorldBtn = new QPushButton("Load World", sceneGroup);
    m_loadWorldBtn->setStyleSheet("QPushButton { background-color: #a6e3a1; color: #1e1e2e; font-weight: bold; padding: 8px; }");
    m_loadWorldBtn->setToolTip("Load the complete World from the project");
    connect(m_loadWorldBtn, &QPushButton::clicked, this, &Studio3DWidget::onLoadWorld);
    sceneLayout->addWidget(m_loadWorldBtn);

    sideLayout->addWidget(sceneGroup);

    // ─── Status ───
    auto* statusGroup = new QGroupBox("Status", sidePanel);
    auto* statusLayout = new QVBoxLayout(statusGroup);
    m_statusLabel = new QLabel("Ready", statusGroup);
    statusLayout->addWidget(m_statusLabel);
    sideLayout->addWidget(statusGroup);

    // ─── Log ───
    auto* logGroup = new QGroupBox("Log", sidePanel);
    auto* logLayout = new QVBoxLayout(logGroup);
    m_logEdit = new QTextEdit(logGroup);
    m_logEdit->setReadOnly(true);
    m_logEdit->setMaximumHeight(150);
    m_logEdit->setStyleSheet(
        "QTextEdit { background-color: #1e1e2e; color: #cdd6f4; "
        "font-family: Consolas; font-size: 11px; }");
    logLayout->addWidget(m_logEdit);
    sideLayout->addWidget(logGroup);

    sideLayout->addStretch();

    sidePanel->setMaximumWidth(QWIDGETSIZE_MAX);
    auto* controlsScroll = new QScrollArea(this);
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlsScroll->setFrameShape(QFrame::NoFrame);
    controlsScroll->setWidget(sidePanel);

    auto* rightPanel = new QSplitter(Qt::Vertical, this);
    rightPanel->setMinimumWidth(300);
    rightPanel->setMaximumWidth(380);
    rightPanel->addWidget(m_inspector);
    rightPanel->addWidget(controlsScroll);
    rightPanel->setStretchFactor(0, 3);
    rightPanel->setStretchFactor(1, 2);
    rightPanel->setSizes({420, 420});

    // ─── Main Unreal-style editor row ───
    auto* editorRow = new QSplitter(Qt::Horizontal, this);
    editorRow->setChildrenCollapsible(false);
    editorRow->addWidget(leftPanel);
    editorRow->addWidget(viewportGroup);
    editorRow->addWidget(rightPanel);
    editorRow->setStretchFactor(0, 0);
    editorRow->setStretchFactor(1, 1);
    editorRow->setStretchFactor(2, 0);
    editorRow->setSizes({250, 900, 350});
    mainLayout->addWidget(editorRow, 1);

    // ─── Bottom: Unreal-style Content Browser / Asset Library ───
    m_contentBrowser = new ContentBrowser(m_ogreWidget, this);
    m_contentBrowser->setMinimumHeight(150);
    m_contentBrowser->setMaximumHeight(230);
    QString assetDir;
    if (m_ctx && m_ctx->projects().hasProject()) {
        assetDir = QDir(m_ctx->projects().current().basePath).filePath("Assets");
    }
    if (assetDir.isEmpty() || !QDir(assetDir).exists()) {
        assetDir = QCoreApplication::applicationDirPath();
    }
    m_contentBrowser->setAssetDirectory(assetDir);
    connect(m_contentBrowser, &ContentBrowser::assetRequested,
            this, [this](const QString& path, const QString& type) {
        appendLog(QString("Asset selected (%1): %2").arg(type, path));
        m_statusLabel->setText(QString("Asset selected: %1").arg(QFileInfo(path).fileName()));
    });
    mainLayout->addWidget(m_contentBrowser, 0);

    setLayout(mainLayout);

    appendLog("3D Studio ready (Unreal-style editor layout)");
}

void Studio3DWidget::appendLog(const QString& msg)
{
    m_logEdit->append("[" + QTime::currentTime().toString("HH:mm:ss") + "] " + msg);
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
        if (it.hasNext()) return it.next();
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
    for (const QString& root : roots) {
        if (!QDir(root).exists()) continue;
        QDirIterator it(root, {"*.png", "*.tif", "*.tiff"},
                        QDir::Files, QDirIterator::Subdirectories);
        if (it.hasNext()) return it.next();
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

    m_statusLabel->setText("Loading terrain...");
    m_ogreWidget->loadTerrain(heightmapPath, albedoPath, terrainSize, heightScale);

    if (!m_ogreWidget->hasTerrain()) {
        m_statusLabel->setText("Terrain load failed");
        appendLog("Terrain load failed. Check the selected heightmap and OGRE log.");
        QMessageBox::warning(this, "Terrain Load Failed",
            QString("The selected heightmap could not be loaded:\n%1").arg(heightmapPath));
        return;
    }
    m_statusLabel->setText("Terrain loaded");
    appendLog("Terrain loaded successfully.");
}

void Studio3DWidget::onClearTerrain()
{
    m_ogreWidget->clearTerrain();
    m_statusLabel->setText("Terrain cleared");
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

    m_statusLabel->setText("Loading roads...");
    m_ogreWidget->loadRoads(xodrPath);

    m_statusLabel->setText("Roads loaded");
    appendLog("Roads loaded successfully.");
}

void Studio3DWidget::onAddBuilding()
{
    // Place a building at a random position near terrain center
    float x = (rand() % 200 - 100);
    float z = (rand() % 200 - 100);
    float y = m_heightScaleSlider->value() * 0.3f;
    QString id = m_ogreWidget->addActor(world::ActorType::Building, x, y, z, 0,
                                          10.0f, 15.0f, 10.0f, "buildings");
    appendLog("Added building: " + id);
    m_statusLabel->setText("Building added");
}

void Studio3DWidget::onAddTree()
{
    float x = (rand() % 400 - 200);
    float z = (rand() % 400 - 200);
    float y = m_heightScaleSlider->value() * 0.3f;
    QString id = m_ogreWidget->addActor(world::ActorType::Tree, x, y, z, 0,
                                          3.0f, 8.0f, 3.0f, "vegetation");
    appendLog("Added tree: " + id);
    m_statusLabel->setText("Tree added");
}

void Studio3DWidget::onAddBox()
{
    float x = (rand() % 200 - 100);
    float z = (rand() % 200 - 100);
    float y = m_heightScaleSlider->value() * 0.3f;
    QString id = m_ogreWidget->addActor(world::ActorType::Prop, x, y, z, 0,
                                          5.0f, 5.0f, 5.0f);
    appendLog("Added box: " + id);
    m_statusLabel->setText("Box added");
}

void Studio3DWidget::onClearObjects()
{
    m_ogreWidget->clearActors();
    appendLog("All objects cleared.");
    m_statusLabel->setText("Objects cleared");
}

void Studio3DWidget::onGenerateBuildings()
{
    m_ogreWidget->generateBuildings(100);
    appendLog("Generated 100 buildings via WorldBuilder.");
    m_statusLabel->setText("Buildings generated");
    m_outliner->refresh();
}

void Studio3DWidget::onGenerateVegetation()
{
    m_ogreWidget->generateVegetation(500, 0.01f);
    appendLog("Generated vegetation via PCG (scatter + height/slope filters).");
    m_statusLabel->setText("Vegetation generated");
    m_outliner->refresh();
}

void Studio3DWidget::onAddLake()
{
    float terrainSize = m_ogreWidget->world()->settings.terrainSize;
    if (terrainSize <= 0) terrainSize = 1000.0f;
    m_ogreWidget->addLake("Lake", 0, 0, terrainSize * 0.3f, terrainSize * 0.3f, 5.0f);
    appendLog("Added lake at world center.");
    m_statusLabel->setText("Lake added");
    m_outliner->refresh();
}

void Studio3DWidget::onAddSunLight()
{
    m_ogreWidget->addSunLight(45.0f, 60.0f, 3.0f);
    appendLog("Added sun light (yaw=45, pitch=60, intensity=3).");
    m_statusLabel->setText("Sun light added");
}

void Studio3DWidget::onAddSkyLight()
{
    m_ogreWidget->addSkyLight(1.0f);
    appendLog("Added sky light.");
    m_statusLabel->setText("Sky light added");
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
    m_statusLabel->setText("Scene saved");
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
    m_statusLabel->setText("Scene loaded");
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
    m_statusLabel->setText("Ready");
}

void Studio3DWidget::onResetCamera()
{
    m_ogreWidget->resetCamera();
    appendLog("Camera reset.");
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
        m_statusLabel->setText("World saved");
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
        m_statusLabel->setText("World loaded");
        if (m_outliner) m_outliner->refresh();
        if (m_layerPanel) m_layerPanel->refresh();
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
