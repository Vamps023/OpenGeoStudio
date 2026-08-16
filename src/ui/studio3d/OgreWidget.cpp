#include "OgreWidget.hpp"

#include "../../core/world/Spline.hpp"
#include "../../core/world/PCGEngine.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QTimer>
#include <QDir>
#include <QFile>
#include "../../core/logger/Logger.hpp"
#include <QImage>
#include <QFileInfo>
#include <QDateTime>
#include <algorithm>
#include <limits>
#include "../terrain/DemDecoder.hpp"

// OGRE-Next headers
#include "OgreRoot.h"
#include "OgreAbiUtils.h"
#include "OgreWindow.h"
#include "OgreSceneManager.h"
#include "OgreCamera.h"
#include "OgreSceneNode.h"
#include "OgreViewport.h"
#include "OgreConfigFile.h"
#include "OgreHlmsManager.h"
#include "OgreHlmsPbs.h"
#include "OgreHlmsUnlit.h"
#include "OgreHlmsUnlitDatablock.h"
#include "OgreArchiveManager.h"
#include "Compositor/OgreCompositorManager2.h"
#include "OgreItem.h"
#include "OgreMesh2.h"
#include "OgreMeshManager2.h"
#include "OgreLight.h"
#include "OgreManualObject2.h"
#include "OgreTextureGpuManager.h"
#include "OgreTextureGpu.h"
#include "OgreResourceGroupManager.h"
#include "OgreStagingTexture.h"

// Math
#include "OgreVector3.h"
#include "OgreQuaternion.h"
#include "OgreMath.h"

// libOpenDRIVE for road loading
#include "OpenDriveMap.h"
#include "Road.h"
#include "RefLine.h"
#include "LaneSection.h"
#include "Lane.h"
#include "Math.hpp"

// ============================================================
// Construction / Destruction
// ============================================================

OgreWidget::OgreWidget(QWidget* parent)
    : QWindow(parent ? parent->windowHandle() : nullptr)
{
    m_container = QWidget::createWindowContainer(this, parent);
    setSurfaceType(QSurface::OpenGLSurface);
    m_container->setMouseTracking(true);
    m_container->setFocusPolicy(Qt::StrongFocus);
}

OgreWidget::~OgreWidget()
{
    shutdownOgre();
}

QWidget* OgreWidget::containerWidget()
{
    return m_container;
}

// ============================================================
// Qt event handling
// ============================================================

void OgreWidget::exposeEvent(QExposeEvent* event)
{
    Q_UNUSED(event);
    if (isExposed() && !m_initialized) {
        initOgre();
        setupScene();
        m_timerId = startTimer(16);
        m_initialized = true;
        if (m_hasPendingScene) {
            loadScene(m_pendingScene);
            m_hasPendingScene = false;
        }
    }
}

void OgreWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event);
    if (m_renderWindow) {
        m_renderWindow->requestResolution(width(), height());
        m_renderWindow->windowMovedOrResized();
    }
}

void OgreWidget::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event);
    render();
}

// ============================================================
// OGRE initialization
// ============================================================

void OgreWidget::initOgre()
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString pluginsCfg = appDir + "/plugins.cfg";

    QString debugLogPath = appDir + "/ogre_debug.log";
    QFile debugLog(debugLogPath);
    debugLog.open(QIODevice::WriteOnly | QIODevice::Append);
    auto logStep = [&](const QString& msg) {
        debugLog.write((QDateTime::currentDateTime().toString("hh:mm:ss.zzz ") + msg + "\n").toUtf8());
        debugLog.flush();
    };
    logStep("=== initOgre started ===");

    if (!QFile::exists(pluginsCfg)) {
        QString cfg = QString(
            "PluginFolder=%1\n"
            "Plugin=RenderSystem_Direct3D11\n"
            "Plugin=RenderSystem_GL3Plus\n"
            "Plugin=Plugin_ParticleFX\n"
        ).arg(appDir);
        QFile f(pluginsCfg);
        f.open(QIODevice::WriteOnly);
        f.write(cfg.toUtf8());
        f.close();
    }

    Ogre::AbiCookie abiCookie = Ogre::generateAbiCookie();
    m_root = OGRE_NEW Ogre::Root(&abiCookie,
                                 pluginsCfg.toStdString(),
                                 appDir.toStdString() + "/ogre.cfg",
                                 appDir.toStdString() + "/ogre.log",
                                 "OpenGeoStudio");

    Ogre::RenderSystemList rsList = m_root->getAvailableRenderers();
    for (auto* rs : rsList) {
        if (rs->getName().find("Direct3D11") != std::string::npos) {
            m_root->setRenderSystem(rs);
            break;
        }
    }
    if (!m_root->getRenderSystem()) {
        appLog().warn("No D3D11 render system found!");
        return;
    }

    m_root->initialise(false);
    logStep("Root initialised");

    Ogre::NameValuePairList params;
    params["externalWindowHandle"] = Ogre::StringConverter::toString(
        (size_t)winId());

    m_renderWindow = m_root->createRenderWindow("OgreWindow",
        width(), height(), false, &params);
    if (!m_renderWindow) {
        appLog().warn("[OGRE] Failed to create render window!");
        return;
    }
    logStep(QString("Render window created: %1x%2").arg(width()).arg(height()));

    m_sceneManager = m_root->createSceneManager(Ogre::ST_GENERIC, 2, "MainSM");
    if (!m_sceneManager) {
        appLog().warn("[OGRE] Failed to create scene manager!");
        return;
    }
    appLog().info("[OGRE] Scene manager created");

    m_camera = m_sceneManager->createCamera("MainCamera");
    if (!m_camera) {
        appLog().warn("[OGRE] Failed to create camera!");
        return;
    }
    m_camera->setNearClipDistance(0.5f);
    m_camera->setFarClipDistance(50000.0f);
    m_camera->setAutoAspectRatio(true);
    m_camera->setPosition(Ogre::Vector3(0, 300, 500));
    m_camera->lookAt(Ogre::Vector3(0, 0, 0));
    appLog().info("[OGRE] Camera created");

    Ogre::CompositorManager2* compositorManager = m_root->getCompositorManager2();
    if (!compositorManager) {
        appLog().warn("[OGRE] No compositor manager!");
        return;
    }
    Ogre::ColourValue bgColor(0.2f, 0.4f, 0.6f, 1.0f);
    if (!compositorManager->hasWorkspaceDefinition("MainWorkspace")) {
        compositorManager->createBasicWorkspaceDef("MainWorkspace", bgColor,
            Ogre::IdString());
    }
    compositorManager->addWorkspace(m_sceneManager,
        m_renderWindow->getTexture(), m_camera, "MainWorkspace", true);
    appLog().info("[OGRE] Compositor setup done");

    {
        QString hlmsFolder = appDir + "/";
        Ogre::String rootHlmsFolder = hlmsFolder.toStdString();

        Ogre::ArchiveManager& archMgr = Ogre::ArchiveManager::getSingleton();
        Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();

        {
            Ogre::String mainFolderPath;
            Ogre::StringVector libraryFoldersPaths;
            Ogre::HlmsPbs::getDefaultPaths(mainFolderPath, libraryFoldersPaths);

            Ogre::Archive* archivePbs = archMgr.load(
                rootHlmsFolder + mainFolderPath, "FileSystem", true);

            Ogre::ArchiveVec archivePbsLibraryFolders;
            for (const auto& libPath : libraryFoldersPaths) {
                Ogre::Archive* archiveLibrary = archMgr.load(
                    rootHlmsFolder + libPath, "FileSystem", true);
                archivePbsLibraryFolders.push_back(archiveLibrary);
            }

            Ogre::HlmsPbs* hlmsPbs = OGRE_NEW Ogre::HlmsPbs(
                archivePbs, &archivePbsLibraryFolders);
            hlmsManager->registerHlms(hlmsPbs);
        }

        {
            Ogre::String mainFolderPath;
            Ogre::StringVector libraryFoldersPaths;
            Ogre::HlmsUnlit::getDefaultPaths(mainFolderPath, libraryFoldersPaths);

            Ogre::Archive* archiveUnlit = archMgr.load(
                rootHlmsFolder + mainFolderPath, "FileSystem", true);

            Ogre::ArchiveVec archiveUnlitLibraryFolders;
            for (const auto& libPath : libraryFoldersPaths) {
                Ogre::Archive* archiveLibrary = archMgr.load(
                    rootHlmsFolder + libPath, "FileSystem", true);
                archiveUnlitLibraryFolders.push_back(archiveLibrary);
            }

            Ogre::HlmsUnlit* hlmsUnlit = OGRE_NEW Ogre::HlmsUnlit(
                archiveUnlit, &archiveUnlitLibraryFolders);
            hlmsManager->registerHlms(hlmsUnlit);
        }
    }
    logStep("Hlms setup complete");

    // Create grid and gizmo
    createGrid();
    showGrid(m_gridVisible);
    createGizmo();

    appLog().info("OGRE-Next initialized successfully");
}

