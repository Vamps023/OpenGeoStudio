#include "map_view_gl.h"
#include <QMouseEvent>
#include <QPainter>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QImage>
#include <QTimer>

#include "main_widget.h"
#include "id_generator.h"
#include "spatial_indexer.h"
#include "triangulation.h"
#include "action_manager.h"
#include "constants.h"
#include "change_tracker.h"
#include "vehicle.h"
#include "junction.h"

#include <spdlog/spdlog.h>

namespace LM
{
    MapViewGL* g_mapViewGL;
    std::string g_PointerRoadID;
    double g_PointerRoadS;
    int g_PointerLane;
    odr::Vec2D g_PointerOnGround;
    odr::Vec3D g_CameraPosition;
    int g_createRoadElevationOption;
    unsigned int g_PointerVehicle;
    bool touchScreen;

    MapViewGL::MapViewGL() :
        permanentBuffer(std::make_unique<GLBufferManage>(MaxRoadVertices)), 
        temporaryBuffer(std::make_unique<GLBufferManage>(MaxTemporaryVertices)),
        backgroundBuffer(std::make_unique<GLBufferManage>(1 << 12)),
        vehicleBuffer{
            GLBufferManageInstanced(":/models/jeep.obj", ":/models/jeep.jpg", MaxInstancesPerType),
            GLBufferManageInstanced(":/models/cadillac.obj", ":/models/cadillac.jpg", MaxInstancesPerType),
            GLBufferManageInstanced(":/models/military.obj", ":/models/military.jpg", MaxInstancesPerType)
        }
    {
        g_mapViewGL = this;
        g_createRoadElevationOption = 0;
        ResetCamera();
        touchScreen = false;
        setAttribute(Qt::WA_AcceptTouchEvents);
    }

    void MapViewGL::CleanupResources()
    {
        makeCurrent();
        permanentBuffer->CleanupResources();
        temporaryBuffer->CleanupResources();
        backgroundBuffer->CleanupResources();
        for (auto& buffer : vehicleBuffer)
        {
            buffer.CleanupResources();
        }
    }

    void MapViewGL::ResetCamera()
    {
        if (m_viewMode == ViewMode::TopDown2D)
        {
            // Top-down: camera directly above XY ground plane, looking straight down -Z.
            m_camera.setTranslation(0, 0, 1000);
            m_camera.setRotation(0, QVector3D(1, 0, 0));
        }
        else
        {
            // 3D perspective: camera at an angle looking down at the ground plane
            m_camera.setTranslation(0, -300, 350);
            m_camera.setRotation(35, QVector3D(1, 0, 0));
        }
    }

    void MapViewGL::SetViewMode(ViewMode mode)
    {
        if (m_viewMode == mode) return;
        m_viewMode = mode;
        ResetCamera();
        update();
    }

    // --- Geographic helpers ---
    void MapViewGL::latLonToTile(double lat, double lon, int z, int& tx, int& ty)
    {
        double n = std::pow(2.0, z);
        tx = int((lon + 180.0) / 360.0 * n);
        ty = int((1.0 - std::log(std::tan(lat * M_PI / 180.0) +
              1.0 / std::cos(lat * M_PI / 180.0)) / M_PI) / 2.0 * n);
    }

    void MapViewGL::tileToLatLon(int tx, int ty, int z, double& lat, double& lon)
    {
        double n = std::pow(2.0, z);
        lon = tx / n * 360.0 - 180.0;
        double r = M_PI * (1.0 - 2.0 * ty / n);
        lat = 180.0 / M_PI * std::atan(0.5 * (std::exp(r) - std::exp(-r)));
    }

    double MapViewGL::metersPerPixel(double lat, int z)
    {
        const double earthCircumference = 40075016.686;
        return earthCircumference * std::cos(lat * M_PI / 180.0) / (std::pow(2.0, z) * 256.0);
    }

    void MapViewGL::SetMapCenter(double lat, double lon)
    {
        m_mapCenterLat = lat;
        m_mapCenterLon = lon;
        m_mapEnabled = true;
        if (!m_tileNam) m_tileNam = new QNetworkAccessManager(this);
        m_lastCameraZ = -1; // force tile update on next paint
        update();
    }

    void MapViewGL::ClearMapBackground()
    {
        m_mapTiles.clear();
        m_mapEnabled = false;
        update();
    }

