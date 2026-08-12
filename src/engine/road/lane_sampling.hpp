#pragma once

// ═══════════════════════════════════════════════════════════
// Lane Sampling — Phase 2.4: Lane Boundary & Centerline Generation
// ═══════════════════════════════════════════════════════════
//
// @file lane_sampling.hpp
// @brief Sampling utilities: convert lane evaluation (2.3) into
//        polylines with adaptive refinement.
//
// @section Responsibility
// This file is a SAMPLING LAYER. It loops over s-positions and
// calls evaluateLaneCenter/evaluateLaneBoundary from lane_geometry.hpp
// to produce polylines. It does NOT:
//   - Generate meshes (that's 2.7)
//   - Generate lane markings (that's 2.6)
//   - Render anything
//
// @section Architecture
//
//   lane_engine.hpp    = Data model
//   lane_geometry.hpp  = Evaluation (single point)
//   lane_sampling.hpp  = Sampling (polyline generation)
//
// @section Adaptive Sampling
// Uses curvature-based refinement: more samples in high-curvature
// regions (arcs, spirals, bezier curves), fewer in straight lines.
// Reuses the same philosophy as GeometrySegment::sampleAdaptive().
//
// The algorithm:
//   1. Start with endpoints (s=0, s=totalLength)
//   2. For each pair of adjacent samples, compute the midpoint of
//      the chord and the actual lane point at the midpoint s
//   3. If the distance exceeds maxError, subdivide and recurse
//   4. Stop when error < maxError or maxSamples reached
//
// @section API Freeze
// NOT YET FROZEN. Will be frozen at Phase 2 Complete.

#include "geometry.hpp"
#include "lane_geometry.hpp"
#include "road_v2.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace geo {

// ═══════════════════════════════════════════════════════════
// SamplePoint — A single sampled point on a lane
// ═══════════════════════════════════════════════════════════
//
// Contains world-space position plus metadata for reuse:
//   - s: arc-length along reference line (for UV mapping, OpenDRIVE)
//   - heading: heading angle (for lane markings, debug overlays)
//   - laneOffset: lateral offset (for mesh generation)
//
struct SamplePoint {
    Point2D position;       // world-space (x, y)
    double s;               // arc-length along reference line
    double heading;         // heading angle in radians
    double laneOffset;      // lateral offset from reference line (right-positive)
};

// ═══════════════════════════════════════════════════════════
// LanePolyline — A sampled polyline representing a lane feature
// ═══════════════════════════════════════════════════════════
//
struct LanePolyline {
    int laneId = 0;                 // lane ID this polyline belongs to
    bool isBoundary = false;        // true = boundary edge, false = centerline
    bool isOuter = false;           // for boundaries: outer (true) or inner (false)
    std::vector<SamplePoint> points; // sampled points

    // Convenience: extract just the positions
    std::vector<Point2D> positions() const {
        std::vector<Point2D> result;
        result.reserve(points.size());
        for (const auto& p : points) {
            result.push_back(p.position);
        }
        return result;
    }

    // Number of sample points
    int numPoints() const { return static_cast<int>(points.size()); }

    // Total polyline length (sum of segment lengths)
    double length() const {
        if (points.size() < 2) return 0.0;
        double total = 0.0;
        for (size_t i = 1; i < points.size(); i++) {
            double dx = points[i].position.x - points[i - 1].position.x;
            double dy = points[i].position.y - points[i - 1].position.y;
            total += std::hypot(dx, dy);
        }
        return total;
    }
};

// ═══════════════════════════════════════════════════════════
// Adaptive sampling parameters
// ═══════════════════════════════════════════════════════════
//
struct SamplingParams {
    double maxError = 0.25;         // chord error tolerance (meters)
    int minSamples = 2;             // minimum samples per segment
    int maxSamples = 10000;         // maximum total samples
    double maxSpacing = 10.0;       // maximum spacing between samples (meters)

    // Default: adaptive with 0.25m tolerance (matches geometry kernel)
    SamplingParams() = default;
};

