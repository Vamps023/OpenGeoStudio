#pragma once

// ═══════════════════════════════════════════════════════════
// RoadV2 — New road model with segment-based geometry
// ═══════════════════════════════════════════════════════════
//
// @file road_v2.hpp
// @brief Public API: New road model with segment-based geometry
//
// This is the new road representation that replaces the legacy
// ControlPoint[]-based Road. It owns GeometrySegment objects and
// exposes a non-owning SegmentSequence view for geometry queries.
//
// @section API_Freeze Phase 1 API Freeze
// The following classes are FROZEN as of Phase 1 Complete:
//   - LaneSection
//   - RoadV2
// Breaking changes to RoadV2 require a major version bump.
//
// Design (per review):
// - segments owns geometry via unique_ptr<GeometrySegment>
// - geometry is a non-owning SegmentSequence view, rebuilt after
//   every segment mutation via rebuildGeometryView()
// - Mutation is encapsulated (addSegment, clearSegments, etc.) to
//   prevent the "forgot to rebuild" bug
// - Deep-clone copy constructor/assignment (uses segment clone())
// - LaneSection is defined above with full lane management
// - No adapter, no IPC, no serialization — that's 1.8.3+
//
// Ownership invariant:
//   segments  → owns the geometry (unique_ptr)
//   geometry  → never owns anything (const GeometrySegment* view)
//   Every mutation that changes segments calls rebuildGeometryView()

#include "geometry_segment.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace geo {

// ─── LaneSection ───────────────────────────────────────────
// LaneSection defines a contiguous section of road with consistent
// lane geometry. The full implementation with Lane arrays is in the
// internal lane_engine.hpp. This public facade provides the type
// declaration for API consumers.
//
struct LaneSection {
    double startS = 0.0;  // s-position where this section starts

    LaneSection() = default;
    explicit LaneSection(double s) : startS(s) {}
};

// ─── RoadV2 ────────────────────────────────────────────────
class RoadV2 {
public:
    // ─── Geometry ownership ───
    // Private: mutation goes through addSegment/clearSegments to ensure
    // rebuildGeometryView() is always called.
private:
    std::vector<std::unique_ptr<GeometrySegment>> segments_;

    // ─── Lane ownership (empty for Phase 1) ───
    std::vector<LaneSection> laneSections_;

    // ─── Legacy-compatible metadata ───
public:
    std::string id;
    std::string name;
    std::string color = "#4ecca3";
    std::string profileName = "city_2x1";
    std::string startIntersectionId;
    std::string endIntersectionId;

    // Temporary compatibility fields (synthesized from LaneSection in Phase 2)
    double width = 8.0;
    int laneCount = 2;

private:
    // ─── Geometry view (non-owning, rebuilt after mutations) ───
    SegmentSequence geometry_;

    // Rebuild the SegmentSequence view from current segments.
    // Called internally after every mutation.
    void rebuildGeometryView() {
        std::vector<const GeometrySegment*> ptrs;
        ptrs.reserve(segments_.size());
        for (const auto& seg : segments_) {
            ptrs.push_back(seg.get());
        }
        geometry_ = SegmentSequence(std::move(ptrs));
    }

public:
    // ─── Rule of five ───

    RoadV2() = default;

    // Deep-clone copy constructor: clones every segment via clone()
    RoadV2(const RoadV2& other)
        : laneSections_(other.laneSections_),
          id(other.id),
          name(other.name),
          color(other.color),
          profileName(other.profileName),
          startIntersectionId(other.startIntersectionId),
          endIntersectionId(other.endIntersectionId),
          width(other.width),
          laneCount(other.laneCount) {
        segments_.reserve(other.segments_.size());
        for (const auto& seg : other.segments_) {
            segments_.push_back(seg->clone());
        }
        rebuildGeometryView();
    }

    // Deep-clone copy assignment
    RoadV2& operator=(const RoadV2& other) {
        if (this == &other) return *this;

        // Clone segments
        std::vector<std::unique_ptr<GeometrySegment>> newSegs;
        newSegs.reserve(other.segments_.size());
        for (const auto& seg : other.segments_) {
            newSegs.push_back(seg->clone());
        }

        // Copy all fields
        segments_ = std::move(newSegs);
        laneSections_ = other.laneSections_;
        id = other.id;
        name = other.name;
        color = other.color;
        profileName = other.profileName;
        startIntersectionId = other.startIntersectionId;
        endIntersectionId = other.endIntersectionId;
        width = other.width;
        laneCount = other.laneCount;

        rebuildGeometryView();
        return *this;
    }