    void MapViewGL::UpdateMapTiles()
    {        if (!m_mapEnabled || m_viewMode != ViewMode::TopDown2D) return;
        if (width() <= 0 || height() <= 0) return;
        if (!m_tileNam) m_tileNam = new QNetworkAccessManager(this);

        float camZ = m_camera.translation().z();
        float aspect = width() / float(height() ? height() : 1);
        float viewSize = camZ * 0.6f;
        if (viewSize < 10.0f) viewSize = 10.0f;

        float camX = m_camera.translation().x();
        float camY = m_camera.translation().y();
        double worldLeft   = camX - viewSize * aspect;
        double worldRight  = camX + viewSize * aspect;
        double worldBottom = camY - viewSize;
        double worldTop    = camY + viewSize;

        double latPerMeter = 1.0 / 111320.0;
        double lonPerMeter = 1.0 / (111320.0 * std::cos(m_mapCenterLat * M_PI / 180.0));

        // OpenGL ortho: +Y = up = north (higher latitude), +X = right = east
        double minLat = m_mapCenterLat + worldBottom * latPerMeter;
        double maxLat = m_mapCenterLat + worldTop * latPerMeter;
        double minLon = m_mapCenterLon + worldLeft * lonPerMeter;
        double maxLon = m_mapCenterLon + worldRight * lonPerMeter;

        minLat = std::max(-85.05, std::min(85.05, minLat));
        maxLat = std::max(-85.05, std::min(85.05, maxLat));
        minLon = std::max(-180.0, std::min(180.0, minLon));
        maxLon = std::max(-180.0, std::min(180.0, maxLon));

        // Calculate the ideal tile zoom level based on the view scale.
        // We want roughly 256px per tile to match screen pixels.
        // mpp = meters per pixel at current zoom
        // tileMeters = 256 * mpp = size of one tile in meters
        // We want tileMeters ~= viewSize so ~2-4 tiles cover the screen
        double targetTileMeters = viewSize * 0.5; // each tile covers half the view
        // tileMeters = earthCircumference * cos(lat) / (2^z * 256)
        // => 2^z = earthCircumference * cos(lat) / (targetTileMeters * 256)
        double earthCirc = 40075016.686;
        double idealZoom = std::log2(earthCirc * std::cos(m_mapCenterLat * M_PI / 180.0)
                                      / (targetTileMeters * 256.0));
        int newZoom = std::max(2, std::min(19, (int)std::round(idealZoom)));

        if (newZoom != m_mapZoom) {
            m_mapTiles.clear(); // clear old zoom-level tiles
            m_mapZoom = newZoom;
        }

        int txMin, tyMin, txMax, tyMax;
        latLonToTile(minLat, minLon, m_mapZoom, txMin, tyMin);
        latLonToTile(maxLat, maxLon, m_mapZoom, txMax, tyMax);
        if (txMin > txMax) std::swap(txMin, txMax);
        if (tyMin > tyMax) std::swap(tyMin, tyMax);

        // Safety cap on tile count
        const int MAX_TILES = 30;
        int count = (txMax - txMin + 1) * (tyMax - tyMin + 1);
        if (count > MAX_TILES) {
            while (count > MAX_TILES && m_mapZoom > 2) {
                m_mapZoom--;
                txMin /= 2; txMax /= 2;
                tyMin /= 2; tyMax /= 2;
                count = (txMax - txMin + 1) * (tyMax - tyMin + 1);
            }
            m_mapTiles.clear();
        }

        for (int ty = tyMin; ty <= tyMax; ty++) {
            for (int tx = txMin; tx <= txMax; tx++) {
                bool found = false;
                for (auto& t : m_mapTiles) {
                    if (t->z == m_mapZoom && t->x == tx && t->y == ty) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    requestTile(m_mapZoom, tx, ty);
                }
            }
        }

        pruneInvisibleTiles();
        m_lastTileZoom = m_mapZoom;
        m_lastCameraX = camX;
        m_lastCameraY = camY;
        m_lastCameraZ = camZ;
    }

    void MapViewGL::requestTile(int z, int x, int y)
    {        if (!m_tileNam) return;

        auto tile = std::make_unique<MapTile>();
        tile->z = z;
        tile->x = x;
        tile->y = y;
        tile->loading = true;

        double tLat, tLon;
        tileToLatLon(x, y, z, tLat, tLon);
        double mpp = metersPerPixel(m_mapCenterLat, z);
        tile->worldSize = 256 * mpp;
        double latPerMeter = 1.0 / 111320.0;
        double lonPerMeter = 1.0 / (111320.0 * std::cos(m_mapCenterLat * M_PI / 180.0));
        // OpenGL: +X = east, +Y = north (up on screen)
        // tileToLatLon gives the NW (top-left) corner of the tile
        // Tile center is half a tile east and half a tile south of NW corner
        tile->worldX = (tLon - m_mapCenterLon) / lonPerMeter + 128.0 * mpp;
        tile->worldY = (tLat - m_mapCenterLat) / latPerMeter - 128.0 * mpp;

        QString url = QString(
            "https://mt1.google.com/vt/lyrs=s&x=%1&y=%2&z=%3")
            .arg(x).arg(y).arg(z);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio/1.0");

        auto* reply = m_tileNam->get(request);
        MapTile* rawPtr = tile.get();

        connect(reply, &QNetworkReply::finished, this, [this, rawPtr, reply]() {            bool tileExists = false;
            for (auto& t : m_mapTiles) {
                if (t.get() == rawPtr) { tileExists = true; break; }
            }
            if (reply->error() == QNetworkReply::NoError && tileExists) {
                QImage img;
                if (img.loadFromData(reply->readAll())) {                    rawPtr->pendingImage = img.convertToFormat(QImage::Format_RGB32);
                    rawPtr->loading = false;
                    update();
                }
            }
            reply->deleteLater();
        });

        m_mapTiles.push_back(std::move(tile));    }

    void MapViewGL::pruneInvisibleTiles()
    {
        // Remove tiles that are too far from the current view
        float camX = m_camera.translation().x();
        float camY = m_camera.translation().y();
        float camZ = m_camera.translation().z();
        float maxDist = camZ * 2.0f + 2000.0f;

        m_mapTiles.erase(
            std::remove_if(m_mapTiles.begin(), m_mapTiles.end(),
                [camX, camY, maxDist, this](const std::unique_ptr<MapTile>& t) {
                    if (t->z != m_mapZoom) return true; // wrong zoom level
                    float dx = t->worldX - camX;
                    float dy = t->worldY - camY;
                    return std::sqrt(dx*dx + dy*dy) > maxDist;
                }),
            m_mapTiles.end());
    }

