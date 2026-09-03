#pragma once

// ═══════════════════════════════════════════════════════════
// Curvature Blending — Automatic spiral (clothoid) insertion
// ═══════════════════════════════════════════════════════════
//
// RoadBuilder-inspired "spiral and curvature blending" for
// road planning: automatically inserts clothoid transition
// spirals between segments with curvature discontinuities
// (line→arc, arc→line, arc→arc with different radii).
//
// This achieves G2 (curvature) continuity along the road,
// eliminating sudden curvature jumps that cause driver jerk
// and are disallowed by highway design standards.
//
// Algorithm:
//   1. Walk consecutive geometry segments.
//   2. At each boundary, compare end-curvature of segment A
//      with start-curvature of segment B.
//   3. If |Δκ| > threshold, insert a SpiralSegment that
//      transitions linearly from κ_A_end to κ_B_start.
//   4. Trim A and B by half the spiral length each (so the
//      spiral fits between them without moving endpoints).
//   5. The spiral length is chosen from the curvature change
//      and a design maximum rate of curvature change (jerk).
//
// All geometry is expressed in the existing geo:: geometry
// segment model (LineSegment, ArcSegment, SpiralSegment).

#include "geometry_segment.hpp"
#include "road_v2.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace geo {

// ─── Curvature blending parameters ──────────────────────────
struct CurvatureBlendingParams {
    // Minimum curvature discontinuity to trigger spiral insertion (1/m)
    double curvatureThreshold = 1e-6;

    // Maximum rate of curvature change (1/m²). Lower = gentler, longer spirals.
    // Typical highway design: 0.5–2.0 × 10⁻³
    double maxCurvatureRate = 1.0e-3;

    // Minimum spiral length (m). Below this, no spiral is inserted.
    double minSpiralLength = 5.0;

    // Maximum spiral length (m). Caps very long transitions.
    double maxSpiralLength = 200.0;

    // Fraction of the shorter adjacent segment to use as spiral length (0–0.5).
    // Ensures the spiral doesn't consume more than half of either segment.
    double maxSegmentFraction = 0.4;
};

// ─── Curvature blending result ──────────────────────────────
struct CurvatureBlendingResult {
    std::vector<std::unique_ptr<GeometrySegment>> segments;
    int spiralsInserted = 0;
    std::vector<std::string> warnings;
};

