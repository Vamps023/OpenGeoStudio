#include "gl_buffer_manage.h"
#include "triangulation.h"
#include "road.h"

#include <QOpenGLShaderProgram>
#include <QVector3D>

namespace LM
{
    // Compute face normal from three vertices (counter-clockwise winding)
    static QVector3D computeFaceNormal(const QVector3D& v0, const QVector3D& v1, const QVector3D& v2) {
        QVector3D edge1 = v1 - v0;
        QVector3D edge2 = v2 - v0;
        QVector3D normal = QVector3D::crossProduct(edge1, edge2);
        normal.normalize();
        return normal;
    }

    GLBufferManage::GLBufferManage(unsigned int capacity):
        m_vertexBufferData(capacity),
        m_vbo(QOpenGLBuffer::VertexBuffer),
        shader(":/shaders/simple.vert", ":/shaders/simple.frag")
    {
        shader.m_uniformNames.append("worldToView");
        shader.m_uniformNames.append("objectInfo");
        shader.m_uniformNames.append("cameraPos");
        shader.m_uniformNames.append("lightDir");
        shader.m_uniformNames.append("lightColor");
        shader.m_uniformNames.append("ambientColor");
        shader.m_uniformNames.append("viewMode3D");
        m_vertexBufferCount = 0;
    }

    void GLBufferManage::Initialize()
    {
        initializeOpenGLFunctions();

        shader.create();

        m_vao.create();
        m_vao.bind();

        m_vbo.create();
        m_vbo.bind();
        m_vbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
        int vertexMemSize = m_vertexBufferData.size() * sizeof(Vertex);
        m_vbo.allocate(m_vertexBufferData.data(), vertexMemSize);

        auto shaderProgramm = shader.shaderProgram();
        shaderProgramm->enableAttributeArray(0);
        shaderProgramm->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(Vertex));
        // attribute 1 = normals (for PBR lighting)
        shaderProgramm->enableAttributeArray(1);
        shaderProgramm->setAttributeBuffer(1, GL_FLOAT, offsetof(Vertex, nx), 3, sizeof(Vertex));
        // attribute 2 = color
        shaderProgramm->enableAttributeArray(2);
        shaderProgramm->setAttributeBuffer(2, GL_FLOAT, offsetof(Vertex, r), 3, sizeof(Vertex));
        // attribute 3 = objectID
        shaderProgramm->enableAttributeArray(3);
        shaderProgramm->setAttributeBuffer(3, GL_FLOAT, offsetof(Vertex, objectID), 1, sizeof(Vertex));