void OgreWidget::setupScene()
{
    appLog().info("[OGRE] setupScene: ready");
}

// ============================================================
// Terrain loading
// ============================================================

void OgreWidget::loadTerrain(const QString& heightmapPath, const QString& albedoPath,
                              float terrainSize, float heightScale)
{
    m_heightmapPath = heightmapPath;
    m_albedoPath = albedoPath;
    m_terrainSize = terrainSize;
    m_heightScale = heightScale;

    if (!m_initialized) {
        appLog().info("OGRE not initialized yet, terrain will load after init");
        return;
    }

    if (!QFile::exists(heightmapPath)) {
        appLog().warn("Heightmap not found:", heightmapPath);
        return;
    }

    clearTerrain();

    QImage heightImg;
    bool demLoaded = false;

    // QImage cannot read Float32/Int16 GeoTIFFs. Decode those with DemDecoder.
    if (heightmapPath.endsWith(".tif", Qt::CaseInsensitive) ||
        heightmapPath.endsWith(".tiff", Qt::CaseInsensitive)) {
        QFile f(heightmapPath);
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray data = f.readAll();
            f.close();
            terrain::DemTile dem = terrain::DemDecoder::decodeAuto(data, "dem");
            if (dem.valid && dem.width > 0 && dem.height > 0) {
                float zMin = std::numeric_limits<float>::max();
                float zMax = std::numeric_limits<float>::lowest();
                for (float e : dem.elevations) {
                    if (e != dem.nodataValue) {
                        zMin = std::min(zMin, e);
                        zMax = std::max(zMax, e);
                    }
                }
                if (zMax <= zMin) { zMin = 0.0f; zMax = 1.0f; }
                float range = zMax - zMin;

                heightImg = QImage(dem.width, dem.height, QImage::Format_Grayscale8);
                for (int y = 0; y < dem.height; ++y) {
                    uint8_t* line = heightImg.scanLine(y);
                    for (int x = 0; x < dem.width; ++x) {
                        float e = dem.elevations[y * dem.width + x];
                        if (e == dem.nodataValue) e = zMin;
                        int v = qBound(0, static_cast<int>(((e - zMin) / range) * 255.0f), 255);
                        line[x] = static_cast<uint8_t>(v);
                    }
                }
                demLoaded = true;
                appLog().info("Decoded GeoTIFF heightmap:", dem.width, "x", dem.height);
            } else {
                appLog().warn("Failed to decode GeoTIFF heightmap:", heightmapPath);
            }
        } else {
            appLog().warn("Failed to open GeoTIFF heightmap:", heightmapPath);
        }
    }

    if (!demLoaded) {
        heightImg = QImage(heightmapPath);
        if (heightImg.isNull()) {
            appLog().warn("Failed to load heightmap image:", heightmapPath);
            return;
        }
    }

    heightImg = heightImg.convertToFormat(QImage::Format_Grayscale8);
    int hmW = heightImg.width();
    int hmH = heightImg.height();
    m_heightmapImage = heightImg;
    m_hasHeightmap = true;
    appLog().info("Heightmap loaded:", hmW, "x", hmH);

    const int maxGridSize = 256;
    int gridW = std::min(hmW, maxGridSize);
    int gridH = std::min(hmH, maxGridSize);

    Ogre::ManualObject* manual = m_sceneManager->createManualObject();
    manual->setName("TerrainMesh");

    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsUnlit = hlmsManager->getHlms(Ogre::HLMS_UNLIT);

    Ogre::String datablockName = "TerrainDatablock";
    Ogre::HlmsDatablock* dbBase = hlmsUnlit->getDatablock(Ogre::IdString(datablockName));
    if (!dbBase) {
        dbBase = hlmsUnlit->createDatablock(
            Ogre::IdString(datablockName), datablockName,
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
    }
    Ogre::HlmsUnlitDatablock* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(dbBase);

    if (!albedoPath.isEmpty() && QFile::exists(albedoPath) &&
        !albedoPath.endsWith(".tif", Qt::CaseInsensitive) &&
        !albedoPath.endsWith(".tiff", Qt::CaseInsensitive)) {
        QString albDir = QFileInfo(albedoPath).absolutePath();
        QString albName = QFileInfo(albedoPath).fileName();
        Ogre::String ogreAlbDir = albDir.toStdString();
        Ogre::String ogreAlbName = albName.toStdString();

        Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
        if (!rgm.resourceGroupExists("TerrainTextures")) {
            rgm.createResourceGroup("TerrainTextures");
            rgm.addResourceLocation(ogreAlbDir, "FileSystem", "TerrainTextures");
            rgm.initialiseResourceGroup("TerrainTextures", false);
        }

        Ogre::TextureGpuManager* texMgr = m_root->getRenderSystem()->getTextureGpuManager();
        Ogre::TextureGpu* albedoTex = texMgr->createOrRetrieveTexture(
            ogreAlbName,
            Ogre::GpuPageOutStrategy::Discard,
            Ogre::TextureFlags::PrefersLoadingFromFileAsSRGB,
            Ogre::TextureTypes::Type2D,
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

        datablock->setTexture(0, albedoTex);
        appLog().info("Albedo texture loaded:", albedoPath);
    } else {
        datablock->setColour(Ogre::ColourValue(0.3f, 0.5f, 0.3f, 1.0f));
    }

    manual->begin(datablockName, Ogre::OT_TRIANGLE_LIST);

    float halfSize = terrainSize * 0.5f;
    float xStep = terrainSize / (gridW - 1);
    float zStep = terrainSize / (gridH - 1);

    for (int gz = 0; gz < gridH; gz++) {
        for (int gx = 0; gx < gridW; gx++) {
            int hx = (gx * (hmW - 1)) / (gridW - 1);
            int hy = (gz * (hmH - 1)) / (gridH - 1);
            uchar* pixel = heightImg.scanLine(hy);
            float h = pixel[hx] / 255.0f;
            float y = h * heightScale;

            float x = -halfSize + gx * xStep;
            float z = -halfSize + gz * zStep;

            float u = (float)gx / (gridW - 1);
            float v = (float)gz / (gridH - 1);

            manual->position(x, y, z);
            manual->textureCoord(u, v);
            manual->normal(0.0f, 1.0f, 0.0f);
        }
    }

    for (int gz = 0; gz < gridH - 1; gz++) {
        for (int gx = 0; gx < gridW - 1; gx++) {
            uint32_t v0 = gz * gridW + gx;
            uint32_t v1 = gz * gridW + gx + 1;
            uint32_t v2 = (gz + 1) * gridW + gx;
            uint32_t v3 = (gz + 1) * gridW + gx + 1;

            manual->index(v0);
            manual->index(v2);
            manual->index(v1);
            manual->index(v1);
            manual->index(v2);
            manual->index(v3);
        }
    }

    manual->end();

    Ogre::String meshName = "TerrainMesh_" + Ogre::StringConverter::toString(
        (size_t)this);
    Ogre::MeshPtr mesh = manual->convertToMesh(
        meshName,
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    m_terrainItem = m_sceneManager->createItem(
        mesh,
        Ogre::SCENE_DYNAMIC);
    m_terrainNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    m_terrainNode->attachObject(m_terrainItem);

    m_sceneManager->destroyManualObject(manual);

    appLog().info("Terrain mesh created:", gridW, "x", gridH, "verts, size:", terrainSize, "m, heightScale:", heightScale, "m");

    resetCamera();
}

void OgreWidget::clearTerrain()
{
    if (m_terrainItem) {
        if (m_terrainNode) {
            m_terrainNode->detachObject(m_terrainItem);
            m_sceneManager->destroySceneNode(m_terrainNode);
            m_terrainNode = nullptr;
        }
        m_sceneManager->destroyItem(m_terrainItem);
        m_terrainItem = nullptr;
    }
    m_hasHeightmap = false;
}

float OgreWidget::sampleTerrainHeight(float x, float z) const
{
    if (!m_hasHeightmap || m_heightmapImage.isNull()) return 0;
    float halfSize = m_terrainSize * 0.5f;
    float u = (x + halfSize) / m_terrainSize;
    float v = (z + halfSize) / m_terrainSize;
    if (u < 0 || u > 1 || v < 0 || v > 1) return 0;
    int hx = int(u * (m_heightmapImage.width() - 1));
    int hy = int(v * (m_heightmapImage.height() - 1));
    const uchar* pixel = m_heightmapImage.constScanLine(hy);
    return (pixel[hx] / 255.0f) * m_heightScale;
}

// ============================================================
// Road loading
// ============================================================

void OgreWidget::loadRoads(const QString& xodrPath)
{
    m_xodrPath = xodrPath;

    if (!m_initialized) {
        appLog().info("OGRE not initialized yet, roads will load after init");
        return;
    }

    if (!QFile::exists(xodrPath)) {
        appLog().warn("XODR file not found:", xodrPath);
        return;
    }

    clearRoads();

    odr::OpenDriveMap odrMap(xodrPath.toStdString());
    auto roads = odrMap.get_roads();

    if (roads.empty()) {
        appLog().warn("No roads found in XODR file:", xodrPath);
        return;
    }

    appLog().info("Loading", roads.size(), "roads from", xodrPath);

    Ogre::ManualObject* manual = m_sceneManager->createManualObject();
    manual->setName("RoadMesh");

    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsUnlit = hlmsManager->getHlms(Ogre::HLMS_UNLIT);
    Ogre::String dblockName = "RoadDatablock";
    Ogre::HlmsDatablock* dbBase = hlmsUnlit->getDatablock(Ogre::IdString(dblockName));
    if (!dbBase) {
        dbBase = hlmsUnlit->createDatablock(
            Ogre::IdString(dblockName), dblockName,
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
    }
    Ogre::HlmsUnlitDatablock* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(dbBase);
    datablock->setColour(Ogre::ColourValue(0.2f, 0.2f, 0.22f, 1.0f));

    manual->begin(dblockName, Ogre::OT_TRIANGLE_LIST);

    const float roadYOffset = 2.0f;
    const double sampleEps = 1.0;
    const float defaultHalfWidth = 4.0f;

    uint32_t vertexOffset = 0;

    for (const auto& road : roads) {
        double roadLength = road.length;
        if (roadLength <= 0) continue;

        auto laneSections = road.get_lanesections();
        auto refLine = road.ref_line;

        std::vector<double> sValues;
        for (double s = 0; s <= roadLength; s += sampleEps) {
            sValues.push_back(s);
        }
        if (sValues.back() < roadLength) {
            sValues.push_back(roadLength);
        }

        for (size_t i = 0; i + 1 < sValues.size(); i++) {
            double s0 = sValues[i];
            double s1 = sValues[i + 1];

            odr::Vec3D p0 = refLine.get_xyz(s0);
            odr::Vec3D p1 = refLine.get_xyz(s1);
            double hdg0 = refLine.get_hdg(s0);

            float halfWidth = defaultHalfWidth;
            if (!laneSections.empty()) {
                double lanesection_s0 = road.get_lanesection_s0(s0);
                auto lsIt = std::find_if(laneSections.begin(), laneSections.end(),
                    [&](const odr::LaneSection& ls) { return ls.s0 == lanesection_s0; });
                if (lsIt != laneSections.end()) {
                    double totalWidth = 0;
                    for (const auto& lanePair : lsIt->id_to_lane) {
                        if (lanePair.first != 0) {
                            totalWidth += lanePair.second.lane_width.get(s0 - lsIt->s0);
                        }
                    }
                    halfWidth = static_cast<float>(totalWidth * 0.5);
                    if (halfWidth < 1.0f) halfWidth = defaultHalfWidth;
                }
            }

            float cosH = static_cast<float>(cos(hdg0));
            float sinH = static_cast<float>(sin(hdg0));
            float perpX = sinH;
            float perpZ = -cosH;

            float lx0 = static_cast<float>(p0[0]) + perpX * halfWidth;
            float lz0 = static_cast<float>(p0[2]) + perpZ * halfWidth;
            float ly0 = static_cast<float>(p0[1]) + roadYOffset;
            float rx0 = static_cast<float>(p0[0]) - perpX * halfWidth;
            float rz0 = static_cast<float>(p0[2]) - perpZ * halfWidth;
            float ry0 = static_cast<float>(p0[1]) + roadYOffset;
            float lx1 = static_cast<float>(p1[0]) + perpX * halfWidth;
            float lz1 = static_cast<float>(p1[2]) + perpZ * halfWidth;
            float ly1 = static_cast<float>(p1[1]) + roadYOffset;
            float rx1 = static_cast<float>(p1[0]) - perpX * halfWidth;
            float rz1 = static_cast<float>(p1[2]) - perpZ * halfWidth;
            float ry1 = static_cast<float>(p1[1]) + roadYOffset;

            manual->position(lx0, ly0, lz0);
            manual->normal(0.0f, 1.0f, 0.0f);
            manual->textureCoord(0.0f, 0.0f);

            manual->position(rx0, ry0, rz0);
            manual->normal(0.0f, 1.0f, 0.0f);
            manual->textureCoord(1.0f, 0.0f);

            manual->position(lx1, ly1, lz1);
            manual->normal(0.0f, 1.0f, 0.0f);
            manual->textureCoord(0.0f, 1.0f);

            manual->position(rx1, ry1, rz1);
            manual->normal(0.0f, 1.0f, 0.0f);
            manual->textureCoord(1.0f, 1.0f);

            manual->index(vertexOffset + 0);
            manual->index(vertexOffset + 1);
            manual->index(vertexOffset + 2);
            manual->index(vertexOffset + 1);
            manual->index(vertexOffset + 3);
            manual->index(vertexOffset + 2);

            vertexOffset += 4;
        }
    }

    manual->end();

    Ogre::String meshName = "RoadMesh_" + Ogre::StringConverter::toString((size_t)this);
    Ogre::MeshPtr mesh = manual->convertToMesh(
        meshName,
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    m_roadItem = m_sceneManager->createItem(mesh, Ogre::SCENE_DYNAMIC);
    m_roadNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    m_roadNode->attachObject(m_roadItem);

    m_sceneManager->destroyManualObject(manual);

    appLog().info("Road mesh created with", vertexOffset, "vertices");
}

void OgreWidget::clearRoads()
{
    if (m_roadItem) {
        if (m_roadNode) {
            m_roadNode->detachObject(m_roadItem);
            m_sceneManager->destroySceneNode(m_roadNode);
            m_roadNode = nullptr;
        }
        m_sceneManager->destroyItem(m_roadItem);
        m_roadItem = nullptr;
    }
}

// ============================================================
// Actor management (delegates to World model + syncs rendering)
// ============================================================

QString OgreWidget::addActor(world::ActorType type, float x, float y, float z,
                              float rotY, float sx, float sy, float sz,
                              const QString& layerId)
{
    world::Actor* actor = m_world.addActor(type, "Actor", layerId);
    actor->transform.posX = x;
    actor->transform.posY = y;
    actor->transform.posZ = z;
    actor->transform.rotY = rotY;
    actor->transform.scaleX = sx;
    actor->transform.scaleY = sy;
    actor->transform.scaleZ = sz;

    // Set default color based on type
    switch (type) {
    case world::ActorType::Building:
        actor->colorR = 0.6f; actor->colorG = 0.6f; actor->colorB = 0.6f; break;
    case world::ActorType::Tree:
        actor->colorR = 0.2f; actor->colorG = 0.5f; actor->colorB = 0.2f; break;
    case world::ActorType::Water:
        actor->colorR = 0.2f; actor->colorG = 0.4f; actor->colorB = 0.6f; break;
    default:
        break;
    }

    rebuildActor(*actor);
    emit actorAdded(actor->id);
    emit sceneChanged();
    return actor->id;
}

void OgreWidget::removeActor(const QString& id)
{
    // Remove render entry
    auto it = m_actorRenders.find(id);
    if (it != m_actorRenders.end()) {
        if (it->second.node) {
            it->second.node->detachObject(it->second.item);
            m_sceneManager->destroySceneNode(it->second.node);
        }
        if (it->second.item)
            m_sceneManager->destroyItem(it->second.item);
        m_actorRenders.erase(it);
    }
    m_world.removeActor(id);
    m_world.selectedActorIds.remove(id);
    emit actorRemoved(id);
    emit sceneChanged();
}

void OgreWidget::clearActors()
{
    clearActorRenderables();
    m_world.actors.clear();
    m_world.selectedActorIds.clear();
    emit sceneChanged();
}

void OgreWidget::updateActorTransform(const QString& id, float x, float y, float z,
                                       float rotY, float sx, float sy, float sz)
{
    world::Actor* a = m_world.findActor(id);
    if (!a) return;
    a->transform.posX = x;
    a->transform.posY = y;
    a->transform.posZ = z;
    a->transform.rotY = rotY;
    a->transform.scaleX = sx;
    a->transform.scaleY = sy;
    a->transform.scaleZ = sz;
    a->touch();
    rebuildActor(*a);
    emit actorTransformed(id);
    emit sceneChanged();
}

void OgreWidget::updateActorVisibility(const QString& id, bool visible)
{
    world::Actor* a = m_world.findActor(id);
    if (!a) return;
    a->visible = visible;
    a->touch();
    auto it = m_actorRenders.find(id);
    if (it != m_actorRenders.end() && it->second.node)
        it->second.node->setVisible(visible && isLayerVisible(a->layerId));
    emit sceneChanged();
}

void OgreWidget::updateActorLayer(const QString& id, const QString& layerId)
{
    world::Actor* a = m_world.findActor(id);
    if (!a) return;
    a->layerId = layerId;
    a->touch();
    auto it = m_actorRenders.find(id);
    if (it != m_actorRenders.end() && it->second.node)
        it->second.node->setVisible(a->visible && isLayerVisible(layerId));
    emit sceneChanged();
}

void OgreWidget::renameActor(const QString& id, const QString& newName)
{
    world::Actor* a = m_world.findActor(id);
    if (!a) return;
    a->name = newName;
    a->touch();
    emit sceneChanged();
}

// ============================================================
// Actor rendering — creates a simple colored box for each actor
// ============================================================

void OgreWidget::rebuildActor(const world::Actor& actor)
{
    if (!m_initialized || !m_sceneManager) return;

    // Remove existing render entry
    auto it = m_actorRenders.find(actor.id);
    if (it != m_actorRenders.end()) {
        if (it->second.node) {
            it->second.node->detachObject(it->second.item);
            m_sceneManager->destroySceneNode(it->second.node);
        }
        if (it->second.item)
            m_sceneManager->destroyItem(it->second.item);
        m_actorRenders.erase(it);
    }

    // Create a simple box mesh for the actor
    Ogre::ManualObject* manual = m_sceneManager->createManualObject();

    // Create or get datablock for this actor's color
    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsUnlit = hlmsManager->getHlms(Ogre::HLMS_UNLIT);
    Ogre::String dblockName = "ActorDatablock_" + actor.id.toStdString();
    Ogre::HlmsDatablock* dbBase = hlmsUnlit->getDatablock(Ogre::IdString(dblockName));
    if (!dbBase) {
        dbBase = hlmsUnlit->createDatablock(
            Ogre::IdString(dblockName), dblockName,
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
    }
    Ogre::HlmsUnlitDatablock* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(dbBase);
    datablock->setColour(Ogre::ColourValue(actor.colorR, actor.colorG, actor.colorB, actor.colorA));

    // Build a unit cube centered at origin
    manual->begin(dblockName, Ogre::OT_TRIANGLE_LIST);

    // 8 vertices of a unit cube (-0.5 to 0.5)
    // Front face
    manual->position(-0.5f, -0.5f, 0.5f); manual->normal(0, 0, 1);
    manual->position(0.5f, -0.5f, 0.5f); manual->normal(0, 0, 1);
    manual->position(0.5f, 0.5f, 0.5f); manual->normal(0, 0, 1);
    manual->position(-0.5f, 0.5f, 0.5f); manual->normal(0, 0, 1);
    // Back face
    manual->position(-0.5f, -0.5f, -0.5f); manual->normal(0, 0, -1);
    manual->position(0.5f, -0.5f, -0.5f); manual->normal(0, 0, -1);
    manual->position(0.5f, 0.5f, -0.5f); manual->normal(0, 0, -1);
    manual->position(-0.5f, 0.5f, -0.5f); manual->normal(0, 0, -1);

    // Front
    manual->index(0); manual->index(1); manual->index(2);
    manual->index(0); manual->index(2); manual->index(3);
    // Back
    manual->index(5); manual->index(4); manual->index(7);
    manual->index(5); manual->index(7); manual->index(6);
    // Left
    manual->index(4); manual->index(0); manual->index(3);
    manual->index(4); manual->index(3); manual->index(7);
    // Right
    manual->index(1); manual->index(5); manual->index(6);
    manual->index(1); manual->index(6); manual->index(2);
    // Top
    manual->index(3); manual->index(2); manual->index(6);
    manual->index(3); manual->index(6); manual->index(7);
    // Bottom
    manual->index(4); manual->index(5); manual->index(1);
    manual->index(4); manual->index(1); manual->index(0);

    manual->end();

    Ogre::String meshName = "ActorMesh_" + actor.id.toStdString();
    Ogre::MeshPtr mesh = manual->convertToMesh(
        meshName,
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    Ogre::Item* item = m_sceneManager->createItem(mesh, Ogre::SCENE_DYNAMIC);
    Ogre::SceneNode* node = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    node->attachObject(item);

    // Apply transform
    node->setPosition(Ogre::Vector3(actor.transform.posX, actor.transform.posY, actor.transform.posZ));
    Ogre::Radian rotY(Ogre::Degree(actor.transform.rotY));
    node->setOrientation(Ogre::Quaternion(rotY, Ogre::Vector3::UNIT_Y));
    node->setScale(Ogre::Vector3(actor.transform.scaleX, actor.transform.scaleY, actor.transform.scaleZ));
    node->setVisible(actor.visible && isLayerVisible(actor.layerId));

    m_sceneManager->destroyManualObject(manual);

    ActorRenderEntry entry;
    entry.actorId = actor.id;
    entry.item = item;
    entry.node = node;
    m_actorRenders[actor.id] = entry;
}

void OgreWidget::clearActorRenderables()
{
    for (auto& pair : m_actorRenders) {
        if (pair.second.node) {
            pair.second.node->detachObject(pair.second.item);
            m_sceneManager->destroySceneNode(pair.second.node);
        }
        if (pair.second.item)
            m_sceneManager->destroyItem(pair.second.item);
    }
    m_actorRenders.clear();
}

void OgreWidget::syncAllActors()
{
    clearActorRenderables();
    for (const auto& actor : m_world.actors)
        rebuildActor(actor);
}

// ============================================================
// World authoring — WorldBuilder-driven procedural generation
// ============================================================

// Copy the WorldBuilder's world into the render world and rebuild renderables.
void OgreWidget::syncBuilderToWorld()
{
    m_world = m_builder.world;
    syncAllActors();
    emit worldChanged();
    emit sceneChanged();
}

void OgreWidget::generateBuildings(int count)
{
    // Bind terrain height sampling so buildings sit on the ground
    m_builder.sampleHeight = [this](float x, float z) {
        return sampleTerrainHeight(x, z);
    };
    if (m_builder.world.settings.name.isEmpty()) {
        m_builder.createWorld("3D Studio World", m_terrainSize);
    }
    m_builder.generateBuildings(count);
    syncBuilderToWorld();
    appLog().info("[OgreWidget] Generated", count, "buildings");
}

void OgreWidget::generateVegetation(int count, float density)
{
    m_builder.sampleHeight = [this](float x, float z) {
        return sampleTerrainHeight(x, z);
    };
    m_builder.sampleSlope = [this](float x, float z) {
        Q_UNUSED(x); Q_UNUSED(z);
        return 0.0f;
    };
    if (m_builder.world.settings.name.isEmpty()) {
        m_builder.createWorld("3D Studio World", m_terrainSize);
    }
    QString graphName = "Vegetation";
    m_builder.createVegetationPCG(graphName, "", density, m_terrainSize);
    m_builder.generateVegetation(graphName);
    syncBuilderToWorld();
    appLog().info("[OgreWidget] Generated vegetation, total actors:", m_world.actorCount());
}

void OgreWidget::addLake(const QString& name, float x, float z,
                         float sizeX, float sizeZ, float level)
{
    if (m_builder.world.settings.name.isEmpty()) {
        m_builder.createWorld("3D Studio World", m_terrainSize);
    }
    m_builder.addLake(name, x, z, sizeX, sizeZ, level);
    syncBuilderToWorld();
    appLog().info("[OgreWidget] Added lake:", name);
}

void OgreWidget::addSunLight(float yaw, float pitch, float intensity)
{
    if (m_builder.world.settings.name.isEmpty()) {
        m_builder.createWorld("3D Studio World", m_terrainSize);
    }
    m_builder.addSunLight(yaw, pitch, intensity);
    syncBuilderToWorld();
    appLog().info("[OgreWidget] Added sun light");
}

void OgreWidget::addSkyLight(float intensity)
{
    if (m_builder.world.settings.name.isEmpty()) {
        m_builder.createWorld("3D Studio World", m_terrainSize);
    }
    m_builder.addSkyLight(intensity);
    syncBuilderToWorld();
    appLog().info("[OgreWidget] Added sky light");
}

void OgreWidget::buildRoadSpline(const QString& name,
                                 const QList<QPair<float, float>>& points,
                                 float width, int laneCount)
{
    if (m_builder.world.settings.name.isEmpty()) {
        m_builder.createWorld("3D Studio World", m_terrainSize);
    }
    m_builder.createRoad(name, points, width, laneCount);
    syncBuilderToWorld();
    appLog().info("[OgreWidget] Built road spline:", name);
}

// ============================================================
// Layer management
// ============================================================

void OgreWidget::addLayer(const world::Layer& layer)
{
    // Check if layer already exists
    if (m_world.findLayer(layer.id)) return;
    world::Layer* l = m_world.addLayer(layer.name);
    l->visible = layer.visible;
    l->locked = layer.locked;
    l->colorR = layer.colorR;
    l->colorG = layer.colorG;
    l->colorB = layer.colorB;
    emit sceneChanged();
}

void OgreWidget::removeLayer(const QString& layerId)
{
    m_world.removeLayer(layerId);
    // Update visibility of actors in that layer (they were moved to default)
    syncAllActors();
    emit sceneChanged();
}

std::vector<world::Layer> OgreWidget::getLayers() const
{
    std::vector<world::Layer> result;
    for (const auto& l : m_world.layers)
        result.push_back(l);
    return result;
}

void OgreWidget::setLayerVisible(const QString& layerId, bool visible)
{
    m_world.setLayerVisible(layerId, visible);
    // Update visibility of all actors in this layer
    for (const auto& actor : m_world.actors) {
        if (actor.layerId == layerId) {
            auto it = m_actorRenders.find(actor.id);
            if (it != m_actorRenders.end() && it->second.node)
                it->second.node->setVisible(visible && actor.visible);
        }
    }
    emit sceneChanged();
}

void OgreWidget::setLayerLocked(const QString& layerId, bool locked)
{
    m_world.setLayerLocked(layerId, locked);
    emit sceneChanged();
}

bool OgreWidget::isLayerVisible(const QString& layerId) const
{
    return m_world.isLayerVisible(layerId);
}

bool OgreWidget::isLayerLocked(const QString& layerId) const
{
    return m_world.isLayerLocked(layerId);
}

// ============================================================
// Selection
// ============================================================

void OgreWidget::selectActor(const QString& id)
{
    m_world.selectOnly(id);
    updateGizmo();
    emit actorSelected(id);
}

void OgreWidget::deselectAll()
{
    m_world.clearSelection();
    updateGizmo();
    emit actorSelected(QString());
}

// ============================================================
// Transform mode
// ============================================================

void OgreWidget::setTransformMode(TransformMode mode)
{
    m_transformMode = mode;
    updateGizmo();
}

// ============================================================
// Grid
// ============================================================

void OgreWidget::setGridVisible(bool visible)
{
    m_gridVisible = visible;
    showGrid(visible);
}

void OgreWidget::setSnapEnabled(bool enabled)
{
    m_snapEnabled = enabled;
    m_world.settings.snapEnabled = enabled;
}

void OgreWidget::setSnapSize(float size)
{
    m_snapSize = size;
    m_world.settings.snapSize = size;
}

void OgreWidget::createGrid()
{
    if (!m_sceneManager) return;

    m_gridManual = m_sceneManager->createManualObject();
    m_gridManual->setName("Grid");

    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsUnlit = hlmsManager->getHlms(Ogre::HLMS_UNLIT);
    Ogre::String dblockName = "GridDatablock";
    Ogre::HlmsDatablock* dbBase = hlmsUnlit->getDatablock(Ogre::IdString(dblockName));
    if (!dbBase) {
        dbBase = hlmsUnlit->createDatablock(
            Ogre::IdString(dblockName), dblockName,
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
    }
    auto* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(dbBase);
    datablock->setColour(Ogre::ColourValue(0.5f, 0.5f, 0.5f, 0.5f));

    m_gridManual->begin(dblockName, Ogre::OT_LINE_LIST);

    float gridSize = 100.0f;
    int divisions = 40;
    float half = gridSize * divisions * 0.5f;
    float step = gridSize;

    uint32_t idx = 0;
    for (int i = 0; i <= divisions; i++) {
        float x = -half + i * step;
        // Line along Z
        m_gridManual->position(x, 0, -half);
        m_gridManual->position(x, 0, half);
        m_gridManual->index(idx); m_gridManual->index(idx + 1);
        idx += 2;
        // Line along X
        m_gridManual->position(-half, 0, x);
        m_gridManual->position(half, 0, x);
        m_gridManual->index(idx); m_gridManual->index(idx + 1);
        idx += 2;
    }

    m_gridManual->end();

    m_gridNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    m_gridNode->attachObject(m_gridManual);
}

void OgreWidget::destroyGrid()
{
    if (m_gridManual) {
        if (m_gridNode) {
            m_gridNode->detachObject(m_gridManual);
            m_sceneManager->destroySceneNode(m_gridNode);
            m_gridNode = nullptr;
        }
        m_sceneManager->destroyManualObject(m_gridManual);
        m_gridManual = nullptr;
    }
}

void OgreWidget::showGrid(bool show)
{
    if (m_gridNode) m_gridNode->setVisible(show);
}

// ============================================================
// Gizmo
// ============================================================

void OgreWidget::createGizmo()
{
    if (!m_sceneManager) return;

    m_gizmoNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();

    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsUnlit = hlmsManager->getHlms(Ogre::HLMS_UNLIT);

    // Move gizmo — 3 axis lines (with indices)
    m_gizmoMove = m_sceneManager->createManualObject();
    {
        Ogre::String dbName = "GizmoMoveDatablock";
        Ogre::HlmsDatablock* db = hlmsUnlit->getDatablock(Ogre::IdString(dbName));
        if (!db) {
            db = hlmsUnlit->createDatablock(
                Ogre::IdString(dbName), dbName,
                Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
        }
        auto* dblock = static_cast<Ogre::HlmsUnlitDatablock*>(db);
        dblock->setColour(Ogre::ColourValue(1, 1, 1, 1));

        m_gizmoMove->begin(dbName, Ogre::OT_LINE_LIST);
        // X axis (red)
        m_gizmoMove->position(0, 0, 0);
        m_gizmoMove->position(50, 0, 0);
        m_gizmoMove->index(0); m_gizmoMove->index(1);
        // Y axis (green)
        m_gizmoMove->position(0, 0, 0);
        m_gizmoMove->position(0, 50, 0);
        m_gizmoMove->index(2); m_gizmoMove->index(3);
        // Z axis (blue)
        m_gizmoMove->position(0, 0, 0);
        m_gizmoMove->position(0, 0, 50);
        m_gizmoMove->index(4); m_gizmoMove->index(5);
        m_gizmoMove->end();
    }
    m_gizmoNode->attachObject(m_gizmoMove);

    // Rotate gizmo — 3 circles (simplified as line segments)
    m_gizmoRotate = m_sceneManager->createManualObject();
    {
        Ogre::String dbName = "GizmoRotateDatablock";
        Ogre::HlmsDatablock* db = hlmsUnlit->getDatablock(Ogre::IdString(dbName));
        if (!db) {
            db = hlmsUnlit->createDatablock(
                Ogre::IdString(dbName), dbName,
                Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
        }
        auto* dblock = static_cast<Ogre::HlmsUnlitDatablock*>(db);
        dblock->setColour(Ogre::ColourValue(1, 1, 0, 1));

        m_gizmoRotate->begin(dbName, Ogre::OT_LINE_LIST);
        // Simple circle approximation in XZ plane
        uint32_t idx = 0;
        for (int i = 0; i < 32; i++) {
            float a0 = float(i) / 32 * 6.2831853f;
            float a1 = float(i + 1) / 32 * 6.2831853f;
            m_gizmoRotate->position(cos(a0) * 40, 0, sin(a0) * 40);
            m_gizmoRotate->position(cos(a1) * 40, 0, sin(a1) * 40);
            m_gizmoRotate->index(idx); m_gizmoRotate->index(idx + 1);
            idx += 2;
        }
        m_gizmoRotate->end();
    }
    m_gizmoNode->attachObject(m_gizmoRotate);

    // Scale gizmo — 3 axis lines with box ends
    m_gizmoScale = m_sceneManager->createManualObject();
    {
        Ogre::String dbName = "GizmoScaleDatablock";
        Ogre::HlmsDatablock* db = hlmsUnlit->getDatablock(Ogre::IdString(dbName));
        if (!db) {
            db = hlmsUnlit->createDatablock(
                Ogre::IdString(dbName), dbName,
                Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
        }
        auto* dblock = static_cast<Ogre::HlmsUnlitDatablock*>(db);
        dblock->setColour(Ogre::ColourValue(0, 1, 1, 1));

        m_gizmoScale->begin(dbName, Ogre::OT_LINE_LIST);
        m_gizmoScale->position(0, 0, 0);
        m_gizmoScale->position(50, 0, 0);
        m_gizmoScale->index(0); m_gizmoScale->index(1);
        m_gizmoScale->position(0, 0, 0);
        m_gizmoScale->position(0, 50, 0);
        m_gizmoScale->index(2); m_gizmoScale->index(3);
        m_gizmoScale->position(0, 0, 0);
        m_gizmoScale->position(0, 0, 50);
        m_gizmoScale->index(4); m_gizmoScale->index(5);
        m_gizmoScale->end();
    }
    m_gizmoNode->attachObject(m_gizmoScale);

    m_gizmoNode->setVisible(false);
}

void OgreWidget::destroyGizmo()
{
    if (m_gizmoNode) {
        if (m_gizmoMove) {
            m_gizmoNode->detachObject(m_gizmoMove);
            m_sceneManager->destroyManualObject(m_gizmoMove);
            m_gizmoMove = nullptr;
        }
        if (m_gizmoRotate) {
            m_gizmoNode->detachObject(m_gizmoRotate);
            m_sceneManager->destroyManualObject(m_gizmoRotate);
            m_gizmoRotate = nullptr;
        }
        if (m_gizmoScale) {
            m_gizmoNode->detachObject(m_gizmoScale);
            m_sceneManager->destroyManualObject(m_gizmoScale);
            m_gizmoScale = nullptr;
        }
        m_sceneManager->destroySceneNode(m_gizmoNode);
        m_gizmoNode = nullptr;
    }
}

void OgreWidget::showGizmo(bool show)
{
    if (m_gizmoNode) m_gizmoNode->setVisible(show);
}

void OgreWidget::updateGizmo()
{
    QString selId = m_world.primarySelection();
    if (selId.isEmpty() || m_transformMode == TransformMode::None) {
        showGizmo(false);
        return;
    }
    world::Actor* a = m_world.findActor(selId);
    if (!a) {
        showGizmo(false);
        return;
    }
    if (m_gizmoNode) {
        m_gizmoNode->setPosition(Ogre::Vector3(a->transform.posX, a->transform.posY, a->transform.posZ));
        showGizmo(true);
    }
}

GizmoAxis OgreWidget::pickGizmoAxis(int screenX, int screenY)
{
    // Simplified — just return None for now
    Q_UNUSED(screenX); Q_UNUSED(screenY);
    return GizmoAxis::None;
}

void OgreWidget::applyGizmoDrag(int screenX, int screenY)
{
    // Simplified — no drag implementation yet
    Q_UNUSED(screenX); Q_UNUSED(screenY);
}

// ============================================================
// Scene serialization (legacy format — backward compatible)
// ============================================================

QJsonObject OgreWidget::saveScene() const
{
    QJsonObject scene;

    // Terrain
    QJsonObject terrain;
    terrain["heightmapPath"] = m_heightmapPath;
    terrain["albedoPath"] = m_albedoPath;
    terrain["terrainSize"] = m_terrainSize;
    terrain["heightScale"] = m_heightScale;
    scene["terrain"] = terrain;

    // Roads
    scene["xodrPath"] = m_xodrPath;

    // Camera
    QJsonObject cam;
    cam["yaw"] = m_camYaw;
    cam["pitch"] = m_camPitch;
    cam["dist"] = m_camDist;
    cam["targetX"] = m_camTargetX;
    cam["targetY"] = m_camTargetY;
    cam["targetZ"] = m_camTargetZ;
    scene["camera"] = cam;

    // Actors (from World model)
    QJsonArray actorsArr;
    for (const auto& a : m_world.actors)
        actorsArr.append(a.toJson());
    scene["actors"] = actorsArr;

    // Layers
    QJsonArray layersArr;
    for (const auto& l : m_world.layers)
        layersArr.append(l.toJson());
    scene["layers"] = layersArr;

    // Selection
    QJsonArray selArr;
    for (const auto& id : m_world.selectedActorIds)
        selArr.append(id);
    scene["selection"] = selArr;

    scene["version"] = "2.0";
    return scene;
}

void OgreWidget::loadScene(const QJsonObject& scene)
{
    if (!m_initialized) {
        m_pendingScene = scene;
        m_hasPendingScene = true;
        return;
    }

    // Clear existing
    clearActorRenderables();
    m_world.actors.clear();
    m_world.selectedActorIds.clear();

    // Load terrain
    if (scene.contains("terrain")) {
        QJsonObject terrain = scene["terrain"].toObject();
        QString hmPath = terrain["heightmapPath"].toString();
        QString albPath = terrain["albedoPath"].toString();
        float tSize = float(terrain["terrainSize"].toDouble(4000));
        float hScale = float(terrain["heightScale"].toDouble(100));
        if (!hmPath.isEmpty() && QFile::exists(hmPath)) {
            loadTerrain(hmPath, albPath, tSize, hScale);
        }
    }

    // Load roads
    if (scene.contains("xodrPath")) {
        QString xodr = scene["xodrPath"].toString();
        if (!xodr.isEmpty() && QFile::exists(xodr)) {
            loadRoads(xodr);
        }
    }

    // Load camera
    if (scene.contains("camera")) {
        QJsonObject cam = scene["camera"].toObject();
        m_camYaw = float(cam["yaw"].toDouble(0));
        m_camPitch = float(cam["pitch"].toDouble(-30));
        m_camDist = float(cam["dist"].toDouble(500));
        m_camTargetX = float(cam["targetX"].toDouble(0));
        m_camTargetY = float(cam["targetY"].toDouble(0));
        m_camTargetZ = float(cam["targetZ"].toDouble(0));
        resetCamera();
    }

    // Load layers
    if (scene.contains("layers")) {
        m_world.layers.clear();
        QJsonArray layersArr = scene["layers"].toArray();
        for (const auto& v : layersArr)
            m_world.layers.append(world::Layer::fromJson(v.toObject()));
        if (m_world.layers.isEmpty()) {
            world::World fresh;
            m_world.layers = fresh.layers;
        }
    }

    // Load actors
    if (scene.contains("actors")) {
        QJsonArray actorsArr = scene["actors"].toArray();
        for (const auto& v : actorsArr) {
            world::Actor a = world::Actor::fromJson(v.toObject());
            m_world.actors.append(a);
            rebuildActor(a);
        }
    }

    // Load selection
    if (scene.contains("selection")) {
        QJsonArray selArr = scene["selection"].toArray();
        for (const auto& v : selArr)
            m_world.selectedActorIds.insert(v.toString());
    }

    updateGizmo();
    emit sceneChanged();
}

// ============================================================
// World serialization
// ============================================================

bool OgreWidget::saveWorld(const QString& path) const
{
    return m_world.saveToFile(path);
}

bool OgreWidget::loadWorld(const QString& path)
{
    world::World loaded = world::World::loadFromFile(path);
    if (loaded.actorCount() == 0 && loaded.layerCount() <= 8) {
        // Empty or failed load
        return false;
    }
    m_world = loaded;
    // Keep the WorldBuilder in sync so procedural generation
    // continues on top of the loaded world
    m_builder.world = m_world;
    syncAllActors();
    updateGizmo();
    emit worldChanged();
    return true;
}

// ============================================================
// Camera
// ============================================================

void OgreWidget::resetCamera()
{
    if (m_camDist == 0) {
        m_camYaw = 0.0f;
        m_camPitch = -30.0f;
        m_camDist = m_terrainSize * 0.8f;
        m_camTargetX = 0;
        m_camTargetY = m_heightScale * 0.3f;
        m_camTargetZ = 0;
    }

    if (m_camera) {
        float yawRad = Ogre::Degree(m_camYaw).valueRadians();
        float pitchRad = Ogre::Degree(m_camPitch).valueRadians();

        float x = m_camTargetX + m_camDist * cos(pitchRad) * sin(yawRad);
        float y = m_camTargetY + m_camDist * sin(-pitchRad);
        float z = m_camTargetZ + m_camDist * cos(pitchRad) * cos(yawRad);

        m_camera->setPosition(Ogre::Vector3(x, y, z));
        m_camera->lookAt(Ogre::Vector3(m_camTargetX, m_camTargetY, m_camTargetZ));
    }
}

void OgreWidget::setCameraPosition(float x, float y, float z)
{
    m_camTargetX = x;
    m_camTargetY = y;
    m_camTargetZ = z;
    resetCamera();
}

void OgreWidget::orbitCamera(float yaw, float pitch)
{
    m_camYaw += yaw;
    m_camPitch += pitch;
    m_camPitch = Ogre::Math::Clamp(m_camPitch, -89.0f, 89.0f);
    resetCamera();
}

void OgreWidget::zoomCamera(float delta)
{
    m_camDist += delta;
    m_camDist = Ogre::Math::Clamp(m_camDist, 10.0f, 10000.0f);
    resetCamera();
}

void OgreWidget::panCamera(float dx, float dy)
{
    m_camTargetX -= dx;
    m_camTargetZ += dy;
    resetCamera();
}

void OgreWidget::updateCameraFromKeys(float dt)
{
    if (!m_flyMode || m_keysDown.empty()) return;

    float speed = m_flySpeed * m_flyBoost * dt;
    Ogre::Vector3 forward = m_camera->getDirection();
    Ogre::Vector3 right = m_camera->getRight();
    Ogre::Vector3 up = Ogre::Vector3::UNIT_Y;
    Ogre::Vector3 move = Ogre::Vector3::ZERO;

    if (m_keysDown.count(Qt::Key_W)) move += forward * speed;
    if (m_keysDown.count(Qt::Key_S)) move -= forward * speed;
    if (m_keysDown.count(Qt::Key_A)) move -= right * speed;
    if (m_keysDown.count(Qt::Key_D)) move += right * speed;
    if (m_keysDown.count(Qt::Key_Q)) move -= up * speed;
    if (m_keysDown.count(Qt::Key_E)) move += up * speed;

    if (move != Ogre::Vector3::ZERO) {
        m_camera->move(move);
        m_camTargetX = m_camera->getPosition().x;
        m_camTargetY = m_camera->getPosition().y;
        m_camTargetZ = m_camera->getPosition().z;
    }
}

// ============================================================
// Mouse / keyboard
// ============================================================

void OgreWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_leftDown = true;
        m_lastMouseX = event->x();
        m_lastMouseY = event->y();

        // Try to pick an actor
        QString pickedId = pickActor(event->x(), event->y());
        if (!pickedId.isEmpty()) {
            selectActor(pickedId);
        } else {
            deselectAll();
        }
    } else if (event->button() == Qt::RightButton) {
        m_rightDown = true;
        m_lastMouseX = event->x();
        m_lastMouseY = event->y();
    } else if (event->button() == Qt::MiddleButton) {
        m_middleDown = true;
        m_lastMouseX = event->x();
        m_lastMouseY = event->y();
    }
}

void OgreWidget::mouseMoveEvent(QMouseEvent* event)
{
    int dx = event->x() - m_lastMouseX;
    int dy = event->y() - m_lastMouseY;

    if (m_leftDown) {
        // Orbit camera (or gizmo drag if gizmo is active)
        if (m_gizmoDragging) {
            applyGizmoDrag(event->x(), event->y());
        } else {
            orbitCamera(dx * 0.3f, -dy * 0.3f);
        }
    } else if (m_rightDown) {
        // Orbit with right button too
        orbitCamera(dx * 0.3f, -dy * 0.3f);
    } else if (m_middleDown) {
        // Pan with middle button
        panCamera(dx * 0.5f, dy * 0.5f);
    }

    m_lastMouseX = event->x();
    m_lastMouseY = event->y();
}

void OgreWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_leftDown = false;
    else if (event->button() == Qt::RightButton) m_rightDown = false;
    else if (event->button() == Qt::MiddleButton) m_middleDown = false;
}

void OgreWidget::wheelEvent(QWheelEvent* event)
{
    float delta = -event->angleDelta().y() * 0.5f;
    zoomCamera(delta);
}

void OgreWidget::keyPressEvent(QKeyEvent* event)
{
    m_keysDown.insert(event->key());

    if (event->key() == Qt::Key_F) {
        // Frame selected actor
        QString selId = m_world.primarySelection();
        if (!selId.isEmpty()) {
            world::Actor* a = m_world.findActor(selId);
            if (a) {
                m_camTargetX = a->transform.posX;
                m_camTargetY = a->transform.posY;
                m_camTargetZ = a->transform.posZ;
                m_camDist = 100;
                resetCamera();
            }
        }
    }

    // Transform mode shortcuts
    if (event->key() == Qt::Key_W && !m_flyMode)
        setTransformMode(TransformMode::Move);
    if (event->key() == Qt::Key_E && !m_flyMode)
        setTransformMode(TransformMode::Rotate);
    if (event->key() == Qt::Key_R && !m_flyMode)
        setTransformMode(TransformMode::Scale);

    // Delete key removes selected actor
    if (event->key() == Qt::Key_Delete) {
        QString selId = m_world.primarySelection();
        if (!selId.isEmpty())
            removeActor(selId);
    }
}

void OgreWidget::keyReleaseEvent(QKeyEvent* event)
{
    m_keysDown.erase(event->key());
}

void OgreWidget::focusOutEvent(QFocusEvent* event)
{
    Q_UNUSED(event);
    m_keysDown.clear();
    m_leftDown = m_rightDown = m_middleDown = false;
}

// ============================================================
// Raycasting (simplified)
// ============================================================

bool OgreWidget::screenToWorld(int screenX, int screenY, WorldPos& worldPos)
{
    if (!m_camera || !m_renderWindow) return false;

    float nx = float(screenX) / width();
    float ny = float(screenY) / height();
    Ogre::Ray ray = m_camera->getCameraToViewportRay(nx, ny);

    // Intersect with ground plane (y=0)
    float t = -ray.getOrigin().y / ray.getDirection().y;
    if (t > 0) {
        Ogre::Vector3 pos = ray.getPoint(t);
        worldPos = {pos.x, pos.y, pos.z};
        return true;
    }
    return false;
}

QString OgreWidget::pickActor(int screenX, int screenY)
{
    // Simplified picking — cast ray and find nearest actor by distance
    WorldPos worldPos;
    if (!screenToWorld(screenX, screenY, worldPos)) return QString();

    QString bestId;
    float bestDist = 1e10f;
    for (const auto& actor : m_world.actors) {
        if (!actor.visible || !actor.selectable) continue;
        if (!isLayerVisible(actor.layerId) || isLayerLocked(actor.layerId)) continue;

        float dx = actor.transform.posX - worldPos.x;
        float dy = actor.transform.posY - worldPos.y;
        float dz = actor.transform.posZ - worldPos.z;
        float dist = sqrt(dx*dx + dy*dy + dz*dz);
        float radius = std::max({actor.transform.scaleX, actor.transform.scaleY, actor.transform.scaleZ}) * 0.5f;
        if (dist < radius && dist < bestDist) {
            bestDist = dist;
            bestId = actor.id;
        }
    }
    return bestId;
}

// ============================================================
// Render loop
// ============================================================

void OgreWidget::render()
{
    if (m_root && m_renderWindow) {
        // Update fly camera
        if (m_flyMode)
            updateCameraFromKeys(0.016f);

        m_root->renderOneFrame();
    }
}

// ============================================================
// Shutdown
// ============================================================

void OgreWidget::shutdownOgre()
{
    if (m_timerId) {
        killTimer(m_timerId);
        m_timerId = 0;
    }
    clearActorRenderables();
    clearRoads();
    clearTerrain();
    destroyGizmo();
    destroyGrid();
    if (m_root) {
        delete m_root;
        m_root = nullptr;
    }
    m_initialized = false;
}
