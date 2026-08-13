// RoadViewport3D — 3D mesh viewport implementation

#include "RoadViewport3D.hpp"
#include "GeoConvert.hpp"

#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <algorithm>

RoadViewport3D::RoadViewport3D(RoadStudioStore* store, RoadEngineService* engine,
                                 QWidget* parent)
    : QOpenGLWidget(parent), m_store(store), m_engine(engine) {
    setFocusPolicy(Qt::StrongFocus);
    updateCameraFromAngles();
}

void RoadViewport3D::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.15f, 0.17f, 0.20f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void RoadViewport3D::showEvent(QShowEvent* event) {
    QOpenGLWidget::showEvent(event);
    // Refresh meshes when the 3D viewport becomes visible
    // (the OpenGL context may not have been ready before)
    refreshMeshes();
}

void RoadViewport3D::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void RoadViewport3D::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Set up projection
    QMatrix4x4 proj;
    proj.perspective(m_fov, float(width()) / std::max(1, height()), 0.1f, 10000.0f);

    // Set up view
    QMatrix4x4 view;
    view.lookAt(m_cameraPos, m_cameraTarget, m_cameraUp);

    // Set up model (identity — mesh is already in local meters)
    QMatrix4x4 model;

    // Combined MVP
    QMatrix4x4 mvp = proj * view * model;

    glLoadMatrixf(mvp.constData());
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj.constData());
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf((view * model).constData());

    // Draw ground plane
    drawGroundPlane();

    // Draw grid
    drawGrid();

    // Draw road meshes
    for (const auto& mesh : m_roadMeshes) {
        if (mesh.valid) drawMesh(mesh);
    }

    // Draw centerlines as debug
    drawCenterline();
}

void RoadViewport3D::refreshMeshes() {
    m_roadMeshes.clear();

    for (const auto& road : m_store->roads()) {
        if (road.points.size() < 2) continue;

        auto meshData = m_engine->generateMesh(
            road, m_store->refLat(), m_store->refLon(), 64);

        GLMesh glMesh;
        // Engine stores vertices as interleaved x,y,z floats
        glMesh.vertices.assign(
            meshData.positions.begin(),
            meshData.positions.end());
        glMesh.indices.assign(
            meshData.indices.begin(),
            meshData.indices.end());
        glMesh.vertexCount = meshData.vertexCount;
        glMesh.valid = !glMesh.vertices.empty() && !glMesh.indices.empty();
        m_roadMeshes.push_back(std::move(glMesh));
    }

    update();
}

void RoadViewport3D::drawMesh(const GLMesh& mesh) {
    if (!mesh.valid) return;

    // Draw road surface as triangles
    glColor3f(0.3f, 0.3f, 0.35f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, mesh.vertices.data());
    glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, mesh.indices.data());
    glDisableClientState(GL_VERTEX_ARRAY);

    // Draw wireframe overlay
    glColor3f(0.5f, 0.5f, 0.6f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, mesh.vertices.data());
    glDrawElements(GL_LINES, mesh.indices.size(), GL_UNSIGNED_INT, mesh.indices.data());
    glDisableClientState(GL_VERTEX_ARRAY);
}

void RoadViewport3D::drawGroundPlane() {
    glColor3f(0.12f, 0.15f, 0.12f);
    glBegin(GL_QUADS);
    const float size = 1000.0f;
    glVertex3f(-size, 0, -size);
    glVertex3f(size, 0, -size);
    glVertex3f(size, 0, size);
    glVertex3f(-size, 0, size);
    glEnd();
}

void RoadViewport3D::drawGrid() {
    glColor3f(0.2f, 0.22f, 0.25f);
    glBegin(GL_LINES);
    const float gridSize = 1000.0f;
    const float step = 50.0f;
    for (float x = -gridSize; x <= gridSize; x += step) {
        glVertex3f(x, 0, -gridSize);
        glVertex3f(x, 0, gridSize);
    }
    for (float z = -gridSize; z <= gridSize; z += step) {
        glVertex3f(-gridSize, 0, z);
        glVertex3f(gridSize, 0, z);
    }
    glEnd();
}

void RoadViewport3D::drawCenterline() {
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_LINES);
    for (const auto& road : m_store->roads()) {
        if (road.points.size() < 2) continue;
        auto samples = m_engine->sampleCenterline(
            road, m_store->refLat(), m_store->refLon(), 64);
        for (size_t i = 1; i < samples.size(); ++i) {
            // C++ engine: X=east, Y=north, Z=up
            // OpenGL: X=right, Y=up, Z=toward viewer
            // Map: glX = X, glY = Z (elevation), glZ = -Y
            glVertex3f(samples[i-1].x, 0.1f, -samples[i-1].y);
            glVertex3f(samples[i].x, 0.1f, -samples[i].y);
        }
    }
    glEnd();
}

void RoadViewport3D::updateCameraFromAngles() {
    const float yawRad = m_cameraYaw * M_PI / 180.0f;
    const float pitchRad = m_cameraPitch * M_PI / 180.0f;

    m_cameraPos.setX(m_cameraDist * std::cos(pitchRad) * std::cos(yawRad));
    m_cameraPos.setY(m_cameraDist * std::sin(pitchRad));
    m_cameraPos.setZ(m_cameraDist * std::cos(pitchRad) * std::sin(yawRad));
}

void RoadViewport3D::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position().toPoint();
}

void RoadViewport3D::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->position().toPoint() - m_lastMousePos;
    m_lastMousePos = event->position().toPoint();

    if (event->buttons() & Qt::LeftButton) {
        // Rotate
        m_cameraYaw -= delta.x() * 0.5f;
        m_cameraPitch = std::clamp(m_cameraPitch + delta.y() * 0.5f, 5.0f, 85.0f);
        updateCameraFromAngles();
        update();
    } else if (event->buttons() & Qt::RightButton) {
        // Pan target
        float panSpeed = m_cameraDist * 0.001f;
        m_cameraTarget.setX(m_cameraTarget.x() - delta.x() * panSpeed);
        m_cameraTarget.setZ(m_cameraTarget.z() + delta.y() * panSpeed);
        update();
    }
}

void RoadViewport3D::wheelEvent(QWheelEvent* event) {
    const float delta = event->angleDelta().y() * 0.1f;
    m_cameraDist = std::clamp(m_cameraDist - delta, 10.0f, 2000.0f);
    updateCameraFromAngles();
    update();
}
