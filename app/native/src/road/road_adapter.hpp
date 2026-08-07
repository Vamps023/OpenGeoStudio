#pragma once

// ═══════════════════════════════════════════════════════════
// Road Adapter — Conversion between legacy Road and RoadV2
// ═══════════════════════════════════════════════════════════
//
// This is the seam between the legacy ControlPoint[]-based Road
// and the new GeometrySegment-based RoadV2. The adapter is a pure
// conversion function — no side effects, no mutation of input.
//
// Two adapter paths:
//
// 1. roadToV2()       — Exact reconstruction path
//    Uses SegmentMetadata when available (arc, spiral, line).
//    Uses bezier handles when present (exact absolute control points).
//    Emits warnings for missing metadata or unknown types.
//    No geometry fitting occurs on this path.
//
// 2. roadToV2Legacy() — Legacy compatibility path
//    Preserves the rendered geometry without attempting to recover
//    the original authoring primitive. Corner points → LineSegment,
//    handle points → BezierSegment. No warnings (this is expected
//    behavior). All segments are marked as legacy in the report.
//    The objective is: produce the same centerline, not recover
//    the original design intent.
//
// Phase history:
//   1.8.3a: Infrastructure + LineSegment only
//   1.8.3b: Bezier/Arc/Spiral exact reconstruction from metadata
//   1.8.3c: Legacy compatibility reconstruction
//   1.8.3d: Update creation tools to emit SegmentMetadata + serialization
//   1.8.4:  Golden parity validation + formatVersion auto-dispatch
//
// formatVersion compatibility table:
//   1 = Legacy ControlPoint[] only (no SegmentMetadata)
//       → roadToV2Auto() dispatches to roadToV2Legacy()
//   2 = ControlPoint[] + SegmentMetadata
//       → roadToV2Auto() dispatches to roadToV2() (exact path)
//   Future versions reserved.
//
// The legacy Road path remains completely untouched.

#include "road.hpp"
#include "road_v2.hpp"
#include <string>
#include <vector>

namespace geo {

// ─── ReconstructionMode ────────────────────────────────────
//
// Describes how a segment was reconstructed during adaptation.
//
//   Exact:          Reconstructed from metadata or handles.
//                   No precision loss.
//   LegacyGeometry: Preserved from sampled ControlPoints as
//                   Line/Bezier segments. No fitting, no heuristics.
//                   The geometry is identical to the legacy centerline
//                   but the original authoring primitive (arc, spiral)
//                   is lost.
//   Unsupported:    Could not be converted. Fallback applied.
//
enum class ReconstructionMode {
    Exact,
    LegacyGeometry,
    Unsupported
};

// ─── AdapterReport: diagnostics from roadToV2 ──────────────
//
// Tracks how each segment was reconstructed. Tests can inspect
// `legacySegmentIndices` and `unsupportedSegmentIndices` to verify
// exactly where compatibility mode was used.
//
struct AdapterReport {
    // Segment type counts
    int lineSegments = 0;
    int bezierSegments = 0;
    int arcSegments = 0;
    int spiralSegments = 0;

    // Reconstruction quality counts
    int exactSegments = 0;
    int legacySegments = 0;
    int unsupportedSegments = 0;

    // Whether the entire road was reconstructed exactly
    bool exact = true;

    // Indices of segments that used legacy compatibility mode
    std::vector<int> legacySegmentIndices;

    // Indices of segments that were unsupported
    std::vector<int> unsupportedSegmentIndices;

    // Warnings (only from the exact path — legacy path doesn't warn)
    std::vector<std::string> warnings;

    int totalSegments() const {
        return lineSegments + bezierSegments + arcSegments + spiralSegments;
    }
};

// ═══════════════════════════════════════════════════════════
// roadToV2 — Exact reconstruction path
// ═══════════════════════════════════════════════════════════
//
// Converts a legacy Road to RoadV2 using exact reconstruction
// whenever metadata or handles are available. Emits warnings for
// missing metadata or unknown types. No geometry fitting.
//
// Conversion rules:
//   1. SegmentMetadata kind=Arc    → ArcSegment (exact)
//   2. SegmentMetadata kind=Spiral → SpiralSegment (exact)
//   3. SegmentMetadata kind=Line   → LineSegment (exact)
//   4. SegmentMetadata kind=Bezier + handles → BezierSegment (exact)
//   5. Handles present (no metadata) → BezierSegment (exact)
//   6. No handles, no metadata       → LineSegment (exact)
//   7. Bezier metadata without handles → warning + LineSegment (unsupported)
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

