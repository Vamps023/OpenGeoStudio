#pragma once

// ============================================================
// GroundMeshGenerator — Ground mesh + boundary spline generation
// ============================================================
//
// RoadBuilder-inspired "seamless and refined ground mesh
// generation along with spline boundary for PCG graph":
//
//   - Generates a ground mesh that fills the area between
//     road boundaries and the surrounding terrain.
//   - Produces boundary splines (left/right edge polylines)
//     that can be consumed by procedural content generation
//     (PCG) systems to place scenery, vegetation, etc.
//   - The mesh is a triangle strip along each road edge,
//     extending outward by a configurable width.
//   - Boundary splines are sampled polylines of the road's
//     left and right outer edges.
//
// The output is format-agnostic (vertices + indices + splines)
// so it can be rendered by OGRE, exported to OBJ, or fed to
// a PCG graph.
//

#include "RoadNetworkBuilder.hpp"
#include "CoordinateConverter.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include "../logger/Logger.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

namespace osm {

// ─── Ground mesh vertex ─────────────────────────────────────
struct GroundVertex {
    float x = 0, y = 0, z = 0;
    float u = 0, v = 0;  // texture coordinates
};

// ─── Ground mesh triangle ───────────────────────────────────
struct GroundTriangle {
    uint32_t v0 = 0, v1 = 0, v2 = 0;
};

// ─── Boundary spline ────────────────────────────────────────
struct BoundarySpline {
    QString roadId;
    QString side;  // "left" or "right"
    std::vector<geo::Point2D> points;  // sampled polyline
    std::vector<double> sValues;       // arc-length parameter for each point

    QJsonObject toJson() const {
        QJsonObject j;
        j["roadId"] = roadId;
        j["side"] = side;
        QJsonArray pts;
        for (const auto& p : points) {
            QJsonArray coord;
            coord.append(p.x);
            coord.append(p.y);
            pts.append(coord);
        }
        j["points"] = pts;
        QJsonArray svals;
        for (double s : sValues) svals.append(s);
        j["sValues"] = svals;
        return j;
    }
};

// ─── Ground mesh result ─────────────────────────────────────
struct GroundMesh {
    std::vector<GroundVertex> vertices;
    std::vector<GroundTriangle> triangles;
    std::vector<BoundarySpline> boundarySplines;

    int vertexCount() const { return int(vertices.size()); }
    int triangleCount() const { return int(triangles.size()); }
    int splineCount() const { return int(boundarySplines.size()); }
};

// ─── Ground mesh generation parameters ──────────────────────
struct GroundMeshParams {
    // Width of the ground mesh extending from each road edge (m)
    double shoulderWidth = 3.0;

    // Distance between mesh vertices along the road (m)
    double sampleInterval = 5.0;

    // Whether to generate boundary splines
    bool generateBoundarySplines = true;

    // Whether to drape the mesh onto terrain (requires elevation provider)
    bool drapeToTerrain = false;

