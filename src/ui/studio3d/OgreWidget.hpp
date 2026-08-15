#pragma once

#include <QWidget>
#include <QWindow>
#include <QString>

// OGRE-Next forward declarations (avoid pulling in headers here)
namespace Ogre {
    class Root;
    class SceneManager;
    class Camera;
    class Window;
    class SceneNode;
    class Item;
    class HlmsPbs;
    class HlmsUnlit;
}

class OgreWidget : public QWindow {
    Q_OBJECT

public:
    explicit OgreWidget(QWidget* parent = nullptr);
    ~OgreWidget();

    // Terrain loading
    void loadTerrain(const QString& heightmapPath, const QString& albedoPath,
                     float terrainSize = 1000.0f, float heightScale = 100.0f);
    void clearTerrain();

    // Road loading
    void loadRoads(const QString& xodrPath);
    void clearRoads();

    // Camera controls
    void resetCamera();
    void setCameraPosition(float x, float y, float z);
    void orbitCamera(float yaw, float pitch);
    void zoomCamera(float delta);

    // Get the QWidget wrapper for embedding in layouts
    QWidget* containerWidget();

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void initOgre();
    void shutdownOgre();
    void setupScene();
    void setupTerra();
    void render();

    QWidget* m_container = nullptr;
    bool m_initialized = false;
    int m_timerId = 0;

    // OGRE core
    Ogre::Root* m_root = nullptr;
    Ogre::SceneManager* m_sceneManager = nullptr;
    Ogre::Camera* m_camera = nullptr;
    Ogre::Window* m_renderWindow = nullptr;

    // Terrain
    Ogre::Item* m_terrainItem = nullptr;
    Ogre::SceneNode* m_terrainNode = nullptr;
    QString m_heightmapPath;
    QString m_albedoPath;
    float m_terrainSize = 1000.0f;
    float m_heightScale = 100.0f;

    // Roads
    Ogre::Item* m_roadItem = nullptr;
    Ogre::SceneNode* m_roadNode = nullptr;
    QString m_xodrPath;

    // Camera state
    float m_camYaw = 0.0f;
    float m_camPitch = -30.0f;
    float m_camDist = 500.0f;
    float m_camTargetX = 0.0f;
    float m_camTargetY = 0.0f;
    float m_camTargetZ = 0.0f;

    // Mouse state
    bool m_mouseDown = false;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
};
