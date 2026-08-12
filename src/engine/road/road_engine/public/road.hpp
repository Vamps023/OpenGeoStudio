#pragma once

// ═══════════════════════════════════════════════════════════
// Road Model — C++ data structures for roads, lanes, intersections
// ═══════════════════════════════════════════════════════════

#include "geometry.hpp"
#include <string>
#include <vector>
#include <optional>

namespace geo {

// ─── Segment Kind ──────────────────────────────────────────
// Strongly-typed enum for segment classification.
// Replaces free-form string types to avoid typos and enable
// exhaustive switch matching.
enum class SegmentKind {
    Line,
    Bezier,
    Arc,
    Spiral
};

// ─── Segment Metadata ──────────────────────────────────────
// Optional metadata stored in a ControlPoint to enable exact
// reconstruction of non-Bezier geometry segments (Arc, Spiral).
//
// When present, the adapter uses these parameters to reconstruct
// the segment exactly — no fitting, no approximation.
// When absent, the adapter reports a warning (no silent fallback).
//
// The metadata describes the segment STARTING at this control point
// (i.e., the segment from this CP to the next CP).
//
// Field semantics (OpenDRIVE-style):
//   kind = Arc:    curvature + arcLength describe a constant-radius arc
//   kind = Spiral: curvatureStart + curvatureEnd + segmentLength describe a clothoid
//   kind = Line:   (no metadata needed — straight line is implicit)
//   kind = Bezier: (no metadata needed — handles are the metadata)
//
// Future: migrate to std::variant<LineMeta, BezierMeta, ArcMeta, SpiralMeta>
// For now, shared fields keep the struct simple and serializable.
struct SegmentMetadata {
    SegmentKind kind = SegmentKind::Line;
    int version = 1;               // schema version for future evolution

    // Arc parameters (kind = Arc)
    double curvature = 0.0;        // signed curvature (1/radius), positive = left/CCW
    double arcLength = 0.0;        // arc length in meters
    double startHeading = 0.0;     // heading at segment start (radians)

    // Spiral parameters (kind = Spiral)
    double curvatureStart = 0.0;   // κ₀ at segment start
    double curvatureEnd = 0.0;     // κ₁ at segment end
    double segmentLength = 0.0;    // total segment length in meters
    // startHeading is shared with arc (same field)
};

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

    // Optional segment metadata for exact reconstruction (Phase 1.8.3b)
    // Describes the segment STARTING at this control point.
    std::optional<SegmentMetadata> segmentMeta;
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
    int formatVersion = 2;       // schema version (1=legacy, 2=with segmentMeta)

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

// ─── Legacy Lane (deprecated — use geo::Lane in lane_engine.hpp) ───
struct LegacyLane {
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
struct FilletCorner {
    Point2D boundaryIntersection;  // where two road edge lines cross
    Point2D tangentIn;             // fillet start point (on edge line)
    Point2D tangentOut;            // fillet end point (on edge line)
    Point2D arcCenter;             // center of fillet arc
    double radius;                 // fillet radius
    std::vector<Point2D> arcPoints; // sampled arc points
    int approachInIdx;             // index into approaches
    int approachOutIdx;            // index into approaches
};

struct TrimLine {
    Point2D leftEnd;    // left edge point at trim line
    Point2D rightEnd;   // right edge point at trim line
    Point2D centerPt;   // centerline point at trim line
    int approachIdx;
};

struct GeneratedIntersection {
    Point2D center;
    std::vector<Point2D> polygon;          // intersection boundary
    std::vector<ApproachRoad> approaches;  // 2-4 approaches
    std::vector<LaneConnection> laneConnections;
    std::vector<StopLine> stopLines;
    std::vector<Crosswalk> crosswalks;