    void MapViewGL::initTexturedShader()
    {
        if (m_texturedShaderInit) return;
        m_texturedShaderInit = true;

        m_texturedShader.addShaderFromSourceCode(QOpenGLShader::Vertex,
            R"(#version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec2 aTexCoord;
            uniform mat4 worldToView;
            out vec2 TexCoord;
            void main() {
                gl_Position = worldToView * vec4(aPos, 1.0);
                TexCoord = aTexCoord;
            })");

        m_texturedShader.addShaderFromSourceCode(QOpenGLShader::Fragment,
            R"(#version 330 core
            in vec2 TexCoord;
            out vec4 FragColor;
            uniform sampler2D tex;
            void main() {
                FragColor = texture(tex, TexCoord);
            })");

        m_texturedShader.link();

        // Create a quad (in world XY plane) — will be resized dynamically
        m_bgQuadVao.create();
        m_bgQuadVao.bind();

        m_bgQuadVbo.create();
        m_bgQuadVbo.bind();
        m_bgQuadVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);

        // Initial quad size — will be updated in drawMapBackground
        float halfExt = 1000.0f;
        float quadVerts[] = {
            // pos              // texcoord
            -halfExt, -halfExt, 0,    0, 1,
             halfExt, -halfExt, 0,    1, 1,
             halfExt,  halfExt, 0,    1, 0,
            -halfExt, -halfExt, 0,    0, 1,
             halfExt,  halfExt, 0,    1, 0,
            -halfExt,  halfExt, 0,    0, 0,
        };
        m_bgQuadVbo.allocate(quadVerts, sizeof(quadVerts));

