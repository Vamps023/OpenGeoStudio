#pragma once

// ============================================================
// Spline — Spline evaluation and mesh generation
// ============================================================

#include "WorldTypes.hpp"
#include <cmath>
#include <vector>

namespace world {

inline QString splineTypeToString(SplineType type) {
    switch (type) {
    case SplineType::Road: return "Road";
    case SplineType::Railway: return "Railway";
    case SplineType::River: return "River";
    case SplineType::Fence: return "Fence";
    case SplineType::Pipeline: return "Pipeline";
    case SplineType::Powerline: return "Powerline";
    case SplineType::CameraPath: return "CameraPath";
    default: return "Generic";
    }
}

inline SplineType stringToSplineType(const QString& s) {
    if (s == "Road") return SplineType::Road;
    if (s == "Railway") return SplineType::Railway;
    if (s == "River") return SplineType::River;
    if (s == "Fence") return SplineType::Fence;
    if (s == "Pipeline") return SplineType::Pipeline;
    if (s == "Powerline") return SplineType::Powerline;
    if (s == "CameraPath") return SplineType::CameraPath;
    return SplineType::Generic;
}

inline QJsonObject Spline::toJson() const {
    QJsonObject j;
    j["id"] = id; j["name"] = name;
    j["type"] = splineTypeToString(type);
    QJsonArray pts;
    for (const auto& p : points) pts.append(p.toJson());
    j["points"] = pts;
    j["width"] = width;
    j["heightOffset"] = heightOffset;
    j["projectToTerrain"] = projectToTerrain;
    j["materialId"] = materialId;
    j["laneCount"] = laneCount;
    j["laneWidth"] = laneWidth;
    j["shoulderWidth"] = shoulderWidth;
    j["hasMedian"] = hasMedian;
    j["medianWidth"] = medianWidth;
    j["hasSidewalk"] = hasSidewalk;
    j["sidewalkWidth"] = sidewalkWidth;
    j["banking"] = banking;
    j["crossfall"] = crossfall;
    j["roadType"] = roadType;
    return j;
}

inline Spline Spline::fromJson(const QJsonObject& j) {
    Spline s;
    s.id = j["id"].toString();
    s.name = j["name"].toString();
    s.type = stringToSplineType(j["type"].toString());
    QJsonArray pts = j["points"].toArray();
    for (const auto& v : pts)
        s.points.append(SplineControlPoint::fromJson(v.toObject()));
    s.width = float(j["width"].toDouble(8));
    s.heightOffset = float(j["heightOffset"].toDouble(0.5));
    s.projectToTerrain = j["projectToTerrain"].toBool(true);
    s.materialId = j["materialId"].toInt(0);
    s.laneCount = j["laneCount"].toInt(2);
    s.laneWidth = float(j["laneWidth"].toDouble(3.5));
    s.shoulderWidth = float(j["shoulderWidth"].toDouble(1.5));
    s.hasMedian = j["hasMedian"].toBool(false);
    s.medianWidth = float(j["medianWidth"].toDouble(2));
    s.hasSidewalk = j["hasSidewalk"].toBool(false);
    s.sidewalkWidth = float(j["sidewalkWidth"].toDouble(2));
    s.banking = float(j["banking"].toDouble(0));
    s.crossfall = float(j["crossfall"].toDouble(0));
    s.roadType = j["roadType"].toString("highway");
    return s;
}

// ============================================================
// SplineEvaluator — Evaluate spline points
// ============================================================

class SplineEvaluator {
public:
    struct Vec3 { float x, y, z; };

