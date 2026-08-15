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
#include "OgreArchiveManager.h"
#include "Compositor/OgreCompositorManager2.h"
#include "OgreItem.h"
#include "OgreMesh2.h"
#include "OgreMeshManager2.h"
#include "OgreLight.h"

// Math
#include "OgreVector3.h"
#include "OgreQuaternion.h"
#include "OgreMath.h"

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
    QString ogreRoot = "D:/git/ogre-next";
    QString buildDir = ogreRoot + "/build";

    // Create plugins.cfg if it doesn't exist
    QString pluginsCfg = buildDir + "/bin/plugins.cfg";
    if (!QFile::exists(pluginsCfg)) {
        QString cfg = QString(
            "PluginFolder=%1/bin\n"
            "Plugin=RenderSystem_Direct3D11\n"
            "Plugin=RenderSystem_GL3Plus\n"
            "Plugin=Plugin_ParticleFX\n"
        ).arg(buildDir);
        QFile f(pluginsCfg);
        f.open(QIODevice::WriteOnly);
        f.write(cfg.toUtf8());
        f.close();
    }

    // Create Root with AbiCookie (required by OGRE-Next 4.0)
    Ogre::AbiCookie abiCookie = Ogre::generateAbiCookie();
    m_root = OGRE_NEW Ogre::Root(&abiCookie,
                                 pluginsCfg.toStdString(),
                                 buildDir.toStdString() + "/ogre.cfg",
                                 buildDir.toStdString() + "/ogre.log",
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

    // Create scene manager
    m_sceneManager = m_root->createSceneManager(Ogre::ST_GENERIC, 2);

    // Create camera
    m_camera = m_sceneManager->createCamera("MainCamera");
    m_camera->setNearClipDistance(0.5f);
    m_camera->setFarClipDistance(50000.0f);
    m_camera->setAspectRatio(Ogre::Real(width()) / Ogre::Real(height()));

    m_cameraNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    m_cameraNode->attachObject(m_camera);
    m_cameraNode->setPosition(0, 300, 500);
    m_cameraNode->lookAt(Ogre::Vector3(0, 0, 0), Ogre::Node::TS_WORLD);

    // Setup compositor (replaces Viewport in OGRE-Next)
    Ogre::CompositorManager2* compositorManager = m_root->getCompositorManager2();
    Ogre::ColourValue bgColor(0.2f, 0.4f, 0.6f, 1.0f);
    if (!compositorManager->hasWorkspaceDefinition("MainWorkspace")) {
        compositorManager->createBasicWorkspaceDef("MainWorkspace", bgColor,
            Ogre::IdString());
    }
    compositorManager->addWorkspace(m_sceneManager,
        m_renderWindow->getTexture(), m_camera, "MainWorkspace", true);

    // Setup Hlms (shader system)
    {
        QString hlmsFolder = buildDir + "/bin/";
        Ogre::String mainPath = hlmsFolder.toStdString() + "Hlms/Common";
        Ogre::String pbsPath = hlmsFolder.toStdString() + "Hlms/Pbs";
        Ogre::String unlitPath = hlmsFolder.toStdString() + "Hlms/Unlit";

        Ogre::ArchiveManager& archMgr = Ogre::ArchiveManager::getSingleton();

        Ogre::Archive* libCommon = archMgr.load(mainPath, "FileSystem", true);
        Ogre::Archive* libPbs = archMgr.load(pbsPath, "FileSystem", true);
        Ogre::Archive* libUnlit = archMgr.load(unlitPath, "FileSystem", true);

        Ogre::HlmsManager* hlmsManager = m_root->getHlmsManager();

        // PBS
        {
            Ogre::ArchiveVec libVec;
            libVec.push_back(libCommon);
            Ogre::HlmsPbs* hlmsPbs = new Ogre::HlmsPbs(
                archMgr.load(pbsPath, "FileSystem", false), &libVec);
            hlmsManager->registerHlms(hlmsPbs);
        }

        // Unlit
        {
            Ogre::ArchiveVec libVec;
            libVec.push_back(libCommon);
            Ogre::HlmsUnlit* hlmsUnlit = new Ogre::HlmsUnlit(
                archMgr.load(unlitPath, "FileSystem", false), &libVec);
            hlmsManager->registerHlms(hlmsUnlit);
        }
    }

    // Lighting
    m_sceneManager->setAmbientLight(Ogre::ColourValue(0.3f, 0.3f, 0.3f),
                                    Ogre::ColourValue(0.1f, 0.1f, 0.1f),
                                    Ogre::Vector3(0, 1, 0));
    Ogre::Light* dirLight = m_sceneManager->createLight();
    dirLight->setType(Ogre::Light::LT_DIRECTIONAL);
    dirLight->setDirection(Ogre::Vector3(-0.5f, -1.0f, -0.3f).normalisedCopy());
    dirLight->setDiffuseColour(Ogre::ColourValue(1.0f, 1.0f, 0.95f));
    Ogre::SceneNode* lightNode = m_sceneManager->getRootSceneNode()->createChildSceneNode();
    lightNode->attachObject(dirLight);

    qDebug() << "OGRE-Next initialized successfully";
}

void OgreWidget::setupScene()
{
    // Create a simple cube to verify rendering works
    Ogre::Item* item = m_sceneManager->createItem(
        "Cube.mesh",
        Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME,
        Ogre::SCENE_DYNAMIC);
    if (item) {
        Ogre::SceneNode* node = m_sceneManager->getRootSceneNode()->createChildSceneNode();
        node->attachObject(item);
        node->setScale(100, 100, 100);
        node->setPosition(0, 50, 0);
    }
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

    if (m_cameraNode) {
        float yawRad = Ogre::Degree(m_camYaw).valueRadians();
        float pitchRad = Ogre::Degree(m_camPitch).valueRadians();

        float x = m_camTargetX + m_camDist * cos(pitchRad) * sin(yawRad);
        float y = m_camTargetY + m_camDist * sin(-pitchRad);
        float z = m_camTargetZ + m_camDist * cos(pitchRad) * cos(yawRad);

        m_cameraNode->setPosition(x, y, z);
        m_cameraNode->lookAt(Ogre::Vector3(m_camTargetX, m_camTargetY, m_camTargetZ),
                              Ogre::Node::TS_WORLD);
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