        m_texturedShader.enableAttributeArray(0);
        m_texturedShader.setAttributeBuffer(0, GL_FLOAT, 0, 3, 5 * sizeof(float));
        m_texturedShader.enableAttributeArray(1);
        m_texturedShader.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 2, 5 * sizeof(float));

        m_bgQuadVbo.release();
        m_bgQuadVao.release();
    }

    void MapViewGL::drawMapBackground()
    {
        if (!m_mapEnabled || m_mapTiles.empty()) return;
        initTexturedShader();
        

        // Upload any pending images as GL textures (must be on GL thread)
        for (auto& tile : m_mapTiles) {
            if (!tile->pendingImage.isNull() && !tile->texture) {
                tile->texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
                tile->texture->setData(tile->pendingImage);
                tile->texture->setMinificationFilter(QOpenGLTexture::Linear);
                tile->texture->setMagnificationFilter(QOpenGLTexture::Linear);
                tile->texture->setWrapMode(QOpenGLTexture::ClampToEdge);
                tile->pendingImage = QImage(); // free the image
            }
        }

        glDisable(GL_DEPTH_TEST);
        m_texturedShader.bind();
        m_texturedShader.setUniformValue("worldToView", m_worldToView);

        for (auto& tile : m_mapTiles) {
            if (!tile->texture || tile->loading) continue;

            float halfSize = float(tile->worldSize) * 0.5f;
            float cx = float(tile->worldX);
            float cy = float(tile->worldY);
            float quadVerts[] = {
                cx - halfSize, cy - halfSize, 0,    0, 1,
                cx + halfSize, cy - halfSize, 0,    1, 1,
                cx + halfSize, cy + halfSize, 0,    1, 0,
                cx - halfSize, cy - halfSize, 0,    0, 1,
                cx + halfSize, cy + halfSize, 0,    1, 0,
                cx - halfSize, cy + halfSize, 0,    0, 0,
            };

            m_bgQuadVbo.bind();
            m_bgQuadVbo.write(0, quadVerts, sizeof(quadVerts));
            m_bgQuadVbo.release();

            tile->texture->bind(0);
            m_texturedShader.setUniformValue("tex", 0);

            m_bgQuadVao.bind();
            glDrawArrays(GL_TRIANGLES, 0, 6);
            m_bgQuadVao.release();
        }

        m_texturedShader.release();
        glEnable(GL_DEPTH_TEST);
    }

    unsigned int MapViewGL::AddQuads(const odr::Line3D& lBorder, const odr::Line3D& rBorder, QColor color, unsigned int objID)
    {
        bool temporary = objID == -1;
        auto gid = IDGenerator::ForType(temporary ? IDType::Graphics_Temporary : IDType::Graphics)->GenerateID(this);
        bool success = true;
        if (temporary)
        {
            success = temporaryBuffer->AddQuads(gid, objID, lBorder, rBorder, color);
        }
        else
        {
            success = permanentBuffer->AddQuads(gid, objID, lBorder, rBorder, color);
        }
        if (!success)
        {
            throw std::logic_error("Graphics buffer out of space!");
        }
        return gid;
    }

    void MapViewGL::LineToQuads(const odr::Line3D& border, double width, odr::Line3D& lBorder, odr::Line3D& rBorder)
    {
        lBorder.reserve(border.size());
        rBorder.reserve(border.size());

        for (int i = 0; i != border.size(); ++i)
        {
            odr::Vec3D tangent;
            if (i == 0)
            {
                tangent = odr::sub(border[i + 1], border[i]);
            }
            else if (i == border.size() - 1)
            {
                tangent = odr::sub(border[i], border[i - 1]);
            }
            else
            {
                auto tan1 = odr::normalize(odr::sub(border[i + 1], border[i]));
                auto tan2 = odr::normalize(odr::sub(border[i], border[i - 1]));
                tangent = odr::add(tan1, tan2);
            }
            if (odr::squaredNorm(tangent) == 0)
                continue;
            tangent = odr::normalize(tangent);

            odr::Vec3D radio{ -tangent[1], tangent[0], 0 };
            lBorder.push_back(odr::add(border[i], odr::mut(width / 2, radio)));
            rBorder.push_back(odr::add(border[i], odr::mut(-width / 2, radio)));
        }
    }

    unsigned int MapViewGL::AddLine(const odr::Line3D& border, double width, QColor color, unsigned int objID)
    {
        bool temporary = objID == -1;
        odr::Line3D lBorder, rBorder;
        LineToQuads(border, width, lBorder, rBorder);
        return AddQuads(lBorder, rBorder, color, objID);
    }

    unsigned int MapViewGL::AddPoly(const odr::Line3D& boundary, QColor color, unsigned int objID)
    {
        bool temporary = objID == -1;
        auto gid = IDGenerator::ForType(temporary ? IDType::Graphics_Temporary : IDType::Graphics)->GenerateID(this);
        bool success = true;
        if (temporary)
        {
            success = temporaryBuffer->AddPoly(gid, objID, boundary, color);
        }
        else
        {
            success = permanentBuffer->AddPoly(gid, objID, boundary, color);
        }
        if (!success)
        {
            throw std::logic_error("Graphics buffer out of space!");
        }
        return gid;
    }

    unsigned int MapViewGL::AddColumn(const odr::Line3D& boundary, double h, QColor color, unsigned int objID)
    {
        bool temporary = objID == -1;
        auto gid = IDGenerator::ForType(temporary ? IDType::Graphics_Temporary : IDType::Graphics)->GenerateID(this);
        bool success = true;
        if (temporary)
        {
            success = temporaryBuffer->AddColumn(gid, objID, boundary, h, color);
        }
        else
        {
            success = permanentBuffer->AddColumn(gid, objID, boundary, h, color);
        }
        if (!success)
        {
            throw std::logic_error("Graphics buffer out of space!");
        }
        return gid;
    }

    void MapViewGL::AddInstance(unsigned int id, QColor color, unsigned int variation)
    {
        vehicleBuffer[variation].AddInstance(id, QMatrix4x4(), color);
    }

    void MapViewGL::UpdateObject(unsigned int id, uint8_t flag)
    {
        permanentBuffer->UpdateItem(id, flag);
    }

    uint8_t MapViewGL::GetObjectFlag(unsigned int objectID)
    {
        return permanentBuffer->GetItemFlag(objectID);
    }

    void MapViewGL::UpdateObjectID(unsigned int graphicsID, unsigned int objectID)
    {
        permanentBuffer->UpdateObjectID(graphicsID, objectID);
    }

    void MapViewGL::RemoveItem(unsigned int id, bool temporary)
    {
        if (!temporary)
        {
            permanentBuffer->RemoveItem(id);
        }
        else
        {
            temporaryBuffer->RemoveItem(id);
        }
        IDGenerator::ForType(temporary ? IDType::Graphics_Temporary : IDType::Graphics)->FreeID(id);
    }

    void MapViewGL::RemoveObject(unsigned int objectID)
    {
        permanentBuffer->RemoveObject(objectID);
    }

    void MapViewGL::UpdateInstance(unsigned int id, const QMatrix4x4 trans, unsigned int variation)
    {
        vehicleBuffer[variation].UpdateInstance(id, trans);
    }

    void MapViewGL::RemoveInstance(unsigned int id, unsigned int variation)
    {
        vehicleBuffer[variation].RemoveInstance(id);
    }

    unsigned int MapViewGL::AddBackgroundLine(const odr::Line3D& line, double width, QColor color)
    {
        odr::Line3D lBorder, rBorder;
        LineToQuads(line, width, lBorder, rBorder);
        auto gid = IDGenerator::ForType(IDType::Graphics_Temporary)->GenerateID(this);
        backgroundBuffer->AddQuads(gid, 0, lBorder, rBorder, color);
        return gid;
    }

    void MapViewGL::RemoveBackground(unsigned int gid)
    {
        backgroundBuffer->RemoveItem(gid);
    }

    void MapViewGL::AddSceneLayover(uint32_t id, odr::Vec3D scenePos, QPixmap icon, QRect ltwh, int syntax)
    {
		sceneTiedLayovers.emplace(id, SceneTiedLayover{ id, scenePos, ltwh, icon, syntax });
        update();
    }

    void MapViewGL::RemoveSceneLayover(uint32_t id)
    {
        sceneTiedLayovers.erase(id);
        update();
    }

    int MapViewGL::VBufferUseage_pct() const
    {
        return permanentBuffer->Useage_pct();
    }

    void MapViewGL::initializeGL()
    {
        m_painting = false;
        initializeOpenGLFunctions();

        // draw both sides of faces
        glDisable(GL_CULL_FACE);

        permanentBuffer->Initialize();
        temporaryBuffer->Initialize();
        backgroundBuffer->Initialize();
        for (auto& buff : vehicleBuffer)
        {
            buff.Initialize();
        }
    }

    void MapViewGL::resizeGL(int width, int height)
    {
        // Projection is updated in paintGL for 2D mode (depends on camera Z).
        // For 3D mode, set perspective here.
        if (m_viewMode != ViewMode::TopDown2D)
        {
            m_projection.setToIdentity();
            m_projection.perspective(
                /* vertical angle */ 60.0f,
                /* aspect ratio */   width / float(height ? height : 1),
                /* near */           5.0f,
                /* far */            2000.0f
            );
        }
    }

    void MapViewGL::paintGL()
    {
        // Prevent recursive paintGL calls (Qt 6 may trigger reinitialization)
        if (m_painting) return;
        m_painting = true;

        MainWidget::Instance()->Painted();

        const qreal retinaScale = devicePixelRatio();
        glViewport(0, 0, width() * retinaScale, height() * retinaScale);

        if (m_viewMode == ViewMode::TopDown2D)
        {
            // Update ortho projection every frame so wheel zoom (camera Z change) works
            float aspect = width() / float(height() ? height() : 1);
            float viewSize = m_camera.translation().z() * 0.6f;
            if (viewSize < 10.0f) viewSize = 10.0f;
            m_projection.setToIdentity();
            m_projection.ortho(
                -viewSize * aspect, viewSize * aspect,
                -viewSize, viewSize,
                -5000.0f, 5000.0f
            );

            glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // Check if camera moved enough to need new tiles
            float camX = m_camera.translation().x();
            float camY = m_camera.translation().y();
            float camZ = m_camera.translation().z();            float moveThreshold = camZ * 0.3f;
            if (m_mapEnabled &&
                (std::abs(camX - m_lastCameraX) > moveThreshold ||
                 std::abs(camY - m_lastCameraY) > moveThreshold ||
                 std::abs(camZ - m_lastCameraZ) > moveThreshold ||
                 m_mapTiles.empty()))
            {
                UpdateMapTiles();
            }
        }
        else
        {
            // 3D perspective mode — update projection every frame (in case of resize)
            float aspect = width() / float(height() ? height() : 1);
            m_projection.setToIdentity();
            m_projection.perspective(60.0f, aspect, 5.0f, 5000.0f);

            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glClearColor(0.1f, 0.15f, 0.3f, 1.0f);
        }

        // update cached world2view matrix
        m_worldToView = m_projection * m_camera.toMatrix();

        // Draw satellite map background (if loaded)
        drawMapBackground();

        glDisable(GL_DEPTH_TEST);
        backgroundBuffer->Draw(m_worldToView);
        glEnable(GL_DEPTH_TEST);
        permanentBuffer->Draw(m_worldToView);
        temporaryBuffer->Draw(m_worldToView);
        for (auto& buff : vehicleBuffer)
        {
            buff.Draw(m_worldToView);
        }

        QPainter painter(this);

        for (auto& id_btn : sceneTiedLayovers)
        {
            auto& btn = id_btn.second;
            painter.drawPixmap(btn.renderedRect(m_worldToView), btn.icon);
        }
        painter.end();
        m_painting = false;
    }

    void MapViewGL::blenderOrbit(QPoint delta)
    {
        // Blender-style orbit: rotate around the target point on the ground
        // Left/right mouse movement = azimuth (rotate around Z axis)
        // Up/down mouse movement = elevation (rotate around horizontal axis)
        float sensitivity = 0.3f;
        float yawDeg = -delta.x() * sensitivity;
        float pitchDeg = -delta.y() * sensitivity;

        // Get the point the camera is looking at (on ground plane z=0)
        QVector3D camPos = m_camera.translation();
        QVector3D fwd = m_camera.forward();
        float t = (std::abs(fwd.z()) > 1e-6f) ? (-camPos.z() / fwd.z()) : 0.0f;
        QVector3D target = camPos + t * fwd;

        // Yaw: rotate around world Z
        QQuaternion yawRot = QQuaternion::fromAxisAndAngle(QVector3D(0, 0, 1), yawDeg);
        // Pitch: rotate around camera's right vector (clamped to avoid flip)
        QVector3D right = m_camera.right();
        QQuaternion pitchRot = QQuaternion::fromAxisAndAngle(right, pitchDeg);

        // Apply rotation: rotate camera position around target, then rotate camera orientation
        QVector3D offset = camPos - target;
        offset = yawRot.rotatedVector(offset);
        offset = pitchRot.rotatedVector(offset);
        m_camera.setTranslation(target + offset);

        // Update camera rotation
        QQuaternion newRot = yawRot * pitchRot * m_camera.rotation();
        m_camera.setRotation(newRot);
    }

    void MapViewGL::blenderPan(QPoint delta)
    {
        // Blender-style pan: move camera and target together along screen plane
        float sensitivity = m_camera.translation().z() * 0.0015f;
        QVector3D right = m_camera.right();
        QVector3D up = m_camera.up();
        QVector3D move = right * (-delta.x() * sensitivity) + up * (delta.y() * sensitivity);
        m_camera.translate(move);
    }

    void MapViewGL::mousePressEvent(QMouseEvent* event)
    {
        bool ctrlPressed = event->modifiers() & Qt::CTRL;

        // Blender-style: middle mouse = orbit, shift+middle = pan (in 3D mode)
        if (event->button() == Qt::MiddleButton && m_viewMode == ViewMode::Perspective3D)
        {
            if (event->modifiers() & Qt::SHIFT)
            {
                m_blenderNav = BlenderNavMode::Pan;
            }
            else
            {
                m_blenderNav = BlenderNavMode::Orbit;
            }
            m_blenderLastPos = event->pos();
            return;
        }

        if (event->button() == Qt::RightButton && !ctrlPressed)
        {
            lastMousePos = event->pos();
            freeRotateSession = FreeRotController();
            freeRotateSession->Update(QVector2D(lastMousePos), m_camera);
        }
        else if (event->button() == Qt::MiddleButton)
        {
            lastMousePos = event->pos();
            dragPan = true;
        }
        else
        {
            int pressedButton = 0;
            for (auto id_layover : sceneTiedLayovers)
            {
                if (id_layover.second.renderedRect(m_worldToView).contains(event->pos()))
                {
                    pressedButton = id_layover.second.syntax;
                    break;
                }
            }
            if (pressedButton != 0)
            {
                LM::KeyPressAction action(pressedButton);
                LM::ActionManager::Instance()->Record(action);
                emit(KeyPerformedAction(action));
            }
            else
            {
                LM::ActionManager::Instance()->Record(event);
                emit(MousePerformedAction(event));
            }
            ignoreNextMouseRelease = pressedButton != 0;
        }
    }

    void MapViewGL::mouseDoubleClickEvent(QMouseEvent* evt)
    {
        if (evt->button() == Qt::LeftButton)
        {
            LM::ActionManager::Instance()->Record(evt);
            emit(MousePerformedAction(evt));
        }
    }

    void MapViewGL::mouseMoveEvent(QMouseEvent* event)
    {
        UpdateRayHit(event->pos());

        // Blender-style navigation (3D mode only)
        if (m_blenderNav != BlenderNavMode::None)
        {
            QPoint delta = event->pos() - m_blenderLastPos;
            if (m_blenderNav == BlenderNavMode::Orbit)
                blenderOrbit(delta);
            else
                blenderPan(delta);
            m_blenderLastPos = event->pos();
            ActionManager::Instance()->Record(m_camera);
            update();
            lastMousePos = event->pos();
            return;
        }

        bool changeViewPoint = true;
        if (freeRotateSession.has_value())
        {
            if (event->x() < 0 || event->x() > width() ||
                event->y() < 0 || event->y() > height())
            {
                return;
            }

            freeRotateSession->Update(QVector2D(event->pos()), m_camera);
        }
        else if (dragPan)
        {
            auto lastGroundPos = PointerOnGround(lastMousePos);
            m_camera.translate((lastGroundPos - QVector2D(g_PointerOnGround[0], g_PointerOnGround[1])).toVector3D());
        }
        else
        {
            changeViewPoint = false;
        }

        if (changeViewPoint)
        {
            ActionManager::Instance()->Record(m_camera);
        }
        else
        {
            LM::ActionManager::Instance()->Record(event);
            emit(MousePerformedAction(event));
        }
        update();
        lastMousePos = event->pos();
    }

    void MapViewGL::mouseReleaseEvent(QMouseEvent* event)
    {
        if (m_blenderNav != BlenderNavMode::None)
        {
            m_blenderNav = BlenderNavMode::None;
            return;
        }
        if (freeRotateSession.has_value())
        {
            freeRotateSession.reset();
        }
        else if (dragPan)
        {
            dragPan = false;
        }
        else
        {
            if (!ignoreNextMouseRelease)
            {
                LM::ActionManager::Instance()->Record(event);
                emit(MousePerformedAction(event));
            }
        }
    }

    void MapViewGL::wheelEvent(QWheelEvent* event)
    {
        bool ctrlPressed = event->modifiers() & Qt::CTRL;
        auto dir = event->angleDelta().y() > 0 ? 1 : -1;

        if (ctrlPressed)
        {
            g_createRoadElevationOption += dir;
            ActionManager::Instance()->Record(g_createRoadElevationOption);
            emit(MousePerformedAction(event)); // immediately repaint cursor
        }
        else if (m_viewMode == ViewMode::TopDown2D)
        {
            // In 2D orthographic mode, zoom = move camera Z.
            // Scroll up (dir=1) should zoom IN = decrease Z = smaller view size.
            // Use proportional zoom step for smooth zoom at any scale.
            float curZ = m_camera.translation().z();
            float newZ = curZ * (1.0f - dir * 0.15f);
            if (newZ < 20.0f) newZ = 20.0f;    // max zoom in
            if (newZ > 50000.0f) newZ = 50000.0f; // max zoom out (city/region level)
            m_camera.setTranslation(m_camera.translation().x(),
                                    m_camera.translation().y(),
                                    newZ);
            ActionManager::Instance()->Record(m_camera);
        }
        else
        {
            // Blender-style zoom in 3D: dolly toward the point under the cursor
            // Scale the step by current distance for smooth zoom at any scale
            float camDist = m_camera.translation().length();
            float step = dir * camDist * 0.1f;
            QVector3D fwd = m_camera.forward();
            fwd.normalize();
            m_camera.translate(fwd * step);
            ActionManager::Instance()->Record(m_camera);
        }

        update();
    }

    void MapViewGL::keyPressEvent(QKeyEvent* event)
    {
        bool changeViewPoint = false;
        
        auto flatForward = m_camera.forward().toVector2D();
        flatForward.normalize();
        flatForward *= 5;
        if (event->key() == Qt::Key_W)
        {
            m_camera.translate(flatForward.x(), flatForward.y(), 0);
            changeViewPoint = true;
        }
        else if (event->key() == Qt::Key_S)
        {
            m_camera.translate(-flatForward.x(), -flatForward.y(), 0);
            changeViewPoint = true;
        }
        else if (event->key() == Qt::Key_A)
        {
            m_camera.translate(-flatForward.y(), flatForward.x(), 0);
            changeViewPoint = true;
        }
        else if (event->key() == Qt::Key_D)
        {
            m_camera.translate(flatForward.y(), -flatForward.x(), 0);
            changeViewPoint = true;
        }
        else if (event->key() == Qt::Key_Q)
        {
            m_camera.rotate(-5, QVector3D(0, 0, 1));
            changeViewPoint = true;
        }
        else if (event->key() == Qt::Key_E)
        {
            m_camera.rotate(5, QVector3D(0, 0, 1));
            changeViewPoint = true;
        }

        if (changeViewPoint)
        {
            LM::ActionManager::Instance()->Record(m_camera);
        }
        else
        {
            LM::ActionManager::Instance()->Record(event);
            emit(KeyPerformedAction(event));
        }
        // update cached world2view matrix
        update();
    }

    bool MapViewGL::event(QEvent* event)
    {
        if (event->type() == QEvent::TouchBegin ||
            event->type() == QEvent::TouchUpdate ||
            event->type() == QEvent::TouchEnd) 
        {
            touchScreen = true;
            QTouchEvent* touchEvent = static_cast<QTouchEvent*>(event);
            const QList<QEventPoint>& points = touchEvent->points();


            if (event->type() == QEvent::TouchEnd)
            {
                touchSessionType.reset();
            }
            else if (!touchSessionType.has_value())
            {
                touchSessionType.emplace(std::min(static_cast<int>(points.count()), 2));
            }

            // TODO: when a single-touch session becomes multi-touch, trigger a MouseRelease
            if (touchSessionType > points.count())
            {
                // During a multi-touch session, only process multi-touch events
                return true;
            }

            if (points.count() == 2 ||
                points.count() == 1 && MainWidget::Instance()->GetEditMode() == LM::EditMode::Mode_None)
            {
                // check if it's a view adjustment event
                if (event->type() == QEvent::TouchUpdate)
                {
                    if (!touchSession.has_value())
                    {
                        touchSession = TouchController();
                    }
                    touchSession->Update(points, m_camera);
                    update();
                }
                else
                {
                    touchSession.reset();
                }
            }
            else
            {
                touchSession.reset();
            }
            return true;
        }

        if (touchSession.has_value() &&
            (event->type() == QEvent::MouseMove ||
                event->type() == QEvent::MouseButtonPress ||
                event->type() == QEvent::MouseButtonDblClick ||
                event->type() == QEvent::MouseButtonRelease))
        {
            // During touch view adjument, disable editing
            return true;
        }

        return QWidget::event(event);
    }

    QVector3D MapViewGL::PointerDirection(QPoint cursor) const
    {
        if (m_viewMode == ViewMode::TopDown2D)
        {
            // In orthographic top-down mode, all rays are parallel pointing straight down
            return QVector3D(0, 0, -1);
        }

        auto halfHeight = height() / 2;
        auto halfWidth = width() / 2;

        auto focalPlanDistance = halfHeight / std::tan(M_PI / 6); // 60 deg FOV
        auto dirY = halfHeight - cursor.y();
        auto dirX = cursor.x() - halfWidth;
        QVector3D dir(dirX, dirY, -focalPlanDistance);
        auto rtn = m_camera.rotation().rotatedVector(dir);
        rtn.normalize();
        return rtn;
    }

    QPointF MapViewGL::PixelLocation(QVector3D globalDir) const
    {
        if (m_viewMode == ViewMode::TopDown2D)
        {
            // In 2D orthographic mode, project world coords to screen
            float retinaScale = devicePixelRatio();
            QVector3D clipPos = (m_projection * m_camera.toMatrix()).mapVector(globalDir);
            float xPixel = (clipPos.x() + 1.0f) * 0.5f * width() * retinaScale;
            float yPixel = (1.0f - clipPos.y()) * 0.5f * height() * retinaScale;
            return QPointF(xPixel, yPixel);
        }

        QVector3D localPos = m_camera.toMatrix().mapVector(globalDir);

        auto halfHeight = height() / 2;
        auto halfWidth = width() / 2;

        auto focalPlanDistance = halfHeight / std::tan(M_PI / 6); // 60 deg FOV
        auto scale = static_cast<float>(-focalPlanDistance) / localPos.z();
        auto xPixel = scale * localPos.x() + halfWidth;
        auto yPixel = -scale * localPos.y() + halfHeight;
        return QPointF(xPixel, yPixel);
    }

    QVector2D MapViewGL::PointerOnGround(QPoint cursor) const
    {
        if (m_viewMode == ViewMode::TopDown2D)
        {
            // In orthographic top-down mode, unproject screen coords to world coords.
            // Camera is at (0,0,H) with no rotation — looking straight down -Z at z=0 plane.
            // In ortho projection, all rays are parallel, so we can directly unproject
            // using the inverse view-projection matrix and then project onto z=0.
            float retinaScale = devicePixelRatio();
            float sx = cursor.x() * retinaScale;
            float sy = cursor.y() * retinaScale;
            float vp_h = height() * retinaScale;
            float vp_w = width() * retinaScale;

            // Normalize to NDC: [-1, 1]
            float ndcX = (2.0f * sx) / vp_w - 1.0f;
            float ndcY = 1.0f - (2.0f * sy) / vp_h;

            // Build the full view-projection matrix and invert it
            QMatrix4x4 viewProj = m_projection * m_camera.toMatrix();
            QMatrix4x4 inv = viewProj.inverted();

            // Unproject two points at different NDC z values to get a ray
            QVector3D p0 = inv.map(QVector3D(ndcX, ndcY, -1.0f));
            QVector3D p1 = inv.map(QVector3D(ndcX, ndcY,  1.0f));

            // Intersect ray with z=0 plane: p = p0 + t*(p1-p0), solve p.z=0
            float denom = p1.z() - p0.z();
            float t = (std::abs(denom) > 1e-6f) ? (-p0.z() / denom) : 0.0f;
            QVector3D worldPos = p0 + t * (p1 - p0);

            return QVector2D(worldPos.x(), worldPos.y());
        }

        // Original perspective code
        QVector3D dir = PointerDirection(cursor);
        auto length = -m_camera.translation().z() / dir.z();
        return QVector2D(m_camera.translation() + length * dir);
    }

    float MapViewGL::Zoom() const
    {
        QVector2D rayOnGround2D = PointerOnGround(lastMousePos);
        QVector3D rayOnGround = rayOnGround2D.toVector3D();
        return 100 / rayOnGround.distanceToPoint(m_camera.translation());
    }

    void MapViewGL::SetViewFromReplay(Transform3D t)
    {
        m_camera.setTranslation(t.translation());
        m_camera.setScale(t.scale());
        m_camera.setRotation(t.rotation());
        update();
    }

    void MapViewGL::UpdateRayHit(QPoint screen, bool fromReplay)
    {
        if (fromReplay)
        {
            lastMousePos = screen; // restore Zoom()
        }
        auto currGroundPos = PointerOnGround(screen);
        g_PointerOnGround[0] = currGroundPos.x();
        g_PointerOnGround[1] = currGroundPos.y();

        auto rayDir = PointerDirection(screen);
        auto pointerRayDir = odr::Vec3D{ rayDir.x(), rayDir.y(), rayDir.z() };
        g_CameraPosition = odr::Vec3D{ m_camera.translation().x(), m_camera.translation().y(), m_camera.translation().z() };
        RayCastQuery ray{
            g_CameraPosition,
            pointerRayDir
        };
        auto hitInfo = SpatialIndexer::Instance()->RayCast(ray);

        if (hitInfo.roadID != g_PointerRoadID && !g_PointerRoadID.empty())
        {
            auto prevHL = IDGenerator::ForType(IDType::Road)->GetByID<Road>(g_PointerRoadID);
            if (prevHL != nullptr)
            {
                prevHL->EnableHighlight(false);
                if (prevHL->IsConnectingRoad())
                {
                    (IDGenerator::ForType(IDType::Junction)->GetByID<Junction>(prevHL->generated.junction))->Hide(false);
                }
            }
        }
        g_PointerRoadID.clear();

        if (hitInfo.hit)
        {
            g_PointerRoadID = hitInfo.roadID;
            g_PointerRoadS = hitInfo.s;
            g_PointerLane = hitInfo.lane;
            auto hitRoad = IDGenerator::ForType(IDType::Road)->GetByID<Road>(g_PointerRoadID);
            hitRoad->EnableHighlight(true);
            if (hitRoad->IsConnectingRoad())
            {
                IDGenerator::ForType(IDType::Junction)->GetByID<Junction>(hitRoad->generated.junction)->Hide(true);
            }
        }

        odr::Vec3D pointerOnGround3D{ g_PointerOnGround[0], g_PointerOnGround[1], 0 };

        auto pointerVehicle = LM::SpatialIndexerDynamic::Instance()->RayCast(LM::g_CameraPosition, pointerRayDir);
        if (pointerVehicle != g_PointerVehicle)
        {
            auto prevHighlight = IDGenerator::ForType(IDType::Vehicle)->GetByID<Vehicle>(std::to_string(g_PointerVehicle));
            if (prevHighlight != nullptr)
            {
                prevHighlight->EnableRouteVisual(false, LM::ChangeTracker::Instance()->Map());
            }
        }
        if (pointerVehicle != -1)
        {
            IDGenerator::ForType(IDType::Vehicle)->GetByID<Vehicle>(std::to_string(pointerVehicle))->EnableRouteVisual(true,
                LM::ChangeTracker::Instance()->Map());
        };

        g_PointerVehicle = pointerVehicle;
    }

    QRect MapViewGL::SceneTiedLayover::renderedRect(QMatrix4x4 worldToView) const
    {
        auto homo = worldToView * QVector4D(pos[0], pos[1], pos[2], 1.0);
        int screenX = (homo.x() / homo.w() + 1) / 2 * g_mapViewGL->width();
        int screenY = (1 - homo.y() / homo.w()) / 2 * g_mapViewGL->height();
        auto rect = lwOffset;
        return QRect(screenX - rect.width() / 2 + rect.left(),
            screenY - rect.height() / 2 + rect.top(),
            rect.width(), rect.height());
    }
}
