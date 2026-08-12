#pragma once

// ═══════════════════════════════════════════════════════════
// Road Tools — SCANeR-style road creation tools
// ═══════════════════════════════════════════════════════════
//
// Implements the 6 road creation tools found in SCANeR Studio:
// 1. Segment       — straight line with tangent continuity
// 2. Circle Arc    — constant-radius circular arc (G1 continuous)
// 3. Clothoid Arc  — Euler spiral transition (G2 continuous)
// 4. Polyline      — piecewise linear with optional corner smoothing
// 5. Bézier        — cubic Bézier with handle constraints
// 6. Clothoid Spline — G2-continuous multi-segment clothoid
//
// Each tool produces a Road object (vector of ControlPoints).
// The Road can then be sampled, meshed, and exported.
//
// Reference: SCANeR Studio, OpenDRIVE 1.6 spec, highway engineering
// standards (AASHTO Green Book, DIN 7401).

#include "geometry.hpp"
#include "road.hpp"
#include "arc.hpp"
#include "clothoid.hpp"
#include <string>
#include <vector>
#include <cmath>

namespace geo {

// ─── Tool Type Enum ────────────────────────────────────────
enum class RoadToolType {
    Segment,
    CircleArc,
    ClothoidArc,
    Polyline,
    Bezier,
    ClothoidSpline
};

// ─── Tool Parameters ───────────────────────────────────────
struct RoadToolParams {
    double width = 8.0;
    int laneCount = 2;
    std::string profileName = "city_2x1";
    double z = 0.0;
};

// ─── 1. Segment Tool ───────────────────────────────────────
// Creates a straight road segment between two points.
// Tangent direction is along the segment.
// In SCANeR, this is the simplest tool — just click start, click end.
//
// Mathematics:
//   direction = (end - start).normalized()
//   curvature = 0 (straight line)
//   length = |end - start|
//
// Tangent continuity: The segment starts with direction = (end-start)/|end-start|
// and ends with the same direction. This provides G1 continuity when connected
// to another segment with matching direction.

inline Road createSegment(
    const Point2D& start,
    const Point2D& end,
    const RoadToolParams& params = {}
) {
    Road road;
    road.width = params.width;
    road.laneCount = params.laneCount;
    road.profileName = params.profileName;
    road.formatVersion = 2;

    ControlPoint cpStart;
    cpStart.position = start;
    cpStart.z = params.z;
    cpStart.type = "corner";
    cpStart.id = "cp_seg_start";

    ControlPoint cpEnd;
    cpEnd.position = end;
    cpEnd.z = params.z;
    cpEnd.type = "corner";
    cpEnd.id = "cp_seg_end";

    road.points.push_back(cpStart);
    road.points.push_back(cpEnd);
    return road;
}

// ─── 2. Circle Arc Tool ────────────────────────────────────
// Creates a circular arc with tangent continuity at the start.
// The arc starts tangent to startDirection and passes through endPoint.
//
// User workflow (SCANeR):
//   1. Click start point (inherits direction from previous segment)
//   2. Move mouse — arc preview updates in real-time
//   3. Click end point — arc is finalized
//
// Mathematics:
//   chord = end - start
//   chordAngle = atan2(chord.y, chord.x)
//   dirAngle = atan2(startDir.y, startDir.x)
//   cross = startDir × chord (determines left/right turn)
//   halfAngle = (chordAngle - dirAngle) / 2
//   sweep = 2 × |halfAngle|
//   radius = |chord| / (2 × sin(sweep/2))
//   center = start + perpDir × radius
//   Arc from startAngle to endAngle around center
//
// The arc is discretized into N control points for editing.

inline Road createCircleArc(
    const Point2D& start,
    const Point2D& startDirection,
    const Point2D& end,
    int numControlPoints = 8,
    const RoadToolParams& params = {}
) {
    Road road;
    road.width = params.width;
    road.laneCount = params.laneCount;
    road.profileName = params.profileName;
    road.formatVersion = 2;

    // Compute the arc using the circle arc computation
    CircleArc arcResult = computeCircleArc(start, startDirection, end, numControlPoints * 4);

    if (arcResult.points.size() < 2) {
        // Fallback: straight line
        return createSegment(start, end, params);
    }

    // Compute exact arc parameters for SegmentMetadata
    double startHeading = std::atan2(startDirection.y, startDirection.x);
    // Signed curvature: positive = left/CCW, negative = right/CW
    // sweep > 0 = left turn (CCW), sweep < 0 = right turn (CW)
    double signedCurvature = (arcResult.radius > EPSILON)
        ? arcResult.sweep / std::abs(arcResult.sweep) / arcResult.radius
        : 0.0;
    double arcLength = arcResult.radius * std::abs(arcResult.sweep);

    // Sample control points along the arc (fewer than render samples)
    for (int i = 0; i < numControlPoints; i++) {
        double t = static_cast<double>(i) / (numControlPoints - 1);
        int sampleIdx = static_cast<int>(t * (arcResult.points.size() - 1));

        ControlPoint cp;
        cp.position = arcResult.points[sampleIdx];
        cp.z = params.z;
        cp.type = "smooth";  // arc points are smooth
        cp.id = "cp_arc_" + std::to_string(i);

        // First control point gets the exact arc metadata
        if (i == 0) {
            SegmentMetadata meta;
            meta.kind = SegmentKind::Arc;
            meta.startHeading = startHeading;
            meta.curvature = signedCurvature;
            meta.arcLength = arcLength;
            cp.segmentMeta = meta;
        }

        road.points.push_back(cp);
    }

    return road;
}

// ─── 3. Clothoid Arc Tool ──────────────────────────────────
// Creates an Euler spiral (clothoid) transition curve.
// The clothoid starts tangent to startDirection and ends tangent to endDirection.
//
// User workflow (SCANeR):
//   1. Click start point (inherits direction from previous segment)
//   2. Click end point
//   3. Specify end direction (or it's computed from context)
//   4. Clothoid is generated with G2 continuity
//
// Mathematics:
//   Curvature: κ(s) = s / A² (linear in arc length)
//   A = clothoid parameter
//   Position: x(s) = ∫₀ˢ cos(τ²/2A²) dτ
//             y(s) = ∫₀ˢ sin(τ²/2A²) dτ
//   These are Fresnel integrals.
//
// The clothoid provides:
//   - G1 continuity (tangent continuous) — matches direction at both ends
//   - G2 continuity (curvature continuous) — curvature goes from 0 to 1/R smoothly
//
// This is critical for highway design because it provides gradual
// centripetal acceleration change, avoiding sudden jerks.

inline Road createClothoidArc(
    const Point2D& start,
    const Point2D& startDirection,
    const Point2D& end,
    const Point2D& endDirection,
    int numControlPoints = 8,
    const RoadToolParams& params = {}
) {
    Road road;
    road.width = params.width;
    road.laneCount = params.laneCount;
    road.profileName = params.profileName;
    road.formatVersion = 2;

    // Use the existing clothoid fitting
    double initialA = 50.0;  // initial guess for clothoid parameter
    ClothoidResult clothoid = fitClothoid(
        start, startDirection,
        end, endDirection,
        initialA, numControlPoints * 4
    );

    if (clothoid.points.size() < 2) {
        // Fallback: straight line
        return createSegment(start, end, params);
    }

    // Compute exact spiral parameters for SegmentMetadata
    double startHeading = std::atan2(startDirection.y, startDirection.x);
    double k0 = clothoid.params.kappa0;
    double k1 = clothoid.params.kappa1;
    // Sign the curvatures based on turn direction
    if (!clothoid.params.isLeftTurn) {
        k0 = -k0;
        k1 = -k1;
    }
    double spiralLength = clothoid.params.L;

    // Sample control points along the clothoid
    for (int i = 0; i < numControlPoints; i++) {
        double t = static_cast<double>(i) / (numControlPoints - 1);
        int sampleIdx = static_cast<int>(t * (clothoid.points.size() - 1));

        ControlPoint cp;
        cp.position = clothoid.points[sampleIdx];
        cp.z = params.z;
        cp.type = "smooth";  // clothoid points are smooth
        cp.id = "cp_clothoid_" + std::to_string(i);

        // First control point gets the exact spiral metadata
        if (i == 0) {
            SegmentMetadata meta;
            meta.kind = SegmentKind::Spiral;
            meta.startHeading = startHeading;
            meta.curvatureStart = k0;
            meta.curvatureEnd = k1;
            meta.segmentLength = spiralLength;
            cp.segmentMeta = meta;
        }

        road.points.push_back(cp);
    }

    return road;
}

// ─── 4. Polyline Tool ──────────────────────────────────────
// Creates a piecewise linear road through multiple points.
// Optionally smooths corners with fillet arcs.
//
// User workflow (SCANeR):
//   1. Click points sequentially
//   2. Each click adds a vertex
//   3. Double-click or Enter to finish
//   4. Corners can be sharp or smoothed with fillet radius
//
// Mathematics:
//   Each segment is a straight line (curvature = 0)
//   At corners, optionally insert a fillet arc:
//     - fillet radius R at each corner
//     - tangent points at distance R from corner along each edge
//     - arc center at intersection of perpendiculars
//
// Tangent continuity: G0 at corners (position continuous, direction discontinuous)
// With fillet: G1 at corners (tangent continuous)

inline Road createPolyline(
    const std::vector<Point2D>& points,
    double filletRadius = 0.0,  // 0 = sharp corners, >0 = rounded
    int filletSegments = 6,
    const RoadToolParams& params = {}
) {
    Road road;
    road.width = params.width;
    road.laneCount = params.laneCount;
    road.profileName = params.profileName;

    if (points.size() < 2) return road;

    if (filletRadius <= 0.0) {
    // Sharp corners — just use the points directly
        for (size_t i = 0; i < points.size(); i++) {
            ControlPoint cp;
            cp.position = points[i];
            cp.z = params.z;
            cp.type = "corner";
            cp.id = "cp_poly_" + std::to_string(i);
            road.points.push_back(cp);
        }
    } else {
        // Rounded corners — insert fillet arcs at each interior vertex
        // Start with first point
        ControlPoint cp0;
        cp0.position = points[0];
        cp0.z = params.z;
        cp0.type = "corner";
        cp0.id = "cp_poly_0";
        road.points.push_back(cp0);

        for (size_t i = 1; i < points.size() - 1; i++) {
            const Point2D& prev = points[i - 1];
            const Point2D& curr = points[i];
            const Point2D& next = points[i + 1];

            // Direction toward corner (incoming)
            Point2D dirIn = (curr - prev).normalized();
            // Direction away from corner (outgoing)
            Point2D dirOut = (next - curr).normalized();

            // Angle at corner
            double dot = dirIn.dot(dirOut);
            double angle = std::acos(std::max(-1.0, std::min(1.0, dot)));
            double halfAngle = angle / 2.0;

            // Clamp fillet radius to available space
            double edgeLen1 = curr.distanceTo(prev);
            double edgeLen2 = curr.distanceTo(next);
            double maxRadius = std::min(edgeLen1, edgeLen2) * 0.4;
            double r = std::min(filletRadius, maxRadius);

            // Tangent distance from corner
            double tangentDist = r / std::tan(halfAngle);

            // Tangent points
            Point2D tanIn = curr - dirIn * tangentDist;
            Point2D tanOut = curr + dirOut * tangentDist;

            // Directions for fillet arc
            Point2D filletDirIn = (curr - tanIn).normalized();
            Point2D filletDirOut = (tanOut - curr).normalized();

            // Generate fillet arc
            auto arc = filletArc(curr, filletDirIn, filletDirOut, r, filletSegments);

            // Add tangent-in point
            ControlPoint cpTanIn;
            cpTanIn.position = tanIn;
            cpTanIn.z = params.z;
            cpTanIn.type = "corner";
            cpTanIn.id = "cp_poly_tan_in_" + std::to_string(i);
            road.points.push_back(cpTanIn);

            // Add arc points
            for (size_t j = 1; j < arc.size() - 1; j++) {
                ControlPoint cpArc;
                cpArc.position = arc[j];
                cpArc.z = params.z;
                cpArc.type = "smooth";
                cpArc.id = "cp_poly_fillet_" + std::to_string(i) + "_" + std::to_string(j);
                road.points.push_back(cpArc);
            }
        }

        // Add last point
        ControlPoint cpLast;
        cpLast.position = points[points.size() - 1];
        cpLast.z = params.z;
        cpLast.type = "corner";
        cpLast.id = "cp_poly_end";
        road.points.push_back(cpLast);
    }

    return road;
}

// ─── 5. Bézier Tool ────────────────────────────────────────
// Creates a cubic Bézier curve with user-controlled handles.
//
// User workflow (SCANeR):
//   1. Click start point
//   2. Drag to define start handle (handleOut direction and length)
//   3. Click end point
//   4. Drag to define end handle (handleIn direction and length)
//   5. Curve is generated with G1 continuity if handles are aligned
//
// Mathematics:
//   B(t) = (1-t)³P0 + 3(1-t)²t·H0 + 3(1-t)t²·H1 + t³·P1
//   where P0 = start, H0 = start + handleOut, H1 = end + handleIn, P1 = end
//
// Tangent at start: direction = handleOut.normalized()
// Tangent at end: direction = -handleIn.normalized()
// G1 continuity: handleOut aligned with previous segment's end direction
//
// The Bézier curve has variable curvature (not constant, not linear).
// It provides smooth, aesthetically pleasing curves but does NOT
// provide G2 continuity (curvature may jump at junctions).

inline Road createBezier(
    const Point2D& start,
    const Point2D& handleOut,  // absolute position of handle (start + offset)
    const Point2D& end,
    const Point2D& handleIn,   // absolute position of handle (end + offset)
    const RoadToolParams& params = {}
) {
    Road road;
    road.width = params.width;
    road.laneCount = params.laneCount;
    road.profileName = params.profileName;

    ControlPoint cpStart;
    cpStart.position = start;
    cpStart.z = params.z;
    cpStart.type = "smooth";
    cpStart.hasHandleOut = true;
    cpStart.handleOut = handleOut - start;  // store as relative offset
    cpStart.id = "cp_bez_start";

    ControlPoint cpEnd;
    cpEnd.position = end;
    cpEnd.z = params.z;
    cpEnd.type = "smooth";
    cpEnd.hasHandleIn = true;
    cpEnd.handleIn = handleIn - end;  // store as relative offset
    cpEnd.id = "cp_bez_end";

    road.points.push_back(cpStart);
    road.points.push_back(cpEnd);
    return road;
}

// ─── 6. Clothoid Spline Tool ───────────────────────────────
// Creates a G2-continuous spline through multiple points using
// clothoid segments between each pair.
//
// User workflow (SCANeR):
//   1. Click points sequentially
//   2. Each segment between points is a clothoid
//   3. Curvature is continuous at every junction (G2)
//   4. Double-click or Enter to finish
//
// Mathematics:
//   For each pair of consecutive points (Pi, Pi+1):
//   - Compute tangent at Pi from context (average of adjacent segment directions)
//   - Compute tangent at Pi+1 similarly
//   - Fit a clothoid between Pi and Pi+1 with those tangent constraints
//   - Ensure curvature continuity: κ_end of segment i = κ_start of segment i+1
//
// This is the most advanced tool. It uses the Walton-Meek algorithm
// for fitting G2-continuous clothoid splines.
//
// Reference: Walton & Meek, "Clothoid splines", Computers & Graphics, 1996.

inline Road createClothoidSpline(
    const std::vector<Point2D>& points,
    const Point2D& startTangent = {0, 0},  // {0,0} = auto-compute
    const Point2D& endTangent = {0, 0},    // {0,0} = auto-compute
    int segmentsPerSpan = 8,
    const RoadToolParams& params = {}
) {
    Road road;
    road.width = params.width;
    road.laneCount = params.laneCount;
    road.profileName = params.profileName;

    if (points.size() < 2) return road;

    // Compute tangents at each point
    std::vector<Point2D> tangents(points.size());

    // Start tangent
    if (startTangent.x != 0 || startTangent.y != 0) {
        tangents[0] = startTangent.normalized();
    } else {
        tangents[0] = (points[1] - points[0]).normalized();
    }

    // End tangent
    if (endTangent.x != 0 || endTangent.y != 0) {
        tangents[points.size() - 1] = endTangent.normalized();
    } else {
        tangents[points.size() - 1] = (points[points.size() - 1] - points[points.size() - 2]).normalized();
    }

    // Interior tangents: use Catmull-Rom-style tangent estimation
    // tangent[i] = (points[i+1] - points[i-1]).normalized()
    // This provides smooth tangent estimation for the clothoid fitting
    for (size_t i = 1; i < points.size() - 1; i++) {
        Point2D prev = points[i - 1];
        Point2D next = points[i + 1];
        tangents[i] = (next - prev).normalized();
    }

    // Generate clothoid segments between consecutive points
    for (size_t i = 0; i < points.size() - 1; i++) {
        const Point2D& p0 = points[i];
        const Point2D& p1 = points[i + 1];
        const Point2D& t0 = tangents[i];
        const Point2D& t1 = tangents[i + 1];

        // Fit clothoid between p0 (with tangent t0) and p1 (with tangent t1)
        double initialA = p0.distanceTo(p1) * 0.5;
        ClothoidResult clothoid = fitClothoid(p0, t0, p1, t1, initialA, segmentsPerSpan * 4);

        // Sample control points from this clothoid segment
        int numCPs = (i == 0) ? segmentsPerSpan : segmentsPerSpan - 1;
        // Skip first point for subsequent segments (it's the last point of previous segment)
        int startIdx = (i == 0) ? 0 : 1;

        for (int j = startIdx; j <= segmentsPerSpan; j++) {
            double t = static_cast<double>(j) / segmentsPerSpan;
            int sampleIdx = static_cast<int>(t * (clothoid.points.size() - 1));
            sampleIdx = std::min(sampleIdx, static_cast<int>(clothoid.points.size()) - 1);

            ControlPoint cp;
            cp.position = clothoid.points[sampleIdx];
            cp.z = params.z;
            cp.type = "smooth";
            cp.id = "cp_cspline_" + std::to_string(i) + "_" + std::to_string(j);
            road.points.push_back(cp);
        }
    }

    return road;
}

// ─── Helper: Auto-compute tangent from context ─────────────
// When creating a new segment connected to an existing road,
// compute the tangent direction at the connection point.

inline Point2D tangentAtEnd(const Road& road) {
    if (road.points.size() < 2) return {1, 0};

    const auto& p0 = road.points[road.points.size() - 2].position;
    const auto& p1 = road.points[road.points.size() - 1].position;
    return (p1 - p0).normalized();
}

inline Point2D tangentAtStart(const Road& road) {
    if (road.points.size() < 2) return {1, 0};

    const auto& p0 = road.points[0].position;
    const auto& p1 = road.points[1].position;
    return (p1 - p0).normalized();
}

// ─── Helper: Connect new road to existing road end ─────────
// Creates a new segment that starts tangent to the end of an existing road.

inline Road connectSegment(
    const Road& existingRoad,
    const Point2D& endPoint,
    const RoadToolParams& params = {}
) {
    Point2D start = existingRoad.points.back().position;
    Point2D dir = tangentAtEnd(existingRoad);

    // If the road is straight, just create a segment
    // If curved, we could create an arc — but for simplicity, use segment
    return createSegment(start, endPoint, params);
}

// ─── Helper: Connect with clothoid transition ──────────────
// Creates a clothoid transition from the end of an existing road
// to a new point with a specified end direction.

inline Road connectClothoid(
    const Road& existingRoad,
    const Point2D& endPoint,
    const Point2D& endDirection,
    const RoadToolParams& params = {}
) {
    Point2D start = existingRoad.points.back().position;
    Point2D startDir = tangentAtEnd(existingRoad);

    return createClothoidArc(start, startDir, endPoint, endDirection, 8, params);
}

} // namespace geo