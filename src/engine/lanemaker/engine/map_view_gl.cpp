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
            // NO rotation — default camera looks down -Z, which is straight down at XY plane.
            m_camera.setTranslation(0, 0, 500);
            m_camera.setRotation(0, QVector3D(1, 0, 0));
        }
        else
        {
            m_camera.setTranslation(0, -200, 250);
            m_camera.setRotation(30, QVector3D(1, 0, 0));
        }
    }

    void MapViewGL::SetViewMode(ViewMode mode)
    {
        if (m_viewMode == mode) return;
        m_viewMode = mode;
        ResetCamera();
        update();
    }

    void MapViewGL::SetMapBackground(const QImage& tileImage, double centerLat, double centerLon, double scale)
    {
        m_mapCenterLat = centerLat;
        m_mapCenterLon = centerLon;
        m_mapScale = scale;
        m_mapTextureValid = !tileImage.isNull();
        // World extent = image width * meters per pixel
        m_mapWorldExtent = tileImage.width() * scale;

        if (m_mapTextureValid)
        {
            makeCurrent();
            m_mapTexture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
            m_mapTexture->setData(tileImage);
            m_mapTexture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            m_mapTexture->setMagnificationFilter(QOpenGLTexture::Linear);
            m_mapTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
            doneCurrent();
        }
        update();
    }

    void MapViewGL::ClearMapBackground()
    {
        makeCurrent();
        m_mapTexture.reset();
        m_mapTextureValid = false;
        doneCurrent();
        update();
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
        if (!m_mapTextureValid || !m_mapTexture) return;
        initTexturedShader();

        // Update quad vertices to match the actual map world extent
        float halfExt = float(m_mapWorldExtent) * 0.5f;
        float quadVerts[] = {
            // pos              // texcoord
            -halfExt, -halfExt, 0,    0, 1,
             halfExt, -halfExt, 0,    1, 1,
             halfExt,  halfExt, 0,    1, 0,
            -halfExt, -halfExt, 0,    0, 1,
             halfExt,  halfExt, 0,    1, 0,
            -halfExt,  halfExt, 0,    0, 0,
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

            glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }
        else
        {
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

    void MapViewGL::mousePressEvent(QMouseEvent* event)
    {
        bool ctrlPressed = event->modifiers() & Qt::CTRL;
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
            // Clamp Z to keep a reasonable range.
            float newZ = m_camera.translation().z() - dir * 30.0f;
            if (newZ < 50.0f) newZ = 50.0f;
            if (newZ > 2000.0f) newZ = 2000.0f;
            m_camera.setTranslation(m_camera.translation().x(),
                                    m_camera.translation().y(),
                                    newZ);
            ActionManager::Instance()->Record(m_camera);
        }
        else
        {
            auto delta = dir * PointerDirection(lastMousePos) * 10;
            m_camera.translate(delta);
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