    // Catmull-Rom interpolation between 4 control points
    static Vec3 catmullRom(const Vec3& p0, const Vec3& p1,
                           const Vec3& p2, const Vec3& p3, float t) {
        float t2 = t * t;
        float t3 = t2 * t;
        return {
            0.5f * ((2 * p1.x) +
                    (-p0.x + p2.x) * t +
                    (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 +
                    (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3),
            0.5f * ((2 * p1.y) +
                    (-p0.y + p2.y) * t +
                    (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 +
                    (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3),
            0.5f * ((2 * p1.z) +
                    (-p0.z + p2.z) * t +
                    (2 * p0.z - 5 * p1.z + 4 * p2.z - p3.z) * t2 +
                    (-p0.z + 3 * p1.z - 3 * p2.z + p3.z) * t3)
        };
    }

    // Sample the spline at uniform intervals
    static std::vector<Vec3> sample(const Spline& spline, float sampleStep = 1.0f) {
        std::vector<Vec3> result;
        if (spline.points.size() < 2) return result;

        for (int i = 0; i < int(spline.points.size()) - 1; i++) {
            const auto& p0 = spline.points[std::max(0, i - 1)];
            const auto& p1 = spline.points[i];
            const auto& p2 = spline.points[i + 1];
            const auto& p3 = spline.points[std::min(int(spline.points.size()) - 1, i + 2)];

            // Calculate segment length (approximate)
            float segLen = distance(p1, p2);
            int steps = std::max(1, int(segLen / sampleStep));

            for (int s = 0; s < steps; s++) {
                float t = float(s) / steps;
                Vec3 p = catmullRom(
                    {p0.x, p0.y, p0.z},
                    {p1.x, p1.y, p1.z},
                    {p2.x, p2.y, p2.z},
                    {p3.x, p3.y, p3.z}, t);
                result.push_back(p);
            }
        }
        // Add last point
        const auto& last = spline.points.last();
        result.push_back({last.x, last.y, last.z});
        return result;
    }

    // Get tangent at a parameter
    static Vec3 tangent(const Spline& spline, int segment, float t) {
        const auto& p0 = spline.points[std::max(0, segment - 1)];
        const auto& p1 = spline.points[segment];
        const auto& p2 = spline.points[segment + 1];
        const auto& p3 = spline.points[std::min(int(spline.points.size()) - 1, segment + 2)];

        float tt = t;
        Vec3 a = catmullRom({p0.x, p0.y, p0.z}, {p1.x, p1.y, p1.z},
                            {p2.x, p2.y, p2.z}, {p3.x, p3.y, p3.z}, tt);
        Vec3 b = catmullRom({p0.x, p0.y, p0.z}, {p1.x, p1.y, p1.z},
                            {p2.x, p2.y, p2.z}, {p3.x, p3.y, p3.z}, tt + 0.001f);
        return normalize({b.x - a.x, b.y - a.y, b.z - a.z});
    }

    // Calculate total spline length
    static float length(const Spline& spline) {
        float total = 0;
        for (int i = 0; i < int(spline.points.size()) - 1; i++) {
            total += distance(spline.points[i], spline.points[i + 1]);
        }
        return total;
    }

    // Generate road mesh vertices from spline
    struct RoadVertex {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
    };

    static std::vector<RoadVertex> generateRoadMesh(
        const Spline& spline, float heightOffset = 0.5f) {
        std::vector<RoadVertex> vertices;
        auto samples = sample(spline, 1.0f);
        if (samples.size() < 2) return vertices;

        float halfWidth = spline.width * 0.5f;
        float totalLen = 0;

        for (size_t i = 0; i < samples.size(); i++) {
            // Get tangent
            Vec3 tan;
            if (i == 0) {
                tan = {samples[1].x - samples[0].x,
                       samples[1].y - samples[0].y,
                       samples[1].z - samples[0].z};
            } else if (i == samples.size() - 1) {
                tan = {samples[i].x - samples[i-1].x,
                       samples[i].y - samples[i-1].y,
                       samples[i].z - samples[i-1].z};
            } else {
                tan = {samples[i+1].x - samples[i-1].x,
                       samples[i+1].y - samples[i-1].y,
                       samples[i+1].z - samples[i-1].z};
            }
            tan = normalize(tan);
            // Perpendicular (in XZ plane)
            Vec3 perp = {-tan.z, 0, tan.x};
            perp = normalize(perp);

            const auto& p = samples[i];
            float v = totalLen / std::max(1.0f, spline.width);

            // Left vertex
            vertices.push_back({p.x + perp.x * halfWidth, p.y + heightOffset, p.z + perp.z * halfWidth,
                                0, 1, 0, 0, v});
            // Right vertex
            vertices.push_back({p.x - perp.x * halfWidth, p.y + heightOffset, p.z - perp.z * halfWidth,
                                0, 1, 0, 1, v});

            if (i > 0) {
                totalLen += distance(samples[i-1], samples[i]);
            }
        }
        return vertices;
    }

    // Generate road indices
    static std::vector<uint32_t> generateRoadIndices(int vertexCount) {
        std::vector<uint32_t> indices;
        int quads = vertexCount / 2 - 1;
        for (int q = 0; q < quads; q++) {
            uint32_t v0 = q * 2;
            uint32_t v1 = q * 2 + 1;
            uint32_t v2 = (q + 1) * 2;
            uint32_t v3 = (q + 1) * 2 + 1;
            indices.push_back(v0); indices.push_back(v2); indices.push_back(v1);
            indices.push_back(v1); indices.push_back(v2); indices.push_back(v3);
        }
        return indices;
    }

private:
    static float distance(const SplineControlPoint& a, const SplineControlPoint& b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    static float distance(const Vec3& a, const Vec3& b) {
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    static Vec3 normalize(const Vec3& v) {
        float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len < 1e-6f) return {0, 1, 0};
        return {v.x / len, v.y / len, v.z / len};
    }
};

} // namespace world
