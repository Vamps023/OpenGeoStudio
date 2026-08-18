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
#include "OgreHlmsPbsDatablock.h"
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
#include "OgreRay.h"
#include "Math/Simple/OgreAabb.h"
#include "OgrePlane.h"

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
    // Parse compositor scripts so the PSSM shadow node exists before the
    // workspace references it. Without a shadow node the render_scene pass
    // renders no shadow maps and Light::setCastShadows has no visible effect.
    Ogre::IdString shadowNodeName;
    {
        const QString compositorDir = appDir + "/compositor";
        Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
        if (QDir(compositorDir).exists() && !rgm.resourceGroupExists("OgsCompositor")) {
            rgm.createResourceGroup("OgsCompositor");
            rgm.addResourceLocation(compositorDir.toStdString(), "FileSystem",
                                    "OgsCompositor");
            rgm.initialiseResourceGroup("OgsCompositor", true);
        }
        if (compositorManager->hasShadowNodeDefinition("OgsShadowNode")) {
            shadowNodeName = Ogre::IdString("OgsShadowNode");
            appLog().info("[OGRE] PSSM shadow node loaded: OgsShadowNode");
        } else {
            appLog().warn("[OGRE] OgsShadowNode not found in", compositorDir,
                          "— rendering without shadows");
        }
    }

    Ogre::ColourValue bgColor(0.2f, 0.4f, 0.6f, 1.0f);
    if (!compositorManager->hasWorkspaceDefinition("MainWorkspace")) {
        compositorManager->createBasicWorkspaceDef("MainWorkspace", bgColor,
            shadowNodeName);
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

    // Real scene lighting — must exist before any PBS material renders,
    // otherwise PBS surfaces are lit only by ambient and look flat/black.
    setupLighting();

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
// Lighting
// ============================================================
// Creates a real directional sun light plus hemisphere ambient. Previously
// addSunLight()/addSkyLight() only appended entries to the world model, so
// the scene had no Ogre::Light at all and every surface had to be unlit.

void OgreWidget::setupLighting()
{
    if (!m_sceneManager) return;

    if (!m_sunLight) {
        m_sunLight = m_sceneManager->createLight();
        m_sunNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
        m_sunNode->attachObject(m_sunLight);
        m_sunLight->setType(Ogre::Light::LT_DIRECTIONAL);
        m_sunLight->setCastShadows(true);
    }

    setSunDirection(m_sunYaw, m_sunPitch);
    setSunIntensity(m_sunIntensity);
    setSkyIntensity(m_skyIntensity);

    appLog().info("[OGRE] Lighting created: directional sun + hemisphere ambient");
}

void OgreWidget::setSunDirection(float yaw, float pitch)
{
    m_sunYaw = yaw;
    m_sunPitch = pitch;
    if (!m_sunLight) return;

    // Spherical to cartesian. pitch is the elevation above the horizon, so a
    // pitch of 90 points straight down; the light direction points *from* the
    // sun toward the scene, hence the negated Y.
    const float yawRad = Ogre::Degree(yaw).valueRadians();
    const float pitchRad = Ogre::Degree(pitch).valueRadians();
    Ogre::Vector3 dir(
        std::cos(pitchRad) * std::sin(yawRad),
        -std::sin(pitchRad),
        std::cos(pitchRad) * std::cos(yawRad));
    dir.normalise();
    m_sunLight->setDirection(dir);
}

void OgreWidget::setSunIntensity(float intensity)
{
    m_sunIntensity = intensity;
    if (!m_sunLight) return;

    // Slightly warm sunlight; power scale carries the intensity so the colour
    // stays a hue rather than doubling as a brightness control.
    m_sunLight->setDiffuseColour(1.0f, 0.97f, 0.92f);
    m_sunLight->setSpecularColour(1.0f, 0.98f, 0.95f);
    m_sunLight->setPowerScale(Ogre::Math::Clamp(intensity, 0.0f, 100.0f) *
                              Ogre::Math::PI);
}

void OgreWidget::setSkyIntensity(float intensity)
{
    m_skyIntensity = intensity;
    if (!m_sceneManager) return;

    // Hemisphere ambient: sky colour from above, bounced ground colour from
    // below. This is what stops shadowed faces from going pure black.
    const float k = Ogre::Math::Clamp(intensity, 0.0f, 100.0f);
    const Ogre::ColourValue skyColour(0.35f, 0.50f, 0.75f);
    const Ogre::ColourValue groundColour(0.28f, 0.26f, 0.22f);
    m_sceneManager->setAmbientLight(skyColour * k, groundColour * k,
                                    Ogre::Vector3::UNIT_Y);
}

void OgreWidget::applyLightingFromWorld()
{
    // Pull sun/sky settings out of any light actors the world model holds so
    // that a reloaded world restores its lighting instead of resetting.
    for (const auto& actor : m_world.actors) {
        if (actor.type == world::ActorType::SunLight) {
            setSunDirection(actor.transform.rotY, actor.transform.rotX);
            setSunIntensity(actor.colorA > 0.0f ? actor.colorA * 3.0f : m_sunIntensity);
        } else if (actor.type == world::ActorType::SkyLight) {
            setSkyIntensity(actor.colorA > 0.0f ? actor.colorA : m_skyIntensity);
        }
    }
}

void OgreWidget::destroyLighting()
{
    if (m_sunNode && m_sceneManager) {
        if (m_sunLight) m_sunNode->detachObject(m_sunLight);
        m_sceneManager->destroySceneNode(m_sunNode);
        m_sunNode = nullptr;
    }
    if (m_sunLight && m_sceneManager) {
        m_sceneManager->destroyLight(m_sunLight);
        m_sunLight = nullptr;
    }
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

    // Terrain uses PBS so it responds to the sun/ambient instead of rendering
    // as a flat unlit texture with no relief.
    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsPbs = hlmsManager->getHlms(Ogre::HLMS_PBS);

    Ogre::String datablockName = "TerrainDatablock";
    Ogre::HlmsDatablock* dbBase = hlmsPbs->getDatablock(Ogre::IdString(datablockName));
    if (!dbBase) {
        dbBase = hlmsPbs->createDatablock(
            Ogre::IdString(datablockName), datablockName,
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
    }
    Ogre::HlmsPbsDatablock* datablock = static_cast<Ogre::HlmsPbsDatablock*>(dbBase);
    // Ground is a rough dielectric — no metalness, high roughness
    datablock->setRoughness(0.85f);
    datablock->setMetalness(0.0f);

    // OGRE's image codecs don't include TIFF. Convert GeoTIFF albedo through
    // QImage when the TIFF image plugin is available, so exported
    // albedo_merged.tif still displays instead of falling back to flat green.
    QString albedoLoadPath = albedoPath;
    if (!albedoPath.isEmpty() && QFile::exists(albedoPath) &&
        (albedoPath.endsWith(".tif", Qt::CaseInsensitive) ||
         albedoPath.endsWith(".tiff", Qt::CaseInsensitive))) {
        QImage tifImg(albedoPath);
        if (!tifImg.isNull()) {
            const QString tmpPath = QDir::temp().filePath(
                "ogs_albedo_" + QString::number(qHash(albedoPath)) + ".png");
            if (tifImg.save(tmpPath, "PNG")) {
                albedoLoadPath = tmpPath;
                appLog().info("Converted TIFF albedo to PNG for display:", albedoPath);
            } else {
                albedoLoadPath.clear();
            }
        } else {
            appLog().warn("TIFF albedo cannot be decoded (no TIFF image plugin):",
                          albedoPath, "— use albedo_merged.png instead");
            albedoLoadPath.clear();
        }
    }

    if (!albedoLoadPath.isEmpty() && QFile::exists(albedoLoadPath)) {
        QString albDir = QFileInfo(albedoLoadPath).absolutePath();
        QString albName = QFileInfo(albedoLoadPath).fileName();
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

        datablock->setTexture(Ogre::PBSM_DIFFUSE, albedoTex);
        appLog().info("Albedo texture loaded:", albedoLoadPath);
    } else {
        datablock->setDiffuse(Ogre::Vector3(0.3f, 0.5f, 0.3f));
    }

    manual->begin(datablockName, Ogre::OT_TRIANGLE_LIST);

    float halfSize = terrainSize * 0.5f;
    float xStep = terrainSize / (gridW - 1);
    float zStep = terrainSize / (gridH - 1);

    // Sample the grid height once so normals can be derived from neighbours.
    // Hardcoding the normal to +Y made every slope receive identical light,
    // which is why lit terrain still looked flat.
    std::vector<float> gridY(static_cast<size_t>(gridW) * gridH, 0.0f);
    for (int gz = 0; gz < gridH; gz++) {
        for (int gx = 0; gx < gridW; gx++) {
            const int hx = (gx * (hmW - 1)) / (gridW - 1);
            const int hy = (gz * (hmH - 1)) / (gridH - 1);
            const uchar* pixel = heightImg.scanLine(hy);
            gridY[static_cast<size_t>(gz) * gridW + gx] =
                (pixel[hx] / 255.0f) * heightScale;
        }
    }
    auto heightAt = [&](int gx, int gz) -> float {
        gx = std::clamp(gx, 0, gridW - 1);
        gz = std::clamp(gz, 0, gridH - 1);
        return gridY[static_cast<size_t>(gz) * gridW + gx];
    };

    for (int gz = 0; gz < gridH; gz++) {
        for (int gx = 0; gx < gridW; gx++) {
            const float y = heightAt(gx, gz);
            const float x = -halfSize + gx * xStep;
            const float z = -halfSize + gz * zStep;

            const float u = (float)gx / (gridW - 1);
            const float v = (float)gz / (gridH - 1);

            // Central-difference normal from the height field
            const float hL = heightAt(gx - 1, gz);
            const float hR = heightAt(gx + 1, gz);
            const float hD = heightAt(gx, gz - 1);
            const float hU = heightAt(gx, gz + 1);
            Ogre::Vector3 normal(hL - hR, 2.0f * xStep, hD - hU);
            if (std::abs(zStep - xStep) > 1e-6f) {
                // Non-square cells: scale the Z gradient by its own spacing
                normal = Ogre::Vector3((hL - hR) * zStep,
                                       2.0f * xStep * zStep,
                                       (hD - hU) * xStep);
            }
            normal.normalise();

            manual->position(x, y, z);
            manual->textureCoord(u, v);
            manual->normal(normal);
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
// Actor rendering — Stage 3: per-type procedural meshes + real .mesh assets
// ============================================================

Ogre::String OgreWidget::actorDatablock(const world::Actor& actor)
{
    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsPbs = hlmsManager->getHlms(Ogre::HLMS_PBS);
    Ogre::String dblockName = "ActorDatablock_" + actor.id.toStdString();
    Ogre::HlmsDatablock* dbBase = hlmsPbs->getDatablock(Ogre::IdString(dblockName));
    if (!dbBase) {
        dbBase = hlmsPbs->createDatablock(
            Ogre::IdString(dblockName), dblockName,
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
    }
    Ogre::HlmsPbsDatablock* datablock = static_cast<Ogre::HlmsPbsDatablock*>(dbBase);
    datablock->setDiffuse(Ogre::Vector3(actor.colorR, actor.colorG, actor.colorB));

    float roughness = 0.6f, metalness = 0.0f;
    switch (actor.type) {
    case world::ActorType::Building:
        roughness = 0.55f; break;
    case world::ActorType::Tree:
    case world::ActorType::Vegetation:
    case world::ActorType::Grass:
        roughness = 0.85f; break;
    case world::ActorType::Rock:
        roughness = 0.90f; break;
    case world::ActorType::Water:
    case world::ActorType::River:
    case world::ActorType::Lake:
        roughness = 0.08f; break;
    case world::ActorType::Prop:
        roughness = 0.45f; metalness = 0.25f; break;
    default:
        break;
    }
    datablock->setRoughness(roughness);
    datablock->setMetalness(metalness);
    return dblockName;
}

void OgreWidget::buildBoxMesh(Ogre::ManualObject* mo, const Ogre::String& db,
                              float hx, float hy, float hz)
{
    mo->begin(db, Ogre::OT_TRIANGLE_LIST);
    // 8 vertices
    mo->position(-hx, -hy,  hz); mo->normal(0, 0, 1);
    mo->position( hx, -hy,  hz); mo->normal(0, 0, 1);
    mo->position( hx,  hy,  hz); mo->normal(0, 0, 1);
    mo->position(-hx,  hy,  hz); mo->normal(0, 0, 1);
    mo->position(-hx, -hy, -hz); mo->normal(0, 0, -1);
    mo->position( hx, -hy, -hz); mo->normal(0, 0, -1);
    mo->position( hx,  hy, -hz); mo->normal(0, 0, -1);
    mo->position(-hx,  hy, -hz); mo->normal(0, 0, -1);
    // Front
    mo->index(0); mo->index(1); mo->index(2); mo->index(0); mo->index(2); mo->index(3);
    // Back
    mo->index(5); mo->index(4); mo->index(7); mo->index(5); mo->index(7); mo->index(6);
    // Left
    mo->index(4); mo->index(0); mo->index(3); mo->index(4); mo->index(3); mo->index(7);
    // Right
    mo->index(1); mo->index(5); mo->index(6); mo->index(1); mo->index(6); mo->index(2);
    // Top
    mo->index(3); mo->index(2); mo->index(6); mo->index(3); mo->index(6); mo->index(7);
    // Bottom
    mo->index(4); mo->index(5); mo->index(1); mo->index(4); mo->index(1); mo->index(0);
    mo->end();
}

void OgreWidget::buildCylinderMesh(Ogre::ManualObject* mo, const Ogre::String& db,
                                    float radius, float height, int segments)
{
    mo->begin(db, Ogre::OT_TRIANGLE_LIST);
    float hy = height * 0.5f;
    // Side faces — two vertices per segment, two triangles per quad.
    for (int i = 0; i < segments; ++i) {
        float a0 = float(i) / segments * 6.2831853f;
        float a1 = float(i + 1) / segments * 6.2831853f;
        float x0 = cos(a0) * radius, z0 = sin(a0) * radius;
        float x1 = cos(a1) * radius, z1 = sin(a1) * radius;
        Ogre::Vector3 n0(x0, 0, z0); n0.normalise();
        Ogre::Vector3 n1(x1, 0, z1); n1.normalise();
        uint32_t base = uint32_t(mo->getCurrentVertexCount());
        mo->position(x0, -hy, z0); mo->normal(n0.x, n0.y, n0.z);
        mo->position(x1, -hy, z1); mo->normal(n1.x, n1.y, n1.z);
        mo->position(x1,  hy, z1); mo->normal(n1.x, n1.y, n1.z);
        mo->position(x0,  hy, z0); mo->normal(n0.x, n0.y, n0.z);
        mo->index(base);     mo->index(base + 1); mo->index(base + 2);
        mo->index(base);     mo->index(base + 2); mo->index(base + 3);
    }
    // Caps — center fans
    uint32_t topCenter = uint32_t(mo->getCurrentVertexCount());
    mo->position(0, hy, 0); mo->normal(0, 1, 0);
    uint32_t topStart = uint32_t(mo->getCurrentVertexCount());
    for (int i = 0; i < segments; ++i) {
        float a = float(i) / segments * 6.2831853f;
        mo->position(cos(a) * radius, hy, sin(a) * radius); mo->normal(0, 1, 0);
    }
    for (int i = 0; i < segments; ++i) {
        mo->index(topCenter);
        mo->index(topStart + i);
        mo->index(topStart + (i + 1) % segments);
    }
    uint32_t botCenter = uint32_t(mo->getCurrentVertexCount());
    mo->position(0, -hy, 0); mo->normal(0, -1, 0);
    uint32_t botStart = uint32_t(mo->getCurrentVertexCount());
    for (int i = 0; i < segments; ++i) {
        float a = float(i) / segments * 6.2831853f;
        mo->position(cos(a) * radius, -hy, sin(a) * radius); mo->normal(0, -1, 0);
    }
    for (int i = 0; i < segments; ++i) {
        mo->index(botCenter);
        mo->index(botStart + (i + 1) % segments);
        mo->index(botStart + i);
    }
    mo->end();
}

void OgreWidget::buildConeMesh(Ogre::ManualObject* mo, const Ogre::String& db,
                                float radius, float height, int segments)
{
    mo->begin(db, Ogre::OT_TRIANGLE_LIST);
    float hy = height * 0.5f;
    uint32_t apex = uint32_t(mo->getCurrentVertexCount());
    mo->position(0, hy, 0); mo->normal(0, 1, 0);
    uint32_t baseStart = uint32_t(mo->getCurrentVertexCount());
    for (int i = 0; i < segments; ++i) {
        float a = float(i) / segments * 6.2831853f;
        Ogre::Vector3 n(cos(a), 0, sin(a)); n.normalise();
        mo->position(cos(a) * radius, -hy, sin(a) * radius);
        mo->normal(n.x, n.y, n.z);
    }
    for (int i = 0; i < segments; ++i) {
        uint32_t a = baseStart + i;
        uint32_t b = baseStart + (i + 1) % segments;
        mo->index(apex); mo->index(a); mo->index(b);
    }
    // Base cap
    uint32_t botCenter = uint32_t(mo->getCurrentVertexCount());
    mo->position(0, -hy, 0); mo->normal(0, -1, 0);
    for (int i = 0; i < segments; ++i) {
        mo->index(botCenter);
        mo->index(baseStart + (i + 1) % segments);
        mo->index(baseStart + i);
    }
    mo->end();
}

void OgreWidget::buildSphereMesh(Ogre::ManualObject* mo, const Ogre::String& db,
                                  float radius, int rings, int segments)
{
    mo->begin(db, Ogre::OT_TRIANGLE_LIST);
    for (int r = 0; r <= rings; ++r) {
        float phi = float(r) / rings * 3.1415927f;
        float y = cos(phi) * radius;
        float ringR = sin(phi) * radius;
        for (int s = 0; s <= segments; ++s) {
            float theta = float(s) / segments * 6.2831853f;
            float x = cos(theta) * ringR;
            float z = sin(theta) * ringR;
            Ogre::Vector3 n(x, y, z); n.normalise();
            mo->position(x, y, z); mo->normal(n.x, n.y, n.z);
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            uint32_t a = uint32_t(r * (segments + 1) + s);
            uint32_t b = a + 1;
            uint32_t c = a + (segments + 1);
            uint32_t d = c + 1;
            mo->index(a); mo->index(c); mo->index(b);
            mo->index(b); mo->index(c); mo->index(d);
        }
    }
    mo->end();
}

void OgreWidget::buildPlaneMesh(Ogre::ManualObject* mo, const Ogre::String& db,
                                 float hx, float hz)
{
    mo->begin(db, Ogre::OT_TRIANGLE_LIST);
    mo->position(-hx, 0,  hz); mo->normal(0, 1, 0);
    mo->position( hx, 0,  hz); mo->normal(0, 1, 0);
    mo->position( hx, 0, -hz); mo->normal(0, 1, 0);
    mo->position(-hx, 0, -hz); mo->normal(0, 1, 0);
    mo->index(0); mo->index(1); mo->index(2);
    mo->index(0); mo->index(2); mo->index(3);
    mo->end();
}

Ogre::MeshPtr OgreWidget::loadActorMeshAsset(const QString& path)
{
    if (path.isEmpty()) return Ogre::MeshPtr();
    QFileInfo fi(path);
    if (!fi.exists()) {
        appLog().warn("Actor mesh asset not found: {}", path);
        return Ogre::MeshPtr();
    }
    Ogre::String meshName = "AssetMesh_" + path.toStdString();
    Ogre::MeshManager& meshMgr = Ogre::MeshManager::getSingleton();
    Ogre::MeshPtr existing = meshMgr.getByName(meshName);
    if (existing) return existing;
    // Load via the resource group system. The path's directory must be
    // registered as a resource location; we add it on demand.
    Ogre::String dir = fi.absolutePath().toStdString();
    Ogre::ResourceGroupManager& rgm = Ogre::ResourceGroupManager::getSingleton();
    try {
        rgm.addResourceLocation(dir, "FileSystem",
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, false, false);
        rgm.loadResourceGroup(Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    } catch (const std::exception&) {
        // Duplicate location or load failure — load() will report a clearer error.
    }
    try {
        return meshMgr.load(fi.fileName().toStdString(),
            Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    } catch (const std::exception& e) {
        appLog().warn("Failed to load actor mesh '{}': {}", path, e.what());
        return Ogre::MeshPtr();
    }
}

Ogre::MeshPtr OgreWidget::buildProceduralActorMesh(const world::Actor& actor)
{
    Ogre::String dblockName = actorDatablock(actor);
    Ogre::ManualObject* manual = m_sceneManager->createManualObject();

    switch (actor.type) {
    case world::ActorType::Building: {
        // Box body + optional pyramid roof (height stored in scaleY).
        buildBoxMesh(manual, dblockName, 0.5f, 0.4f, 0.5f);
        // Roof as a separate section using the same datablock.
        manual->begin(dblockName, Ogre::OT_TRIANGLE_LIST);
        // Simple 4-sided cone roof on top of the box.
        float roofBase = 0.4f;
        float roofTop = 0.5f;
        uint32_t apex = 0;
        manual->position(0, roofTop, 0); manual->normal(0, 1, 0);
        manual->position(-0.5f, roofBase,  0.5f); manual->normal(-0.5f, 0.5f, 0.5f);
        manual->position( 0.5f, roofBase,  0.5f); manual->normal( 0.5f, 0.5f, 0.5f);
        manual->position( 0.5f, roofBase, -0.5f); manual->normal( 0.5f, 0.5f, -0.5f);
        manual->position(-0.5f, roofBase, -0.5f); manual->normal(-0.5f, 0.5f, -0.5f);
        manual->index(apex); manual->index(1); manual->index(2);
        manual->index(apex); manual->index(2); manual->index(3);
        manual->index(apex); manual->index(3); manual->index(4);
        manual->index(apex); manual->index(4); manual->index(1);
        manual->end();
        break;
    }
    case world::ActorType::Tree: {
        // Trunk cylinder + foliage sphere.
        buildCylinderMesh(manual, dblockName, 0.08f, 0.4f, 12);
        // Foliage as a sphere placed above the trunk. We bake it into the
        // same mesh at a local offset; the actor's scale handles sizing.
        manual->begin(dblockName, Ogre::OT_TRIANGLE_LIST);
        const int rings = 8, segs = 16;
        float r = 0.35f;
        float yOffset = 0.45f;
        for (int ring = 0; ring <= rings; ++ring) {
            float phi = float(ring) / rings * 3.1415927f;
            float y = cos(phi) * r + yOffset;
            float ringR = sin(phi) * r;
            for (int s = 0; s <= segs; ++s) {
                float theta = float(s) / segs * 6.2831853f;
                float x = cos(theta) * ringR;
                float z = sin(theta) * ringR;
                Ogre::Vector3 n(x, y - yOffset, z); n.normalise();
                manual->position(x, y, z); manual->normal(n.x, n.y, n.z);
            }
        }
        for (int ring = 0; ring < rings; ++ring) {
            for (int s = 0; s < segs; ++s) {
                uint32_t a = uint32_t(ring * (segs + 1) + s);
                uint32_t b = a + 1;
                uint32_t c = a + (segs + 1);
                uint32_t d = c + 1;
                manual->index(a); manual->index(c); manual->index(b);
                manual->index(b); manual->index(c); manual->index(d);
            }
        }
        manual->end();
        break;
    }
    case world::ActorType::Vegetation:
    case world::ActorType::Grass: {
        // Grass tuft — a small crossed-plane quad cluster.
        buildPlaneMesh(manual, dblockName, 0.25f, 0.25f);
        manual->begin(dblockName, Ogre::OT_TRIANGLE_LIST);
        // Second plane rotated 90° around Y.
        manual->position(0, 0, -0.25f); manual->normal(0, 1, 0);
        manual->position(0, 0,  0.25f); manual->normal(0, 1, 0);
        manual->position(0.25f, 0,  0.25f); manual->normal(0, 1, 0);
        manual->position(0.25f, 0, -0.25f); manual->normal(0, 1, 0);
        manual->index(0); manual->index(1); manual->index(2);
        manual->index(0); manual->index(2); manual->index(3);
        manual->end();
        break;
    }
    case world::ActorType::Rock: {
        // Low-poly icosahedron-ish sphere for a chunky rock look.
        buildSphereMesh(manual, dblockName, 0.5f, 3, 8);
        break;
    }
    case world::ActorType::Prop: {
        // Generic prop — a small cylinder + box base.
        buildBoxMesh(manual, dblockName, 0.5f, 0.1f, 0.5f);
        buildCylinderMesh(manual, dblockName, 0.2f, 0.6f, 12);
        break;
    }
    case world::ActorType::Water:
    case world::ActorType::River:
    case world::ActorType::Lake: {
        // Flat water plane at y=0.
        buildPlaneMesh(manual, dblockName, 0.5f, 0.5f);
        break;
    }
    case world::ActorType::SunLight:
    case world::ActorType::SkyLight:
    case world::ActorType::Light: {
        // Light marker — a small sphere so the user can see/select it.
        buildSphereMesh(manual, dblockName, 0.25f, 6, 12);
        break;
    }
    case world::ActorType::Camera: {
        // Camera marker — a small box + frustum cone.
        buildBoxMesh(manual, dblockName, 0.2f, 0.2f, 0.3f);
        buildConeMesh(manual, dblockName, 0.25f, 0.4f, 12);
        break;
    }
    case world::ActorType::SplineControlPoint: {
        // Small sphere marker.
        buildSphereMesh(manual, dblockName, 0.15f, 6, 12);
        break;
    }
    case world::ActorType::Empty:
    case world::ActorType::Group: {
        // Empty/group actors render as a tiny axis tripod cube so they
        // remain selectable but visually unobtrusive.
        buildBoxMesh(manual, dblockName, 0.05f, 0.05f, 0.05f);
        break;
    }
    default: {
        // Fallback — unit cube (preserves prior behaviour).
        buildBoxMesh(manual, dblockName, 0.5f, 0.5f, 0.5f);
        break;
    }
    }

    Ogre::String meshName = "ActorMesh_" + actor.id.toStdString();
    Ogre::MeshManager& meshMgr = Ogre::MeshManager::getSingleton();
    if (meshMgr.getByName(meshName)) meshMgr.remove(meshName);
    Ogre::MeshPtr mesh = manual->convertToMesh(
        meshName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    m_sceneManager->destroyManualObject(manual);
    return mesh;
}

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

    // Stage 3: prefer a real .mesh asset when assetPath is set, otherwise
    // build a procedural mesh appropriate to the actor type.
    Ogre::MeshPtr mesh;
    if (!actor.assetPath.isEmpty()) {
        mesh = loadActorMeshAsset(actor.assetPath);
        if (!mesh) {
            appLog().warn("Falling back to procedural mesh for actor '{}' (asset '{}')",
                          actor.name, actor.assetPath);
        }
    }
    if (!mesh) mesh = buildProceduralActorMesh(actor);
    if (!mesh) return;

    Ogre::Item* item = m_sceneManager->createItem(mesh, Ogre::SCENE_DYNAMIC);
    Ogre::SceneNode* node = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    node->attachObject(item);

    // Actors both cast and receive shadows once a shadow node is active.
    // Light/camera marker actors do not cast shadows.
    bool isMarker = (actor.type == world::ActorType::Light ||
                     actor.type == world::ActorType::SunLight ||
                     actor.type == world::ActorType::SkyLight ||
                     actor.type == world::ActorType::Camera ||
                     actor.type == world::ActorType::SplineControlPoint ||
                     actor.type == world::ActorType::Empty ||
                     actor.type == world::ActorType::Group);
    item->setCastShadows(!isMarker);

    // Apply transform
    node->setPosition(Ogre::Vector3(actor.transform.posX, actor.transform.posY, actor.transform.posZ));
    Ogre::Radian rotY(Ogre::Degree(actor.transform.rotY));
    node->setOrientation(Ogre::Quaternion(rotY, Ogre::Vector3::UNIT_Y));
    node->setScale(Ogre::Vector3(actor.transform.scaleX, actor.transform.scaleY, actor.transform.scaleZ));
    node->setVisible(actor.visible && isLayerVisible(actor.layerId));

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

    // Drive the real directional light, not just the world-model entry
    setSunDirection(yaw, pitch);
    setSunIntensity(intensity);
    appLog().info("[OgreWidget] Added sun light: yaw", yaw, "pitch", pitch,
                  "intensity", intensity);
}

void OgreWidget::addSkyLight(float intensity)
{
    if (m_builder.world.settings.name.isEmpty()) {
        m_builder.createWorld("3D Studio World", m_terrainSize);
    }
    m_builder.addSkyLight(intensity);
    syncBuilderToWorld();

    // Drive the real hemisphere ambient, not just the world-model entry
    setSkyIntensity(intensity);
    appLog().info("[OgreWidget] Added sky light: intensity", intensity);
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
    emit transformModeChanged(static_cast<int>(mode));
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

    // Helper lambda to create or fetch a coloured unlit datablock.
    auto makeDb = [](Ogre::Hlms* hlms, const char* name,
                     const Ogre::ColourValue& c) -> Ogre::String {
        Ogre::HlmsDatablock* db = hlms->getDatablock(Ogre::IdString(name));
        if (!db) {
            db = hlms->createDatablock(
                Ogre::IdString(name), name,
                Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
        }
        static_cast<Ogre::HlmsUnlitDatablock*>(db)->setColour(c);
        return name;
    };

    Ogre::String dbX = makeDb(hlmsUnlit, "GizmoAxisX", Ogre::ColourValue(1, 0, 0, 1));
    Ogre::String dbY = makeDb(hlmsUnlit, "GizmoAxisY", Ogre::ColourValue(0, 1, 0, 1));
    Ogre::String dbZ = makeDb(hlmsUnlit, "GizmoAxisZ", Ogre::ColourValue(0, 0.4f, 1, 1));

    // ---- Move gizmo: three coloured axis lines + small arrow heads ----
    m_gizmoMove = m_sceneManager->createManualObject();
    // X axis
    m_gizmoMove->begin(dbX, Ogre::OT_LINE_LIST);
    m_gizmoMove->position(0, 0, 0); m_gizmoMove->position(1, 0, 0);
    m_gizmoMove->index(0); m_gizmoMove->index(1);
    m_gizmoMove->end();
    // Y axis
    m_gizmoMove->begin(dbY, Ogre::OT_LINE_LIST);
    m_gizmoMove->position(0, 0, 0); m_gizmoMove->position(0, 1, 0);
    m_gizmoMove->index(0); m_gizmoMove->index(1);
    m_gizmoMove->end();
    // Z axis
    m_gizmoMove->begin(dbZ, Ogre::OT_LINE_LIST);
    m_gizmoMove->position(0, 0, 0); m_gizmoMove->position(0, 0, 1);
    m_gizmoMove->index(0); m_gizmoMove->index(1);
    m_gizmoMove->end();
    m_gizmoNode->attachObject(m_gizmoMove);

    // ---- Rotate gizmo: three coloured circles (one per axis plane) ----
    m_gizmoRotate = m_sceneManager->createManualObject();
    auto buildCircle = [&](const Ogre::String& db, int segs,
                           const Ogre::Vector3& u, const Ogre::Vector3& v) {
        m_gizmoRotate->begin(db, Ogre::OT_LINE_LIST);
        uint32_t idx = 0;
        for (int i = 0; i < segs; ++i) {
            float a0 = float(i) / segs * 6.2831853f;
            float a1 = float(i + 1) / segs * 6.2831853f;
            Ogre::Vector3 p0 = u * cos(a0) + v * sin(a0);
            Ogre::Vector3 p1 = u * cos(a1) + v * sin(a1);
            m_gizmoRotate->position(p0.x, p0.y, p0.z);
            m_gizmoRotate->position(p1.x, p1.y, p1.z);
            m_gizmoRotate->index(idx);     m_gizmoRotate->index(idx + 1);
            idx += 2;
        }
        m_gizmoRotate->end();
    };
    // X axis ring lies in the YZ plane, etc.
    buildCircle(dbX, 48, Ogre::Vector3::UNIT_Y, Ogre::Vector3::UNIT_Z);
    buildCircle(dbY, 48, Ogre::Vector3::UNIT_X, Ogre::Vector3::UNIT_Z);
    buildCircle(dbZ, 48, Ogre::Vector3::UNIT_X, Ogre::Vector3::UNIT_Y);
    m_gizmoNode->attachObject(m_gizmoRotate);

    // ---- Scale gizmo: three coloured axis lines with cube ends ----
    m_gizmoScale = m_sceneManager->createManualObject();
    auto buildScaleAxis = [&](const Ogre::String& db, const Ogre::Vector3& dir) {
        m_gizmoScale->begin(db, Ogre::OT_LINE_LIST);
        m_gizmoScale->position(0, 0, 0);
        m_gizmoScale->position(dir.x, dir.y, dir.z);
        m_gizmoScale->index(0); m_gizmoScale->index(1);
        m_gizmoScale->end();
    };
    buildScaleAxis(dbX, Ogre::Vector3::UNIT_X);
    buildScaleAxis(dbY, Ogre::Vector3::UNIT_Y);
    buildScaleAxis(dbZ, Ogre::Vector3::UNIT_Z);
    m_gizmoNode->attachObject(m_gizmoScale);

    // Only the gizmo for the current transform mode is visible; the others
    // are hidden by updateGizmo() / showGizmo().
    m_gizmoMove->setVisible(false);
    m_gizmoRotate->setVisible(false);
    m_gizmoScale->setVisible(false);
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
    if (!m_gizmoNode) return;
    // Only the gizmo for the active transform mode is shown.
    if (m_gizmoMove)   m_gizmoMove->setVisible(show && m_transformMode == TransformMode::Move);
    if (m_gizmoRotate) m_gizmoRotate->setVisible(show && m_transformMode == TransformMode::Rotate);
    if (m_gizmoScale)  m_gizmoScale->setVisible(show && m_transformMode == TransformMode::Scale);
    m_gizmoNode->setVisible(show);
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
        Ogre::Vector3 origin(a->transform.posX, a->transform.posY, a->transform.posZ);
        m_gizmoNode->setPosition(origin);
        // Screen-constant size: scale the unit-length gizmo so it occupies a
        // fixed fraction of the viewport regardless of camera distance.
        float s = gizmoWorldScale(origin);
        m_gizmoNode->setScale(s, s, s);
        showGizmo(true);
    }
}

Ogre::Vector3 OgreWidget::gizmoAxisVector(GizmoAxis axis)
{
    switch (axis) {
    case GizmoAxis::X: return Ogre::Vector3::UNIT_X;
    case GizmoAxis::Y: return Ogre::Vector3::UNIT_Y;
    case GizmoAxis::Z: return Ogre::Vector3::UNIT_Z;
    default:           return Ogre::Vector3::ZERO;
    }
}

float OgreWidget::gizmoWorldScale(const Ogre::Vector3& origin) const
{
    if (!m_camera) return 50.0f;
    // Distance from camera to gizmo origin along the view direction.
    Ogre::Vector3 camPos = m_camera->getDerivedPosition();
    Ogre::Vector3 dir = m_camera->getDerivedDirection();
    float dist = std::max(1.0f, (origin - camPos).dotProduct(dir));
    // Target on-screen size in world units at unit distance. The gizmo mesh
    // is 1 unit long, so this scale makes it appear ~80px tall at the
    // default vertical FOV. We use the camera's vertical FOV to convert.
    Ogre::Radian fov = m_camera->getFOVy();
    float worldPerPx = float(2.0 * tan(fov.valueRadians() * 0.5) / height());
    // 80 px on screen -> world units at the gizmo's depth.
    return dist * worldPerPx * 80.0f;
}

bool OgreWidget::projectToScreen(const Ogre::Vector3& world, float& sx, float& sy) const
{
    if (!m_camera || !m_renderWindow) return false;
    Ogre::Vector3 eye = m_camera->getViewMatrix() * world;
    if (eye.z <= 0) return false; // behind camera
    Ogre::Vector3 clip = m_camera->getProjectionMatrix() * eye;
    if (clip.z < -1 || clip.z > 1) return false;
    sx = (clip.x * 0.5f + 0.5f) * width();
    sy = (1.0f - (clip.y * 0.5f + 0.5f)) * height();
    return true;
}

Ogre::Ray OgreWidget::screenRay(int screenX, int screenY) const
{
    if (!m_camera) return Ogre::Ray();
    float nx = float(screenX) / width();
    float ny = float(screenY) / height();
    return m_camera->getCameraToViewportRay(nx, ny);
}

bool OgreWidget::rayOnGizmoPlane(int screenX, int screenY,
                                 const Ogre::Vector3& origin, Ogre::Vector3& hit) const
{
    if (!m_camera) return false;
    Ogre::Ray ray = screenRay(screenX, screenY);
    // Plane through origin facing the camera.
    Ogre::Vector3 normal = -m_camera->getDerivedDirection().normalisedCopy();
    Ogre::Plane plane(normal, origin);
    auto hitPair = ray.intersects(plane);
    if (!hitPair.first) return false;
    hit = ray.getPoint(hitPair.second);
    return true;
}

GizmoAxis OgreWidget::pickGizmoAxis(int screenX, int screenY)
{
    if (m_transformMode == TransformMode::None) return GizmoAxis::None;
    QString selId = m_world.primarySelection();
    if (selId.isEmpty()) return GizmoAxis::None;
    world::Actor* a = m_world.findActor(selId);
    if (!a || !m_gizmoNode) return GizmoAxis::None;

    Ogre::Vector3 origin(a->transform.posX, a->transform.posY, a->transform.posZ);
    float s = gizmoWorldScale(origin);

    // Project the three axis endpoints to screen and pick the one whose
    // screen segment passes closest to the cursor within a pixel threshold.
    auto segmentDist = [](float ax, float ay, float bx, float by,
                          float px, float py) -> float {
        float dx = bx - ax, dy = by - ay;
        float len2 = dx * dx + dy * dy;
        if (len2 < 1e-6f) return std::hypot(px - ax, py - ay);
        float t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = std::clamp(t, 0.0f, 1.0f);
        float cx = ax + t * dx, cy = ay + t * dy;
        return std::hypot(px - cx, py - cy);
    };

    struct AxisScreen { GizmoAxis axis; float ax, ay, bx, by; };
    std::vector<AxisScreen> segs;

    auto addAxis = [&](GizmoAxis axis, const Ogre::Vector3& dir) {
        Ogre::Vector3 start = origin;
        Ogre::Vector3 end = origin + dir * s;
        float sx0, sy0, sx1, sy1;
        if (!projectToScreen(start, sx0, sy0)) return;
        if (!projectToScreen(end, sx1, sy1))   return;
        segs.push_back({axis, sx0, sy0, sx1, sy1});
    };

    if (m_transformMode == TransformMode::Move ||
        m_transformMode == TransformMode::Scale) {
        addAxis(GizmoAxis::X, Ogre::Vector3::UNIT_X);
        addAxis(GizmoAxis::Y, Ogre::Vector3::UNIT_Y);
        addAxis(GizmoAxis::Z, Ogre::Vector3::UNIT_Z);
    } else if (m_transformMode == TransformMode::Rotate) {
        // For rotate, the hit target is the ring; approximate by testing the
        // screen distance from the cursor to the projected ring centre along
        // each axis. We pick the axis whose ring plane is most facing the
        // camera (i.e. the projected ring is largest), but to keep it simple
        // and robust we test distance from cursor to each ring's projected
        // circle by sampling points on it.
        auto addRing = [&](GizmoAxis axis, const Ogre::Vector3& u,
                           const Ogre::Vector3& v) {
            const int N = 32;
            float best = 1e9f;
            for (int i = 0; i < N; ++i) {
                float ang = float(i) / N * 6.2831853f;
                Ogre::Vector3 p = origin + (u * cos(ang) + v * sin(ang)) * s;
                float sxp, syp;
                if (!projectToScreen(p, sxp, syp)) continue;
                float d = std::hypot(sxp - screenX, syp - screenY);
                best = std::min(best, d);
            }
            segs.push_back({axis, float(screenX), float(screenY),
                            float(screenX) + best, float(screenY)});
        };
        addRing(GizmoAxis::X, Ogre::Vector3::UNIT_Y, Ogre::Vector3::UNIT_Z);
        addRing(GizmoAxis::Y, Ogre::Vector3::UNIT_X, Ogre::Vector3::UNIT_Z);
        addRing(GizmoAxis::Z, Ogre::Vector3::UNIT_X, Ogre::Vector3::UNIT_Y);
    }

    const float threshold = 12.0f; // pixels
    GizmoAxis best = GizmoAxis::None;
    float bestDist = threshold;
    for (const auto& seg : segs) {
        float d = segmentDist(seg.ax, seg.ay, seg.bx, seg.by,
                               float(screenX), float(screenY));
        if (d < bestDist) { bestDist = d; best = seg.axis; }
    }
    return best;
}

void OgreWidget::beginGizmoDrag(GizmoAxis axis, int screenX, int screenY)
{
    if (axis == GizmoAxis::None) return;
    QString selId = m_world.primarySelection();
    if (selId.isEmpty()) return;
    world::Actor* a = m_world.findActor(selId);
    if (!a) return;

    m_activeGizmoAxis = axis;
    m_gizmoDragging = true;
    m_gizmoStartX = screenX;
    m_gizmoStartY = screenY;
    m_gizmoStartTransform = a->transform;

    // For move/scale we record the initial ray-plane hit so drags are
    // measured relative to it. For rotate we record the initial angle
    // between the cursor and the axis origin on the gizmo plane.
    Ogre::Vector3 origin(a->transform.posX, a->transform.posY, a->transform.posZ);
    Ogre::Vector3 hit;
    if (rayOnGizmoPlane(screenX, screenY, origin, hit)) {
        Ogre::Vector3 delta = hit - origin;
        if (m_transformMode == TransformMode::Move) {
            m_gizmoStartValue = delta.dotProduct(gizmoAxisVector(axis));
        } else if (m_transformMode == TransformMode::Scale) {
            m_gizmoStartValue = delta.dotProduct(gizmoAxisVector(axis));
        } else if (m_transformMode == TransformMode::Rotate) {
            // Angle of the cursor around the axis on the gizmo plane.
            Ogre::Vector3 u, v;
            if (axis == GizmoAxis::X) { u = Ogre::Vector3::UNIT_Y; v = Ogre::Vector3::UNIT_Z; }
            else if (axis == GizmoAxis::Y) { u = Ogre::Vector3::UNIT_X; v = Ogre::Vector3::UNIT_Z; }
            else                            { u = Ogre::Vector3::UNIT_X; v = Ogre::Vector3::UNIT_Y; }
            float ang = atan2f(delta.dotProduct(v), delta.dotProduct(u));
            m_gizmoStartValue = ang;
        }
    } else {
        m_gizmoStartValue = 0.0f;
    }
}

void OgreWidget::applyGizmoDrag(int screenX, int screenY)
{
    if (!m_gizmoDragging || m_activeGizmoAxis == GizmoAxis::None) return;
    QString selId = m_world.primarySelection();
    if (selId.isEmpty()) return;
    world::Actor* a = m_world.findActor(selId);
    if (!a) return;

    Ogre::Vector3 origin(m_gizmoStartTransform.posX,
                         m_gizmoStartTransform.posY,
                         m_gizmoStartTransform.posZ);
    Ogre::Vector3 hit;
    if (!rayOnGizmoPlane(screenX, screenY, origin, hit)) return;

    Ogre::Vector3 delta = hit - origin;
    Ogre::Vector3 axisVec = gizmoAxisVector(m_activeGizmoAxis);
    world::Transform t = m_gizmoStartTransform;

    if (m_transformMode == TransformMode::Move) {
        float along = delta.dotProduct(axisVec) - m_gizmoStartValue;
        if (m_snapEnabled) along = std::round(along / m_snapSize) * m_snapSize;
        if (m_activeGizmoAxis == GizmoAxis::X) t.posX += along;
        else if (m_activeGizmoAxis == GizmoAxis::Y) t.posY += along;
        else                                        t.posZ += along;
    } else if (m_transformMode == TransformMode::Scale) {
        float along = delta.dotProduct(axisVec) - m_gizmoStartValue;
        // Map world-space drag to a scale delta relative to the start scale.
        float baseScale = 1.0f;
        if (m_activeGizmoAxis == GizmoAxis::X) baseScale = m_gizmoStartTransform.scaleX;
        else if (m_activeGizmoAxis == GizmoAxis::Y) baseScale = m_gizmoStartTransform.scaleY;
        else                                        baseScale = m_gizmoStartTransform.scaleZ;
        // Use gizmo world size as a reference so the drag feels consistent.
        float ref = gizmoWorldScale(origin);
        float factor = 1.0f + (along / std::max(1.0f, ref));
        factor = std::max(0.05f, factor);
        if (m_activeGizmoAxis == GizmoAxis::X) t.scaleX = baseScale * factor;
        else if (m_activeGizmoAxis == GizmoAxis::Y) t.scaleY = baseScale * factor;
        else                                        t.scaleZ = baseScale * factor;
    } else if (m_transformMode == TransformMode::Rotate) {
        Ogre::Vector3 u, v;
        if (m_activeGizmoAxis == GizmoAxis::X) { u = Ogre::Vector3::UNIT_Y; v = Ogre::Vector3::UNIT_Z; }
        else if (m_activeGizmoAxis == GizmoAxis::Y) { u = Ogre::Vector3::UNIT_X; v = Ogre::Vector3::UNIT_Z; }
        else                                        { u = Ogre::Vector3::UNIT_X; v = Ogre::Vector3::UNIT_Y; }
        float ang = atan2f(delta.dotProduct(v), delta.dotProduct(u));
        float deltaAng = ang - m_gizmoStartValue;
        if (m_snapEnabled) {
            float snapRad = Ogre::Degree(15.0f).valueRadians();
            deltaAng = std::round(deltaAng / snapRad) * snapRad;
        }
        float deg = Ogre::Radian(deltaAng).valueDegrees();
        if (m_activeGizmoAxis == GizmoAxis::X) t.rotX = m_gizmoStartTransform.rotX + deg;
        else if (m_activeGizmoAxis == GizmoAxis::Y) t.rotY = m_gizmoStartTransform.rotY + deg;
        else                                        t.rotZ = m_gizmoStartTransform.rotZ + deg;
    }

    // Update the actor's transform in the world model directly during the
    // drag for live feedback; the final undo command is pushed on release.
    a->transform = t;
    a->touch();
    auto it = m_actorRenders.find(selId);
    if (it != m_actorRenders.end() && it->second.node) {
        it->second.node->setPosition(Ogre::Vector3(t.posX, t.posY, t.posZ));
        Ogre::Radian ry(Ogre::Degree(t.rotY));
        Ogre::Radian rx(Ogre::Degree(t.rotX));
        Ogre::Radian rz(Ogre::Degree(t.rotZ));
        Ogre::Quaternion q = Ogre::Quaternion(rz, Ogre::Vector3::UNIT_Z)
                           * Ogre::Quaternion(rx, Ogre::Vector3::UNIT_X)
                           * Ogre::Quaternion(ry, Ogre::Vector3::UNIT_Y);
        it->second.node->setOrientation(q);
        it->second.node->setScale(Ogre::Vector3(t.scaleX, t.scaleY, t.scaleZ));
    }
    updateGizmo();
    emit actorTransformed(selId);
}

void OgreWidget::endGizmoDrag()
{
    if (!m_gizmoDragging) return;
    m_gizmoDragging = false;
    QString selId = m_world.primarySelection();
    m_activeGizmoAxis = GizmoAxis::None;

    if (selId.isEmpty()) return;
    world::Actor* a = m_world.findActor(selId);
    if (!a) return;

    // Push a single undoable command capturing the full transform delta.
    // We revert the live edit first so redo() applies the new transform.
    world::Transform newT = a->transform;
    a->transform = m_gizmoStartTransform;
    a->touch();
    auto it = m_actorRenders.find(selId);
    if (it != m_actorRenders.end() && it->second.node) {
        const auto& t = m_gizmoStartTransform;
        it->second.node->setPosition(Ogre::Vector3(t.posX, t.posY, t.posZ));
        Ogre::Radian ry(Ogre::Degree(t.rotY));
        it->second.node->setOrientation(Ogre::Quaternion(ry, Ogre::Vector3::UNIT_Y));
        it->second.node->setScale(Ogre::Vector3(t.scaleX, t.scaleY, t.scaleZ));
    }
    m_undoStack.push(new world::TransformActorCommand(
        &m_world, selId, m_gizmoStartTransform, newT));
    updateGizmo();
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
    // Keep the gizmo screen-constant as the camera moves.
    updateGizmo();
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

        // If a gizmo is visible, test gizmo axis hits FIRST so that clicking
        // an axis does not also re-pick the actor or orbit the camera.
        if (m_transformMode != TransformMode::None) {
            GizmoAxis axis = pickGizmoAxis(event->x(), event->y());
            if (axis != GizmoAxis::None) {
                beginGizmoDrag(axis, event->x(), event->y());
                return;
            }
        }

        // Otherwise try to pick an actor.
        QString pickedId = pickActor(event->x(), event->y());
        if (!pickedId.isEmpty()) {
            selectActor(pickedId);
            // If the newly selected actor has a gizmo, allow starting a drag
            // immediately on the same press (common editor behaviour).
            if (m_transformMode != TransformMode::None) {
                GizmoAxis axis = pickGizmoAxis(event->x(), event->y());
                if (axis != GizmoAxis::None)
                    beginGizmoDrag(axis, event->x(), event->y());
            }
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
    if (event->button() == Qt::LeftButton) {
        if (m_gizmoDragging) endGizmoDrag();
        m_leftDown = false;
    } else if (event->button() == Qt::RightButton) {
        m_rightDown = false;
    } else if (event->button() == Qt::MiddleButton) {
        m_middleDown = false;
    }
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
    if (event->key() == Qt::Key_Q && !m_flyMode)
        setTransformMode(TransformMode::None);
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

    Ogre::Ray ray = screenRay(screenX, screenY);

    // Intersect with ground plane (y=0) as a fallback for empty-space clicks.
    if (std::abs(ray.getDirection().y) > 1e-5f) {
        float t = -ray.getOrigin().y / ray.getDirection().y;
        if (t > 0) {
            Ogre::Vector3 pos = ray.getPoint(t);
            worldPos = {pos.x, pos.y, pos.z};
            return true;
        }
    }
    return false;
}

QString OgreWidget::pickActor(int screenX, int screenY)
{
    if (!m_camera) return QString();

    Ogre::Ray ray = screenRay(screenX, screenY);

    QString bestId;
    float bestT = std::numeric_limits<float>::max();

    for (const auto& actor : m_world.actors) {
        if (!actor.visible || !actor.selectable) continue;
        if (!isLayerVisible(actor.layerId) || isLayerLocked(actor.layerId)) continue;

        // Build a world-space AABB from the actor's transform. The actor
        // render mesh is a unit cube centred at origin, so the local AABB
        // is [-0.5, 0.5]^3 scaled by the actor's per-axis scale.
        Ogre::Vector3 center(actor.transform.posX,
                             actor.transform.posY,
                             actor.transform.posZ);
        Ogre::Vector3 half(actor.transform.scaleX * 0.5f,
                           actor.transform.scaleY * 0.5f,
                           actor.transform.scaleZ * 0.5f);

        // If we have a real render entry, prefer its world AABB which
        // accounts for the actual mesh bounds.
        auto it = m_actorRenders.find(actor.id);
        if (it != m_actorRenders.end() && it->second.item) {
            Ogre::Aabb aabb = it->second.item->getWorldAabb();
            center = aabb.mCenter;
            half = aabb.mHalfSize;
        }

        // Ray/AABB intersection (slab method).
        Ogre::Vector3 minP = center - half;
        Ogre::Vector3 maxP = center + half;
        float tmin = 0.0f, tmax = bestT;
        for (int i = 0; i < 3; ++i) {
            float o = ray.getOrigin()[i];
            float d = ray.getDirection()[i];
            if (std::abs(d) < 1e-8f) {
                if (o < minP[i] || o > maxP[i]) { tmin = -1; break; }
            } else {
                float inv = 1.0f / d;
                float t1 = (minP[i] - o) * inv;
                float t2 = (maxP[i] - o) * inv;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) { tmin = -1; break; }
            }
        }
        if (tmin >= 0 && tmin < bestT) {
            bestT = tmin;
            bestId = actor.id;
        } else if (tmax >= 0 && tmax < bestT) {
            // Origin inside the box — pick the exit point.
            bestT = tmax;
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
    destroyLighting();
    destroyGizmo();
    destroyGrid();
    if (m_root) {
        delete m_root;
        m_root = nullptr;
    }
    m_initialized = false;
}
