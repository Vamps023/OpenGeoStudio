#pragma once

// ═══════════════════════════════════════════════════════════
// LaneMaker-style curve fitting — Segment + Arc composition
// ═══════════════════════════════════════════════════════════
//
// Inspired by guotata1996/lanemaker's ConnectRays, but instead of
// clothoid/Bezier fitting, this builds roads from the two primitives
// that are already well-tested in the OpenGeoStudio engine:
//
//   createSegment  — straight line (SegmentKind::Line)
//   createCircleArc — circular arc  (SegmentKind::Arc)
//
// Algorithm: "Two Tangents and an Arc" (classic road design)
//   Given: P0 + D0 (start ray), P1 + D1 (end ray)
//
//   1. If both rays are collinear with P0→P1, return a single Line.
//   2. Otherwise, compose: Segment(P0→T1) + Arc(T1→T2) + Segment(T2→P1)
//      where T1 and T2 are tangent points on a circle of radius R.
//      The arc is tangent to D0 at T1 and tangent to D1 at T2.
//      Solve the 2×2 linear system for tangent lengths s1, s2.
//   3. If no valid R gives positive s1, s2, fall back to a single Arc
//      (tangent to D0 at P0, passing through P1).
//   4. If the single arc is degenerate, fall back to a cubic Bezier.
//
// This produces roads with proper SegmentMetadata (Line + Arc) that
// the RoadV2 adapter can reconstruct exactly — no fitting, no approximation.

#include "geometry.hpp"
#include "road.hpp"
#include "clothoid.hpp"
#include "skia_arc.hpp"
#include "road_tools.hpp"
#include <cmath>

