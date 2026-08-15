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

    // TODO: Load terrain using Terra system
    // For now, just log that we received the request
    qDebug() << "Terrain load requested:" << heightmapPath;
    qDebug() << "  Albedo:" << albedoPath;
    qDebug() << "  Size:" << terrainSize << "m, Height scale:" << heightScale;

    resetCamera();
}

void OgreWidget::clearTerrain()
{
    // TODO: Clear Terra terrain
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