// ═══════════════════════════════════════════════════════════
// Internal: adaptive subdivision helper
// ═══════════════════════════════════════════════════════════
//
// Recursively subdivides [sLo, sHi] until chord error < maxError.
// Appends results to `out` (excluding sLo which is already in `out`).
//
namespace detail {

inline void adaptiveSubdivide(
    std::vector<SamplePoint>& out,
    const std::function<LanePoint(double)>& eval,
    double sLo, double sHi,
    const Point2D& pLo, const Point2D& pHi,
    double maxError, int depth, int maxDepth,
    int& totalSamples, int maxSamples
) {
    if (depth >= maxDepth || totalSamples >= maxSamples) {
        SamplePoint sp;
        LanePoint lp = eval(sHi);
        sp.position = lp.position;
        sp.s = sHi;
        sp.heading = lp.heading;
        sp.laneOffset = lp.laneOffset;
        out.push_back(sp);
        totalSamples++;
        return;
    }

    double sMid = (sLo + sHi) / 2.0;

    // Check max spacing constraint
    if (sHi - sLo >= 10.0) {
        // Force subdivision for spacing
        LanePoint lpMid = eval(sMid);
        Point2D pMid = lpMid.position;

        adaptiveSubdivide(out, eval, sLo, sMid, pLo, pMid,
                          maxError, depth + 1, maxDepth, totalSamples, maxSamples);
        adaptiveSubdivide(out, eval, sMid, sHi, pMid, pHi,
                          maxError, depth + 1, maxDepth, totalSamples, maxSamples);
        return;
    }

    // Compute chord midpoint and actual curve point
    Point2D chordMid = {(pLo.x + pHi.x) / 2.0, (pLo.y + pHi.y) / 2.0};
    LanePoint lpMid = eval(sMid);
    Point2D pMid = lpMid.position;

    double dx = pMid.x - chordMid.x;
    double dy = pMid.y - chordMid.y;
    double error = std::hypot(dx, dy);

    if (error < maxError) {
        // Chord is close enough — just add the endpoint
        SamplePoint sp;
        sp.position = lpMid.position;
        sp.s = sMid;
        sp.heading = lpMid.heading;
        sp.laneOffset = lpMid.laneOffset;
        // Actually we just need the endpoint
        LanePoint lpEnd = eval(sHi);
        sp.position = lpEnd.position;
        sp.s = sHi;
        sp.heading = lpEnd.heading;
        sp.laneOffset = lpEnd.laneOffset;
        out.push_back(sp);
        totalSamples++;
    } else {
        // Subdivide
        adaptiveSubdivide(out, eval, sLo, sMid, pLo, pMid,
                          maxError, depth + 1, maxDepth, totalSamples, maxSamples);
        adaptiveSubdivide(out, eval, sMid, sHi, pMid, pHi,
                          maxError, depth + 1, maxDepth, totalSamples, maxSamples);
    }
}

} // namespace detail

// ═══════════════════════════════════════════════════════════
// sampleLaneCenter — Sample a lane centerline
// ═══════════════════════════════════════════════════════════
//
// Produces a polyline of the lane center from s=0 to s=totalLength.
// Uses adaptive sampling: more points in high-curvature regions.
//
// @param road  The road to sample
// @param laneId  Lane ID to sample (0=center, negative=left, positive=right)
// @param params  Sampling parameters (error tolerance, min/max samples)
// @return LanePolyline with adaptively sampled points
//
inline LanePolyline sampleLaneCenter(
    const RoadV2& road,
    int laneId,
    const SamplingParams& params = {}
) {
    LanePolyline result;
    result.laneId = laneId;
    result.isBoundary = false;
    result.isOuter = false;

    double totalLen = road.totalLength();
    if (totalLen <= 0.0) return result;

    // Evaluation function
    auto eval = [&](double s) -> LanePoint {
        return evaluateLaneCenter(road, laneId, s);
    };

    // Start point
    LanePoint lp0 = eval(0.0);
    SamplePoint sp0;
    sp0.position = lp0.position;
    sp0.s = 0.0;
    sp0.heading = lp0.heading;
    sp0.laneOffset = lp0.laneOffset;
    result.points.push_back(sp0);

    // End point
    LanePoint lpEnd = eval(totalLen);

    // Adaptive subdivision
    int totalSamples = 1;
    detail::adaptiveSubdivide(
        result.points, eval,
        0.0, totalLen,
        lp0.position, lpEnd.position,
        params.maxError, 0, 20,
        totalSamples, params.maxSamples
    );

    return result;
}