namespace geo {

// ─── Bezier fit between two rays (final fallback) ──────────
inline Road createBezierFromRays(
    const Point2D& startPos,
    const Vec2& startHdg,
    const Point2D& endPos,
    const Vec2& endHdg,
    const RoadToolParams& params = {}
) {
    double dist = startPos.distanceTo(endPos);
    double handleLen = std::max(dist / 3.0, 1.0);

    Point2D h0 = startPos + startHdg.normalized() * handleLen;
    Point2D h1 = endPos - endHdg.normalized() * handleLen;

    return createBezier(startPos, h0, endPos, h1, params);
}

// ─── Segment-Arc-Segment composition ───────────────────────
// Builds a road: Line(P0→T1) + Arc(T1→T2) + Line(T2→P1)
// where the arc is tangent to D0 at T1 and tangent to D1 at T2.
//
// Math (for turn sign σ = +1 left / -1 right):
//   perp(D) = σ × (-D.y, D.x)   [points toward arc center]
//   V = P1 - P0
//   B = perp(D0) - perp(D1)
//   V = D0·s1 + D1·s2 + R·B     [2 equations, 3 unknowns]
//
// Fix R, solve for s1, s2:
//   det = D0 × D1 = sin(turnAngle)
//   s1 = (D1.y·(V.x - R·B.x) - D1.x·(V.y - R·B.y)) / det
//   s2 = (D0.x·(V.y - R·B.y) - D0.y·(V.x - R·B.x)) / det
inline Road createSegmentArcSegment(
    const Point2D& startPos,
    const Vec2& startDir,
    const Point2D& endPos,
    const Vec2& endDir,
    double radius,
    const RoadToolParams& params = {}
) {
    Vec2 sDir = startDir.normalized();
    Vec2 eDir = endDir.normalized();
    double dist = startPos.distanceTo(endPos);

    double startAngle = std::atan2(sDir.y, sDir.x);
    double endAngle   = std::atan2(eDir.y, eDir.x);
    double turnAngle  = normalizeAnglePi(endAngle - startAngle);
    double absTurn    = std::abs(turnAngle);
    double sigma      = (turnAngle >= 0) ? 1.0 : -1.0;

    // Perpendicular vectors pointing toward arc center
    Vec2 perpS = { -sigma * sDir.y, sigma * sDir.x };
    Vec2 perpE = { -sigma * eDir.y, sigma * eDir.x };
    Vec2 B     = { perpS.x - perpE.x, perpS.y - perpE.y };

    Vec2 V = { endPos.x - startPos.x, endPos.y - startPos.y };

    double det = sDir.x * eDir.y - eDir.x * sDir.y;  // = sin(turnAngle)

    // Solve for s1, s2
    double rhsX = V.x - radius * B.x;
    double rhsY = V.y - radius * B.y;
    double s1 = (eDir.y * rhsX - eDir.x * rhsY) / det;
    double s2 = (sDir.x * rhsY - sDir.y * rhsX) / det;

    // Tangent points
    Point2D T1 = { startPos.x + sDir.x * s1, startPos.y + sDir.y * s1 };
    Point2D T2 = { endPos.x   - eDir.x * s2, endPos.y   - eDir.y * s2 };
    Point2D center = { T1.x + perpS.x * radius, T1.y + perpS.y * radius };

    // Arc parameters
    double arcStartAngle = std::atan2(T1.y - center.y, T1.x - center.x);
    double arcSweep      = sigma * absTurn;
    double arcLength     = radius * absTurn;
    double signedCurv    = sigma / radius;

    // Build the road
    Road road;
    road.width = params.width;
    road.laneCount = params.laneCount;
    road.profileName = params.profileName;
    road.formatVersion = 2;

    // ── CP0: start point (Line metadata for segment P0→T1) ──
    if (s1 > 0.01) {
        ControlPoint cp0;
        cp0.position = startPos;
        cp0.z = params.z;
        cp0.type = "corner";
        cp0.id = "cp_lm_start";
        SegmentMetadata meta0;
        meta0.kind = SegmentKind::Line;
        meta0.startHeading = startAngle;
        cp0.segmentMeta = meta0;
        road.points.push_back(cp0);
    }

    // ── Arc: sample from T1 to T2 ──
    int arcSamples = 8;
    int startIdx = static_cast<int>(road.points.size());

    for (int i = 0; i < arcSamples; i++) {
        double t = static_cast<double>(i) / (arcSamples - 1);
        double angle = arcStartAngle + arcSweep * t;

        ControlPoint cp;
        cp.position = { center.x + radius * std::cos(angle),
                        center.y + radius * std::sin(angle) };
        cp.z = params.z;
        cp.type = "smooth";
        cp.id = "cp_lm_arc_" + std::to_string(i);

        // First arc CP gets Arc metadata
        if (i == 0) {
            SegmentMetadata meta;
            meta.kind = SegmentKind::Arc;
            meta.startHeading = startAngle;
            meta.curvature = signedCurv;
            meta.arcLength = arcLength;
            cp.segmentMeta = meta;
        }

        road.points.push_back(cp);
    }

    // ── Last segment: T2 → P1 (Line metadata) ──
    if (s2 > 0.01) {
        // T2 is already the last arc point — add Line metadata to it
        if (!road.points.empty()) {
            SegmentMetadata metaT2;
            metaT2.kind = SegmentKind::Line;
            metaT2.startHeading = endAngle;
            road.points.back().segmentMeta = metaT2;
        }

        ControlPoint cpEnd;
        cpEnd.position = endPos;
        cpEnd.z = params.z;
        cpEnd.type = "corner";
        cpEnd.id = "cp_lm_end";
        road.points.push_back(cpEnd);
    }

    return road;
}

// ─── LaneMaker-style automatic curve fitting ───────────────
// Picks the best geometry (line, segment-arc-segment, arc, or bezier)
// between two rays.
//   startPos/startHdg : incoming ray
//   endPos/endHdg     : outgoing ray
// Returns a geo::Road with sampled control points and SegmentMetadata.
inline Road createLanemakerConnection(
    const Point2D& startPos,
    const Vec2& startHdg,
    const Point2D& endPos,
    const Vec2& endHdg,
    const RoadToolParams& params = {}
) {
    Vec2 sDir = startHdg.normalized();
    Vec2 eDir = endHdg.normalized();
    double dist = startPos.distanceTo(endPos);

    // ── 1. Collinear → single Line segment ──
    Vec2 toEnd = (endPos - startPos).normalized();
    bool sameDir = sDir.dot(toEnd) > 0.999 && eDir.dot(toEnd) > 0.999;
    if (sameDir && dist > 0.1) {
        return createSegment(startPos, endPos, params);
    }

    // ── 2. Compute turn angle ──
    double startAngle = std::atan2(sDir.y, sDir.x);
    double endAngle   = std::atan2(eDir.y, eDir.x);
    double turnAngle  = normalizeAnglePi(endAngle - startAngle);
    double absTurn    = std::abs(turnAngle);
    double det        = sDir.x * eDir.y - eDir.x * sDir.y;  // sin(turnAngle)

    // ── 3. Try Segment-Arc-Segment with various radii ──
    if (std::abs(det) > 0.001 && absTurn < PI - 0.01) {
        // Try radii from large to small — pick the first that gives
        // positive tangent lengths (s1, s2 > 0)
        double radii[] = {
            dist * 0.5,
            dist * 0.3,
            dist * 0.2,
            dist * 0.15,
            dist * 0.1,
            std::max(dist * 0.05, 5.0),
        };

        for (double R : radii) {
            if (R < 1.0) continue;

            // Compute s1, s2 for this R
            double sigma = (turnAngle >= 0) ? 1.0 : -1.0;
            Vec2 perpS = { -sigma * sDir.y, sigma * sDir.x };
            Vec2 perpE = { -sigma * eDir.y, sigma * eDir.x };
            Vec2 B     = { perpS.x - perpE.x, perpS.y - perpE.y };
            Vec2 V     = { endPos.x - startPos.x, endPos.y - startPos.y };

            double rhsX = V.x - R * B.x;
            double rhsY = V.y - R * B.y;
            double s1 = (eDir.y * rhsX - eDir.x * rhsY) / det;
            double s2 = (sDir.x * rhsY - sDir.y * rhsX) / det;

            if (s1 > 0.1 && s2 > 0.1) {
                return createSegmentArcSegment(
                    startPos, sDir, endPos, eDir, R, params);
            }
        }
    }

    // ── 4. Fallback: single Arc (tangent to start, through end) ──
    CircleArc arc = computeSkiaCircleArc(startPos, sDir, endPos, 64);
    if (arc.radius > 0.5 && std::isfinite(arc.radius)) {
        Road road;
        road.width = params.width;
        road.laneCount = params.laneCount;
        road.profileName = params.profileName;
        road.formatVersion = 2;

        int numCPs = 8;
        for (int i = 0; i < numCPs; i++) {
            double t = static_cast<double>(i) / (numCPs - 1);
            int sampleIdx = static_cast<int>(t * (arc.points.size() - 1));

            ControlPoint cp;
            cp.position = arc.points[sampleIdx];
            cp.z = params.z;
            cp.type = "smooth";
            cp.id = "cp_lm_arc_" + std::to_string(i);

            if (i == 0) {
                SegmentMetadata meta;
                meta.kind = SegmentKind::Arc;
                meta.startHeading = std::atan2(sDir.y, sDir.x);
                meta.curvature = (arc.sweep > 0 ? 1.0 : -1.0) / arc.radius;
                meta.arcLength = arc.radius * std::abs(arc.sweep);
                cp.segmentMeta = meta;
            }

            road.points.push_back(cp);
        }
        return road;
    }

    // ── 5. Final fallback: cubic Bezier ──
    return createBezierFromRays(startPos, sDir, endPos, eDir, params);
}

} // namespace geo
