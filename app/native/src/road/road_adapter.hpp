#pragma once

// ═══════════════════════════════════════════════════════════
// Road Adapter — Conversion between legacy Road and RoadV2
// ═══════════════════════════════════════════════════════════
//
// This is the seam between the legacy ControlPoint[]-based Road
// and the new GeometrySegment-based RoadV2. The adapter is a pure
// conversion function — no side effects, no mutation of input.
//
// Phase 1.8.3a: Infrastructure + LineSegment only
//   - Metadata copy (id, name, color, etc.)
//   - Empty road handling
//   - Corner points (no handles) → LineSegment
//   - Bezier segments (hasHandleOut/hasHandleIn) → TODO 1.8.3b
//
// Phase 1.8.3b: Arc/Spiral/Bezier exact reconstruction from metadata
// Phase 1.8.3c: Legacy fallback (best-effort, no metadata)
// Phase 1.8.3d: Golden parity — all 7 fixtures
//
// The legacy Road path remains completely untouched.

#include "road.hpp"
#include "road_v2.hpp"

namespace geo {

// ─── roadToV2: pure conversion function ────────────────────
//
// Converts a legacy Road (ControlPoint[]) to a RoadV2 (GeometrySegment[]).
// Pure function: no side effects, no mutation of input.
//
// Phase 1.8.3a: LineSegment only (corner points, no handles).
// Segments with handles are skipped with a TODO marker.
//
inline RoadV2 roadToV2(const Road& legacy) {
    RoadV2 v2;

    // ─── Metadata copy ───
    v2.id = legacy.id;
    v2.name = legacy.name;
    v2.color = legacy.color;
    v2.profileName = legacy.profileName;
    v2.startIntersectionId = legacy.startIntersectionId;
    v2.endIntersectionId = legacy.endIntersectionId;
    v2.width = legacy.width;
    v2.laneCount = legacy.laneCount;

    // ─── Empty road ───
    if (legacy.points.empty()) {
        return v2;
    }

    // ─── Single point (degenerate) ───
    if (legacy.points.size() == 1) {
        // No segments — just a point. RoadV2 with no geometry.
        return v2;
    }

    // ─── Convert each pair of consecutive control points ───
    v2.reserveSegments(legacy.points.size() - 1);

    for (size_t i = 0; i < legacy.points.size() - 1; i++) {
        const auto& p0 = legacy.points[i];
        const auto& p1 = legacy.points[i + 1];
        bool isBezier = p0.hasHandleOut || p1.hasHandleIn;

        if (isBezier) {
            // TODO 1.8.3b: Convert to BezierSegment with absolute control points:
            //   P0 = p0.position
            //   P1 = p0.position + p0.handleOut
            //   P2 = p1.position + p1.handleIn
            //   P3 = p1.position
            // For now, fall back to LineSegment (best-effort, will be replaced).
            v2.addSegment<LineSegment>(p0.position, p1.position);
        } else {
            // Straight line segment
            v2.addSegment<LineSegment>(p0.position, p1.position);
        }
    }

    return v2;
}

} // namespace geo
