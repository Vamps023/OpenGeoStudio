#pragma once

// ═══════════════════════════════════════════════════════════
// Circle Arc — Constant-radius circular arc with tangent continuity
// ═══════════════════════════════════════════════════════════
//
// Public API: Circle arc computation for road geometry
// Provides createCircleArc function for road tool implementations

#include "geometry.hpp"
#include "road.hpp"

namespace geo {

// Compute a circular arc from a start point + direction to an end point.
// The arc starts tangent to the given direction (G1 continuity).
// Returns the arc with sampled points, or nullopt if invalid.
inline CircleArc computeCircleArc(const Point2D& startPoint,
                                   const Vec2& startDirection,
                                   const Point2D& endPoint,
                                   int segments = 32) {
    CircleArc arc;

    // Vector from start to end
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

    // Angles
    double chordAngle = std::atan2(dy, dx);
    double dirAngle = std::atan2(startDirection.y, startDirection.x);

    // Cross product determines turn direction
    double cross = startDirection.x * dy - startDirection.y * dx;
    bool isLeftTurn = cross > 0;

    // Perpendicular direction (left = +90°, right = -90°)
    Vec2 perpDir = isLeftTurn
        ? Vec2{-startDirection.y, startDirection.x}
        : Vec2{startDirection.y, -startDirection.x};

    // Sweep angle
    double sweepAngle = 2 * std::abs(chordAngle - dirAngle);
    if (sweepAngle > PI) sweepAngle = TWO_PI - sweepAngle;

    // Radius
    double radius = chordLen / (2 * std::sin(sweepAngle / 2));

    if (radius < 0.5 || !std::isfinite(radius)) {
        // Fallback: straight line
        arc.center = {startPoint.x + perpDir.x * 1000,
                      startPoint.y + perpDir.y * 1000};
        arc.radius = 1000;
        arc.sweep = 0;
        arc.tangentIn = startDirection;
        arc.tangentOut = {dx / chordLen, dy / chordLen};
        for (int i = 0; i <= segments; i++) {
            double t = static_cast<double>(i) / segments;
            arc.points.push_back({startPoint.x + dx * t, startPoint.y + dy * t});
        }
        return arc;
    }

    // Center
    arc.center = {startPoint.x + perpDir.x * radius,
                  startPoint.y + perpDir.y * radius};
    arc.radius = radius;

    // Start and end angles from center
    arc.startAngle = std::atan2(startPoint.y - arc.center.y,
                                 startPoint.x - arc.center.x);
    arc.endAngle = std::atan2(endPoint.y - arc.center.y,
                               endPoint.x - arc.center.x);

    // Sweep
    if (isLeftTurn) {
        arc.sweep = normalizeAngle(arc.endAngle - arc.startAngle);
    } else {
        arc.sweep = -normalizeAngle(arc.startAngle - arc.endAngle);
    }

    // Sample points using shared utility
    arc.points = sampleCircleArc(arc.center, radius, arc.startAngle, arc.sweep, segments);

    // Tangents
    arc.tangentIn = startDirection;
    arc.tangentOut = isLeftTurn
        ? Vec2{-std::sin(arc.endAngle), std::cos(arc.endAngle)}
        : Vec2{std::sin(arc.endAngle), -std::cos(arc.endAngle)};

    return arc;
}

// ─── Fillet Arc ────────────────────────────────────────────
// Generate a true circular fillet arc between two lines meeting at a corner.
// dirIn = direction TOWARD corner (incoming)
// dirOut = direction AWAY from corner (outgoing)
// Returns sampled arc points (including start and end tangent points).
#define GEO_FILLET_ARC_DEFINED
inline std::vector<Point2D> filletArc(const Point2D& corner,
                                       const Vec2& dirIn,
                                       const Vec2& dirOut,
                                       double radius,
                                       int segments = 8) {
    // Tangent points: at distance `radius` from corner along each direction
    Point2D p1 = {corner.x - dirIn.x * radius, corner.y - dirIn.y * radius};
    Point2D p2 = {corner.x + dirOut.x * radius, corner.y + dirOut.y * radius};

    // Arc center = intersection of perpendiculars at tangent points
    Vec2 perpIn = {-dirIn.y, dirIn.x};
    Vec2 perpOut = {-dirOut.y, dirOut.x};

    Point2D center = lineIntersection(p1, perpIn, p2, perpOut);

    if (!isValid(center)) {
        // Parallel edges → straight line
        std::vector<Point2D> points;
        points.reserve(segments + 1);
        for (int i = 0; i <= segments; i++) {
            double t = static_cast<double>(i) / segments;
            points.push_back(Point2D::lerp(p1, p2, t));
        }
        return points;
    }

    // Sample circular arc from p1 to p2 around center using shared utility
    double startAngle = std::atan2(p1.y - center.y, p1.x - center.x);
    double endAngle = std::atan2(p2.y - center.y, p2.x - center.x);

    // Determine shortest sweep
    double sweep = endAngle - startAngle;
    sweep = normalizeAnglePi(sweep);

    return sampleCircleArc(center, radius, startAngle, sweep, segments);
}

} // namespace geo