    // Texture repeat distance (m) for UV coordinates
    double textureRepeatDistance = 10.0;
};

// ─── GroundMeshGenerator ────────────────────────────────────
class GroundMeshGenerator {
public:
    // Generate ground mesh for a single road
    static GroundMesh generateForRoad(const geo::RoadV2& road,
                                       const GroundMeshParams& params = {})
    {
        GroundMesh mesh;
        const double totalLen = road.totalLength();
        if (totalLen <= 0 || road.numLaneSections() == 0) return mesh;

        // Compute road half-width (outer edge offset)
        const auto& ls = road.laneSection(0);
        double rightEdge = 0, leftEdge = 0;
        for (const auto& lane : ls.lanes()) {
            if (lane.isRight()) rightEdge += lane.widthAt(0);
            if (lane.isLeft()) leftEdge -= lane.widthAt(0);
        }
        const double roadHalfWidth = std::max(rightEdge, std::abs(leftEdge));

        // Sample the road centerline at regular intervals
        const int sampleCount = std::max(2, int(std::ceil(totalLen / params.sampleInterval)) + 1);
        const double dt = totalLen / (sampleCount - 1);

        // ─── Generate left ground strip ───
        // Left edge of road → left ground edge (extending outward)
        uint32_t baseVert = uint32_t(mesh.vertices.size());
        for (int i = 0; i < sampleCount; ++i) {
            const double s = dt * i;
            const auto center = road.geometry().positionAt(s);
            const auto normal = road.geometry().normalAt(s);  // left normal

            // Road edge point (left)
            const double edgeOffset = -roadHalfWidth;  // left edge
            const geo::Point2D edgePoint{
                center.x + normal.x * edgeOffset,
                center.y + normal.y * edgeOffset
            };
            // Ground edge point (further left by shoulderWidth)
            const double groundOffset = edgeOffset - params.shoulderWidth;
            const geo::Point2D groundPoint{
                center.x + normal.x * groundOffset,
                center.y + normal.y * groundOffset
            };

            // Add two vertices: road edge and ground edge
            GroundVertex edgeVert;
            edgeVert.x = float(edgePoint.x);
            edgeVert.y = float(edgePoint.y);
            edgeVert.u = float(s / params.textureRepeatDistance);
            edgeVert.v = 0.0f;
            mesh.vertices.push_back(edgeVert);

            GroundVertex groundVert;
            groundVert.x = float(groundPoint.x);
            groundVert.y = float(groundPoint.y);
            groundVert.u = float(s / params.textureRepeatDistance);
            groundVert.v = 1.0f;
            mesh.vertices.push_back(groundVert);
        }
        // Generate triangles for left strip
        for (int i = 0; i < sampleCount - 1; ++i) {
            const uint32_t v0 = baseVert + uint32_t(i * 2);
            const uint32_t v1 = baseVert + uint32_t(i * 2 + 1);
            const uint32_t v2 = baseVert + uint32_t((i + 1) * 2);
            const uint32_t v3 = baseVert + uint32_t((i + 1) * 2 + 1);
            mesh.triangles.push_back({v0, v2, v1});
            mesh.triangles.push_back({v1, v2, v3});
        }

        // ─── Generate right ground strip ───
        baseVert = uint32_t(mesh.vertices.size());
        for (int i = 0; i < sampleCount; ++i) {
            const double s = dt * i;
            const auto center = road.geometry().positionAt(s);
            const auto normal = road.geometry().normalAt(s);

            const double edgeOffset = roadHalfWidth;  // right edge
            const geo::Point2D edgePoint{
                center.x + normal.x * edgeOffset,
                center.y + normal.y * edgeOffset
            };
            const double groundOffset = edgeOffset + params.shoulderWidth;
            const geo::Point2D groundPoint{
                center.x + normal.x * groundOffset,
                center.y + normal.y * groundOffset
            };

            GroundVertex edgeVert;
            edgeVert.x = float(edgePoint.x);
            edgeVert.y = float(edgePoint.y);
            edgeVert.u = float(s / params.textureRepeatDistance);
            edgeVert.v = 0.0f;
            mesh.vertices.push_back(edgeVert);

            GroundVertex groundVert;
            groundVert.x = float(groundPoint.x);
            groundVert.y = float(groundPoint.y);
            groundVert.u = float(s / params.textureRepeatDistance);
            groundVert.v = 1.0f;
            mesh.vertices.push_back(groundVert);
        }
        for (int i = 0; i < sampleCount - 1; ++i) {
            const uint32_t v0 = baseVert + uint32_t(i * 2);
            const uint32_t v1 = baseVert + uint32_t(i * 2 + 1);
            const uint32_t v2 = baseVert + uint32_t((i + 1) * 2);
            const uint32_t v3 = baseVert + uint32_t((i + 1) * 2 + 1);
            mesh.triangles.push_back({v0, v1, v2});
            mesh.triangles.push_back({v1, v3, v2});
        }

        // ─── Generate boundary splines ───
        if (params.generateBoundarySplines) {
            // Left boundary spline
            BoundarySpline leftSpline;
            leftSpline.roadId = QString::fromStdString(road.id);
            leftSpline.side = "left";
            for (int i = 0; i < sampleCount; ++i) {
                const double s = dt * i;
                const auto center = road.geometry().positionAt(s);
                const auto normal = road.geometry().normalAt(s);
                const double offset = -roadHalfWidth;
                leftSpline.points.emplace_back(
                    center.x + normal.x * offset,
                    center.y + normal.y * offset);
                leftSpline.sValues.push_back(s);
            }
            mesh.boundarySplines.push_back(std::move(leftSpline));

            // Right boundary spline
            BoundarySpline rightSpline;
            rightSpline.roadId = QString::fromStdString(road.id);
            rightSpline.side = "right";
            for (int i = 0; i < sampleCount; ++i) {
                const double s = dt * i;
                const auto center = road.geometry().positionAt(s);
                const auto normal = road.geometry().normalAt(s);
                const double offset = roadHalfWidth;
                rightSpline.points.emplace_back(
                    center.x + normal.x * offset,
                    center.y + normal.y * offset);
                rightSpline.sValues.push_back(s);
            }
            mesh.boundarySplines.push_back(std::move(rightSpline));
        }

        return mesh;
    }

