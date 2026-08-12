#pragma once

// ═══════════════════════════════════════════════════════════
// Lane Geometry — Phase 2.3: World-Space Lane Evaluation
// ═══════════════════════════════════════════════════════════
//
// @file lane_geometry.hpp
// @brief World-space lane evaluation: combine SegmentSequence
//        (Phase 1) with LaneSection (Phase 2.1) and Polynomial3
//        (Phase 2.2) to produce world-space lane positions.
//
// @section Responsibility
// This file is an EVALUATION LAYER only. It computes world-space
// positions, tangents, normals, and headings for lanes at a given
// s-position. It does NOT:
//   - Sample polylines (that's 2.4)
//   - Generate meshes (that's 2.7)
//   - Render anything
//   - Allocate buffers
//
// @section Architecture
//
//   RoadV2
//      │
//      ├── SegmentSequence (Phase 1) → positionAt(s), normalAt(s), headingAt(s)
//      │
//      └── LaneSection (Phase 2.1) → boundaryOffset(laneId, ds), laneCenterOffset(laneId, ds)
//               │
//               └── Polynomial3 (Phase 2.2) → width evaluation
//
//   World-space equation:
//     Pworld = ReferenceLine.positionAt(s) - normalAt(s) * lateralOffset
//
//   Note: normalAt() returns the LEFT normal (positive = left, CCW from tangent).
//   LaneSection::boundaryOffset() returns right-positive (right lanes > 0, left < 0).
//   So we NEGATE the offset to convert right-positive → left-positive:
//     Pworld = positionAt(s) + normalAt(s) * (-offset)
//            = positionAt(s) - normalAt(s) * offset
//
// @section API Invariant
// evaluateLaneCenter() returns C¹ continuous results provided:
//   - The reference line geometry is C¹ (Line, Arc, Bezier are; Spiral is G1/C1)
//   - The lane width polynomial is C¹ (constant and linear are; cubic smooth taper is)
//
// @section Performance
// Each evaluation is O(log N) where N = number of geometry segments
// (binary search in SegmentSequence). No allocations.
//
// @section API Freeze
// NOT YET FROZEN. Will be frozen at Phase 2 Complete.

#include "geometry.hpp"
#include "geometry_segment.hpp"
#include "lane_engine.hpp"
#include "road_v2.hpp"

namespace geo {

// ═══════════════════════════════════════════════════════════
// LanePoint — Result of lane evaluation
// ═══════════════════════════════════════════════════════════
//
// All fields are in world space. This struct is returned by value
// and contains no allocations.
//
struct LanePoint {
    Point2D position;       // world-space position (x, y)
    Vec2 tangent;           // unit tangent vector (direction of travel)
    Vec2 normal;            // unit normal vector (left = 90° CCW from tangent)
    double heading;         // heading angle in radians (same as reference line)
    double laneOffset;      // lateral offset from reference line (right-positive)
};

// ═══════════════════════════════════════════════════════════
// evaluateLaneCenter — World-space position of a lane center
// ═══════════════════════════════════════════════════════════
//
// Evaluates the center of lane `laneId` at arc-length position `s`
// along the road. Uses the active LaneSection at position s.
//
// For legacy roads (no explicit LaneSection), uses the cached
// synthesized LaneSection from width/laneCount.
//
// @param road  The road to evaluate
// @param laneId  Lane ID (0=center, negative=left, positive=right)
// @param s  Arc-length position along the reference line
// @return LanePoint with world-space position, tangent, normal, heading, offset
//
// @note Returns C¹ continuous results if geometry and width polynomials are C¹.
// @note O(log N) per call, no allocations.
//
inline LanePoint evaluateLaneCenter(const RoadV2& road, int laneId, double s) {
    LanePoint result;

    // Get reference line geometry at s
    const SegmentSequence& seq = road.geometry();
    result.position = seq.positionAt(s);
    result.tangent = seq.tangentAt(s);
    result.normal = seq.normalAt(s);
    result.heading = seq.headingAt(s);

    // Get the active lane section
    const LaneSection* ls = road.laneSectionAt(s);
    if (!ls) {
        // Legacy road — use synthesized lane section
        ls = &road.legacyLaneSection();
    }

    // Compute lateral offset of lane center (right-positive)
    double ds = s - ls->startS;
    result.laneOffset = ls->laneCenterOffset(laneId, ds);

    // Convert to world space: right-positive offset → subtract from left normal
    // Pworld = position - normal * offset
    result.position = result.position - result.normal * result.laneOffset;

    return result;
}

// ═══════════════════════════════════════════════════════════
// evaluateLaneBoundary — World-space position of a lane edge
// ═══════════════════════════════════════════════════════════
//
// Evaluates a lane boundary at arc-length position `s`.
// A lane boundary is the edge between two adjacent lanes.
//
// @param road  The road to evaluate
// @param laneId  Lane ID whose boundary to evaluate
// @param outer  If true: outer edge (farther from center)
//               If false: inner edge (closer to center)
// @param s  Arc-length position along the reference line
// @return LanePoint with world-space position and reference line data
//
// Examples:
//   evaluateLaneBoundary(road, 1, true, s)  → outer edge of lane 1 (boundary with lane 2)
//   evaluateLaneBoundary(road, 1, false, s) → inner edge of lane 1 (boundary with center)
//   evaluateLaneBoundary(road, -1, true, s) → outer edge of lane -1 (boundary with lane -2)
//   evaluateLaneBoundary(road, 0, true, s)  → outer edge of center (= center line, offset=0)
//
inline LanePoint evaluateLaneBoundary(const RoadV2& road, int laneId,
                                       bool outer, double s) {
    LanePoint result;

    // Get reference line geometry at s
    const SegmentSequence& seq = road.geometry();
    result.position = seq.positionAt(s);
    result.tangent = seq.tangentAt(s);
    result.normal = seq.normalAt(s);
    result.heading = seq.headingAt(s);

    // Get the active lane section
    const LaneSection* ls = road.laneSectionAt(s);
    if (!ls) {
        ls = &road.legacyLaneSection();
    }

    // Compute lateral offset of the requested edge (right-positive)
    double ds = s - ls->startS;
    result.laneOffset = outer
        ? ls->laneOuterEdgeOffset(laneId, ds)
        : ls->laneInnerEdgeOffset(laneId, ds);

    // Convert to world space
    result.position = result.position - result.normal * result.laneOffset;

    return result;
}

// ═══════════════════════════════════════════════════════════
// evaluateLaneAtOffset — World-space position at arbitrary lateral offset
// ═══════════════════════════════════════════════════════════
//
// Evaluates a point at an arbitrary lateral offset from the reference line.
// This is the most general form — lane center and boundary are special cases.
//
// @param road  The road to evaluate
// @param lateralOffset  Lateral offset from reference line (right-positive)
// @param s  Arc-length position along the reference line
// @return LanePoint with world-space position
//
// This is equivalent to positionAtST(s, -lateralOffset) since
// positionAtST uses left-positive t convention.
//
inline LanePoint evaluateLaneAtOffset(const RoadV2& road,
                                       double lateralOffset, double s) {
    LanePoint result;

    const SegmentSequence& seq = road.geometry();
    result.position = seq.positionAt(s);
    result.tangent = seq.tangentAt(s);
    result.normal = seq.normalAt(s);
    result.heading = seq.headingAt(s);
    result.laneOffset = lateralOffset;

    // Convert right-positive offset to world space
    result.position = result.position - result.normal * lateralOffset;

    return result;
}

} // namespace geo
