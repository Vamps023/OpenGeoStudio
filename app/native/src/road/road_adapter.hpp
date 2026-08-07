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
// Phase 1.8.3b: Bezier (exact from handles) + Arc/Spiral (exact from metadata)
//   - Bezier: reconstructed from absolute control points (handles)
//   - Arc:    reconstructed from SegmentMetadata (curvature, length, heading)
//   - Spiral: reconstructed from SegmentMetadata (kappa0, kappa1, length, heading)
//   - Missing metadata → AdapterReport warning (NO silent fallback)
//
// Phase 1.8.3c: Legacy fallback (best-effort, no metadata) — separate path
// Phase 1.8.3d: Golden parity — all 7 fixtures
//
// The legacy Road path remains completely untouched.

#include "road.hpp"
#include "road_v2.hpp"
#include <string>
#include <vector>

namespace geo {

// ─── AdapterReport: diagnostics from roadToV2 ──────────────
//
// Tracks how many segments were reconstructed exactly vs
// approximately vs unsupported. Warnings are emitted for
// missing metadata or unsupported geometry types.
//
struct AdapterReport {
    int lineSegments = 0;
    int bezierSegments = 0;
    int arcSegments = 0;
    int spiralSegments = 0;

    int reconstructedExactly = 0;
    int reconstructedApproximately = 0;  // Always 0 in 1.8.3b (no fitting)
    int unsupportedSegments = 0;

    std::vector<std::string> warnings;

    int totalSegments() const {
        return lineSegments + bezierSegments + arcSegments + spiralSegments;
    }
};

// ─── roadToV2: pure conversion function ────────────────────
//
// Converts a legacy Road (ControlPoint[]) to a RoadV2 (GeometrySegment[]).
// Pure function: no side effects, no mutation of input.
//
// Returns RoadV2 by value. The caller can inspect the AdapterReport
// via the overload below to check for warnings.
//
// Conversion rules:
//   1. Corner points (no handles, no metadata) → LineSegment
//   2. Handles present → BezierSegment (exact, from absolute control points)
//   3. SegmentMetadata type="arc" → ArcSegment (exact, from metadata)
//   4. SegmentMetadata type="spiral" → SpiralSegment (exact, from metadata)
//   5. Metadata missing for non-line/non-bezier → warning, LineSegment fallback
//
inline RoadV2 roadToV2(const Road& legacy, AdapterReport& report) {
    RoadV2 v2;
    report = AdapterReport{};

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
        return v2;
    }

    // ─── Convert each pair of consecutive control points ───
    v2.reserveSegments(legacy.points.size() - 1);

    for (size_t i = 0; i < legacy.points.size() - 1; i++) {
        const auto& p0 = legacy.points[i];
        const auto& p1 = legacy.points[i + 1];
        bool isBezier = p0.hasHandleOut || p1.hasHandleIn;
        bool hasMeta = p0.segmentMeta.has_value();

        if (hasMeta) {
            const auto& meta = *p0.segmentMeta;

            if (meta.type == "arc") {
                // Exact ArcSegment reconstruction from metadata
                v2.addSegment<ArcSegment>(
                    p0.position,
                    meta.startHeading,
                    meta.curvature,
                    meta.arcLength
                );
                report.arcSegments++;
                report.reconstructedExactly++;

            } else if (meta.type == "spiral") {
                // Exact SpiralSegment reconstruction from metadata
                v2.addSegment<SpiralSegment>(
                    p0.position,
                    meta.startHeading,
                    meta.curvatureStart,
                    meta.curvatureEnd,
                    meta.segmentLength
                );
                report.spiralSegments++;
                report.reconstructedExactly++;

            } else if (meta.type == "line") {
                // Explicit line metadata → LineSegment
                v2.addSegment<LineSegment>(p0.position, p1.position);
                report.lineSegments++;
                report.reconstructedExactly++;

            } else if (meta.type == "bezier") {
                // Bezier metadata is redundant — handles are the source of truth
                if (isBezier) {
                    Point2D cp0 = p0.position;
                    Point2D cp1 = p0.position + p0.handleOut;
                    Point2D cp2 = p1.position + p1.handleIn;
                    Point2D cp3 = p1.position;
                    v2.addSegment<BezierSegment>(cp0, cp1, cp2, cp3);
                    report.bezierSegments++;
                    report.reconstructedExactly++;
                } else {
                    report.warnings.push_back(
                        "Segment " + std::to_string(i) +
                        ": metadata type='bezier' but no handles present — falling back to LineSegment");
                    v2.addSegment<LineSegment>(p0.position, p1.position);
                    report.lineSegments++;
                    report.unsupportedSegments++;
                }

            } else {
                report.warnings.push_back(
                    "Segment " + std::to_string(i) +
                    ": unknown metadata type '" + meta.type + "' — falling back to LineSegment");
                v2.addSegment<LineSegment>(p0.position, p1.position);
                report.lineSegments++;
                report.unsupportedSegments++;
            }

        } else if (isBezier) {
            // Bezier: exact reconstruction from handles (absolute control points)
            Point2D cp0 = p0.position;
            Point2D cp1 = p0.position + p0.handleOut;
            Point2D cp2 = p1.position + p1.handleIn;
            Point2D cp3 = p1.position;
            v2.addSegment<BezierSegment>(cp0, cp1, cp2, cp3);
            report.bezierSegments++;
            report.reconstructedExactly++;

        } else {
            // Straight line segment (no handles, no metadata)
            v2.addSegment<LineSegment>(p0.position, p1.position);
            report.lineSegments++;
            report.reconstructedExactly++;
        }
    }

    return v2;
}

// ─── Convenience overload (no report) ──────────────────────
inline RoadV2 roadToV2(const Road& legacy) {
    AdapterReport unused;
    return roadToV2(legacy, unused);
}

} // namespace geo