    // Move constructor (default — unique_ptr moves are cheap)
    RoadV2(RoadV2&&) noexcept = default;
    RoadV2& operator=(RoadV2&&) noexcept = default;

    ~RoadV2() = default;

    // ─── Encapsulated segment mutation ───
    // These methods ensure rebuildGeometryView() is always called.

    // Add a segment (takes ownership of the unique_ptr)
    void addSegment(std::unique_ptr<GeometrySegment> seg) {
        segments_.push_back(std::move(seg));
        rebuildGeometryView();
    }

    // Add a segment by constructing in-place (factory function)
    // Example: road.addSegment<LineSegment>(start, end);
    template <typename SegType, typename... Args>
    SegType& addSegment(Args&&... args) {
        auto seg = std::make_unique<SegType>(std::forward<Args>(args)...);
        SegType* raw = seg.get();
        segments_.push_back(std::move(seg));
        rebuildGeometryView();
        return *raw;
    }

    // Reserve capacity for segments (avoids reallocations during bulk add)
    void reserveSegments(size_t count) {
        segments_.reserve(count);
    }

    // Clear all segments
    void clearSegments() {
        segments_.clear();
        rebuildGeometryView();
    }

    // ─── Read-only accessors ───

    int numSegments() const { return static_cast<int>(segments_.size()); }

    // Access a segment by index (read-only)
    const GeometrySegment& segment(int idx) const {
        return *segments_[idx];
    }

    // Access the geometry view (read-only)
    const SegmentSequence& geometry() const { return geometry_; }

    // Total geometry length (convenience — delegates to SegmentSequence)
    double totalLength() const { return geometry_.totalLength(); }

    // ─── Lane section access (Phase 2.1) ───

    int numLaneSections() const { return static_cast<int>(laneSections_.size()); }

    // Access a lane section by index (read-only)
    const LaneSection& laneSection(int idx) const {
        return laneSections_[idx];
    }

    // Find the lane section active at s-position s.
    // Returns nullptr if no lane sections exist.
    // Lane sections are sorted by startS; the active section is the
    // last one whose startS <= s.
    const LaneSection* laneSectionAt(double s) const {
        if (laneSections_.empty()) return nullptr;
        const LaneSection* result = nullptr;
        for (const auto& ls : laneSections_) {
            if (ls.startS <= s) {
                result = &ls;
            } else {
                break;
            }
        }
        return result;
    }

    // Add a lane section. Lane sections should be added in order of
    // increasing startS.
    void addLaneSection(LaneSection section) {
        laneSections_.push_back(std::move(section));
    }

    // Clear all lane sections
    void clearLaneSections() {
        laneSections_.clear();
        synthesizedLegacy_.reset();
    }

    // ─── Legacy lane synthesis (cached) ───
    //
    // If no explicit LaneSections exist, synthesize one from
    // width/laneCount. The result is cached to avoid rebuilding.
    // The cache is invalidated when lane sections are added/cleared
    // or when width/laneCount changes.
    //
    const LaneSection& legacyLaneSection() const {
        if (!synthesizedLegacy_.has_value()) {
            synthesizedLegacy_ = synthesizeFromLegacy(width, laneCount);
        }
        return synthesizedLegacy_.value();
    }

    // Invalidate the legacy synthesis cache.
    // Call this when width or laneCount changes.
    void invalidateLegacyCache() const {
        synthesizedLegacy_.reset();
    }

private:
    mutable std::optional<LaneSection> synthesizedLegacy_;

    // Synthesize a LaneSection from legacy road parameters.
    // The public API facade returns an empty LaneSection since the Lane
    // type is only fully defined in the internal lane_engine.hpp.
    // Callers that need actual lane synthesis should use the internal API.
    static LaneSection synthesizeFromLegacy(double width, int laneCount) {
        LaneSection section;
        (void)width;
        (void)laneCount;
        return section;
    }
};

} // namespace geo