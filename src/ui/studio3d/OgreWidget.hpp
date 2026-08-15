#pragma once

#include <QWidget>
#include <QWindow>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <map>

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

// A placed 3D object in the scene
struct SceneObject {
    QString id;          // unique identifier
    QString type;        // "building", "tree", "box", etc.
    QString name;        // display name
    float posX = 0, posY = 0, posZ = 0;
    float rotY = 0;      // rotation around Y axis (degrees)
    float scaleX = 1, scaleY = 1, scaleZ = 1;
    float colorR = 0.8f, colorG = 0.8f, colorB = 0.8f;

    QJsonObject toJson() const;
    static SceneObject fromJson(const QJsonObject& j);
};

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

    // Object placement
    QString addObject(const QString& type, float x, float y, float z,
                      float rotY = 0, float sx = 1, float sy = 1, float sz = 1);
    void removeObject(const QString& id);
    void clearObjects();
    void updateObjectTransform(const QString& id, float x, float y, float z,
                               float rotY, float sx, float sy, float sz);
    SceneObject getObject(const QString& id) const;
    std::vector<SceneObject> getObjects() const;

    // Scene serialization
    QJsonObject saveScene() const;
    void loadScene(const QJsonObject& scene);

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
    void rebuildObject(const SceneObject& obj);

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

    // Objects — each object has its own Item + SceneNode
    struct ObjectEntry {
        SceneObject data;
        Ogre::Item* item = nullptr;
        Ogre::SceneNode* node = nullptr;
    };
    std::map<QString, ObjectEntry> m_objects;

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
