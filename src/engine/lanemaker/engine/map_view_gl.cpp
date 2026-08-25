#include "map_view_gl.h"
#include <QMouseEvent>
#include <QPainter>
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QImage>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <limits>

#include "MapSubsystem.hpp"

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
            SetMapZoom(m_requestedMapZoom);
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

    double MapViewGL::metersPerPixel(double lat, double z)
    {
        const double earthCircumference = 40075016.686;
        return earthCircumference * std::cos(lat * M_PI / 180.0) / (std::pow(2.0, z) * 256.0);
    }

    bool MapViewGL::IsSatelliteNoDataTile(const QImage& image)
    {
        if (image.isNull()) return true;
        const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
        double sum = 0.0;
        double sumSquares = 0.0;
        double chroma = 0.0;
        int count = 0;
        for (int y = 0; y < rgb.height(); y += 8) {
            const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
            for (int x = 0; x < rgb.width(); x += 8) {
                const int r = qRed(line[x]);
                const int g = qGreen(line[x]);
                const int b = qBlue(line[x]);
                const double luminance = (r + g + b) / 3.0;
                sum += luminance;
                sumSquares += luminance * luminance;
                chroma += std::max({r, g, b}) - std::min({r, g, b});
                ++count;
            }
        }
        if (count == 0) return true;
        const double mean = sum / count;
        const double variance = std::max(0.0, sumSquares / count - mean * mean);
        return mean > 170.0 && std::sqrt(variance) < 12.0 && chroma / count < 3.0;
    }

    void MapViewGL::SetMapCenter(double lat, double lon)
    {
        m_mapCenterLat = std::clamp(lat, -85.05112878, 85.05112878);
        m_mapCenterLon = std::clamp(lon, -180.0, 180.0);
        m_mapTiles.clear();
        m_mapTexture.reset();
        m_mapCompositeImage = QImage();
        m_mapCompositeDirty = true;
        m_mapEnabled = true;
        if (!m_tileNam) m_tileNam = new QNetworkAccessManager(this);
        m_lastCameraZ = -1;
        update();
    }

    void MapViewGL::SetMapZoom(double zoom)
    {
        if (m_viewMode != ViewMode::TopDown2D) return;
        m_requestedMapZoom = std::clamp(zoom, 2.0, 19.0);
        // The widget can be zero-sized while LaneMaker is being constructed.
        // Defer the camera-height calculation until the first resize event.
        if (height() <= 0) return;
        // MapViewGL derives its slippy-map zoom from the visible vertical
        // extent. Set camera height so it matches MapLibre's zoom level.
        const double mpp = metersPerPixel(m_mapCenterLat, m_requestedMapZoom);
        const float viewportHeight = float(height());
        const float cameraHeight = float(mpp * viewportHeight / 1.2);
        auto translation = m_camera.translation();
        translation.setZ(std::max(10.0f, cameraHeight));
        m_camera.setTranslation(translation);
        m_lastCameraZ = -1; // force tile update on next paint
        update();
    }

    void MapViewGL::ClearMapBackground()
    {
        m_mapTiles.clear();
        m_mapTexture.reset();
        m_mapCompositeImage = QImage();
        m_mapCompositeDirty = true;
        m_mapEnabled = false;
        update();
    }

    void MapViewGL::UpdateMapTiles()
    {
        if (!m_mapEnabled || m_viewMode != ViewMode::TopDown2D) return;
        if (width() <= 0 || height() <= 0) return;
        if (!m_tileNam) m_tileNam = new QNetworkAccessManager(this);

        float camZ = m_camera.translation().z();
        float aspect = width() / float(height() ? height() : 1);
        float viewSize = camZ * 0.6f;
        if (viewSize < 10.0f) viewSize = 10.0f;

        float camX = m_camera.translation().x();
        float camY = m_camera.translation().y();

        // Use the new map subsystem for proper coordinate conversion.
        // The camera world position (camX, camY) is an offset in meters
        // from the map center. Convert the visible world extent to
        // lat/lon using CoordinateTransform (EPSG:4326 ↔ EPSG:3857).
        double worldLeft   = camX - viewSize * aspect;
        double worldRight  = camX + viewSize * aspect;
        double worldBottom = camY - viewSize;
        double worldTop    = camY + viewSize;

        // Map center in Web Mercator (EPSG:3857)
        auto centerMerc = map::CoordinateTransform::lonLatToMercator(m_mapCenterLon, m_mapCenterLat);

        // Visible extent in Web Mercator (world coords are meter offsets from center)
        map::MapRectangle mercExtent(
            centerMerc.x + worldLeft,
            centerMerc.y + worldBottom,
            centerMerc.x + worldRight,
            centerMerc.y + worldTop
        );

        const int newZoom = std::clamp(
            static_cast<int>(std::round(m_requestedMapZoom)), 2, 19);

        if (newZoom != m_mapZoom) {
            m_mapTiles.clear();
            m_mapZoom = newZoom;
            m_mapCompositeDirty = true;
        }

        // Use TileMatrix for proper tile range calculation
        auto tileMatrix = map::TileMatrix::fromWebMercator(m_mapZoom);
        auto tileRange = tileMatrix.tileRangeFromExtent(mercExtent);

        if (!tileRange.isValid()) {
            pruneInvisibleTiles();
            m_lastCameraX = camX;
            m_lastCameraY = camY;
            m_lastCameraZ = camZ;
            return;
        }

        // Safety cap on tile count
        const int MAX_TILES = 30;
        int count = tileRange.count();
        if (count > MAX_TILES) {
            while (count > MAX_TILES && m_mapZoom > 2) {
                m_mapZoom--;
                tileMatrix = map::TileMatrix::fromWebMercator(m_mapZoom);
                tileRange = tileMatrix.tileRangeFromExtent(mercExtent);
                count = tileRange.count();
            }
            m_mapTiles.clear();
            m_mapCompositeDirty = true;
        }

        for (int ty = tileRange.startRow; ty <= tileRange.endRow; ty++) {
            for (int tx = tileRange.startCol; tx <= tileRange.endCol; tx++) {
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
        m_lastCameraX = camX;
        m_lastCameraY = camY;
        m_lastCameraZ = camZ;
    }

    void MapViewGL::requestTile(int z, int x, int y)
    {
        if (!m_tileNam) return;

        auto tile = std::make_unique<MapTile>();
        tile->z = z;
        tile->x = x;
        tile->y = y;
        tile->loading = true;

        // Use TileMatrix for proper tile extent in Web Mercator
        auto tileMatrix = map::TileMatrix::fromWebMercator(z);
        auto tileExtent = tileMatrix.tileExtent(map::TileXYZ(x, y, z));

        // Map center in Web Mercator
        auto centerMerc = map::CoordinateTransform::lonLatToMercator(m_mapCenterLon, m_mapCenterLat);

        // World position = tile center relative to map center (in meters)
        tile->worldX = tileExtent.centerX() - centerMerc.x;
        tile->worldY = tileExtent.centerY() - centerMerc.y;
        tile->worldSize = tileExtent.width();  // square tiles

        MapTile* rawPtr = tile.get();
        m_mapTiles.push_back(std::move(tile));
        requestTileImage(rawPtr, z, x, y);
    }

    void MapViewGL::requestTileImage(MapTile* tile, int sourceZ, int sourceX, int sourceY)
    {
        if (!m_tileNam || !tile) return;
        const QString url = QString(
            "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%1/%2/%3")
            .arg(sourceZ).arg(sourceY).arg(sourceX);
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio/1.0");
        auto* reply = m_tileNam->get(request);

        connect(reply, &QNetworkReply::finished, this,
                [this, tile, sourceZ, sourceX, sourceY, reply]() {
            bool tileExists = false;
            for (auto& current : m_mapTiles) {
                if (current.get() == tile) { tileExists = true; break; }
            }
            if (!tileExists) {
                reply->deleteLater();
                return;
            }

            QImage image;
            if (reply->error() == QNetworkReply::NoError)
                image.loadFromData(reply->readAll());
            const bool needsFallback = image.isNull() || IsSatelliteNoDataTile(image);
            reply->deleteLater();

            if (needsFallback && sourceZ > 2) {
                requestTileImage(tile, sourceZ - 1, sourceX / 2, sourceY / 2);
                return;
            }

            if (!needsFallback) {
                const int delta = tile->z - sourceZ;
                if (delta > 0) {
                    const int factor = 1 << delta;
                    const int localX = tile->x - sourceX * factor;
                    const int localY = tile->y - sourceY * factor;
                    const int x0 = localX * image.width() / factor;
                    const int y0 = localY * image.height() / factor;
                    const int x1 = (localX + 1) * image.width() / factor;
                    const int y1 = (localY + 1) * image.height() / factor;
                    image = image.copy(x0, y0, x1 - x0, y1 - y0)
                        .scaled(256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                }
                tile->image = image.convertToFormat(QImage::Format_RGB32);
            } else {
                spdlog::warn("No satellite imagery available: z={} x={} y={}",
                             tile->z, tile->x, tile->y);
            }
            tile->loading = false;
            m_mapCompositeDirty = true;
            update();
        });
    }

    void MapViewGL::pruneInvisibleTiles()
    {
        // Use AABB overlap test: keep tile if its bounding box overlaps
        // the visible area. This is more correct than distance-from-center
        // because a large tile may have its center far away but still
        // overlap the visible region.
        float camX = m_camera.translation().x();
        float camY = m_camera.translation().y();
        float camZ = m_camera.translation().z();
        float aspect = width() / float(height() ? height() : 1);
        float viewSize = camZ * 0.6f;
        if (viewSize < 10.0f) viewSize = 10.0f;

        // Visible area bounds in world coordinates
        float viewLeft   = camX - viewSize * aspect;
        float viewRight  = camX + viewSize * aspect;
        float viewBottom = camY - viewSize;
        float viewTop    = camY + viewSize;

        m_mapTiles.erase(
            std::remove_if(m_mapTiles.begin(), m_mapTiles.end(),
                [viewLeft, viewRight, viewBottom, viewTop, this](const std::unique_ptr<MapTile>& t) {
                    if (t->z != m_mapZoom) return true; // wrong zoom level
                    float half = float(t->worldSize) * 0.5f;
                    float tileLeft   = float(t->worldX) - half;
                    float tileRight  = float(t->worldX) + half;
                    float tileBottom = float(t->worldY) - half;
                    float tileTop    = float(t->worldY) + half;
                    // AABB overlap: keep if tiles intersects visible area
                    bool overlaps = !(tileRight < viewLeft || tileLeft > viewRight ||
                                      tileTop < viewBottom || tileBottom > viewTop);
                    return !overlaps;
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

    void MapViewGL::compositeMapImage()
    {
        if (m_mapTiles.empty()) return;

        int minTileX = std::numeric_limits<int>::max();
        int minTileY = std::numeric_limits<int>::max();
        int maxTileX = std::numeric_limits<int>::min();
        int maxTileY = std::numeric_limits<int>::min();
        int tilesWithImage = 0;
        double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;
        for (auto& tile : m_mapTiles) {
            minTileX = std::min(minTileX, tile->x);
            minTileY = std::min(minTileY, tile->y);
            maxTileX = std::max(maxTileX, tile->x);
            maxTileY = std::max(maxTileY, tile->y);
            const double half = tile->worldSize * 0.5;
            minX = std::min(minX, tile->worldX - half);
            minY = std::min(minY, tile->worldY - half);
            maxX = std::max(maxX, tile->worldX + half);
            maxY = std::max(maxY, tile->worldY + half);
            if (!tile->image.isNull()) ++tilesWithImage;
        }
        if (tilesWithImage == 0) return;

        const int tilesX = maxTileX - minTileX + 1;
        const int tilesY = maxTileY - minTileY + 1;
        const int imgW = std::min(tilesX * 256, 4096);
        const int imgH = std::min(tilesY * 256, 4096);

        m_mapCompositeImage = QImage(imgW, imgH, QImage::Format_RGB32);
        m_mapCompositeImage.fill(QColor(13, 17, 23));

        QPainter painter(&m_mapCompositeImage);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (auto& tile : m_mapTiles) {
            if (tile->image.isNull()) continue;
            const int col = tile->x - minTileX;
            const int row = tile->y - minTileY;
            const int x0 = qRound(static_cast<double>(col) * imgW / tilesX);
            const int y0 = qRound(static_cast<double>(row) * imgH / tilesY);
            const int x1 = qRound(static_cast<double>(col + 1) * imgW / tilesX);
            const int y1 = qRound(static_cast<double>(row + 1) * imgH / tilesY);
            painter.drawImage(QRect(x0, y0, x1 - x0, y1 - y0), tile->image);
        }
        painter.end();

        m_mapCompositeWorldMinX = minX;
        m_mapCompositeWorldMinY = minY;
        m_mapCompositeWorldMaxX = maxX;
        m_mapCompositeWorldMaxY = maxY;
        m_mapCompositeDirty = false;
    }

    void MapViewGL::drawMapBackground()
    {
        if (!m_mapEnabled) return;

        bool hasImages = false;
        bool allFinished = !m_mapTiles.empty();
        for (auto& tile : m_mapTiles) {
            if (!tile->image.isNull()) hasImages = true;
            if (tile->loading) allFinished = false;
        }

        initTexturedShader();

        if (m_mapCompositeDirty && hasImages && (allFinished || !m_mapTexture)) {
            compositeMapImage();
            if (!m_mapCompositeImage.isNull()) {
                m_mapTexture = std::make_unique<QOpenGLTexture>(m_mapCompositeImage);
                m_mapTexture->setMinificationFilter(QOpenGLTexture::Linear);
                m_mapTexture->setMagnificationFilter(QOpenGLTexture::Linear);
                m_mapTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
            }
        }

        if (!m_mapTexture) return;

        // Draw one quad covering the composite area in world coordinates
        float cx = (float)((m_mapCompositeWorldMinX + m_mapCompositeWorldMaxX) * 0.5);
        float cy = (float)((m_mapCompositeWorldMinY + m_mapCompositeWorldMaxY) * 0.5);
        float halfW = (float)((m_mapCompositeWorldMaxX - m_mapCompositeWorldMinX) * 0.5);
        float halfH = (float)((m_mapCompositeWorldMaxY - m_mapCompositeWorldMinY) * 0.5);

        float quadVerts[] = {
            // pos                          // texcoord (V=0 at top/north)
            cx - halfW, cy - halfH, 0,     0, 1,
            cx + halfW, cy - halfH, 0,     1, 1,
            cx + halfW, cy + halfH, 0,     1, 0,
            cx - halfW, cy - halfH, 0,     0, 1,
            cx + halfW, cy + halfH, 0,     1, 0,
            cx - halfW, cy + halfH, 0,     0, 0,
        };

        m_bgQuadVbo.bind();
        m_bgQuadVbo.write(0, quadVerts, sizeof(quadVerts));
        m_bgQuadVbo.release();

        glDisable(GL_DEPTH_TEST);
        m_texturedShader.bind();
        m_texturedShader.setUniformValue("worldToView", m_worldToView);
        m_mapTexture->bind(0);
        m_texturedShader.setUniformValue("tex", 0);

        m_bgQuadVao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_bgQuadVao.release();

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
        if (m_glInitialized) return; // already initialized
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

        m_glInitialized = true;
        emit GLInitialized();
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
        else if (height > 0)
        {
            SetMapZoom(m_requestedMapZoom);
        }
    }

    void MapViewGL::drawSkyGradient()
    {
        // Draw a fullscreen sky gradient quad (top=zenith blue, bottom=horizon warm)
        // Uses a simple shader with no depth testing
        if (!m_skyShaderInit) {
            m_skyShaderInit = true;
            m_skyShader.addShaderFromSourceCode(QOpenGLShader::Vertex,
                R"(#version 330 core
                layout(location = 0) in vec2 aPos;
                out vec2 vScreenPos;
                void main() {
                    vScreenPos = aPos;
                    gl_Position = vec4(aPos, 0.999, 1.0);  // far plane
                })");

            m_skyShader.addShaderFromSourceCode(QOpenGLShader::Fragment,
                R"(#version 330 core
                in vec2 vScreenPos;
                out vec4 FragColor;
                void main() {
                    // vScreenPos.y: -1 = bottom, +1 = top
                    float t = clamp(vScreenPos.y * 0.5 + 0.5, 0.0, 1.0);
                    // Zenith (top): deep sky blue
                    vec3 zenith = vec3(0.25, 0.45, 0.75);
                    // Horizon (bottom): warm haze
                    vec3 horizon = vec3(0.65, 0.70, 0.78);
                    // Ground (below horizon): warm earth
                    vec3 ground = vec3(0.35, 0.32, 0.28);
                    vec3 color;
                    if (t > 0.5) {
                        color = mix(horizon, zenith, smoothstep(0.5, 1.0, t));
                    } else {
                        color = mix(ground, horizon, smoothstep(0.0, 0.5, t));
                    }
                    FragColor = vec4(color, 1.0);
                })");

            m_skyShader.link();

            // Fullscreen quad
            float quad[] = {
                -1, -1,  1, -1,  1,  1,
                -1, -1,  1,  1, -1,  1,
            };
            m_skyVao.create();
            m_skyVao.bind();
            m_skyVbo.create();
            m_skyVbo.bind();
            m_skyVbo.allocate(quad, sizeof(quad));
            m_skyShader.enableAttributeArray(0);
            m_skyShader.setAttributeBuffer(0, GL_FLOAT, 0, 2, 0);
            m_skyVbo.release();
            m_skyVao.release();
        }

        glDisable(GL_DEPTH_TEST);
        m_skyShader.bind();
        m_skyVao.bind();
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_skyVao.release();
        m_skyShader.release();
        glEnable(GL_DEPTH_TEST);
    }

    void MapViewGL::drawGroundGrid()
    {
        // Draw a subtle ground grid centered on the camera for spatial reference
        // Grid is drawn at z=0, extends ±gridSize around camera X/Y
        if (!m_gridShaderInit) {
            m_gridShaderInit = true;
            m_gridShader.addShaderFromSourceCode(QOpenGLShader::Vertex,
                R"(#version 330 core
                layout(location = 0) in vec3 aPos;
                uniform mat4 worldToView;
                void main() {
                    gl_Position = worldToView * vec4(aPos, 1.0);
                })");

            m_gridShader.addShaderFromSourceCode(QOpenGLShader::Fragment,
                R"(#version 330 core
                out vec4 FragColor;
                uniform vec4 gridColor;
                void main() {
                    FragColor = gridColor;
                })");

            m_gridShader.link();
            m_gridShaderUniformLoc = m_gridShader.uniformLocation("worldToView");
            m_gridColorLoc = m_gridShader.uniformLocation("gridColor");

            m_gridVao.create();
            m_gridVao.bind();
            m_gridVbo.create();
            m_gridVbo.bind();
            m_gridVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
            m_gridVbo.allocate(nullptr, 2 * 200 * 3 * sizeof(float));  // max 200 lines
            m_gridShader.enableAttributeArray(0);
            m_gridShader.setAttributeBuffer(0, GL_FLOAT, 0, 3, 0);
            m_gridVbo.release();
            m_gridVao.release();
        }

        // Build grid lines centered on camera
        QVector3D camPos = m_camera.translation();
        float gridSize = 200.0f;  // total extent
        float step = 10.0f;       // grid cell size
        float cx = camPos.x();
        float cy = camPos.y();
        float x0 = std::floor((cx - gridSize) / step) * step;
        float x1 = std::ceil((cx + gridSize) / step) * step;
        float y0 = std::floor((cy - gridSize) / step) * step;
        float y1 = std::ceil((cy + gridSize) / step) * step;

        std::vector<float> verts;
        for (float x = x0; x <= x1; x += step) {
            verts.push_back(x); verts.push_back(y0); verts.push_back(0);
            verts.push_back(x); verts.push_back(y1); verts.push_back(0);
        }
        for (float y = y0; y <= y1; y += step) {
            verts.push_back(x0); verts.push_back(y); verts.push_back(0);
            verts.push_back(x1); verts.push_back(y); verts.push_back(0);
        }

        m_gridVao.bind();
        m_gridVbo.bind();
        m_gridVbo.write(0, verts.data(), verts.size() * sizeof(float));

        m_gridShader.bind();
        m_gridShader.setUniformValue(m_gridShaderUniformLoc, m_worldToView);
        // Major grid lines (every 50m) brighter, minor lines subtle
        m_gridShader.setUniformValue(m_gridColorLoc, QVector4D(0.4f, 0.42f, 0.48f, 0.3f));

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_LINES, 0, verts.size() / 3);
        glDisable(GL_BLEND);

        m_gridShader.release();
        m_gridVbo.release();
        m_gridVao.release();
    }

    void MapViewGL::paintGL()
    {
        // If initializeGL() hasn't completed yet, skip painting
        if (!m_glInitialized) return;

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
            const float depthRange = std::max(5000.0f,
                std::abs(m_camera.translation().z()) * 2.0f);
            m_projection.setToIdentity();
            m_projection.ortho(
                -viewSize * aspect, viewSize * aspect,
                -viewSize, viewSize,
                -depthRange, depthRange
            );

            glClearColor(0.09f, 0.09f, 0.11f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            // 2D mode — disable 3D lighting
            permanentBuffer->m_viewMode3D = false;
            temporaryBuffer->m_viewMode3D = false;
            for (auto& buff : vehicleBuffer) {
                buff.m_viewMode3D = false;
            }
            backgroundBuffer->m_viewMode3D = false;

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

            // Sky gradient background (top = light blue, bottom = warm horizon)
            // Replaces the old flat blue clear color
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawSkyGradient();
            glClear(GL_DEPTH_BUFFER_BIT);  // clear depth after sky

            // Update lighting state for all buffer managers
            QVector3D camPos = m_camera.translation();
            permanentBuffer->m_cameraPos = camPos;
            permanentBuffer->m_viewMode3D = true;
            temporaryBuffer->m_cameraPos = camPos;
            temporaryBuffer->m_viewMode3D = true;
            for (auto& buff : vehicleBuffer) {
                buff.m_cameraPos = camPos;
                buff.m_viewMode3D = true;
            }
            backgroundBuffer->m_cameraPos = camPos;
            backgroundBuffer->m_viewMode3D = true;

            // Draw ground reference grid
            drawGroundGrid();
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
            SetMapZoom(m_requestedMapZoom + dir * 0.5);
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

            // When a single-touch session becomes multi-touch, the touch
            // points are handled by the multi-touch branch below.
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

    void MapViewGL::ZoomIn()
    {
        if (m_viewMode == ViewMode::TopDown2D)
        {
            SetMapZoom(m_requestedMapZoom + 1.0);
        }
        else
        {
            QVector3D fwd = m_camera.forward();
            fwd.normalize();
            float camDist = m_camera.translation().length();
            m_camera.translate(fwd * (camDist * 0.1f));
        }
        update();
    }

    void MapViewGL::ZoomOut()
    {
        if (m_viewMode == ViewMode::TopDown2D)
        {
            SetMapZoom(m_requestedMapZoom - 1.0);
        }
        else
        {
            QVector3D fwd = m_camera.forward();
            fwd.normalize();
            float camDist = m_camera.translation().length();
            m_camera.translate(fwd * (-camDist * 0.1f));
        }
        update();
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