    // ─── Construction debug data ───────────────────────────
    std::vector<FilletCorner> corners;     // fillet corners with arc data
    std::vector<TrimLine> trimLines;       // trim lines for each approach
    std::vector<Point2D> boundaryIntersections;  // all edge-line intersection points
    double cornerRadius = 0;               // corner radius used
    double trimDistance1 = 0;              // trim distance for road 1
    double trimDistance2 = 0;              // trim distance for road 2
    double intersectionAngle = 0;          // angle between roads (radians)
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
    uint32_t vertexCount = 0;      // number of vertices (not floats)
    uint32_t indexCount = 0;       // number of indices
    uint32_t triangleCount = 0;    // number of triangles
};

// ─── Road implementation (inline for header-only) ──────────

inline std::vector<Point2D> Road::sampleCenterline(int numSamples) const {
    std::vector<Point2D> samples;
    if (points.empty()) return samples;

    if (points.size() == 1) {
        samples.push_back(points[0].position);
        return samples;
    }

    // ─── Arc-length-based sampling ───────────────────────────
    // 1. Compute segment lengths (approximate for bezier via subdivision)
    // 2. Distribute samples proportional to segment length
    // This ensures uniform sample density along the road, regardless of
    // whether segments are straight or curved.

    struct SegmentInfo {
        double length;
        Point2D cp0, cp1, cp2, cp3;  // bezier control points (cp1=cp0+handleOut, etc.)
        bool isBezier;
    };

    std::vector<SegmentInfo> segs;
    segs.reserve(points.size() - 1);
    double totalLength = 0;

    for (size_t i = 0; i < points.size() - 1; i++) {
        const auto& p0 = points[i];
        const auto& p1 = points[i + 1];
        SegmentInfo si;
        si.cp0 = p0.position;
        si.cp3 = p1.position;
        si.isBezier = p0.hasHandleOut || p1.hasHandleIn;

        if (si.isBezier) {
            si.cp1 = p0.position + p0.handleOut;
            si.cp2 = p1.position + p1.handleIn;
            // Approximate bezier length by sampling
            double len = 0;
            Point2D prev = si.cp0;
            const int N = 16;
            for (int j = 1; j <= N; j++) {
                double t = static_cast<double>(j) / N;
                Point2D pt = bezierCubic(si.cp0, si.cp1, si.cp2, si.cp3, t);
                len += pt.distanceTo(prev);
                prev = pt;
            }
            si.length = len;
        } else {
            si.cp1 = si.cp0;
            si.cp2 = si.cp3;
            si.length = p0.position.distanceTo(p1.position);
        }
        totalLength += si.length;
        segs.push_back(si);
    }

    if (totalLength < EPSILON) {
        // Degenerate road — just return control points
        for (const auto& p : points) samples.push_back(p.position);
        return samples;
    }

    // Distribute samples proportional to segment length
    // Each segment gets at least 2 samples
    std::vector<int> segSampleCounts(segs.size(), 0);
    int remaining = numSamples;
    for (size_t i = 0; i < segs.size(); i++) {
        segSampleCounts[i] = 2;  // minimum
        remaining -= 2;
    }
    // Distribute remaining samples proportionally
    if (remaining > 0) {
        for (size_t i = 0; i < segs.size() && remaining > 0; i++) {
            int extra = static_cast<int>(std::round(static_cast<double>(remaining) * segs[i].length / totalLength));
            segSampleCounts[i] += extra;
        }
    }

    // Sample each segment
    for (size_t i = 0; i < segs.size(); i++) {
        const auto& si = segs[i];
        int n = segSampleCounts[i];
        if (n < 2) n = 2;

        if (si.isBezier) {
            for (int j = 0; j < n; j++) {
                double t = static_cast<double>(j) / n;
                samples.push_back(bezierCubic(si.cp0, si.cp1, si.cp2, si.cp3, t));
            }
        } else {
            for (int j = 0; j < n; j++) {
                double t = static_cast<double>(j) / n;
                samples.push_back(Point2D::lerp(si.cp0, si.cp3, t));
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

    if (xy.empty()) return result;

    // ─── Arc-length-based z interpolation ────────────────────
    // Compute cumulative arc length of the 2D samples, then interpolate z
    // based on the arc-length position within the control point sequence.
    // This ensures elevation follows the actual road geometry.

    // Compute cumulative arc length of samples
    std::vector<double> sampleArcLen(xy.size(), 0);
    for (size_t i = 1; i < xy.size(); i++) {
        sampleArcLen[i] = sampleArcLen[i - 1] + xy[i].distanceTo(xy[i - 1]);
    }
    double totalSampleLen = sampleArcLen.back();

    // Compute cumulative arc length of control points (2D)
    std::vector<double> cpArcLen(points.size(), 0);
    for (size_t i = 1; i < points.size(); i++) {
        cpArcLen[i] = cpArcLen[i - 1] + points[i].position.distanceTo(points[i - 1].position);
    }
    double totalCpLen = cpArcLen.back();

    if (totalCpLen < EPSILON) {
        // All control points at same position — use first z
        double z = points.empty() ? 0 : points[0].z;
        for (const auto& p : xy) result.push_back({p.x, p.y, z});
        return result;
    }

    // For each sample, find its arc-length position and interpolate z
    for (size_t i = 0; i < xy.size(); i++) {
        double t = totalSampleLen > EPSILON ? sampleArcLen[i] / totalSampleLen : 0;
        double targetLen = t * totalCpLen;

        // Find which control point segment this falls in
        size_t idx = 0;
        for (size_t j = 1; j < points.size(); j++) {
            if (cpArcLen[j] >= targetLen) {
                idx = j - 1;
                break;
            }
            idx = j;
        }
        if (idx >= points.size() - 1) {
            result.push_back({xy[i].x, xy[i].y, points.back().z});
        } else {
            double segLen = cpArcLen[idx + 1] - cpArcLen[idx];
            double frac = segLen > EPSILON ? (targetLen - cpArcLen[idx]) / segLen : 0;
            frac = std::max(0.0, std::min(1.0, frac));
            double z = points[idx].z + (points[idx + 1].z - points[idx].z) * frac;
            result.push_back({xy[i].x, xy[i].y, z});
        }
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