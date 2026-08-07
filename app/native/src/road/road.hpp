#pragma once

// ═══════════════════════════════════════════════════════════
// Road Model — C++ data structures for roads, lanes, intersections
// ═══════════════════════════════════════════════════════════

#include "geometry.hpp"
#include <string>
#include <vector>

namespace geo {

// ─── Control Point (with bezier handles) ───────────────────
struct ControlPoint {
    Point2D position;       // x, y in local meters
    double z = 0.0;         // elevation
    Point2D handleIn;       // bezier handle (relative to position)
    Point2D handleOut;      // bezier handle (relative to position)
    bool hasHandleIn = false;
    bool hasHandleOut = false;
    std::string type = "corner";  // "corner" or "smooth"
    std::string id;                // unique ID
};

// ─── Road ──────────────────────────────────────────────────
struct Road {
    std::string id;
    std::string name;
    std::vector<ControlPoint> points;
    double width = 8.0;          // road width in meters
    int laneCount = 2;           // number of lanes
    std::string color = "#4ecca3";
    std::string profileName = "city_2x1";
    std::string startIntersectionId;
    std::string endIntersectionId;

    // Sample the road centerline at N points
    // Uses cubic bezier for smooth points, linear for corner points
    std::vector<Point2D> sampleCenterline(int numSamples = 24) const;

    // Sample with elevation (returns 3D points)
    std::vector<Point3D> sampleCenterline3D(int numSamples = 24) const;

    // Get left and right edge polylines
    std::vector<Point2D> leftEdge(int numSamples = 24) const;
    std::vector<Point2D> rightEdge(int numSamples = 24) const;

    // Total length
    double length() const;

    // Bounding box
    BoundingBox2D bounds() const;
};

// ─── Lane ──────────────────────────────────────────────────
struct Lane {
    int id;
    std::string roadId;
    int index;              // lane index within road
    std::string direction;  // "forward" or "backward"
    double width;
    std::vector<Point2D> centerline;
};

// ─── Lane Connection ───────────────────────────────────────
struct LaneConnection {
    std::string fromApproach;  // "north", "south", "east", "west"
    std::string toApproach;
    std::string type;          // "straight", "left", "right", "uturn"
    std::vector<Point2D> path; // driving path
};

// ─── Approach Road (at an intersection) ────────────────────
struct ApproachRoad {
    std::string roadId;
    std::string direction;     // "north", "south", "east", "west"
    std::vector<Point2D> centerline;  // from intersection outward
    double width;
    int laneCount;
    double z;
};

// ─── Stop Line ─────────────────────────────────────────────
struct StopLine {
    std::string approach;
    Point2D p1, p2;
};

// ─── Crosswalk ─────────────────────────────────────────────
struct Crosswalk {
    std::string approach;
    std::vector<Point2D> corners;  // 4 corners
};

// ─── Generated Intersection ────────────────────────────────
struct GeneratedIntersection {
    Point2D center;
    std::vector<Point2D> polygon;          // intersection boundary
    std::vector<ApproachRoad> approaches;  // 2-4 approaches
    std::vector<LaneConnection> laneConnections;
    std::vector<StopLine> stopLines;
    std::vector<Crosswalk> crosswalks;
};

// ─── Circle Arc ────────────────────────────────────────────
struct CircleArc {
    Point2D center;
    double radius;
    double startAngle;
    double endAngle;
    double sweep;           // radians, positive = CCW (left), negative = CW (right)
    std::vector<Point2D> points;  // sampled points
    Vec2 tangentIn;         // direction at arc start
    Vec2 tangentOut;        // direction at arc end
};

// ─── Mesh Data ─────────────────────────────────────────────
struct MeshData {
    std::vector<float> vertices;   // x, y, z interleaved
    std::vector<float> normals;    // nx, ny, nz
    std::vector<float> uvs;        // u, v
    std::vector<uint32_t> indices; // triangle indices
};

// ─── Road implementation (inline for header-only) ──────────

inline std::vector<Point2D> Road::sampleCenterline(int numSamples) const {
    std::vector<Point2D> samples;
    if (points.empty()) return samples;

    if (points.size() == 1) {
        samples.push_back(points[0].position);
        return samples;
    }

    // For each segment between control points, sample
    for (size_t i = 0; i < points.size() - 1; i++) {
        const auto& p0 = points[i];
        const auto& p1 = points[i + 1];

        int segSamples = numSamples / static_cast<int>(points.size() - 1);
        if (segSamples < 2) segSamples = 2;

        // Check if this segment uses bezier handles
        bool hasBezier = p0.hasHandleOut || p1.hasHandleIn;

        if (hasBezier) {
            // Cubic bezier: P0, P0+handleOut, P1+handleIn, P1
            Point2D cp0 = p0.position;
            Point2D cp1 = p0.position + p0.handleOut;
            Point2D cp2 = p1.position + p1.handleIn;
            Point2D cp3 = p1.position;

            for (int j = 0; j < segSamples; j++) {
                double t = static_cast<double>(j) / segSamples;
                samples.push_back(bezierCubic(cp0, cp1, cp2, cp3, t));
            }
        } else {
            // Linear interpolation
            for (int j = 0; j < segSamples; j++) {
                double t = static_cast<double>(j) / segSamples;
                samples.push_back(Point2D::lerp(p0.position, p1.position, t));
            }
        }
    }
    // Add final point
    samples.push_back(points.back().position);

    return samples;
}

inline std::vector<Point3D> Road::sampleCenterline3D(int numSamples) const {
    auto xy = sampleCenterline(numSamples);
    std::vector<Point3D> result;
    result.reserve(xy.size());

    // Interpolate z along the centerline
    for (size_t i = 0; i < xy.size(); i++) {
        double t = static_cast<double>(i) / std::max(1.0, static_cast<double>(xy.size() - 1));
        // Find z by interpolating between control points
        double z = 0;
        if (!points.empty()) {
            double totalT = t * (points.size() - 1);
            size_t idx = static_cast<size_t>(totalT);
            double frac = totalT - idx;
            if (idx >= points.size() - 1) {
                z = points.back().z;
            } else {
                z = points[idx].z + (points[idx + 1].z - points[idx].z) * frac;
            }
        }
        result.push_back({xy[i].x, xy[i].y, z});
    }
    return result;
}

inline std::vector<Point2D> Road::leftEdge(int numSamples) const {
    auto cl = sampleCenterline(numSamples);
    return offsetPolyline(cl, width / 2.0);
}

inline std::vector<Point2D> Road::rightEdge(int numSamples) const {
    auto cl = sampleCenterline(numSamples);
    return offsetPolyline(cl, -width / 2.0);
}

inline double Road::length() const {
    return polylineLength(sampleCenterline(32));
}

inline BoundingBox2D Road::bounds() const {
    BoundingBox2D bb;
    for (const auto& p : points) {
        bb.expand(p.position);
    }
    // Expand by half width
    bb.min.x -= width / 2;
    bb.min.y -= width / 2;
    bb.max.x += width / 2;
    bb.max.y += width / 2;
    return bb;
}

} // namespace geo
