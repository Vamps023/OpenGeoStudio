#pragma once

// ============================================================
// SplineEditor — Spline editing and road generation tools
// ============================================================

#include "World.hpp"
#include "Spline.hpp"
#include <QString>
#include <QList>
#include <functional>
#include <cmath>

namespace world {

class SplineEditor {
public:
    // Add a control point to a spline
    static void addPoint(Spline& spline, float x, float y, float z) {
        SplineControlPoint p;
        p.x = x; p.y = y; p.z = z;
        spline.points.append(p);
    }

    // Insert a point at a specific index
    static void insertPoint(Spline& spline, int index, float x, float y, float z) {
        SplineControlPoint p;
        p.x = x; p.y = y; p.z = z;
        if (index < 0) index = 0;
        if (index > spline.points.size()) index = spline.points.size();
        spline.points.insert(index, p);
    }

    // Remove a point at a specific index
    static bool removePoint(Spline& spline, int index) {
        if (index < 0 || index >= spline.points.size()) return false;
        spline.points.removeAt(index);
        return true;
    }

    // Move a point
    static bool movePoint(Spline& spline, int index, float x, float y, float z) {
        if (index < 0 || index >= spline.points.size()) return false;
        spline.points[index].x = x;
        spline.points[index].y = y;
        spline.points[index].z = z;
        return true;
    }

    // Project spline points onto terrain
    static void projectToTerrain(Spline& spline,
                                  std::function<float(float, float)> sampleHeight,
                                  float offset = 0.5f)
    {
        for (auto& p : spline.points) {
            float h = sampleHeight(p.x, p.z);
            p.y = h + offset;
        }
    }

    // Create a road spline from a series of points
    static Spline createRoad(const QString& name,
                              const QList<QPair<float, float>>& xzPoints,
                              float width = 8.0f,
                              int laneCount = 2,
                              float laneWidth = 3.5f)
    {
        Spline spline;
        spline.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        spline.name = name;
        spline.type = SplineType::Road;
        spline.width = width;
        spline.laneCount = laneCount;
        spline.laneWidth = laneWidth;
        spline.projectToTerrain = true;
        spline.heightOffset = 0.5f;

        for (const auto& pt : xzPoints) {
            SplineControlPoint p;
            p.x = pt.first;
            p.z = pt.second;
            spline.points.append(p);
        }

        return spline;
    }

    // Create a river spline
    static Spline createRiver(const QString& name,
                               const QList<QPair<float, float>>& xzPoints,
                               float width = 5.0f)
    {
        Spline spline;
        spline.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        spline.name = name;
        spline.type = SplineType::River;
        spline.width = width;
        spline.projectToTerrain = true;
        spline.heightOffset = -0.5f;

        for (const auto& pt : xzPoints) {
            SplineControlPoint p;
            p.x = pt.first;
            p.z = pt.second;
            spline.points.append(p);
        }

        return spline;
    }

    // Calculate total road length
    static float roadLength(const Spline& spline) {
        return SplineEvaluator::length(spline);
    }

    // Generate road mesh data from spline
    struct RoadMeshData {
        std::vector<float> vertices;  // x,y,z per vertex
        std::vector<float> normals;   // nx,ny,nz per vertex
        std::vector<float> uvs;       // u,v per vertex
        std::vector<uint32_t> indices;
    };

    static RoadMeshData generateRoadMeshData(const Spline& spline, float heightOffset = 0.5f) {
        RoadMeshData data;
        auto verts = SplineEvaluator::generateRoadMesh(spline, heightOffset);
        auto indices = SplineEvaluator::generateRoadIndices(int(verts.size()));

        for (const auto& v : verts) {
            data.vertices.push_back(v.x);
            data.vertices.push_back(v.y);
            data.vertices.push_back(v.z);
            data.normals.push_back(v.nx);
            data.normals.push_back(v.ny);
            data.normals.push_back(v.nz);
            data.uvs.push_back(v.u);
            data.uvs.push_back(v.v);
        }
        data.indices = indices;
        return data;
    }

    // Convert spline to actor (for placement in the world)
    static Actor splineToActor(const Spline& spline) {
        Actor a;
        a.name = spline.name;
        a.type = ActorType::Road;
        a.layerId = "roads";
        a.metadata["splineId"] = spline.id;
        a.metadata["splineType"] = splineTypeToString(spline.type);
        a.metadata["width"] = QString::number(spline.width);
        a.metadata["laneCount"] = QString::number(spline.laneCount);
        a.colorR = 0.3f; a.colorG = 0.3f; a.colorB = 0.3f;
        return a;
    }

    // Generate buildings along a road spline
    static QList<Actor> generateBuildingsAlongRoad(const Spline& spline,
                                                     float spacing = 50.0f,
                                                     float offset = 20.0f,
                                                     float minHeight = 10.0f,
                                                     float maxHeight = 30.0f,
                                                     int seed = 42)
    {
        QList<Actor> buildings;
        auto samples = SplineEvaluator::sample(spline, spacing);

        // Simple LCG for deterministic random
        uint32_t rng = seed;
        auto rand01 = [&rng]() {
            rng = rng * 1103515245 + 12345;
            return (rng >> 16) / 65535.0f;
        };

        for (size_t i = 0; i < samples.size(); i++) {
            // Alternate sides
            float side = (i % 2 == 0) ? 1.0f : -1.0f;

            // Get tangent
            SplineEvaluator::Vec3 tan;
            if (i == 0 && samples.size() > 1) {
                tan = {samples[1].x - samples[0].x, 0, samples[1].z - samples[0].z};
            } else if (i == samples.size() - 1 && samples.size() > 1) {
                tan = {samples[i].x - samples[i-1].x, 0, samples[i].z - samples[i-1].z};
            } else if (samples.size() > 2) {
                tan = {samples[i+1].x - samples[i-1].x, 0, samples[i+1].z - samples[i-1].z};
            } else {
                continue;
            }

            // Normalize
            float len = std::sqrt(tan.x * tan.x + tan.z * tan.z);
            if (len < 1e-6f) continue;
            tan.x /= len; tan.z /= len;

            // Perpendicular
            float perpX = -tan.z;
            float perpZ = tan.x;

            // Building position
            float bx = samples[i].x + perpX * offset * side;
            float bz = samples[i].z + perpZ * offset * side;
            float by = samples[i].y;

            // Building dimensions
            float h = minHeight + rand01() * (maxHeight - minHeight);
            float w = 8.0f + rand01() * 12.0f;
            float d = 8.0f + rand01() * 12.0f;

            Actor building;
            building.name = QString("Building_%1").arg(i);
            building.type = ActorType::Building;
            building.layerId = "buildings";
            building.transform.posX = bx;
            building.transform.posY = by;
            building.transform.posZ = bz;
            building.transform.rotY = std::atan2(tan.x, tan.z) * 180.0f / 3.14159265358979f;
            building.transform.scaleX = w;
            building.transform.scaleY = h;
            building.transform.scaleZ = d;
            building.colorR = 0.5f + rand01() * 0.3f;
            building.colorG = 0.5f + rand01() * 0.3f;
            building.colorB = 0.5f + rand01() * 0.3f;
            building.metadata["roadSplineId"] = spline.id;
            building.seed = int(rng);

            buildings.append(building);
        }

        return buildings;
    }
};

} // namespace world