        m_objectInfo = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target1D);
        m_objectInfo->setData(QImage(MaxObjectID, 1, QImage::Format_Grayscale8));
        Reset();

        shaderProgramm->setUniformValue(shader.m_uniformIDs[1], 0);

        // Release (unbind) all
        m_vao.release();
        m_vbo.release();
    }

    void GLBufferManage::Reset()
    {
        memset(m_objectFlag, static_cast<uint8_t>(ObjectDisplayFlag::Normal), MaxObjectID);
        m_objectInfo->setData(QOpenGLTexture::PixelFormat::Red, QOpenGLTexture::PixelType::UInt8, m_objectFlag);
    }

    void GLBufferManage::CleanupResources()
    {
        m_objectInfo.reset();
    }

    bool GLBufferManage::AddQuads(unsigned int gid, unsigned int objectID, const odr::Line3D& lBorder, const odr::Line3D& rBorder, QColor color)
    {
        assert(lBorder.size() == rBorder.size());
        assert(lBorder.size() >= 2);

        auto nNewVertex = (lBorder.size() - 1) * 3 * 2;
        if (m_vertexBufferCount + nNewVertex > m_vertexBufferData.size())
        {
            return false;
        }

        m_vao.bind();
        m_vbo.bind();

        std::set<GLuint> vids;
        const auto vertexBufferChangeBegin = m_vertexBufferCount;
        for (int i = 0; i < lBorder.size() - 1; ++i)
        {
            auto l0 = lBorder[i], l1 = lBorder[i + 1];
            auto r0 = rBorder[i], r1 = rBorder[i + 1];

            QVector3D p_l0(l0[0], l0[1], l0[2]);
            QVector3D p_l1(l1[0], l1[1], l1[2]);
            QVector3D p_r0(r0[0], r0[1], r0[2]);
            QVector3D p_r1(r1[0], r1[1], r1[2]);

            // Triangle 1: l0, r0, l1
            QVector3D n1 = computeFaceNormal(p_l0, p_r0, p_l1);
            auto v11 = m_vertexBufferCount++;
            m_vertexBufferData[v11] = Vertex(p_l0, n1, color, gid, objectID);
            vids.emplace(v11);
            auto v12 = m_vertexBufferCount++;
            m_vertexBufferData[v12] = Vertex(p_r0, n1, color, gid, objectID);
            vids.emplace(v12);
            auto v13 = m_vertexBufferCount++;
            m_vertexBufferData[v13] = Vertex(p_l1, n1, color, gid, objectID);
            vids.emplace(v13);

            // Triangle 2: l1, r0, r1
            QVector3D n2 = computeFaceNormal(p_l1, p_r0, p_r1);
            auto v21 = m_vertexBufferCount++;
            m_vertexBufferData[v21] = Vertex(p_l1, n2, color, gid, objectID);
            vids.emplace(v21);
            auto v22 = m_vertexBufferCount++;
            m_vertexBufferData[v22] = Vertex(p_r0, n2, color, gid, objectID);
            vids.emplace(v22);
            auto v23 = m_vertexBufferCount++;
            m_vertexBufferData[v23] = Vertex(p_r1, n2, color, gid, objectID);
            vids.emplace(v23);
        }
        auto ptr_v = m_vbo.mapRange(vertexBufferChangeBegin * sizeof(Vertex),
            (m_vertexBufferCount - vertexBufferChangeBegin) * sizeof(Vertex),
            QOpenGLBuffer::RangeInvalidate | QOpenGLBuffer::RangeWrite);
        assert(ptr_v != nullptr);
        memcpy(ptr_v, m_vertexBufferData.data() + vertexBufferChangeBegin,
            (m_vertexBufferCount - vertexBufferChangeBegin) * sizeof(Vertex));
        m_vbo.unmap();

        m_vbo.release();
        m_vao.release();

        idToVids.emplace(gid, vids);

        assert(vertexBufferChangeBegin + nNewVertex == m_vertexBufferCount);
        return true;
    }

    bool GLBufferManage::AddPoly(unsigned int gid, unsigned int objectID, const odr::Line3D& boundary, QColor color)
    {
        auto newTriangles = Triangulate_2_5d(boundary);
        auto nNewVertex = newTriangles.size() * 3;
        if (m_vertexBufferCount + nNewVertex > m_vertexBufferData.size())
        {
            return false;
        }

        m_vao.bind();
        m_vbo.bind();

        std::set<GLuint> vids;
        const auto vertexBufferChangeBegin = m_vertexBufferCount;
        for (auto tri : newTriangles)
        {
            auto p1 = boundary[std::get<0>(tri)];
            auto p2 = boundary[std::get<1>(tri)];
            auto p3 = boundary[std::get<2>(tri)];

            QVector3D vp1(p1[0], p1[1], p1[2]);
            QVector3D vp2(p2[0], p2[1], p2[2]);
            QVector3D vp3(p3[0], p3[1], p3[2]);
            QVector3D normal = computeFaceNormal(vp1, vp2, vp3);

            auto v1 = m_vertexBufferCount++;
            vids.emplace(v1);
            m_vertexBufferData[v1] = Vertex(vp1, normal, color, gid, objectID);

            auto v2 = m_vertexBufferCount++;
            vids.emplace(v2);
            m_vertexBufferData[v2] = Vertex(vp2, normal, color, gid, objectID);

            auto v3 = m_vertexBufferCount++;
            vids.emplace(v3);
            m_vertexBufferData[v3] = Vertex(vp3, normal, color, gid, objectID);
        }

        auto ptr_v = m_vbo.mapRange(vertexBufferChangeBegin * sizeof(Vertex),
            (m_vertexBufferCount - vertexBufferChangeBegin) * sizeof(Vertex),
            QOpenGLBuffer::RangeInvalidate | QOpenGLBuffer::RangeWrite);
        memcpy(ptr_v, m_vertexBufferData.data() + vertexBufferChangeBegin,
            (m_vertexBufferCount - vertexBufferChangeBegin) * sizeof(Vertex));
        m_vbo.unmap();

        m_vbo.release();
        m_vao.release();

        idToVids.emplace(gid, vids);

        assert(vertexBufferChangeBegin + nNewVertex == m_vertexBufferCount);
        return true;
    }

    bool GLBufferManage::AddColumn(unsigned int gid, unsigned int objectID, const odr::Line3D& boundary, double h, QColor color)
    {
        auto capTriangles = Triangulate_2_5d(boundary);
        auto nNewVertex = capTriangles.size() * 3 * 2 + boundary.size() * 3 * 2;
        if (m_vertexBufferCount + nNewVertex > m_vertexBufferData.size())
        {
            return false;
        }

        m_vao.bind();
        m_vbo.bind();

        std::set<GLuint> vids;
        const auto vertexBufferChangeBegin = m_vertexBufferCount;
        // Top & bottom
        for (auto tri : capTriangles)
        {
            auto p1 = boundary[std::get<0>(tri)];
            auto p2 = boundary[std::get<1>(tri)];
            auto p3 = boundary[std::get<2>(tri)];

            QVector3D vp1(p1[0], p1[1], p1[2]);
            QVector3D vp2(p2[0], p2[1], p2[2]);
            QVector3D vp3(p3[0], p3[1], p3[2]);
            QVector3D nBottom = computeFaceNormal(vp1, vp2, vp3);
            QVector3D nTop(-nBottom.x(), -nBottom.y(), -nBottom.z()); // flipped for top

            auto v1 = m_vertexBufferCount++;
            vids.emplace(v1);
            m_vertexBufferData[v1] = Vertex(vp1, nBottom, color, gid, objectID);

            auto v2 = m_vertexBufferCount++;
            vids.emplace(v2);
            m_vertexBufferData[v2] = Vertex(vp2, nBottom, color, gid, objectID);

            auto v3 = m_vertexBufferCount++;
            vids.emplace(v3);
            m_vertexBufferData[v3] = Vertex(vp3, nBottom, color, gid, objectID);

            QVector3D vp1h(p1[0], p1[1], p1[2] + h);
            QVector3D vp2h(p2[0], p2[1], p2[2] + h);
            QVector3D vp3h(p3[0], p3[1], p3[2] + h);

            auto v1h = m_vertexBufferCount++;
            vids.emplace(v1h);
            m_vertexBufferData[v1h] = Vertex(vp1h, nTop, color, gid, objectID);

            auto v2h = m_vertexBufferCount++;
            vids.emplace(v2h);
            m_vertexBufferData[v2h] = Vertex(vp2h, nTop, color, gid, objectID);

            auto v3h = m_vertexBufferCount++;
            vids.emplace(v3h);
            m_vertexBufferData[v3h] = Vertex(vp3h, nTop, color, gid, objectID);
        }

        // side
        for (int i = 0; i != boundary.size(); ++i)
        {
            auto p1 = boundary[i];
            auto p2 = boundary[(i + 1) % boundary.size()];
            auto p3 = odr::Vec3D{ p1[0], p1[1], p1[2] + h };
            auto p4 = odr::Vec3D{ p2[0], p2[1], p2[2] + h };

            QVector3D vp1(p1[0], p1[1], p1[2]);
            QVector3D vp2(p2[0], p2[1], p2[2]);
            QVector3D vp3(p3[0], p3[1], p3[2]);
            QVector3D vp4(p4[0], p4[1], p4[2]);

            QVector3D nSide = computeFaceNormal(vp1, vp2, vp3);

            auto v1 = m_vertexBufferCount++;
            vids.emplace(v1);
            m_vertexBufferData[v1] = Vertex(vp1, nSide, color, gid, objectID);

            auto v2 = m_vertexBufferCount++;
            vids.emplace(v2);
            m_vertexBufferData[v2] = Vertex(vp2, nSide, color, gid, objectID);

            auto v3 = m_vertexBufferCount++;
            vids.emplace(v3);
            m_vertexBufferData[v3] = Vertex(vp3, nSide, color, gid, objectID);

            QVector3D nSide2 = computeFaceNormal(vp3, vp2, vp4);

            auto v1_1 = m_vertexBufferCount++;
            vids.emplace(v1_1);
            m_vertexBufferData[v1_1] = Vertex(vp3, nSide2, color, gid, objectID);

            auto v2_1 = m_vertexBufferCount++;
            vids.emplace(v2_1);
            m_vertexBufferData[v2_1] = Vertex(vp2, nSide2, color, gid, objectID);

            auto v3_1 = m_vertexBufferCount++;
            vids.emplace(v3_1);
            m_vertexBufferData[v3_1] = Vertex(vp4, nSide2, color, gid, objectID);
        }

        auto ptr_v = m_vbo.mapRange(vertexBufferChangeBegin * sizeof(Vertex),
            (m_vertexBufferCount - vertexBufferChangeBegin) * sizeof(Vertex),
            QOpenGLBuffer::RangeInvalidate | QOpenGLBuffer::RangeWrite);
        memcpy(ptr_v, m_vertexBufferData.data() + vertexBufferChangeBegin,
            (m_vertexBufferCount - vertexBufferChangeBegin) * sizeof(Vertex));
        m_vbo.unmap();

        m_vbo.release();
        m_vao.release();

        idToVids.emplace(gid, vids);

        assert(vertexBufferChangeBegin + nNewVertex == m_vertexBufferCount);
        return true;
    }

    void GLBufferManage::UpdateItem(unsigned int objectID, uint8_t flag)
    {
        if (objectID >= MaxObjectID)
        {
            throw std::logic_error("Object ID out of range!");
        }
        m_objectFlag[objectID] = flag;
        m_objectInfo->setData(objectID,0,0,1,1,1,
            QOpenGLTexture::PixelFormat::Red, QOpenGLTexture::PixelType::UInt8, m_objectFlag + objectID);
    }

    uint8_t GLBufferManage::GetItemFlag(unsigned int objectID)
    {
        return m_objectFlag[objectID];
    }

    void GLBufferManage::UpdateObjectID(unsigned int graphicsID, unsigned int objectID)
    {
        m_vao.bind();
        m_vbo.bind();

        for (auto vid : idToVids.at(graphicsID))
        {
            m_vertexBufferData[vid].objectID = objectID;

            auto ptr_v = m_vbo.mapRange(vid * sizeof(Vertex), sizeof(Vertex),
                QOpenGLBuffer::RangeInvalidate | QOpenGLBuffer::RangeWrite);
            memcpy(ptr_v, m_vertexBufferData.data() + vid, sizeof(Vertex));
            m_vbo.unmap();
        }

        m_vbo.release();
        m_vao.release();
    }

    void GLBufferManage::RemoveItem(unsigned int gid)
    {
        if (!Road::ClearingMap)
        {
            m_vao.bind();
            m_vbo.bind();
        }
        const auto& vidsToRemove = idToVids.at(gid);
        for (auto vid_it = vidsToRemove.rbegin(); vid_it != vidsToRemove.rend(); ++vid_it)
        {
            auto vid = *vid_it;
            if (!Road::ClearingMap && m_vertexBufferCount != 0 && m_vertexBufferCount - 1 != vid)
            {
                assert(m_vertexBufferData[vid].graphicsID == gid);
                auto objectToMove = m_vertexBufferData[m_vertexBufferCount - 1].graphicsID;
                auto oldVIt = idToVids.at(objectToMove).find(m_vertexBufferCount - 1);
                idToVids.at(objectToMove).erase(oldVIt);

                assert(idToVids.at(objectToMove).find(vid) == idToVids.at(objectToMove).end());
                idToVids.at(objectToMove).emplace(vid);
                m_vertexBufferData[vid] = m_vertexBufferData[m_vertexBufferCount - 1];
                auto ptr_v = m_vbo.mapRange(vid * sizeof(Vertex), sizeof(Vertex),
                    QOpenGLBuffer::RangeInvalidate | QOpenGLBuffer::RangeWrite);
                assert(ptr_v != nullptr);
                memcpy(ptr_v, m_vertexBufferData.data() + vid, sizeof(Vertex));
                m_vbo.unmap();
            }
            m_vertexBufferCount--;
        }
        idToVids.erase(gid);

        if (!Road::ClearingMap)
        {
            m_vao.release();
            m_vbo.release();
        }
    }

    void GLBufferManage::RemoveObject(unsigned int objectID)
    {
        m_objectFlag[objectID] = static_cast<int>(ObjectDisplayFlag::Normal);
        m_objectInfo->setData(objectID, 0, 0, 1, 1, 1,
            QOpenGLTexture::PixelFormat::Red, QOpenGLTexture::PixelType::UInt8, m_objectFlag + objectID);
    }

    void GLBufferManage::Draw(QMatrix4x4 worldToView)
    {
        auto shaderProgramm = shader.shaderProgram();
        shaderProgramm->bind();
        shaderProgramm->setUniformValue(shader.m_uniformIDs[0], worldToView);
        // Lighting uniforms (indices 2-6: cameraPos, lightDir, lightColor, ambientColor, viewMode3D)
        if (shader.m_uniformIDs.size() > 6) {
            shaderProgramm->setUniformValue(shader.m_uniformIDs[2], m_cameraPos);
            shaderProgramm->setUniformValue(shader.m_uniformIDs[3], m_lightDir);
            shaderProgramm->setUniformValue(shader.m_uniformIDs[4], m_lightColor);
            shaderProgramm->setUniformValue(shader.m_uniformIDs[5], m_ambientColor);
            shaderProgramm->setUniformValue(shader.m_uniformIDs[6], m_viewMode3D ? 1 : 0);
        }
        m_vao.bind();
        m_objectInfo->bind(0);
        glDrawArrays(GL_TRIANGLES, 0, m_vertexBufferCount);
        m_vao.release();
        m_objectInfo->release();
        shader.shaderProgram()->release();
    }

    int GLBufferManage::Useage_pct() const
    {
        return static_cast<float>(m_vertexBufferCount) / m_vertexBufferData.size() * 100;
    }
}