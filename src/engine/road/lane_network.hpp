#pragma once

// ═══════════════════════════════════════════════════════════
// Lane Network — Phase 2.5: Persistent Lane Representation
// ═══════════════════════════════════════════════════════════
//
// @file lane_network.hpp
// @brief Canonical lane network: the persistent representation
//        that all downstream subsystems consume.
//
// @section Architecture
//
//   lane_engine.hpp   = Data model (Lane, LaneSection, Polynomial3)
//   lane_geometry.hpp = Evaluation (single point, world-space)
//   lane_sampling.hpp = Sampling (adaptive polyline generation)
//   lane_network.hpp  = Persistent lane representation (THIS FILE)
//
// @section Responsibility
// LaneNetwork is generated ONCE from a RoadV2 and then consumed by:
//   - Lane markings (2.6)
//   - Mesh generation (2.7)
//   - Road graph / AI (Phase 3)
//   - OpenDRIVE export
//   - Debug overlays
//
// Without this layer, each subsystem would re-sample lanes independently,
// duplicating work. LaneNetwork is the single source of truth for lane
// geometry after sampling.
//
// @section Multi-Section Handling
// For roads with multiple LaneSections (e.g., 2→4 lane transition),
// LaneNetwork generates separate centerlines/boundaries per section.
// Boundaries that exist in both sections are split at the section boundary
// to maintain clean topology.
//
// @section API Freeze
// NOT YET FROZEN. Will be frozen at Phase 2 Complete.

#include "geometry.hpp"
#include "lane_engine.hpp"
#include "lane_sampling.hpp"
#include "road_v2.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace geo {

// ═══════════════════════════════════════════════════════════
// LaneCenterline — Canonical centerline for a single lane
// ═══════════════════════════════════════════════════════════
//
// Represents the driving center of a lane, sampled adaptively.
// Contains rich metadata for downstream consumers.
//
struct LaneCenterline {
    int laneId = 0;                         // 0=center, negative=left, positive=right
    LaneType type = LaneType::Driving;      // lane type
    double startS = 0.0;                    // s-position where this centerline starts
    double endS = 0.0;                      // s-position where this centerline ends
    double length = 0.0;                    // total centerline length (meters)
    std::vector<SamplePoint> samples;       // adaptively sampled points

    // Convenience accessors
    int numSamples() const { return static_cast<int>(samples.size()); }
    std::vector<Point2D> positions() const {
        std::vector<Point2D> result;
        result.reserve(samples.size());
        for (const auto& s : samples) result.push_back(s.position);
        return result;
    }

    // Whether this lane is drivable
    bool isDrivable() const {
        return type == LaneType::Driving ||
               type == LaneType::Acceleration ||
               type == LaneType::Deceleration ||
               type == LaneType::Bus ||
               type == LaneType::Taxi ||
               type == LaneType::HOV;
    }

    // Whether this is the center lane
    bool isCenter() const { return laneId == 0; }
    bool isLeft() const { return laneId < 0; }
    bool isRight() const { return laneId > 0; }
};

// ═══════════════════════════════════════════════════════════
// LaneBoundary — Canonical boundary between two lanes (or lane edge)
// ═══════════════════════════════════════════════════════════
//
// Represents an edge between two adjacent lanes, or the outer edge
// of the road. Sampled adaptively.
//
struct LaneBoundary {
    int innerLaneId = 0;                    // lane on the center side of this boundary
    int outerLaneId = 0;                    // lane on the outer side (0 if road edge)
    bool isRoadEdge = false;                // true if this is the outer edge of the road
    double startS = 0.0;                    // s-position where this boundary starts
    double endS = 0.0;                      // s-position where this boundary ends
    double length = 0.0;                    // total boundary length (meters)
    LaneRoadMarkType markType = LaneRoadMarkType::None;  // marking on this boundary
    std::string markColor = "white";        // marking color
    double markWidth = 0.15;                // marking width
    std::vector<SamplePoint> samples;       // adaptively sampled points

    // Convenience accessors
    int numSamples() const { return static_cast<int>(samples.size()); }
    std::vector<Point2D> positions() const {
        std::vector<Point2D> result;
        result.reserve(samples.size());
        for (const auto& s : samples) result.push_back(s.position);
        return result;
    }

    // Human-readable identifier for debugging
    std::string id() const {
        return "boundary(" + std::to_string(innerLaneId) + "," +
               std::to_string(outerLaneId) + ")";
    }
};

