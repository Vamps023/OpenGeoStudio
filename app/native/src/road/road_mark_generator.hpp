#pragma once

// ═══════════════════════════════════════════════════════════
// Road Mark Generator — Phase 2.6: Semantic Road Markings
// ═══════════════════════════════════════════════════════════
//
// @file road_mark_generator.hpp
// @brief Generates semantic road marking descriptions from LaneNetwork.
//        No mesh, no rendering — only logical marking data.
//
// @section Architecture
//
//   lane_engine.hpp         = Data model
//   lane_geometry.hpp       = Evaluation
//   lane_sampling.hpp       = Sampling
//   lane_network.hpp        = Persistent lane network
//   road_mark_generator.hpp = Mark generation (THIS FILE)
//
// @section Responsibility
// Transforms LaneBoundary marking metadata into structured
// RoadMarkPolyline objects. Dashed lines are represented
// parametrically (DashPattern) rather than as thousands of tiny
// segments. This keeps the representation compact and lets the
// mesh generator (2.7) decide actual tessellation.
//
// @section One Source of Truth
// LaneBoundary already stores markType, markColor, markWidth.
// RoadMarkGenerator transforms these into RoadMarkPolyline without
// duplicating the source data. The LaneNetwork remains the single
// source of truth for lane geometry; RoadMarkNetwork is a derived
// view focused on markings.
//
// @section Style Library
// Default marking styles are defined in RoadMarkLibrary, mapping
// boundary roles (center, inner, edge) to marking types. This makes
// localization and future standards (MUTCD, European) easy to swap.
//
// @section API Freeze
// NOT YET FROZEN. Will be frozen at Phase 2 Complete.

#include "geometry.hpp"
#include "lane_engine.hpp"
#include "lane_network.hpp"
#include "lane_sampling.hpp"
#include <string>
#include <vector>

namespace geo {

// ═══════════════════════════════════════════════════════════
// DashPattern — Parametric representation of dashed lines
// ═══════════════════════════════════════════════════════════
//
// Instead of generating thousands of tiny line segments, dashed
// lines are represented as a pattern + the underlying polyline.
// The mesh generator (2.7) tessellates this into actual geometry.
//
// Standard patterns (MUTCD-style):
//   Broken yellow: 3m dash, 9m gap
//   Broken white:  3m dash, 9m gap
//   Dotted:        0.1m dash, 0.1m gap
//
struct DashPattern {
    double dashLength = 3.0;    // length of each dash (meters)
    double gapLength = 9.0;     // gap between dashes (meters)
    double phase = 0.0;         // offset from start (meters)

    // Total pattern period (dash + gap)
    double period() const { return dashLength + gapLength; }

    // Whether a given distance along the line falls in a dash
    bool isDashAt(double distance) const {
        double p = period();
        if (p <= 0.0) return true;  // degenerate: always dash
        double local = fmod(distance - phase, p);
        if (local < 0.0) local += p;
        return local < dashLength;
    }

    // Default: standard broken line (3m dash, 9m gap)
    DashPattern() = default;

    DashPattern(double dash, double gap, double phase_ = 0.0)
        : dashLength(dash), gapLength(gap), phase(phase_) {}
};

// ═══════════════════════════════════════════════════════════
// RoadMarkStyle — Style definition for a marking
// ═══════════════════════════════════════════════════════════
//
struct RoadMarkStyle {
    LaneRoadMarkType type = LaneRoadMarkType::Dashed;
    std::string color = "white";
    double width = 0.15;                // marking width in meters
    DashPattern dashPattern;            // only used if type is dashed

    // Whether this style is dashed (needs parametric dash representation)
    bool isDashed() const {
        return type == LaneRoadMarkType::Dashed ||
               type == LaneRoadMarkType::DashedDashed ||
               type == LaneRoadMarkType::Broken ||
               type == LaneRoadMarkType::BrokenBroken;
    }

    // Whether this is a solid line
    bool isSolid() const {
        return type == LaneRoadMarkType::Solid ||
               type == LaneRoadMarkType::SolidSolid;
    }

