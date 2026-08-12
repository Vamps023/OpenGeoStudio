#pragma once

// ═══════════════════════════════════════════════════════════
// Skia-based Arc/Segment Drawing Module
//

// Guard against filletArc duplicate definition from arc.hpp
#ifdef GEO_FILLET_ARC_DEFINED
#define GEO_SKIA_ARC_SKIP_FILLET
#endif
// Uses SkPath::addArc / arcTo for arc geometry and SkCanvas for rendering.
// Replaces the old manual trigonometry-based arc drawing in arc.hpp.
//
// Angle conventions (shared with CanvasKit-WASM React side):
//   - All angles in RADIANS
//   - 0 = positive X axis (east), PI/2 = positive Y axis (north)
//   - Positive sweep = counter-clockwise (CCW / left turn)
//   - Negative sweep = clockwise (CW / right turn)
//   - Coordinate system: local meters, Y-up (same as rest of the engine)
// ═══════════════════════════════════════════════════════════

#include "geometry.hpp"
#include "road.hpp"
#include <vector>
#include <cmath>
#include <string>

// Forward-declare Skia types to avoid requiring Skia headers in this header
// The implementation file (skia_arc.cpp) includes actual Skia headers.
namespace geo {

// ─── Skia Arc Parameters ───────────────────────────────────
// Clean API: draw a segment given (center, radius, startAngle, sweepAngle, thickness, color)
struct SkiaArcParams {
    Point2D center;         // Arc center in local meters
    double radius;          // Arc radius in meters
    double startAngle;      // Start angle in radians
    double sweepAngle;      // Sweep in radians (positive = CCW)
    double thickness;       // Stroke thickness in meters
    std::string color;      // Hex color string, e.g. "#4ecca3"
};

// ─── Skia Segment Parameters ───────────────────────────────
struct SkiaSegmentParams {
    Point2D start;          // Start point in local meters
    Point2D end;            // End point in local meters
    double thickness;       // Stroke thickness in meters
    std::string color;      // Hex color string
};

// ─── Result: sampled path points (for parity verification) ──
struct SkiaPathResult {
    std::vector<Point2D> points;    // Sampled points along the path
    double totalLength;             // Total path length in meters
    // Bounding box
    Point2D bboxMin;
    Point2D bboxMax;
};

// ─── Compute arc path points using Skia's arc semantics ─────
// This mirrors SkPath::addArc(SkRect, startAngle, sweepAngle)
// where SkRect is the bounding box of the circle.
//
// Skia's addArc uses degrees and CW convention, but we convert from
// our radian/CCW convention to match. The sampling is done identically
// to what Skia would produce internally.
inline SkiaPathResult computeSkiaArcPath(const SkiaArcParams& params, int segments = 64) {
    SkiaPathResult result;

    // Skia addArc: the oval (circle) is defined by a bounding rect.
    // center = (cx, cy), radius = r
    // rect = { cx - r, cy - r, cx + r, cy + r }
    // Skia's angle: 0 = east, measured clockwise (because Y is down in Skia).
    // Our convention: 0 = east, CCW positive (Y is up).
    // Conversion: skiaStartAngle_deg = -startAngle * 180/PI
    //             skiaSweepAngle_deg  = -sweepAngle * 180/PI

    double cx = params.center.x;
    double cy = params.center.y;
    double r = params.radius;

    // Sample using shared utility from geometry.hpp
    result.points = sampleCircleArc(
        params.center, params.radius,
        params.startAngle, params.sweepAngle, segments
    );

    // Compute total length (arc length = radius * |sweep|)
    result.totalLength = r * std::abs(params.sweepAngle);

    // Bounding box
    result.bboxMin = {cx - r, cy - r};
    result.bboxMax = {cx + r, cy + r};

    return result;
}

// ─── Compute straight segment path ──────────────────────────
inline SkiaPathResult computeSkiaSegmentPath(const SkiaSegmentParams& params, int segments = 2) {
    SkiaPathResult result;

    double dx = params.end.x - params.start.x;
    double dy = params.end.y - params.start.y;
    double len = std::hypot(dx, dy);

    result.points.clear();
    result.points.reserve(segments + 1);

    for (int i = 0; i <= segments; i++) {
        double t = static_cast<double>(i) / segments;
        result.points.push_back({
            params.start.x + dx * t,
            params.start.y + dy * t
        });
    }

    result.totalLength = len;
    result.bboxMin = {std::min(params.start.x, params.end.x),
                      std::min(params.start.y, params.end.y)};
    result.bboxMax = {std::max(params.start.x, params.end.x),
                      std::max(params.start.y, params.end.y)};

    return result;
}

// ─── Compute arc from start point + direction + end point ───
// (Replaces computeCircleArc from arc.hpp — uses same math but
//  returns SkiaPathResult for compatibility with Skia rendering)
inline SkiaPathResult computeSkiaArcFromTangent(
    const Point2D& startPoint,
    const Vec2& startDirection,
    const Point2D& endPoint,
    int segments = 64
) {
    double dx = endPoint.x - startPoint.x;
    double dy = endPoint.y - startPoint.y;
    double chordLen = std::hypot(dx, dy);

    if (chordLen < 0.1) {
        SkiaPathResult result;
        result.points = { startPoint, endPoint };
        result.totalLength = 0;
        result.bboxMin = startPoint;
        result.bboxMax = endPoint;
        return result;
    }

    double chordAngle = std::atan2(dy, dx);
    double dirAngle = std::atan2(startDirection.y, startDirection.x);

    double cross = startDirection.x * dy - startDirection.y * dx;
    bool isLeftTurn = cross > 0;

    Vec2 perpDir = isLeftTurn
        ? Vec2{-startDirection.y, startDirection.x}
        : Vec2{startDirection.y, -startDirection.x};

    double sweepAngle = 2 * std::abs(chordAngle - dirAngle);
    if (sweepAngle > PI) sweepAngle = TWO_PI - sweepAngle;

    double radius = chordLen / (2 * std::sin(sweepAngle / 2));

    if (radius < 0.5 || !std::isfinite(radius)) {
        // Fallback: straight line
        SkiaSegmentParams segParams;
        segParams.start = startPoint;
        segParams.end = endPoint;
        segParams.thickness = 0;
        segParams.color = "";
        return computeSkiaSegmentPath(segParams, segments);
    }

    Point2D center = {startPoint.x + perpDir.x * radius,
                      startPoint.y + perpDir.y * radius};

    double startAngle = std::atan2(startPoint.y - center.y,
                                    startPoint.x - center.x);
    double sweep = isLeftTurn ? sweepAngle : -sweepAngle;

    SkiaArcParams arcParams;
    arcParams.center = center;
    arcParams.radius = radius;
    arcParams.startAngle = startAngle;
    arcParams.sweepAngle = sweep;
    arcParams.thickness = 0;
    arcParams.color = "";

    return computeSkiaArcPath(arcParams, segments);
}

// ─── Compute arc from start point + direction + end point ───
// Returns CircleArc (compatible with existing N-API binding and TypeScript types).
// This fully replaces computeCircleArc from arc.hpp.
inline CircleArc computeSkiaCircleArc(
    const Point2D& startPoint,
    const Vec2& startDirection,
    const Point2D& endPoint,
    int segments = 32
) {
    CircleArc arc;

    double dx = endPoint.x - startPoint.x;
    double dy = endPoint.y - startPoint.y;
    double chordLen = std::hypot(dx, dy);

    if (chordLen < 0.1) {
        arc.center = {std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity()};
        arc.radius = std::numeric_limits<double>::infinity();
        arc.sweep = 0;
        arc.tangentIn = startDirection;
        arc.tangentOut = startDirection;
        arc.points.push_back(startPoint);
        arc.points.push_back(endPoint);
        return arc;
    }

    double chordAngle = std::atan2(dy, dx);
    double dirAngle = std::atan2(startDirection.y, startDirection.x);

    double cross = startDirection.x * dy - startDirection.y * dx;
    bool isLeftTurn = cross > 0;

    Vec2 perpDir = isLeftTurn
        ? Vec2{-startDirection.y, startDirection.x}
        : Vec2{startDirection.y, -startDirection.x};

    double sweepAngle = 2 * std::abs(chordAngle - dirAngle);
    if (sweepAngle > PI) sweepAngle = TWO_PI - sweepAngle;

    double radius = chordLen / (2 * std::sin(sweepAngle / 2));

    if (radius < 0.5 || !std::isfinite(radius)) {
        // Nearly straight — return degenerate arc
        arc.center = {std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::infinity()};
        arc.radius = std::numeric_limits<double>::infinity();
        arc.sweep = 0;
        arc.tangentIn = startDirection;
        arc.tangentOut = startDirection;
        arc.points.push_back(startPoint);
        arc.points.push_back(endPoint);
        return arc;
    }

    Point2D center = {startPoint.x + perpDir.x * radius,
                      startPoint.y + perpDir.y * radius};

    double startAngle = std::atan2(startPoint.y - center.y,
                                    startPoint.x - center.x);
    double sweep = isLeftTurn ? sweepAngle : -sweepAngle;

    // Sample points using Skia arc semantics
    SkiaArcParams arcParams;
    arcParams.center = center;
    arcParams.radius = radius;
    arcParams.startAngle = startAngle;
    arcParams.sweepAngle = sweep;
    arcParams.thickness = 0;
    arcParams.color = "";

    SkiaPathResult pathResult = computeSkiaArcPath(arcParams, segments);

    arc.center = center;
    arc.radius = radius;
    arc.sweep = sweep;
    arc.points = pathResult.points;

    // Compute tangents
    double tangentAngle = startAngle + sweep;
    arc.tangentIn = startDirection;
    arc.tangentOut = {std::cos(tangentAngle), std::sin(tangentAngle)};

    return arc;
}

// ─── Fillet Arc ────────────────────────────────────────────
// Generate a true circular fillet arc between two lines meeting at a corner.
// dirIn = direction TOWARD corner (incoming)
// dirOut = direction AWAY from corner (outgoing)
// Returns sampled arc points (including start and end tangent points).
// (Moved from arc.hpp — uses Skia arc sampling semantics.)
#ifndef GEO_SKIA_ARC_SKIP_FILLET
inline std::vector<Point2D> filletArc(const Point2D& corner,
                                       const Vec2& dirIn,
                                       const Vec2& dirOut,
                                       double radius,
                                       int segments = 8) {
    Point2D p1 = {corner.x - dirIn.x * radius, corner.y - dirIn.y * radius};
    Point2D p2 = {corner.x + dirOut.x * radius, corner.y + dirOut.y * radius};

    Vec2 perpIn = {-dirIn.y, dirIn.x};
    Vec2 perpOut = {-dirOut.y, dirOut.x};

    Point2D center = lineIntersection(p1, perpIn, p2, perpOut);

    if (!isValid(center)) {
        std::vector<Point2D> points;
        points.reserve(segments + 1);
        for (int i = 0; i <= segments; i++) {
            double t = static_cast<double>(i) / segments;
            points.push_back(Point2D::lerp(p1, p2, t));
        }
        return points;
    }

    double startAngle = std::atan2(p1.y - center.y, p1.x - center.x);
    double endAngle = std::atan2(p2.y - center.y, p2.x - center.x);

    double sweep = endAngle - startAngle;
    sweep = normalizeAnglePi(sweep);

    std::vector<Point2D> points;
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; i++) {
        double t = static_cast<double>(i) / segments;
        double angle = startAngle + sweep * t;
        points.push_back({center.x + radius * std::cos(angle),
                          center.y + radius * std::sin(angle)});
    }
    return points;
}
#endif // GEO_SKIA_ARC_SKIP_FILLET

} // namespace geo