// ═══════════════════════════════════════════════════════════
// LaneNetwork — The canonical lane representation
// ═══════════════════════════════════════════════════════════
//
// Generated once from a RoadV2. Contains all centerlines and
// boundaries for the entire road. This is the single source of
// truth that downstream subsystems consume.
//
struct LaneNetwork {
    std::string roadId;                     // ID of the source road
    double totalLength = 0.0;               // total road length
    int numLaneSections = 0;                // number of lane sections used

    std::vector<LaneCenterline> centerlines;  // one per lane per section
    std::vector<LaneBoundary> boundaries;     // one per lane edge per section

    // ─── Queries ───

    int numCenterlines() const { return static_cast<int>(centerlines.size()); }
    int numBoundaries() const { return static_cast<int>(boundaries.size()); }

    // Find centerline by lane ID (returns first match — for single-section roads)
    const LaneCenterline* findCenterline(int laneId) const {
        for (const auto& c : centerlines) {
            if (c.laneId == laneId) return &c;
        }
        return nullptr;
    }

    // Find all centerlines for a lane ID (multi-section roads may have multiple)
    std::vector<const LaneCenterline*> findCenterlines(int laneId) const {
        std::vector<const LaneCenterline*> result;
        for (const auto& c : centerlines) {
            if (c.laneId == laneId) result.push_back(&c);
        }
        return result;
    }

    // Find boundaries between two specific lanes
    std::vector<const LaneBoundary*> findBoundaries(int innerId, int outerId) const {
        std::vector<const LaneBoundary*> result;
        for (const auto& b : boundaries) {
            if (b.innerLaneId == innerId && b.outerLaneId == outerId)
                result.push_back(&b);
        }
        return result;
    }

    // Find road edges (outer boundaries)
    std::vector<const LaneBoundary*> roadEdges() const {
        std::vector<const LaneBoundary*> result;
        for (const auto& b : boundaries) {
            if (b.isRoadEdge) result.push_back(&b);
        }
        return result;
    }

    // Find center line (boundaries adjacent to the center lane, id=0)
    // These are the boundaries between center and the first left/right lane.
    std::vector<const LaneBoundary*> centerLines() const {
        std::vector<const LaneBoundary*> result;
        for (const auto& b : boundaries) {
            // Center line: boundary where inner lane is 0 (center)
            // and outer lane is ±1 (first driving lane)
            if (b.innerLaneId == 0 && !b.isRoadEdge) {
                result.push_back(&b);
            }
        }
        return result;
    }

    // Total number of drivable lanes (across all sections, max)
    int maxDrivableLanes() const {
        int maxLanes = 0;
        for (const auto& c : centerlines) {
            if (c.isDrivable()) maxLanes++;
        }
        return maxLanes;
    }
};