    // Generate ground mesh for the entire network
    static GroundMesh generateAll(const RoadNetworkBuilder::Result& network,
                                   const GroundMeshParams& params = {})
    {
        GroundMesh combined;
        for (const auto& road : network.roads) {
            auto mesh = generateForRoad(road, params);
            combined.vertices.insert(combined.vertices.end(),
                                      mesh.vertices.begin(), mesh.vertices.end());
            combined.triangles.insert(combined.triangles.end(),
                                       mesh.triangles.begin(), mesh.triangles.end());
            combined.boundarySplines.insert(combined.boundarySplines.end(),
                                             mesh.boundarySplines.begin(),
                                             mesh.boundarySplines.end());
        }
        appLog().info("[GroundMeshGenerator] Generated",
                      combined.vertexCount(), "vertices,",
                      combined.triangleCount(), "triangles,",
                      combined.splineCount(), "boundary splines");
        return combined;
    }

    // ─── Export ground mesh as OBJ ───────────────────────────
    static bool exportToObj(const QString& path, const GroundMesh& mesh,
                             QString* error = nullptr)
    {
        QString obj;
        obj += "# OpenGeoStudio ground mesh\n";
        for (const auto& v : mesh.vertices)
            obj += QString("v %1 %2 %3\n").arg(v.x).arg(v.y).arg(v.z);
        for (const auto& v : mesh.vertices)
            obj += QString("vt %1 %2\n").arg(v.u).arg(v.v);
        for (const auto& t : mesh.triangles) {
            const uint32_t a = t.v0 + 1, b = t.v1 + 1, c = t.v2 + 1;
            obj += QString("f %1/%1 %2/%2 %3/%3\n").arg(a).arg(b).arg(c);
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error) *error = QString("Cannot write OBJ: %1").arg(path);
            return false;
        }
        file.write(obj.toUtf8());
        file.close();
        return true;
    }

    // ─── Export boundary splines as JSON (for PCG) ───────────
    static bool exportSplinesToJson(const QString& path,
                                     const GroundMesh& mesh,
                                     QString* error = nullptr)
    {
        QJsonArray splines;
        for (const auto& spline : mesh.boundarySplines)
            splines.append(spline.toJson());
        QJsonDocument doc(splines);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error) *error = QString("Cannot write spline JSON: %1").arg(path);
            return false;
        }
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }
};

} // namespace osm