    // ─── Whole-curve metadata (Arc/Spiral) ────────────────
    // When the first CP has Arc or Spiral metadata, it describes the
    // ENTIRE curve. The remaining CPs are sampled points for editing
    // and are not converted to individual segments.
    if (legacy.points[0].segmentMeta.has_value()) {
        const auto& meta = *legacy.points[0].segmentMeta;
        const auto& p0 = legacy.points[0];

        if (meta.kind == SegmentKind::Arc) {
            v2.addSegment<ArcSegment>(
                p0.position, meta.startHeading,
                meta.curvature, meta.arcLength
            );
            report.arcSegments++;
            report.exactSegments++;
            return v2;
        }

        if (meta.kind == SegmentKind::Spiral) {
            v2.addSegment<SpiralSegment>(
                p0.position, meta.startHeading,
                meta.curvatureStart, meta.curvatureEnd,
                meta.segmentLength
            );
            report.spiralSegments++;
            report.exactSegments++;
            return v2;
        }
        // Line and Bezier metadata fall through to per-segment loop
    }

    // ─── Convert each pair of consecutive control points ───
    v2.reserveSegments(legacy.points.size() - 1);

    for (size_t i = 0; i < legacy.points.size() - 1; i++) {
        const auto& p0 = legacy.points[i];
        const auto& p1 = legacy.points[i + 1];
        bool isBezier = p0.hasHandleOut || p1.hasHandleIn;
        bool hasMeta = p0.segmentMeta.has_value();
        int segIdx = static_cast<int>(i);

        if (hasMeta) {
            const auto& meta = *p0.segmentMeta;

            switch (meta.kind) {
            case SegmentKind::Arc:
                v2.addSegment<ArcSegment>(
                    p0.position, meta.startHeading,
                    meta.curvature, meta.arcLength
                );
                report.arcSegments++;
                report.exactSegments++;
                break;

            case SegmentKind::Spiral:
                v2.addSegment<SpiralSegment>(
                    p0.position, meta.startHeading,
                    meta.curvatureStart, meta.curvatureEnd,
                    meta.segmentLength
                );
                report.spiralSegments++;
                report.exactSegments++;
                break;

            case SegmentKind::Line:
                v2.addSegment<LineSegment>(p0.position, p1.position);
                report.lineSegments++;
                report.exactSegments++;
                break;

            case SegmentKind::Bezier:
                if (isBezier) {
                    Point2D cp0 = p0.position;
                    Point2D cp1 = p0.position + p0.handleOut;
                    Point2D cp2 = p1.position + p1.handleIn;
                    Point2D cp3 = p1.position;
                    v2.addSegment<BezierSegment>(cp0, cp1, cp2, cp3);
                    report.bezierSegments++;
                    report.exactSegments++;
                } else {
                    report.warnings.push_back(
                        "Segment " + std::to_string(i) +
                        ": metadata kind=Bezier but no handles present — falling back to LineSegment");
                    v2.addSegment<LineSegment>(p0.position, p1.position);
                    report.lineSegments++;
                    report.unsupportedSegments++;
                    report.unsupportedSegmentIndices.push_back(segIdx);
                    report.exact = false;
                }
                break;

            default:
                // Corrupted metadata (invalid enum value)
                report.warnings.push_back(
                    "Segment " + std::to_string(i) +
                    ": invalid metadata kind — falling back to LineSegment");
                v2.addSegment<LineSegment>(p0.position, p1.position);
                report.lineSegments++;
                report.unsupportedSegments++;
                report.unsupportedSegmentIndices.push_back(segIdx);
                report.exact = false;
                break;
            }

        } else if (isBezier) {
            // Bezier: exact reconstruction from handles
            Point2D cp0 = p0.position;
            Point2D cp1 = p0.position + p0.handleOut;
            Point2D cp2 = p1.position + p1.handleIn;
            Point2D cp3 = p1.position;
            v2.addSegment<BezierSegment>(cp0, cp1, cp2, cp3);
            report.bezierSegments++;
            report.exactSegments++;

        } else {
            // Straight line (no handles, no metadata)
            v2.addSegment<LineSegment>(p0.position, p1.position);
            report.lineSegments++;
            report.exactSegments++;
        }
    }