// ═══════════════════════════════════════════════════════════
// generateLaneNetwork — Build LaneNetwork from RoadV2
// ═══════════════════════════════════════════════════════════
//
// Generates the canonical lane representation by sampling all
// centerlines and boundaries. Handles:
//   - Multi-section roads (separate centerlines per section)
//   - Legacy roads (synthesized from width/laneCount)
//   - Adaptive sampling (curvature-based)
//
// @param road  The road to generate the network from
// @param params  Sampling parameters (error tolerance, spacing)
// @return LaneNetwork containing all centerlines and boundaries
//
inline LaneNetwork generateLaneNetwork(
    const RoadV2& road,
    const SamplingParams& params = {}
) {
    LaneNetwork network;
    network.roadId = road.id;
    network.totalLength = road.totalLength();
    network.numLaneSections = std::max(1, road.numLaneSections());

    if (network.totalLength <= 0.0) return network;

    // Determine lane sections to process
    // If no explicit sections, use legacy synthesis (single section at s=0)
    std::vector<const LaneSection*> sections;
    if (road.numLaneSections() > 0) {
        for (int i = 0; i < road.numLaneSections(); i++) {
            sections.push_back(&road.laneSection(i));
        }
    } else {
        sections.push_back(&road.legacyLaneSection());
    }

    // Compute section boundaries (s-ranges)
    // Section i covers [startS_i, startS_{i+1}) or [startS_i, totalLength] for last
    std::vector<double> sectionStarts;
    std::vector<double> sectionEnds;
    for (size_t i = 0; i < sections.size(); i++) {
        double startS = sections[i]->startS;
        double endS = (i + 1 < sections.size()) ? sections[i + 1]->startS
                                                 : network.totalLength;
        sectionStarts.push_back(startS);
        sectionEnds.push_back(endS);
    }

    // ─── Generate centerlines ───
    // For each section, for each lane, sample the centerline
    for (size_t secIdx = 0; secIdx < sections.size(); secIdx++) {
        const LaneSection* ls = sections[secIdx];
        double sStart = sectionStarts[secIdx];
        double sEnd = sectionEnds[secIdx];

        for (const auto& lane : ls->lanes()) {
            LaneCenterline cl;
            cl.laneId = lane.id;
            cl.type = lane.type;
            cl.startS = sStart;
            cl.endS = sEnd;

            // Sample centerline within this section's s-range
            // We sample the full road but clip to [sStart, sEnd]
            // For simplicity, sample the section's range directly
            auto eval = [&](double s) -> LanePoint {
                return evaluateLaneCenter(road, lane.id, s);
            };

            // Start point
            LanePoint lpStart = eval(sStart);
            SamplePoint spStart;
            spStart.position = lpStart.position;
            spStart.s = sStart;
            spStart.heading = lpStart.heading;
            spStart.laneOffset = lpStart.laneOffset;
            cl.samples.push_back(spStart);

            // End point
            LanePoint lpEnd = eval(sEnd);

            // Adaptive subdivision within [sStart, sEnd]
            int totalSamples = 1;
            detail::adaptiveSubdivide(
                cl.samples, eval,
                sStart, sEnd,
                lpStart.position, lpEnd.position,
                params.maxError, 0, 20,
                totalSamples, params.maxSamples
            );

            // Compute length
            cl.length = 0.0;
            for (size_t i = 1; i < cl.samples.size(); i++) {
                double dx = cl.samples[i].position.x - cl.samples[i - 1].position.x;
                double dy = cl.samples[i].position.y - cl.samples[i - 1].position.y;
                cl.length += std::hypot(dx, dy);
            }

            network.centerlines.push_back(std::move(cl));
        }
    }

    // ─── Generate boundaries ───
    // For each section, generate boundaries between adjacent lanes
    for (size_t secIdx = 0; secIdx < sections.size(); secIdx++) {
        const LaneSection* ls = sections[secIdx];
        double sStart = sectionStarts[secIdx];
        double sEnd = sectionEnds[secIdx];

        // Get sorted lane IDs
        auto leftLanes = ls->leftLanes();    // sorted by |id|, innermost first
        auto rightLanes = ls->rightLanes();  // sorted by id, innermost first

        // Right side boundaries:
        //   boundary(0, 1) — between center and lane 1
        //   boundary(1, 2) — between lane 1 and 2
        //   ...
        //   boundary(N, 0) — outer edge (road edge) after last right lane
        {
            int prevId = 0;  // center
            for (const Lane* lane : rightLanes) {
                // Boundary between prevId and lane->id
                LaneBoundary b;
                b.innerLaneId = prevId;
                b.outerLaneId = lane->id;
                b.isRoadEdge = false;
                b.startS = sStart;
                b.endS = sEnd;

                // Sample: outer edge of prevId = inner edge of lane->id
                auto eval = [&](double s) -> LanePoint {
                    return evaluateLaneBoundary(road, prevId, true, s);
                };

                LanePoint lpStart = eval(sStart);
                SamplePoint spStart;
                spStart.position = lpStart.position;
                spStart.s = sStart;
                spStart.heading = lpStart.heading;
                spStart.laneOffset = lpStart.laneOffset;
                b.samples.push_back(spStart);

                LanePoint lpEnd = eval(sEnd);
                int totalSamples = 1;
                detail::adaptiveSubdivide(
                    b.samples, eval,
                    sStart, sEnd,
                    lpStart.position, lpEnd.position,
                    params.maxError, 0, 20,
                    totalSamples, params.maxSamples
                );

                b.length = 0.0;
                for (size_t i = 1; i < b.samples.size(); i++) {
                    double dx = b.samples[i].position.x - b.samples[i - 1].position.x;
                    double dy = b.samples[i].position.y - b.samples[i - 1].position.y;
                    b.length += std::hypot(dx, dy);
                }

                // Default marking: dashed white for inner boundaries
                if (prevId == 0) {
                    b.markType = LaneRoadMarkType::Dashed;
                    b.markColor = "yellow";
                } else {
                    b.markType = LaneRoadMarkType::Dashed;
                    b.markColor = "white";
                }

                network.boundaries.push_back(std::move(b));
                prevId = lane->id;
            }

            // Outer edge of last right lane (road edge)
            if (!rightLanes.empty()) {
                int lastId = rightLanes.back()->id;
                LaneBoundary b;
                b.innerLaneId = lastId;
                b.outerLaneId = 0;  // no lane beyond
                b.isRoadEdge = true;
                b.startS = sStart;
                b.endS = sEnd;

                auto eval = [&](double s) -> LanePoint {
                    return evaluateLaneBoundary(road, lastId, true, s);
                };

                LanePoint lpStart = eval(sStart);
                SamplePoint spStart;
                spStart.position = lpStart.position;
                spStart.s = sStart;
                spStart.heading = lpStart.heading;
                spStart.laneOffset = lpStart.laneOffset;
                b.samples.push_back(spStart);

                LanePoint lpEnd = eval(sEnd);
                int totalSamples = 1;
                detail::adaptiveSubdivide(
                    b.samples, eval,
                    sStart, sEnd,
                    lpStart.position, lpEnd.position,
                    params.maxError, 0, 20,
                    totalSamples, params.maxSamples
                );

                b.length = 0.0;
                for (size_t i = 1; i < b.samples.size(); i++) {
                    double dx = b.samples[i].position.x - b.samples[i - 1].position.x;
                    double dy = b.samples[i].position.y - b.samples[i - 1].position.y;
                    b.length += std::hypot(dx, dy);
                }

                // Road edge: solid white
                b.markType = LaneRoadMarkType::Solid;
                b.markColor = "white";

                network.boundaries.push_back(std::move(b));
            }
        }

        // Left side boundaries (mirror, negative IDs)
        {
            int prevId = 0;  // center
            for (const Lane* lane : leftLanes) {
                LaneBoundary b;
                b.innerLaneId = prevId;
                b.outerLaneId = lane->id;
                b.isRoadEdge = false;
                b.startS = sStart;
                b.endS = sEnd;

                auto eval = [&](double s) -> LanePoint {
                    return evaluateLaneBoundary(road, prevId, true, s);
                };

                LanePoint lpStart = eval(sStart);
                SamplePoint spStart;
                spStart.position = lpStart.position;
                spStart.s = sStart;
                spStart.heading = lpStart.heading;
                spStart.laneOffset = lpStart.laneOffset;
                b.samples.push_back(spStart);

                LanePoint lpEnd = eval(sEnd);
                int totalSamples = 1;
                detail::adaptiveSubdivide(
                    b.samples, eval,
                    sStart, sEnd,
                    lpStart.position, lpEnd.position,
                    params.maxError, 0, 20,
                    totalSamples, params.maxSamples
                );

                b.length = 0.0;
                for (size_t i = 1; i < b.samples.size(); i++) {
                    double dx = b.samples[i].position.x - b.samples[i - 1].position.x;
                    double dy = b.samples[i].position.y - b.samples[i - 1].position.y;
                    b.length += std::hypot(dx, dy);
                }

                if (prevId == 0) {
                    b.markType = LaneRoadMarkType::Dashed;
                    b.markColor = "yellow";
                } else {
                    b.markType = LaneRoadMarkType::Dashed;
                    b.markColor = "white";
                }

                network.boundaries.push_back(std::move(b));
                prevId = lane->id;
            }

            // Outer edge of last left lane (road edge)
            if (!leftLanes.empty()) {
                int lastId = leftLanes.back()->id;
                LaneBoundary b;
                b.innerLaneId = lastId;
                b.outerLaneId = 0;
                b.isRoadEdge = true;
                b.startS = sStart;
                b.endS = sEnd;

                auto eval = [&](double s) -> LanePoint {
                    return evaluateLaneBoundary(road, lastId, true, s);
                };

                LanePoint lpStart = eval(sStart);
                SamplePoint spStart;
                spStart.position = lpStart.position;
                spStart.s = sStart;
                spStart.heading = lpStart.heading;
                spStart.laneOffset = lpStart.laneOffset;
                b.samples.push_back(spStart);

                LanePoint lpEnd = eval(sEnd);
                int totalSamples = 1;
                detail::adaptiveSubdivide(
                    b.samples, eval,
                    sStart, sEnd,
                    lpStart.position, lpEnd.position,
                    params.maxError, 0, 20,
                    totalSamples, params.maxSamples
                );

                b.length = 0.0;
                for (size_t i = 1; i < b.samples.size(); i++) {
                    double dx = b.samples[i].position.x - b.samples[i - 1].position.x;
                    double dy = b.samples[i].position.y - b.samples[i - 1].position.y;
                    b.length += std::hypot(dx, dy);
                }

                b.markType = LaneRoadMarkType::Solid;
                b.markColor = "white";

                network.boundaries.push_back(std::move(b));
            }
        }
    }

    return network;
}

} // namespace geo
