#include "OgreWidget.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QTimer>
#include <QDir>
#include <QDebug>

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
#include "OgreStagingTexture.h"
#include "OgreResourceGroupManager.h"

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

void OgreWidget::exposeEvent(QExposeEvent* event)
{
    Q_UNUSED(event);
    if (isExposed() && !m_initialized) {
        initOgre();
        setupScene();
        m_timerId = startTimer(16);
        m_initialized = true;
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

void OgreWidget::initOgre()
{
    // OGRE DLLs and plugins are deployed alongside the exe
    QString appDir = QCoreApplication::applicationDirPath();
    QString pluginsCfg = appDir + "/plugins.cfg";

    // Debug log file â€” write directly to file so we can see where it crashes
    QString debugLogPath = appDir + "/ogre_debug.log";
    QFile debugLog(debugLogPath);
    debugLog.open(QIODevice::WriteOnly | QIODevice::Append);
    auto logStep = [&](const QString& msg) {
        debugLog.write((QDateTime::currentDateTime().toString("hh:mm:ss.zzz ") + msg + "\n").toUtf8());
        debugLog.flush();
    };
    // Create plugins.cfg pointing to the app directory
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

    // Create Root with AbiCookie (required by OGRE-Next 4.0)
    Ogre::AbiCookie abiCookie = Ogre::generateAbiCookie();
    m_root = OGRE_NEW Ogre::Root(&abiCookie,
                                 pluginsCfg.toStdString(),
                                 appDir.toStdString() + "/ogre.cfg",
                                 appDir.toStdString() + "/ogre.log",
                                 "OpenGeoStudio");

    // Use D3D11 on Windows
    Ogre::RenderSystemList rsList = m_root->getAvailableRenderers();
    for (auto* rs : rsList) {
        if (rs->getName().find("Direct3D11") != std::string::npos) {
            m_root->setRenderSystem(rs);
            break;
        }
    }
    if (!m_root->getRenderSystem()) {
        qWarning() << "No D3D11 render system found!";
        return;
    }

    m_root->initialise(false);
    // Create render window using Qt's window handle
    Ogre::NameValuePairList params;
    params["externalWindowHandle"] = Ogre::StringConverter::toString(
        (size_t)winId());

    m_renderWindow = m_root->createRenderWindow("OgreWindow",
        width(), height(), false, &params);
    if (!m_renderWindow) {
        qWarning() << "[OGRE] Failed to create render window!";
        return;
    }
    logStep(QString("Render window created: %1x%2").arg(width()).arg(height()));

    // Create scene manager â€” use instance name like the samples
    m_sceneManager = m_root->createSceneManager(Ogre::ST_GENERIC, 2, "MainSM");
    if (!m_sceneManager) {
        qWarning() << "[OGRE] Failed to create scene manager!";
        return;
    }
    // Create camera (OGRE-Next 4.0: cameras are NOT attached to SceneNodes)
    m_camera = m_sceneManager->createCamera("MainCamera");
    if (!m_camera) {
        qWarning() << "[OGRE] Failed to create camera!";
        return;
    }
    m_camera->setNearClipDistance(0.5f);
    m_camera->setFarClipDistance(50000.0f);
    m_camera->setAutoAspectRatio(true);
    m_camera->setPosition(Ogre::Vector3(0, 300, 500));
    m_camera->lookAt(Ogre::Vector3(0, 0, 0));
    // Setup compositor (replaces Viewport in OGRE-Next)
    Ogre::CompositorManager2* compositorManager = m_root->getCompositorManager2();
    if (!compositorManager) {
        qWarning() << "[OGRE] No compositor manager!";
        return;
    }
    Ogre::ColourValue bgColor(0.2f, 0.4f, 0.6f, 1.0f);
    if (!compositorManager->hasWorkspaceDefinition("MainWorkspace")) {
        compositorManager->createBasicWorkspaceDef("MainWorkspace", bgColor,
            Ogre::IdString());
    }
    compositorManager->addWorkspace(m_sceneManager,
        m_renderWindow->getTexture(), m_camera, "MainWorkspace", true);
    // Setup Hlms (shader system) â€” use getDefaultPaths like the OGRE samples
    {
        QString hlmsFolder = appDir + "/";
        Ogre::String rootHlmsFolder = hlmsFolder.toStdString();

        Ogre::ArchiveManager& archMgr = Ogre::ArchiveManager::getSingleton();
        Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();

        // PBS
        {
            Ogre::String mainFolderPath;
            Ogre::StringVector libraryFoldersPaths;
            Ogre::HlmsPbs::getDefaultPaths(mainFolderPath, libraryFoldersPaths);
            logStep("PBS main folder: " + QString::fromStdString(mainFolderPath));

            Ogre::Archive* archivePbs = archMgr.load(
                rootHlmsFolder + mainFolderPath, "FileSystem", true);

            Ogre::ArchiveVec archivePbsLibraryFolders;
            for (const auto& libPath : libraryFoldersPaths) {
                logStep("PBS lib folder: " + QString::fromStdString(libPath));
                Ogre::Archive* archiveLibrary = archMgr.load(
                    rootHlmsFolder + libPath, "FileSystem", true);
                archivePbsLibraryFolders.push_back(archiveLibrary);
            }

            Ogre::HlmsPbs* hlmsPbs = OGRE_NEW Ogre::HlmsPbs(
                archivePbs, &archivePbsLibraryFolders);
            hlmsManager->registerHlms(hlmsPbs);
        }

        // Unlit
        {
            Ogre::String mainFolderPath;
            Ogre::StringVector libraryFoldersPaths;
            Ogre::HlmsUnlit::getDefaultPaths(mainFolderPath, libraryFoldersPaths);
            logStep("Unlit main folder: " + QString::fromStdString(mainFolderPath));

            Ogre::Archive* archiveUnlit = archMgr.load(
                rootHlmsFolder + mainFolderPath, "FileSystem", true);

            Ogre::ArchiveVec archiveUnlitLibraryFolders;
            for (const auto& libPath : libraryFoldersPaths) {
                logStep("Unlit lib folder: " + QString::fromStdString(libPath));
                Ogre::Archive* archiveLibrary = archMgr.load(
                    rootHlmsFolder + libPath, "FileSystem", true);
                archiveUnlitLibraryFolders.push_back(archiveLibrary);
            }

            Ogre::HlmsUnlit* hlmsUnlit = OGRE_NEW Ogre::HlmsUnlit(
                archiveUnlit, &archiveUnlitLibraryFolders);
            hlmsManager->registerHlms(hlmsUnlit);
        }
    }
    // Lighting - OGRE-Next 4.0: lights MUST be attached to a SceneNode
    // BEFORE calling setDirection() (it redirects to the node)
    m_sceneManager->setAmbientLight(Ogre::ColourValue(0.3f, 0.3f, 0.3f),
                                    Ogre::ColourValue(0.1f, 0.1f, 0.1f),
                                    Ogre::Vector3(0, 1, 0));
    Ogre::Light* dirLight = m_sceneManager->createLight();
    dirLight->setType(Ogre::Light::LT_DIRECTIONAL);
    dirLight->setDiffuseColour(Ogre::ColourValue(1.0f, 1.0f, 0.95f));
    Ogre::SceneNode* lightNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    lightNode->attachObject(dirLight);
    lightNode->setDirection(Ogre::Vector3(-0.5f, -1.0f, -0.3f).normalisedCopy());

    qDebug() << "OGRE-Next initialized successfully";
}

void OgreWidget::setupScene()
{
    // No mesh files deployed - terrain loaded separately via loadTerrain()
}

void OgreWidget::setupTerra()
{
    // TODO: Implement Terra terrain loading
    // Terra requires CompositorManager2 which needs more setup
}

void OgreWidget::loadTerrain(const QString& heightmapPath, const QString& albedoPath,
                              float terrainSize, float heightScale)
{
    m_heightmapPath = heightmapPath;
    m_albedoPath = albedoPath;
    m_terrainSize = terrainSize;
    m_heightScale = heightScale;

    if (!m_initialized) {
        qDebug() << "OGRE not initialized yet, terrain will load after init";
        return;
    }

    if (!QFile::exists(heightmapPath)) {
        qWarning() << "Heightmap not found:" << heightmapPath;
        return;
    }

    clearTerrain();

    // Load heightmap image
    QImage heightmap(heightmapPath);
    if (heightmap.isNull()) {
        qWarning() << "Failed to load heightmap image:" << heightmapPath;
        return;
    }
    heightmap = heightmap.convertToFormat(QImage::Format_Grayscale8);

    int imgW = heightmap.width();
    int imgH = heightmap.height();
    qDebug() << "Terrain load:" << heightmapPath << "  " << imgW << "x" << imgH
             << "  size=" << terrainSize << "m  hScale=" << heightScale;

    // Cap grid resolution to avoid excessive vertex counts
    const int maxGridDim = 200;
    int gridW = imgW;
    int gridH = imgH;
    while (gridW > maxGridDim || gridH > maxGridDim) {
        gridW /= 2;
        gridH /= 2;
    }
    if (gridW < 2) gridW = 2;
    if (gridH < 2) gridH = 2;

    qDebug() << "  Grid resolution:" << gridW << "x" << gridH;

    // Create manual object for terrain mesh
    Ogre::ManualObject* manual = m_sceneManager->createManualObject();
    manual->setName("TerrainMesh");

    // Create datablock — use Unlit so we can apply albedo texture
    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsUnlit = hlmsManager->getHlms(Ogre::HLMS_UNLIT);
    Ogre::String dblockName = "TerrainDatablock";
    Ogre::HlmsDatablock* dbBase = hlmsUnlit->getDatablock(Ogre::IdString(dblockName));
    if (!dbBase) {
        dbBase = hlmsUnlit->createDatablock(
            Ogre::IdString(dblockName), dblockName,
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
    }
    Ogre::HlmsUnlitDatablock* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(dbBase);

    // Load albedo texture if available
    if (!albedoPath.isEmpty() && QFile::exists(albedoPath)) {
        qDebug() << "  Loading albedo:" << albedoPath;
        // Use setTexture with filename — OGRE will load it via TextureGpuManager
        Ogre::String texName = albedoPath.toStdString();
        datablock->setTexture(0, texName);
    } else {
        datablock->setColour(Ogre::ColourValue(0.3f, 0.4f, 0.25f, 1.0f));
    }

    manual->begin(dblockName, Ogre::OT_TRIANGLE_LIST);

    // Generate vertices — sample heightmap at grid resolution
    float halfSize = terrainSize * 0.5f;
    float stepX = terrainSize / (gridW - 1);
    float stepZ = terrainSize / (gridH - 1);

    for (int gz = 0; gz < gridH; gz++) {
        for (int gx = 0; gx < gridW; gx++) {
            int px = (gx * (imgW - 1)) / (gridW - 1);
            int py = (gz * (imgH - 1)) / (gridH - 1);
            
            QRgb pixel = heightmap.pixel(px, py);
            int gray = qGray(pixel);
            float elevation = (gray / 255.0f) * heightScale;

            float worldX = -halfSize + gx * stepX;
            float worldZ = -halfSize + gz * stepZ;

            manual->position(worldX, elevation, worldZ);
            
            // Compute normal from neighboring pixels
            int pxL = (px > 0) ? px - 1 : 0;
            int pxR = (px < imgW - 1) ? px + 1 : imgW - 1;
            int pyU = (py > 0) ? py - 1 : 0;
            int pyD = (py < imgH - 1) ? py + 1 : imgH - 1;
            float hL = (qGray(heightmap.pixel(pxL, py)) / 255.0f) * heightScale;
            float hR = (qGray(heightmap.pixel(pxR, py)) / 255.0f) * heightScale;
            float hU = (qGray(heightmap.pixel(px, pyU)) / 255.0f) * heightScale;
            float hD = (qGray(heightmap.pixel(px, pyD)) / 255.0f) * heightScale;
            float nx = (hL - hR);
            float ny = 2.0f * (stepX / terrainSize) * heightScale;
            float nz = (hU - hD);
            float nLen = sqrt(nx * nx + ny * ny + nz * nz);
            if (nLen > 0.0001f) {
                manual->normal(nx / nLen, ny / nLen, nz / nLen);
            } else {
                manual->normal(0.0f, 1.0f, 0.0f);
            }

            float u = (float)gx / (gridW - 1);
            float v = (float)gz / (gridH - 1);
            manual->textureCoord(u, v);
        }
    }

    // Generate indices — two triangles per grid cell
    for (int gz = 0; gz < gridH - 1; gz++) {
        for (int gx = 0; gx < gridW - 1; gx++) {
            uint32_t v00 = gz * gridW + gx;
            uint32_t v10 = gz * gridW + (gx + 1);
            uint32_t v01 = (gz + 1) * gridW + gx;
            uint32_t v11 = (gz + 1) * gridW + (gx + 1);

            manual->index(v00);
            manual->index(v01);
            manual->index(v10);
            manual->index(v10);
            manual->index(v01);
            manual->index(v11);
        }
    }

    manual->end();

    // Convert to mesh and create scene item
    Ogre::String meshName = "TerrainMesh_" + Ogre::StringConverter::toString((size_t)this);
    Ogre::MeshPtr mesh = manual->convertToMesh(
        meshName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    m_terrainItem = m_sceneManager->createItem(mesh, Ogre::SCENE_DYNAMIC);
    m_terrainNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    m_terrainNode->attachObject(m_terrainItem);

    m_sceneManager->destroyManualObject(manual);

    qDebug() << "Terrain mesh created:" << gridW << "x" << gridH
             << "=" << (gridW * gridH) << "vertices,"
             << ((gridW - 1) * (gridH - 1) * 2) << "triangles";

    // Frame camera on terrain
    m_camTargetX = 0;
    m_camTargetY = heightScale * 0.3f;
    m_camTargetZ = 0;
    m_camDist = terrainSize * 0.8f;
    m_camYaw = 0;
    m_camPitch = -30.0f;
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
}
void OgreWidget::loadRoads(const QString& xodrPath)
{
    m_xodrPath = xodrPath;

    if (!m_initialized) {
        qDebug() << "OGRE not initialized yet, roads will load after init";
        return;
    }

    if (!QFile::exists(xodrPath)) {
        qWarning() << "XODR file not found:" << xodrPath;
        return;
    }

    clearRoads();

    odr::OpenDriveMap odrMap(xodrPath.toStdString());
    auto roads = odrMap.get_roads();

    if (roads.empty()) {
        qWarning() << "No roads found in XODR file:" << xodrPath;
        return;
    }

    qDebug() << "Loading" << roads.size() << "roads from" << xodrPath;

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
                for (const auto& ls : laneSections) {
                    if (ls.s0 == lanesection_s0) {
                        double totalWidth = 0;
                        for (const auto& lp : ls.id_to_lane) {
                            if (lp.first != 0) {
                                totalWidth += lp.second.lane_width.get(s0 - ls.s0);
                            }
                        }
                        halfWidth = static_cast<float>(totalWidth * 0.5);
                        if (halfWidth < 1.0f) halfWidth = defaultHalfWidth;
                        break;
                    }
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
        meshName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    m_roadItem = m_sceneManager->createItem(mesh, Ogre::SCENE_DYNAMIC);
    m_roadNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    m_roadNode->attachObject(m_roadItem);

    m_sceneManager->destroyManualObject(manual);
    qDebug() << "Road mesh created with" << vertexOffset << "vertices";
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
// SceneObject JSON serialization
// ============================================================
QJsonObject SceneObject::toJson() const
{
    QJsonObject j;
    j["id"] = id;
    j["type"] = type;
    j["name"] = name;
    j["posX"] = posX;
    j["posY"] = posY;
    j["posZ"] = posZ;
    j["rotY"] = rotY;
    j["scaleX"] = scaleX;
    j["scaleY"] = scaleY;
    j["scaleZ"] = scaleZ;
    j["colorR"] = colorR;
    j["colorG"] = colorG;
    j["colorB"] = colorB;
    return j;
}

SceneObject SceneObject::fromJson(const QJsonObject& j)
{
    SceneObject o;
    o.id = j["id"].toString();
    o.type = j["type"].toString();
    o.name = j["name"].toString();
    o.posX = static_cast<float>(j["posX"].toDouble());
    o.posY = static_cast<float>(j["posY"].toDouble());
    o.posZ = static_cast<float>(j["posZ"].toDouble());
    o.rotY = static_cast<float>(j["rotY"].toDouble());
    o.scaleX = static_cast<float>(j["scaleX"].toDouble(1.0));
    o.scaleY = static_cast<float>(j["scaleY"].toDouble(1.0));
    o.scaleZ = static_cast<float>(j["scaleZ"].toDouble(1.0));
    o.colorR = static_cast<float>(j["colorR"].toDouble(0.8));
    o.colorG = static_cast<float>(j["colorG"].toDouble(0.8));
    o.colorB = static_cast<float>(j["colorB"].toDouble(0.8));
    return o;
}

// ============================================================
// Object placement
// ============================================================
QString OgreWidget::addObject(const QString& type, float x, float y, float z,
                              float rotY, float sx, float sy, float sz)
{
    if (!m_initialized || !m_sceneManager) return QString();

    // Generate unique ID
    QString id = type + "_" + QString::number(m_objects.size() + 1);
    int suffix = 1;
    while (m_objects.count(id)) {
        suffix++;
        id = type + "_" + QString::number(m_objects.size() + suffix);
    }

    SceneObject obj;
    obj.id = id;
    obj.type = type;
    obj.name = type + " " + QString::number(m_objects.size() + 1);
    obj.posX = x;
    obj.posY = y;
    obj.posZ = z;
    obj.rotY = rotY;
    obj.scaleX = sx;
    obj.scaleY = sy;
    obj.scaleZ = sz;

    // Set color based on type
    if (type == "building") {
        obj.colorR = 0.6f; obj.colorG = 0.5f; obj.colorB = 0.4f;
    } else if (type == "tree") {
        obj.colorR = 0.2f; obj.colorG = 0.5f; obj.colorB = 0.2f;
    } else {
        obj.colorR = 0.8f; obj.colorG = 0.8f; obj.colorB = 0.8f;
    }

    rebuildObject(obj);
    return id;
}

void OgreWidget::rebuildObject(const SceneObject& obj)
{
    if (!m_sceneManager) return;

    // Remove existing entry if present
    auto it = m_objects.find(obj.id);
    if (it != m_objects.end()) {
        if (it->second.node) {
            it->second.node->detachObject(it->second.item);
            m_sceneManager->destroySceneNode(it->second.node);
        }
        if (it->second.item) {
            m_sceneManager->destroyItem(it->second.item);
        }
    }

    // Create a manual mesh for the object (a box)
    Ogre::ManualObject* manual = m_sceneManager->createManualObject();
    Ogre::String moName = "Obj_" + obj.id.toStdString();
    manual->setName(moName);

    // Create datablock with object color
    Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();
    Ogre::Hlms* hlmsUnlit = hlmsManager->getHlms(Ogre::HLMS_UNLIT);
    Ogre::String dblockName = "ObjDb_" + obj.id.toStdString();
    Ogre::HlmsDatablock* dbBase = hlmsUnlit->getDatablock(Ogre::IdString(dblockName));
    if (!dbBase) {
        dbBase = hlmsUnlit->createDatablock(
            Ogre::IdString(dblockName), dblockName,
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec());
    }
    Ogre::HlmsUnlitDatablock* datablock = static_cast<Ogre::HlmsUnlitDatablock*>(dbBase);
    datablock->setColour(Ogre::ColourValue(obj.colorR, obj.colorG, obj.colorB, 1.0f));

    // Build a unit cube centered at origin
    manual->begin(dblockName, Ogre::OT_TRIANGLE_LIST);

    // 8 vertices of a unit cube (-0.5 to 0.5)
    // Front face
    manual->position(-0.5f, -0.5f, 0.5f);  manual->normal(0, 0, 1);  manual->textureCoord(0, 0);
    manual->position(0.5f, -0.5f, 0.5f);   manual->normal(0, 0, 1);  manual->textureCoord(1, 0);
    manual->position(0.5f, 0.5f, 0.5f);    manual->normal(0, 0, 1);  manual->textureCoord(1, 1);
    manual->position(-0.5f, 0.5f, 0.5f);   manual->normal(0, 0, 1);  manual->textureCoord(0, 1);
    // Back face
    manual->position(-0.5f, -0.5f, -0.5f); manual->normal(0, 0, -1); manual->textureCoord(0, 0);
    manual->position(0.5f, -0.5f, -0.5f);  manual->normal(0, 0, -1); manual->textureCoord(1, 0);
    manual->position(0.5f, 0.5f, -0.5f);   manual->normal(0, 0, -1); manual->textureCoord(1, 1);
    manual->position(-0.5f, 0.5f, -0.5f);  manual->normal(0, 0, -1); manual->textureCoord(0, 1);

    // Front face triangles (0,1,2) (0,2,3)
    manual->index(0); manual->index(1); manual->index(2);
    manual->index(0); manual->index(2); manual->index(3);
    // Back face triangles (5,4,7) (5,7,6)
    manual->index(5); manual->index(4); manual->index(7);
    manual->index(5); manual->index(7); manual->index(6);
    // Left face triangles (4,0,3) (4,3,7)
    manual->index(4); manual->index(0); manual->index(3);
    manual->index(4); manual->index(3); manual->index(7);
    // Right face triangles (1,5,6) (1,6,2)
    manual->index(1); manual->index(5); manual->index(6);
    manual->index(1); manual->index(6); manual->index(2);
    // Top face triangles (3,2,6) (3,6,7)
    manual->index(3); manual->index(2); manual->index(6);
    manual->index(3); manual->index(6); manual->index(7);
    // Bottom face triangles (4,5,1) (4,1,0)
    manual->index(4); manual->index(5); manual->index(1);
    manual->index(4); manual->index(1); manual->index(0);

    manual->end();

    // Convert to mesh
    Ogre::String meshName = "ObjMesh_" + obj.id.toStdString();
    Ogre::MeshPtr mesh = manual->convertToMesh(
        meshName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    Ogre::Item* item = m_sceneManager->createItem(mesh, Ogre::SCENE_DYNAMIC);
    Ogre::SceneNode* node = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    node->attachObject(item);

    // Set position, rotation, scale
    node->setPosition(Ogre::Vector3(obj.posX, obj.posY, obj.posZ));
    float rotRad = obj.rotY * 3.14159265358979f / 180.0f;
    node->setOrientation(Ogre::Quaternion(Ogre::Radian(rotRad), Ogre::Vector3::UNIT_Y));
    node->setScale(Ogre::Vector3(obj.scaleX, obj.scaleY, obj.scaleZ));

    m_sceneManager->destroyManualObject(manual);

    // Store in map
    ObjectEntry entry;
    entry.data = obj;
    entry.item = item;
    entry.node = node;
    m_objects[obj.id] = entry;
}

void OgreWidget::removeObject(const QString& id)
{
    auto it = m_objects.find(id);
    if (it == m_objects.end()) return;

    if (it->second.node) {
        it->second.node->detachObject(it->second.item);
        m_sceneManager->destroySceneNode(it->second.node);
    }
    if (it->second.item) {
        m_sceneManager->destroyItem(it->second.item);
    }
    m_objects.erase(it);
}

void OgreWidget::clearObjects()
{
    for (auto& pair : m_objects) {
        if (pair.second.node) {
            pair.second.node->detachObject(pair.second.item);
            m_sceneManager->destroySceneNode(pair.second.node);
        }
        if (pair.second.item) {
            m_sceneManager->destroyItem(pair.second.item);
        }
    }
    m_objects.clear();
}

void OgreWidget::updateObjectTransform(const QString& id, float x, float y, float z,
                                       float rotY, float sx, float sy, float sz)
{
    auto it = m_objects.find(id);
    if (it == m_objects.end()) return;

    it->second.data.posX = x;
    it->second.data.posY = y;
    it->second.data.posZ = z;
    it->second.data.rotY = rotY;
    it->second.data.scaleX = sx;
    it->second.data.scaleY = sy;
    it->second.data.scaleZ = sz;

    if (it->second.node) {
        it->second.node->setPosition(Ogre::Vector3(x, y, z));
        float rotRad = rotY * 3.14159265358979f / 180.0f;
        it->second.node->setOrientation(Ogre::Quaternion(Ogre::Radian(rotRad), Ogre::Vector3::UNIT_Y));
        it->second.node->setScale(Ogre::Vector3(sx, sy, sz));
    }
}

SceneObject OgreWidget::getObject(const QString& id) const
{
    auto it = m_objects.find(id);
    if (it != m_objects.end()) return it->second.data;
    return SceneObject();
}

std::vector<SceneObject> OgreWidget::getObjects() const
{
    std::vector<SceneObject> result;
    for (const auto& pair : m_objects) {
        result.push_back(pair.second.data);
    }
    return result;
}

// ============================================================
// Scene serialization
// ============================================================
QJsonObject OgreWidget::saveScene() const
{
    QJsonObject scene;

    // Terrain state
    QJsonObject terrain;
    terrain["heightmapPath"] = m_heightmapPath;
    terrain["albedoPath"] = m_albedoPath;
    terrain["terrainSize"] = static_cast<double>(m_terrainSize);
    terrain["heightScale"] = static_cast<double>(m_heightScale);
    scene["terrain"] = terrain;

    // Road state
    scene["xodrPath"] = m_xodrPath;

    // Camera state
    QJsonObject camera;
    camera["yaw"] = static_cast<double>(m_camYaw);
    camera["pitch"] = static_cast<double>(m_camPitch);
    camera["dist"] = static_cast<double>(m_camDist);
    camera["targetX"] = static_cast<double>(m_camTargetX);
    camera["targetY"] = static_cast<double>(m_camTargetY);
    camera["targetZ"] = static_cast<double>(m_camTargetZ);
    scene["camera"] = camera;

    // Objects
    QJsonArray objArray;
    for (const auto& pair : m_objects) {
        objArray.append(pair.second.data.toJson());
    }
    scene["objects"] = objArray;

    return scene;
}

void OgreWidget::loadScene(const QJsonObject& scene)
{
    if (!m_initialized) {
        qDebug() << "OGRE not initialized, scene will load after init";
        return;
    }

    // Clear current scene
    clearObjects();
    clearRoads();
    clearTerrain();

    // Load terrain
    QJsonObject terrain = scene["terrain"].toObject();
    if (terrain.contains("heightmapPath")) {
        QString hmPath = terrain["heightmapPath"].toString();
        QString albPath = terrain["albedoPath"].toString();
        float tSize = static_cast<float>(terrain["terrainSize"].toDouble(4000.0));
        float hScale = static_cast<float>(terrain["heightScale"].toDouble(100.0));
        if (!hmPath.isEmpty() && QFile::exists(hmPath)) {
            loadTerrain(hmPath, albPath, tSize, hScale);
        }
    }

    // Load roads
    QString xodr = scene["xodrPath"].toString();
    if (!xodr.isEmpty() && QFile::exists(xodr)) {
        loadRoads(xodr);
    }

    // Load objects
    QJsonArray objArray = scene["objects"].toArray();
    for (const auto& val : objArray) {
        SceneObject obj = SceneObject::fromJson(val.toObject());
        if (!obj.id.isEmpty()) {
            rebuildObject(obj);
        }
    }

    // Restore camera
    QJsonObject camera = scene["camera"].toObject();
    if (camera.contains("yaw")) {
        m_camYaw = static_cast<float>(camera["yaw"].toDouble());
        m_camPitch = static_cast<float>(camera["pitch"].toDouble());
        m_camDist = static_cast<float>(camera["dist"].toDouble());
        m_camTargetX = static_cast<float>(camera["targetX"].toDouble());
        m_camTargetY = static_cast<float>(camera["targetY"].toDouble());
        m_camTargetZ = static_cast<float>(camera["targetZ"].toDouble());
        // Apply camera state
        if (m_camera) {
            float yawRad = m_camYaw * 3.14159265358979f / 180.0f;
            float pitchRad = m_camPitch * 3.14159265358979f / 180.0f;
            float x = m_camTargetX + m_camDist * cos(pitchRad) * sin(yawRad);
            float y = m_camTargetY + m_camDist * sin(-pitchRad);
            float z = m_camTargetZ + m_camDist * cos(pitchRad) * cos(yawRad);
            m_camera->setPosition(Ogre::Vector3(x, y, z));
            m_camera->lookAt(Ogre::Vector3(m_camTargetX, m_camTargetY, m_camTargetZ));
        }
    }
}

void OgreWidget::render()
{
    if (m_root && m_renderWindow) {
        m_root->renderOneFrame();
    }
}

void OgreWidget::shutdownOgre()
{
    if (m_timerId) {
        killTimer(m_timerId);
        m_timerId = 0;
    }
    clearObjects();
    clearRoads();
    clearTerrain();
    if (m_root) {
        delete m_root;
        m_root = nullptr;
    }
    m_initialized = false;
}

void OgreWidget::resetCamera()
{
    m_camYaw = 0.0f;
    m_camPitch = -30.0f;
    m_camDist = 500.0f;
    m_camTargetX = 0;
    m_camTargetY = 0;
    m_camTargetZ = 0;

    if (m_camera) {
        float yawRad = m_camYaw * 3.14159265358979f / 180.0f;
        float pitchRad = m_camPitch * 3.14159265358979f / 180.0f;

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

void OgreWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_mouseDown = true;
        m_lastMouseX = event->x();
        m_lastMouseY = event->y();
    }
}

void OgreWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_mouseDown) {
        int dx = event->x() - m_lastMouseX;
        int dy = event->y() - m_lastMouseY;
        orbitCamera(dx * 0.3f, -dy * 0.3f);
        m_lastMouseX = event->x();
        m_lastMouseY = event->y();
    }
}

void OgreWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_mouseDown = false;
    }
}

void OgreWidget::wheelEvent(QWheelEvent* event)
{
    float delta = -event->angleDelta().y() * 0.5f;
    zoomCamera(delta);
}