    return v2;
}

// ─── Convenience overload (no report) ──────────────────────
inline RoadV2 roadToV2(const Road& legacy) {
    AdapterReport unused;
    return roadToV2(legacy, unused);
}

// ═══════════════════════════════════════════════════════════
// roadToV2Legacy — Legacy compatibility path
// ═══════════════════════════════════════════════════════════
//
// Preserves the rendered geometry without attempting to recover
// the original authoring primitive. This is the correct path for
// roads created by legacy tools that sampled arcs/clothoids into
// ControlPoints without storing construction parameters.
//
// Conversion rules:
//   1. Handles present → BezierSegment (preserves exact curve)
//   2. No handles      → LineSegment (preserves sampled polyline)
//
// No warnings are emitted — legacy compatibility is expected behavior.
// All segments are marked as LegacyGeometry in the report.
//
// The resulting centerline is geometrically identical to what the
// legacy engine would produce from the same ControlPoints.
//
inline RoadV2 roadToV2Legacy(const Road& legacy, AdapterReport& report) {
    RoadV2 v2;
    report = AdapterReport{};
    report.exact = false;  // Legacy path is never "exact"

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
        report.exact = true;  // Empty road is trivially exact
        return v2;
    }

    // ─── Single point (degenerate) ───
    if (legacy.points.size() == 1) {
        report.exact = true;
        return v2;
    }

    // ─── Convert each pair of consecutive control points ───
    v2.reserveSegments(legacy.points.size() - 1);

    for (size_t i = 0; i < legacy.points.size() - 1; i++) {
        const auto& p0 = legacy.points[i];
        const auto& p1 = legacy.points[i + 1];
        bool isBezier = p0.hasHandleOut || p1.hasHandleIn;
        int segIdx = static_cast<int>(i);

        if (isBezier) {
            // Preserve bezier curve exactly from handles
            Point2D cp0 = p0.position;
            Point2D cp1 = p0.position + p0.handleOut;
            Point2D cp2 = p1.position + p1.handleIn;
            Point2D cp3 = p1.position;
            v2.addSegment<BezierSegment>(cp0, cp1, cp2, cp3);
            report.bezierSegments++;
        } else {
            // Preserve sampled polyline as line segments
            v2.addSegment<LineSegment>(p0.position, p1.position);
            report.lineSegments++;
        }

        report.legacySegments++;
        report.legacySegmentIndices.push_back(segIdx);
    }

    return v2;
}

// ─── Convenience overload (no report) ──────────────────────
inline RoadV2 roadToV2Legacy(const Road& legacy) {
    AdapterReport unused;
    return roadToV2Legacy(legacy, unused);
}

// ═══════════════════════════════════════════════════════════
// roadToV2Auto — Format-version-aware auto-dispatch
// ═══════════════════════════════════════════════════════════
//
// Automatically selects the correct adapter path based on
// Road::formatVersion:
//   formatVersion >= 2 → roadToV2() (exact path)
//   formatVersion < 2  → roadToV2Legacy() (legacy compatibility)
//
// This removes the need for callers to decide which adapter to
// invoke. The formatVersion is set by the creation tools and
// preserved through serialization.
//
// For explicit control, callers can still invoke roadToV2() or
// roadToV2Legacy() directly.
//
inline RoadV2 roadToV2Auto(const Road& legacy, AdapterReport& report) {
    if (legacy.formatVersion >= 2) {
        return roadToV2(legacy, report);
    }
    return roadToV2Legacy(legacy, report);
}

// ─── Convenience overload (no report) ──────────────────────
inline RoadV2 roadToV2Auto(const Road& legacy) {
    AdapterReport unused;
    return roadToV2Auto(legacy, unused);
}

} // namespace geo
