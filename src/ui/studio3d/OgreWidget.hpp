#pragma once

#include <QWidget>
#include <QWindow>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <map>
#include <set>
#include <vector>

#include "../../core/world/World.hpp"
#include "../../core/world/UndoRedo.hpp"

// OGRE-Next forward declarations
namespace Ogre {
    class Root;
    class SceneManager;
    class Camera;
    class Window;
    class SceneNode;
    class Item;
    class HlmsPbs;
    class HlmsUnlit;
    class ManualObject;
}

// Transform mode for the gizmo
enum class TransformMode {
    None,
    Move,
    Rotate,
    Scale
};

// Which axis the gizmo is dragging
enum class GizmoAxis {
    None,
    X,
    Y,
    Z
};

class OgreWidget : public QWindow {
    Q_OBJECT

public:
    explicit OgreWidget(QWidget* parent = nullptr);
    ~OgreWidget();

    // ─── World model access ───
    world::World* world() { return &m_world; }
    const world::World* world() const { return &m_world; }
    QUndoStack* undoStack() { return &m_undoStack; }

    // ─── Terrain loading ───
    void loadTerrain(const QString& heightmapPath, const QString& albedoPath,
                     float terrainSize = 1000.0f, float heightScale = 100.0f);
    void clearTerrain();
    float sampleTerrainHeight(float x, float z) const;

    // ─── Road loading ───
    void loadRoads(const QString& xodrPath);
    void clearRoads();

    // ─── Actor placement (delegates to World + syncs rendering) ───
    QString addActor(world::ActorType type, float x, float y, float z,
                     float rotY = 0, float sx = 1, float sy = 1, float sz = 1,
                     const QString& layerId = "default");
    void removeActor(const QString& id);
    void clearActors();
    void updateActorTransform(const QString& id, float x, float y, float z,
                              float rotY, float sx, float sy, float sz);
    void updateActorVisibility(const QString& id, bool visible);
    void updateActorLayer(const QString& id, const QString& layerId);
    void renameActor(const QString& id, const QString& newName);
    int actorCount() const { return m_world.actorCount(); }

    // ─── Layer management ───
    void addLayer(const world::Layer& layer);
    void removeLayer(const QString& layerId);
    std::vector<world::Layer> getLayers() const;
    void setLayerVisible(const QString& layerId, bool visible);
    void setLayerLocked(const QString& layerId, bool locked);

    // ─── Selection ───
    void selectActor(const QString& id);
    void deselectAll();
    QString getSelectedActorId() const { return m_world.primarySelection(); }

    // ─── Transform mode ───
    void setTransformMode(TransformMode mode);
    TransformMode getTransformMode() const { return m_transformMode; }

    // ─── Grid ───
    void setGridVisible(bool visible);
    bool isGridVisible() const { return m_gridVisible; }
    void setSnapEnabled(bool enabled);
    bool isSnapEnabled() const { return m_snapEnabled; }
    void setSnapSize(float size);
    float getSnapSize() const { return m_snapSize; }

    // ─── Scene serialization (legacy format for backward compat) ───
    QJsonObject saveScene() const;
    void loadScene(const QJsonObject& scene);

    // ─── World serialization ───
    bool saveWorld(const QString& path) const;
    bool loadWorld(const QString& path);

    // ─── Camera controls ───
    void resetCamera();
    void setCameraPosition(float x, float y, float z);
    void orbitCamera(float yaw, float pitch);
    void zoomCamera(float delta);
    void panCamera(float dx, float dy);

    // ─── Get the QWidget wrapper for embedding in layouts ───
    QWidget* containerWidget();

    // ─── Raycast from screen coordinates to world ───
    struct WorldPos { float x, y, z; };
    bool screenToWorld(int screenX, int screenY, WorldPos& worldPos);
    QString pickActor(int screenX, int screenY);

signals:
    void actorSelected(const QString& id);
    void actorTransformed(const QString& id);
    void actorAdded(const QString& id);
    void actorRemoved(const QString& id);
    void sceneChanged();
    void worldChanged();

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void timerEvent(QTimerEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    void initOgre();
    void shutdownOgre();
    void setupScene();
    void render();
    void rebuildActor(const world::Actor& actor);
    void updateCameraFromKeys(float dt);
    void updateGizmo();
    void createGizmo();
    void destroyGizmo();
    void showGizmo(bool show);
    GizmoAxis pickGizmoAxis(int screenX, int screenY);
    void applyGizmoDrag(int screenX, int screenY);
    void createGrid();
    void destroyGrid();
    void showGrid(bool show);
    bool isLayerVisible(const QString& layerId) const;
    bool isLayerLocked(const QString& layerId) const;
    void syncAllActors();
    void clearActorRenderables();

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
    QImage m_heightmapImage;
    bool m_hasHeightmap = false;

    // Roads
    Ogre::Item* m_roadItem = nullptr;
    Ogre::SceneNode* m_roadNode = nullptr;
    QString m_xodrPath;

    // Actor render entries — each actor has its own Item + SceneNode
    struct ActorRenderEntry {
        QString actorId;
        Ogre::Item* item = nullptr;
        Ogre::SceneNode* node = nullptr;
    };
    std::map<QString, ActorRenderEntry> m_actorRenders;

    // World model (the single source of truth)
    world::World m_world;
    QUndoStack m_undoStack;

    // Transform gizmo
    TransformMode m_transformMode = TransformMode::None;
    GizmoAxis m_activeGizmoAxis = GizmoAxis::None;
    Ogre::ManualObject* m_gizmoMove = nullptr;
    Ogre::ManualObject* m_gizmoRotate = nullptr;
    Ogre::ManualObject* m_gizmoScale = nullptr;
    Ogre::SceneNode* m_gizmoNode = nullptr;
    bool m_gizmoDragging = false;
    int m_gizmoStartX = 0;
    int m_gizmoStartY = 0;
    float m_gizmoStartValue = 0.0f;
    world::Transform m_gizmoStartTransform;

    // Grid
    Ogre::ManualObject* m_gridManual = nullptr;
    Ogre::SceneNode* m_gridNode = nullptr;
    bool m_gridVisible = true;
    bool m_snapEnabled = false;
    float m_snapSize = 1.0f;

    // Camera state — orbit mode
    float m_camYaw = 0.0f;
    float m_camPitch = -30.0f;
    float m_camDist = 500.0f;
    float m_camTargetX = 0.0f;
    float m_camTargetY = 0.0f;
    float m_camTargetZ = 0.0f;

    // Camera state — fly mode (WASD)
    bool m_flyMode = false;
    std::set<int> m_keysDown;
    float m_flySpeed = 100.0f;
    float m_flyBoost = 1.0f;

    // Pending scene to load after OGRE initialization
    QJsonObject m_pendingScene;
    bool m_hasPendingScene = false;

    // Mouse state
    bool m_leftDown = false;
    bool m_rightDown = false;
    bool m_middleDown = false;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
};
