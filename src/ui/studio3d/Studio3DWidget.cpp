#include "Studio3DWidget.hpp"
#include "OgreWidget.hpp"

#include "core/ApplicationContext.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QSlider>
#include <QProgressBar>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
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
    auto* mainLayout = new QHBoxLayout(this);

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
    mainLayout->addWidget(viewportGroup, 3);

    // ─── Right: Controls Panel ───
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
    mainLayout->addWidget(sidePanel, 1);

    setLayout(mainLayout);

    appendLog("3D Studio ready (OGRE-Next embedded)");
}

void Studio3DWidget::appendLog(const QString& msg)
{
    m_logEdit->append("[" + QTime::currentTime().toString("HH:mm:ss") + "] " + msg);
}

QString Studio3DWidget::findHeightmapInProject()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();

    QString terrainDir = m_ctx->projects().current().basePath + "/Terrain";

    // Check heightmaps subfolder
    QString hmDir = terrainDir + "/heightmaps";
    if (QDir(hmDir).exists()) {
        QStringList filters;
        filters << "*.png" << "*.tif" << "*.tiff";
        QStringList files = QDir(hmDir).entryList(filters, QDir::Files);
        if (!files.isEmpty()) return hmDir + "/" + files.first();
    }

    // Check Terrain root
    if (QDir(terrainDir).exists()) {
        QStringList filters;
        filters << "heightmap*.png" << "heightmap*.tif" << "*.tif" << "*.png";
        QStringList files = QDir(terrainDir).entryList(filters, QDir::Files);
        if (!files.isEmpty()) return terrainDir + "/" + files.first();
    }

    return QString();
}

QString Studio3DWidget::findAlbedoInProject()
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();

    QString terrainDir = m_ctx->projects().current().basePath + "/Terrain";

    // Check albedo subfolder
    QString albDir = terrainDir + "/albedo";
    if (QDir(albDir).exists()) {
        QStringList filters;
        filters << "*.png" << "*.tif" << "*.tiff";
        QStringList files = QDir(albDir).entryList(filters, QDir::Files);
        if (!files.isEmpty()) return albDir + "/" + files.first();
    }

    // Check Terrain root for albedo/satellite imagery
    if (QDir(terrainDir).exists()) {
        QStringList filters;
        filters << "albedo*.png" << "albedo*.tif" << "satellite*.png" << "imagery*.png";
        QStringList files = QDir(terrainDir).entryList(filters, QDir::Files);
        if (!files.isEmpty()) return terrainDir + "/" + files.first();
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
        QMessageBox::warning(this, "No Terrain Data",
            "No heightmap found in project.\n"
            "Download terrain in Terrain Studio first.");
        return;
    }

    QString albedoPath = findAlbedoInProject();

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
    QString id = m_ogreWidget->addObject("building", x, y, z, 0,
                                          10.0f, 15.0f, 10.0f);
    appendLog("Added building: " + id);
    m_statusLabel->setText("Building added");
}

void Studio3DWidget::onAddTree()
{
    float x = (rand() % 400 - 200);
    float z = (rand() % 400 - 200);
    float y = m_heightScaleSlider->value() * 0.3f;
    QString id = m_ogreWidget->addObject("tree", x, y, z, 0,
                                          3.0f, 8.0f, 3.0f);
    appendLog("Added tree: " + id);
    m_statusLabel->setText("Tree added");
}

void Studio3DWidget::onAddBox()
{
    float x = (rand() % 200 - 100);
    float z = (rand() % 200 - 100);
    float y = m_heightScaleSlider->value() * 0.3f;
    QString id = m_ogreWidget->addObject("box", x, y, z, 0,
                                          5.0f, 5.0f, 5.0f);
    appendLog("Added box: " + id);
    m_statusLabel->setText("Box added");
}

void Studio3DWidget::onClearObjects()
{
    m_ogreWidget->clearObjects();
    appendLog("All objects cleared.");
    m_statusLabel->setText("Objects cleared");
}

QString Studio3DWidget::sceneFilePath() const
{
    if (!m_ctx || !m_ctx->projects().hasProject()) return QString();
    return m_ctx->projects().current().basePath + "/Scene/scene3d.json";
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

    m_ogreWidget->loadScene(doc.object());
    appendLog("Scene loaded from: " + path);
    m_statusLabel->setText("Scene loaded");
}

void Studio3DWidget::onProjectOpened()
{
    // Auto-load the saved scene if it exists
    if (!m_ctx || !m_ctx->projects().hasProject()) return;

    QString path = sceneFilePath();
    if (QFile::exists(path)) {
        appendLog("Auto-loading saved scene...");
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            file.close();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (!doc.isNull()) {
                // Defer to allow OGRE to initialize first
                QTimer::singleShot(500, this, [this, doc]() {
                    m_ogreWidget->loadScene(doc.object());
                    appendLog("Scene auto-loaded from project.");
                });
            }
        }
    } else {
        // No saved scene — try auto-loading terrain + roads individually
        QTimer::singleShot(500, this, [this]() {
            QString hmPath = findHeightmapInProject();
            if (!hmPath.isEmpty()) {
                QString albPath = findAlbedoInProject();
                float heightScale = static_cast<float>(m_heightScaleSlider->value());
                float terrainSize = 4000.0f;
                if (m_ctx && m_ctx->projects().hasProject() &&
                    m_ctx->projects().current().bounds.valid) {
                    auto& b = m_ctx->projects().current().bounds;
                    double latMid = (b.minLat + b.maxLat) / 2.0;
                    double latM = (b.maxLat - b.minLat) * 111320.0;
                    double lonM = (b.maxLon - b.minLon) * 111320.0 *
                        cos(latMid * 3.14159265358979 / 180.0);
                    terrainSize = static_cast<float>(std::max(latM, lonM));
                    if (terrainSize < 10.0f) terrainSize = 4000.0f;
                }
                m_ogreWidget->loadTerrain(hmPath, albPath, terrainSize, heightScale);
                appendLog("Terrain auto-loaded from project.");
            }

            QString xodrPath = findXodrInProject();
            if (!xodrPath.isEmpty()) {
                m_ogreWidget->loadRoads(xodrPath);
                appendLog("Roads auto-loaded from project.");
            }
        });
    }
}

void Studio3DWidget::onProjectClosed()
{
    m_ogreWidget->clearObjects();
    m_ogreWidget->clearRoads();
    m_ogreWidget->clearTerrain();
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