// ═══════════════════════════════════════════════════════════
// sampleLaneBoundary — Sample a lane boundary edge
// ═══════════════════════════════════════════════════════════
//
// Produces a polyline of a lane boundary (inner or outer edge).
// Uses adaptive sampling.
//
// @param road  The road to sample
// @param laneId  Lane ID whose boundary to sample
// @param outer  If true: outer edge (farther from center); false: inner edge
// @param params  Sampling parameters
// @return LanePolyline with adaptively sampled points
//
inline LanePolyline sampleLaneBoundary(
    const RoadV2& road,
    int laneId,
    bool outer,
    const SamplingParams& params = {}
) {
    LanePolyline result;
    result.laneId = laneId;
    result.isBoundary = true;
    result.isOuter = outer;

    double totalLen = road.totalLength();
    if (totalLen <= 0.0) return result;

    auto eval = [&](double s) -> LanePoint {
        return evaluateLaneBoundary(road, laneId, outer, s);
    };

    LanePoint lp0 = eval(0.0);
    SamplePoint sp0;
    sp0.position = lp0.position;
    sp0.s = 0.0;
    sp0.heading = lp0.heading;
    sp0.laneOffset = lp0.laneOffset;
    result.points.push_back(sp0);

    LanePoint lpEnd = eval(totalLen);

    int totalSamples = 1;
    detail::adaptiveSubdivide(
        result.points, eval,
        0.0, totalLen,
        lp0.position, lpEnd.position,
        params.maxError, 0, 20,
        totalSamples, params.maxSamples
    );

    return result;
}

// ═══════════════════════════════════════════════════════════
// sampleAllBoundaries — Sample all lane boundaries for a road
// ═══════════════════════════════════════════════════════════
//
// Samples all lane boundaries (both inner and outer edges for
// every lane). Useful for rendering lane lines and mesh generation.
//
// For a 2-lane road (lanes -1, 0, +1):
//   - Lane -1 inner (boundary with center)
//   - Lane -1 outer (boundary with edge of road)
//   - Lane 0 inner (= center line, offset=0)
//   - Lane 0 outer (= center line, offset=0)
//   - Lane +1 inner (boundary with center)
//   - Lane +1 outer (boundary with edge of road)
//
// Note: center lane (id=0) inner and outer are both at offset=0.
//
inline std::vector<LanePolyline> sampleAllBoundaries(
    const RoadV2& road,
    const SamplingParams& params = {}
) {
    std::vector<LanePolyline> result;

    // Get lane configuration from the first lane section (or legacy)
    const LaneSection* ls = road.numLaneSections() > 0
        ? &road.laneSection(0)
        : &road.legacyLaneSection();

    if (!ls) return result;

    // Sample boundaries for each lane
    for (const auto& lane : ls->lanes()) {
        // Inner boundary (edge closest to center)
        result.push_back(sampleLaneBoundary(road, lane.id, false, params));
        // Outer boundary (edge farthest from center)
        result.push_back(sampleLaneBoundary(road, lane.id, true, params));
    }

    return result;
}

// ═══════════════════════════════════════════════════════════
// sampleAllCenterlines — Sample all lane centerlines
// ═══════════════════════════════════════════════════════════
//
inline std::vector<LanePolyline> sampleAllCenterlines(
    const RoadV2& road,
    const SamplingParams& params = {}
) {
    std::vector<LanePolyline> result;

    const LaneSection* ls = road.numLaneSections() > 0
        ? &road.laneSection(0)
        : &road.legacyLaneSection();

    if (!ls) return result;

    for (const auto& lane : ls->lanes()) {
        result.push_back(sampleLaneCenter(road, lane.id, params));
    }

    return result;
}

} // namespace geo
