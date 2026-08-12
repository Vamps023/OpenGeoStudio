#pragma once

// ============================================================
// RoadViewport3D — 3D mesh viewport using OpenGL
// ============================================================
//
// Replaces RoadViewport.tsx (Three.js renderer).
// Uses QOpenGLWidget with direct OpenGL calls to render
// the C++ engine's triangle mesh data.
//
// Features:
// - Road surface mesh (from geo::generateRoadMesh)
// - Lane marking mesh (future)
// - Centerline debug line
// - Ground plane
// - Grid helper
// - Orbit camera (rotate, pan, zoom)
//

#include "RoadStudioStore.hpp"
#include "RoadEngineService.hpp"

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QMatrix4x4>
#include <QVector3D>
#include <QPoint>
#include <cmath>

class RoadViewport3D : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    explicit RoadViewport3D(RoadStudioStore* store, RoadEngineService* engine,
                             QWidget* parent = nullptr);

    void refreshMeshes();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // Camera state
    QVector3D m_cameraPos{0, 100, 100};
    QVector3D m_cameraTarget{0, 0, 0};
    QVector3D m_cameraUp{0, 1, 0};
    float m_fov = 45.0f;

    // Mouse interaction
    QPoint m_lastMousePos;
    float m_cameraDist = 150.0f;
    float m_cameraYaw = 45.0f;   // degrees
    float m_cameraPitch = 35.0f; // degrees

    // Mesh data (per road)
    struct GLMesh {
        std::vector<float> vertices; // x,y,z
        std::vector<unsigned int> indices;
        int vertexCount = 0;
        bool valid = false;
    };
    std::vector<GLMesh> m_roadMeshes;

    RoadStudioStore* m_store;
    RoadEngineService* m_engine;

    void updateCameraFromAngles();
    void drawMesh(const GLMesh& mesh);
    void drawGroundPlane();
    void drawGrid();
    void drawCenterline();
};