// ─── Blend curvatures in a road's geometry ──────────────────
// Returns a new sequence of segments with clothoid transitions
// inserted at curvature discontinuities. The input road is not
// modified; the caller can apply the result if desired.
inline CurvatureBlendingResult blendCurvatures(
    const RoadV2& road,
    const CurvatureBlendingParams& params = {})
{
    CurvatureBlendingResult result;
    const int n = road.numSegments();
    if (n == 0) return result;

    // Helper: compute spiral length from curvature change
    auto computeSpiralLength = [&](double dk, double availLen) -> double {
        if (std::abs(dk) < params.curvatureThreshold) return 0.0;
        // L = |dk| / maxCurvatureRate  (so curvature changes at most maxCurvatureRate per meter)
        double L = std::abs(dk) / params.maxCurvatureRate;
        L = std::clamp(L, params.minSpiralLength, params.maxSpiralLength);
        // Don't consume more than maxSegmentFraction of available length
        L = std::min(L, availLen * params.maxSegmentFraction);
        return L;
    };

    // Helper: trim a segment's end by a given length, returning the new endpoint
    auto trimSegmentEnd = [](GeometrySegment& seg, double trimLen) {
        const double newLen = seg.length() - trimLen;
        if (newLen <= EPSILON) return seg.startPoint();
        return seg.positionAt(newLen);
    };

    // Helper: trim a segment's start by a given length, returning the new start
    auto trimSegmentStart = [](GeometrySegment& seg, double trimLen) {
        if (trimLen <= EPSILON) return seg.startPoint();
        return seg.positionAt(trimLen);
    };

    for (int i = 0; i < n; ++i) {
        const auto& seg = road.segment(i);
        const double segLen = seg.length();
        const double segEndCurv = seg.curvatureDS(segLen);
        const double segStartCurv = seg.curvatureDS(0.0);

        // Get the start point/heading of this segment (possibly trimmed)
        Point2D segStart = seg.startPoint();
        double segStartHeading = seg.startHeading();

        // Check boundary with previous segment (already in result)
        if (!result.segments.empty()) {
            auto& prevSeg = *result.segments.back();
            const double prevEndCurv = prevSeg.curvatureDS(prevSeg.length());
            const double dk = segStartCurv - prevEndCurv;

            if (std::abs(dk) > params.curvatureThreshold) {
                // Need a spiral transition
                const double availLen = std::min(prevSeg.length(), segLen);
                const double spiralLen = computeSpiralLength(dk, availLen);

                if (spiralLen >= params.minSpiralLength &&
                    prevSeg.length() > spiralLen * 0.5 + EPSILON &&
                    segLen > spiralLen * 0.5 + EPSILON) {
                    // Trim previous segment end by spiralLen/2
                    const double trimPrev = spiralLen * 0.5;
                    const double prevNewLen = prevSeg.length() - trimPrev;
                    // Get the connection point and heading
                    const Point2D spiralStart = prevSeg.positionAt(prevNewLen);
                    const double spiralStartHeading = prevSeg.headingAt(prevNewLen);
                    const double spiralStartCurv = prevSeg.curvatureDS(prevNewLen);

                    // Trim current segment start by spiralLen/2
                    const double trimThis = spiralLen * 0.5;
                    const Point2D spiralEnd = seg.positionAt(trimThis);
                    const double spiralEndHeading = seg.headingAt(trimThis);
                    const double spiralEndCurv = seg.curvatureDS(trimThis);

                    // Rebuild previous segment with trimmed end
                    // (For LineSegment, just shorten the endpoint)
                    if (prevSeg.type() == GeometryType::Line) {
                        auto* lineSeg = static_cast<LineSegment*>(&prevSeg);
                        lineSeg->p1 = spiralStart;
                    } else if (prevSeg.type() == GeometryType::Arc) {
                        auto* arcSeg = static_cast<ArcSegment*>(&prevSeg);
                        arcSeg->arcLength_ = prevNewLen;
                    }

                    // Insert the spiral
                    auto spiral = std::make_unique<SpiralSegment>(
                        spiralStart, spiralStartHeading,
                        spiralStartCurv, spiralEndCurv, spiralLen);
                    result.segments.push_back(std::move(spiral));
                    result.spiralsInserted++;

                    // Adjust this segment's start to the spiral end
                    segStart = spiralEnd;
                    segStartHeading = spiralEndHeading;
                } else if (spiralLen > 0.0) {
                    result.warnings.push_back(
                        "Boundary " + std::to_string(i) +
                        ": spiral too short or segments too small for transition");
                }
            }
        }

        // Rebuild current segment with possibly trimmed start
        if (seg.type() == GeometryType::Line) {
            const auto& line = static_cast<const LineSegment&>(seg);
            // If start was trimmed, use new start; keep original end
            Point2D actualEnd = line.p1;
            // Check if we trimmed the start
            if (!result.segments.empty() &&
                result.segments.back()->type() == GeometryType::Spiral) {
                // Start was trimmed — use spiralEnd as new start
                result.segments.push_back(std::make_unique<LineSegment>(segStart, actualEnd));
            } else {
                result.segments.push_back(std::make_unique<LineSegment>(line));
            }
        } else if (seg.type() == GeometryType::Arc) {
            const auto& arc = static_cast<const ArcSegment&>(seg);
            if (!result.segments.empty() &&
                result.segments.back()->type() == GeometryType::Spiral) {
                // Start was trimmed — rebuild arc from new start
                const double trimThis = [&]() -> double {
                    // Find how much was trimmed from this segment
                    // by checking the spiral length
                    auto& spiral = *result.segments.back();
                    return spiral.length() * 0.5;
                }();
                const double newArcLen = arc.length() - trimThis;
                result.segments.push_back(std::make_unique<ArcSegment>(
                    segStart, segStartHeading, arc.curvature_, newArcLen));
            } else {
                result.segments.push_back(std::make_unique<ArcSegment>(arc));
            }
        } else if (seg.type() == GeometryType::Spiral) {
            const auto& spiral = static_cast<const SpiralSegment&>(seg);
            if (!result.segments.empty() &&
                result.segments.back()->type() == GeometryType::Spiral) {
                // Start was trimmed — rebuild spiral from new start
                const double trimThis = result.segments.back()->length() * 0.5;
                const double newLen = spiral.length() - trimThis;
                // Adjust curvature start to match the trimming point
                const double newK0 = spiral.curvatureDS(trimThis);
                result.segments.push_back(std::make_unique<SpiralSegment>(
                    segStart, segStartHeading, newK0, spiral.curvatureEnd_, newLen));
            } else {
                result.segments.push_back(std::make_unique<SpiralSegment>(spiral));
            }
        } else {
            // Unknown segment type — clone as-is
            result.segments.push_back(seg.clone());
        }
    }

    return result;
}

// ─── Apply curvature blending to a road in-place ────────────
// Returns the number of spirals inserted.
inline int applyCurvatureBlending(RoadV2& road,
                                   const CurvatureBlendingParams& params = {})
{
    auto result = blendCurvatures(road, params);
    if (result.spiralsInserted == 0) return 0;
    road.clearSegments();
    for (auto& seg : result.segments)
        road.addSegment(std::move(seg));
    return result.spiralsInserted;
}

// ─── Detect curvature discontinuities (diagnostic) ─────────
struct CurvatureDiscontinuity {
    int segmentIndex;       // boundary between segment[i] and segment[i+1]
    double curvatureBefore; // end curvature of segment[i]
    double curvatureAfter;  // start curvature of segment[i+1]
    double deltaK;          // curvatureAfter - curvatureBefore
};

inline std::vector<CurvatureDiscontinuity> detectCurvatureDiscontinuities(
    const RoadV2& road, double threshold = 1e-6)
{
    std::vector<CurvatureDiscontinuity> discontinuities;
    const int n = road.numSegments();
    for (int i = 0; i < n - 1; ++i) {
        const auto& a = road.segment(i);
        const auto& b = road.segment(i + 1);
        const double kEnd = a.curvatureDS(a.length());
        const double kStart = b.curvatureDS(0.0);
        const double dk = kStart - kEnd;
        if (std::abs(dk) > threshold) {
            discontinuities.push_back({i, kEnd, kStart, dk});
        }
    }
    return discontinuities;
}

} // namespace geo