    // Whether this is a double line
    bool isDouble() const {
        return type == LaneRoadMarkType::SolidSolid ||
               type == LaneRoadMarkType::DashedDashed ||
               type == LaneRoadMarkType::SolidDashed ||
               type == LaneRoadMarkType::DashedSolid ||
               type == LaneRoadMarkType::BrokenBroken;
    }
};

// ═══════════════════════════════════════════════════════════
// RoadMarkPolyline — A single road marking
// ═══════════════════════════════════════════════════════════
//
// Represents one continuous marking along the road.
// For solid lines: samples contain the full polyline.
// For dashed lines: samples contain the centerline of the marking,
//   and dashPattern describes how to tessellate dashes.
//
struct RoadMarkPolyline {
    RoadMarkStyle style;                    // marking style
    int innerLaneId = 0;                    // lane on center side
    int outerLaneId = 0;                    // lane on outer side
    bool isRoadEdge = false;                // true if road edge marking
    bool isCenterLine = false;              // true if center line marking
    double startS = 0.0;                    // s-position where marking starts
    double endS = 0.0;                      // s-position where marking ends
    double length = 0.0;                    // total marking length (meters)
    std::vector<SamplePoint> samples;       // polyline samples

    // Convenience
    int numSamples() const { return static_cast<int>(samples.size()); }
    std::vector<Point2D> positions() const {
        std::vector<Point2D> result;
        result.reserve(samples.size());
        for (const auto& s : samples) result.push_back(s.position);
        return result;
    }

    // Human-readable identifier
    std::string id() const {
        return "mark(" + std::to_string(innerLaneId) + "," +
               std::to_string(outerLaneId) + "," + style.color + ")";
    }
};

// ═══════════════════════════════════════════════════════════
// RoadMarkNetwork — All markings for a road
// ═══════════════════════════════════════════════════════════
//
struct RoadMarkNetwork {
    std::string roadId;
    std::vector<RoadMarkPolyline> markings;

    int numMarkings() const { return static_cast<int>(markings.size()); }

    // Find markings by type
    std::vector<const RoadMarkPolyline*> findByType(LaneRoadMarkType type) const {
        std::vector<const RoadMarkPolyline*> result;
        for (const auto& m : markings) {
            if (m.style.type == type) result.push_back(&m);
        }
        return result;
    }

    // Find center line markings
    std::vector<const RoadMarkPolyline*> centerMarks() const {
        std::vector<const RoadMarkPolyline*> result;
        for (const auto& m : markings) {
            if (m.isCenterLine) result.push_back(&m);
        }
        return result;
    }

    // Find road edge markings
    std::vector<const RoadMarkPolyline*> edgeMarks() const {
        std::vector<const RoadMarkPolyline*> result;
        for (const auto& m : markings) {
            if (m.isRoadEdge) result.push_back(&m);
        }
        return result;
    }

    // Find dashed markings
    std::vector<const RoadMarkPolyline*> dashedMarks() const {
        std::vector<const RoadMarkPolyline*> result;
        for (const auto& m : markings) {
            if (m.style.isDashed()) result.push_back(&m);
        }
        return result;
    }

    // Find solid markings
    std::vector<const RoadMarkPolyline*> solidMarks() const {
        std::vector<const RoadMarkPolyline*> result;
        for (const auto& m : markings) {
            if (m.style.isSolid()) result.push_back(&m);
        }
        return result;
    }
};

// ═══════════════════════════════════════════════════════════
// RoadMarkLibrary — Default marking styles
// ═══════════════════════════════════════════════════════════
//
// Maps boundary roles to default marking styles.
// Can be customized for different standards (MUTCD, European, etc.)
//
struct RoadMarkLibrary {
    // Center line (between left and right lanes)
    RoadMarkStyle centerStyle;

    // Inner lane boundary (between two driving lanes on same side)
    RoadMarkStyle innerStyle;

    // Road edge (outer boundary)
    RoadMarkStyle edgeStyle;

    // Default: MUTCD-style
    //   Center: dashed yellow (broken yellow)
    //   Inner:  dashed white
    //   Edge:   solid white
    RoadMarkLibrary() {
        centerStyle.type = LaneRoadMarkType::Dashed;
        centerStyle.color = "yellow";
        centerStyle.width = 0.15;
        centerStyle.dashPattern = DashPattern(3.0, 9.0);

        innerStyle.type = LaneRoadMarkType::Dashed;
        innerStyle.color = "white";
        innerStyle.width = 0.12;
        innerStyle.dashPattern = DashPattern(3.0, 9.0);

        edgeStyle.type = LaneRoadMarkType::Solid;
        edgeStyle.color = "white";
        edgeStyle.width = 0.15;
    }

    // European-style defaults
    static RoadMarkLibrary european() {
        RoadMarkLibrary lib;
        lib.centerStyle.color = "white";
        lib.edgeStyle.color = "white";
        return lib;
    }

    // No markings (for testing)
    static RoadMarkLibrary none() {
        RoadMarkLibrary lib;
        lib.centerStyle.type = LaneRoadMarkType::None;
        lib.innerStyle.type = LaneRoadMarkType::None;
        lib.edgeStyle.type = LaneRoadMarkType::None;
        return lib;
    }
};

// ═══════════════════════════════════════════════════════════
// generateRoadMarks — Build RoadMarkNetwork from LaneNetwork
// ═══════════════════════════════════════════════════════════
//
// Transforms LaneBoundary marking metadata into RoadMarkPolyline
// objects. Uses the style library to determine marking appearance
// based on boundary role (center, inner, edge).
//
// @param network  The lane network to generate marks from
// @param library  Marking style library (defaults to MUTCD)
// @return RoadMarkNetwork containing all markings
//
inline RoadMarkNetwork generateRoadMarks(
    const LaneNetwork& network,
    const RoadMarkLibrary& library = {}
) {
    RoadMarkNetwork result;
    result.roadId = network.roadId;

    for (const auto& b : network.boundaries) {
        // Skip boundaries with no samples
        if (b.samples.empty()) continue;

        RoadMarkPolyline mark;
        mark.innerLaneId = b.innerLaneId;
        mark.outerLaneId = b.outerLaneId;
        mark.isRoadEdge = b.isRoadEdge;
        mark.startS = b.startS;
        mark.endS = b.endS;
        mark.length = b.length;
        mark.samples = b.samples;  // copy sample points

        // Determine role and assign style
        if (b.isRoadEdge) {
            // Road edge: solid white
            mark.style = library.edgeStyle;
        } else if (b.innerLaneId == 0) {
            // Center line (boundary adjacent to center lane)
            mark.isCenterLine = true;
            mark.style = library.centerStyle;
        } else {
            // Inner lane boundary
            mark.style = library.innerStyle;
        }

        // Skip None-type markings
        if (mark.style.type != LaneRoadMarkType::None) {
            result.markings.push_back(std::move(mark));
        }
    }

    return result;
}

// ═══════════════════════════════════════════════════════════
// generateDashedSegments — Tessellate dashed pattern into segments
// ═══════════════════════════════════════════════════════════
//
// Utility function: given a RoadMarkPolyline with a dash pattern,
// returns a list of (startDistance, endDistance) pairs indicating
// where each dash falls along the polyline.
//
// This is NOT mesh generation — it's parametric dash placement.
// The mesh generator (2.7) uses this to create actual geometry.
//
inline std::vector<std::pair<double, double>> generateDashedSegments(
    const RoadMarkPolyline& mark
) {
    std::vector<std::pair<double, double>> segments;
    if (!mark.style.isDashed() || mark.length <= 0.0) return segments;

    const DashPattern& dp = mark.style.dashPattern;
    double p = dp.period();
    if (p <= 0.0) {
        // Degenerate: treat as solid
        segments.push_back({0.0, mark.length});
        return segments;
    }

    double pos = dp.phase;
    while (pos < mark.length) {
        double dashStart = pos;
        double dashEnd = pos + dp.dashLength;
        if (dashEnd > mark.length) dashEnd = mark.length;
        if (dashStart < mark.length) {
            segments.push_back({dashStart, dashEnd});
        }
        pos += p;
    }

    return segments;
}

} // namespace geo
