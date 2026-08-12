#pragma once

// ═══════════════════════════════════════════════════════════
// Lane Engine — Phase 2.1: Data Model
// ═══════════════════════════════════════════════════════════
//
// @file lane_engine.hpp
// @brief Lane data model: Lane, LaneSection, Polynomial3, validation
//
// This file defines the lane data structures for RoadV2. It follows
// the OpenDRIVE lane model closely.
//
// @section Milestone 2.1 Scope
// This file implements ONLY the data model and pure algorithms:
//   - Polynomial3 (cubic polynomial for lane width)
//   - LaneType, LaneRoadMarkType (enums)
//   - LaneRoadMark, Lane, LaneSection (structs)
//   - LaneSection helper methods (findLane, leftLanes, etc.)
//   - LaneValidation (invariant checking)
//   - synthesizeFromLegacy() (width/laneCount → LaneSection)
//
// No world-space geometry, no boundary generation, no mesh.
// Those are milestones 2.3–2.7.
//
// @section Lane ID Convention (OpenDRIVE)
//   ID  0  = center lane (virtual, width=0, type=Border)
//   ID -1, -2, -3, ... = left lanes (oncoming direction)
//   ID +1, +2, +3, ... = right lanes (forward direction)
//   |ID| increases outward from center.
//
// @section API Freeze
// NOT YET FROZEN. Will be frozen at Phase 2 Complete.

#include "geometry.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace geo {

// ═══════════════════════════════════════════════════════════
// Polynomial3 — Cubic polynomial for lane width
// ═══════════════════════════════════════════════════════════
//
// OpenDRIVE width polynomial:
//   width(ds) = a + b·ds + c·ds² + d·ds³
// where ds = s - sOffset (delta from lane section start).
//
struct Polynomial3 {
    double a = 0.0;  // constant term
    double b = 0.0;  // linear term
    double c = 0.0;  // quadratic term
    double d = 0.0;  // cubic term

    // Evaluate polynomial at ds: p(ds) = a + b·ds + c·ds² + d·ds³
    double evaluate(double ds) const {
        return a + b * ds + c * ds * ds + d * ds * ds * ds;
    }

    // First derivative: p'(ds) = b + 2c·ds + 3d·ds²
    double derivative(double ds) const {
        return b + 2.0 * c * ds + 3.0 * d * ds * ds;
    }

    // Second derivative: p''(ds) = 2c + 6d·ds
    // Useful for smooth tapers, curvature-aware width, superelevation, mesh smoothing
    double secondDerivative(double ds) const {
        return 2.0 * c + 6.0 * d * ds;
    }

    // Check if all coefficients are finite (not NaN/Inf)
    bool isValid() const {
        return std::isfinite(a) && std::isfinite(b) &&
               std::isfinite(c) && std::isfinite(d);
    }

    // Default: zero-width polynomial (a=b=c=d=0)
    Polynomial3() = default;

    // Constant width constructor
    explicit Polynomial3(double constantWidth)
        : a(constantWidth), b(0), c(0), d(0) {}

    // Full polynomial constructor
    Polynomial3(double a_, double b_, double c_, double d_)
        : a(a_), b(b_), c(c_), d(d_) {}
};

// ═══════════════════════════════════════════════════════════
// LaneType — OpenDRIVE lane type classification
// ═══════════════════════════════════════════════════════════
//
enum class LaneType {
    Driving,        // normal driving lane
    Shoulder,       // soft shoulder
    Sidewalk,       // pedestrian walkway
    Border,         // virtual border (center lane, width=0)
    Parking,        // parking lane
    Stop,           // stop/standing lane
    Restricted,     // restricted (no driving)
    Biking,         // bike lane
    Tram,           // tram lane
    Bus,            // bus lane
    Taxi,           // taxi lane
    HOV,            // high-occupancy vehicle
    Acceleration,   // acceleration lane (merge)
    Deceleration,   // deceleration lane (exit)
    None,           // virtual lane (no physical surface)
};

// Convert LaneType to OpenDRIVE string
inline std::string laneTypeToString(LaneType type) {
    switch (type) {
        case LaneType::Driving:      return "driving";
        case LaneType::Shoulder:     return "shoulder";
        case LaneType::Sidewalk:     return "sidewalk";
        case LaneType::Border:       return "border";
        case LaneType::Parking:      return "parking";
        case LaneType::Stop:         return "stop";
        case LaneType::Restricted:   return "restricted";
        case LaneType::Biking:       return "biking";
        case LaneType::Tram:         return "tram";
        case LaneType::Bus:          return "bus";
        case LaneType::Taxi:         return "taxi";
        case LaneType::HOV:          return "hov";
        case LaneType::Acceleration: return "acceleration";
        case LaneType::Deceleration: return "deceleration";
        case LaneType::None:         return "none";
    }
    return "none";
}

// ═══════════════════════════════════════════════════════════
// LaneRoadMarkType — Lane boundary marking types
// ═══════════════════════════════════════════════════════════
//
enum class LaneRoadMarkType {
    None,
    Solid,          // ─────
    Dashed,         // - - - -
    SolidSolid,     // ═════ (double solid)
    SolidDashed,    // ═-══- (solid left, dashed right)
    DashedSolid,    // -═-═- (dashed left, solid right)
    DashedDashed,   // =-=-= (double dashed)
    Broken,         // alias for Dashed
    BrokenBroken,   // alias for DashedDashed
    Curb,           // physical curb
    Edge,           // road edge
};

// Convert LaneRoadMarkType to OpenDRIVE string
inline std::string roadMarkTypeToString(LaneRoadMarkType type) {
    switch (type) {
        case LaneRoadMarkType::None:          return "none";
        case LaneRoadMarkType::Solid:         return "solid";
        case LaneRoadMarkType::Dashed:        return "broken";
        case LaneRoadMarkType::SolidSolid:    return "solid solid";
        case LaneRoadMarkType::SolidDashed:   return "solid broken";
        case LaneRoadMarkType::DashedSolid:   return "broken solid";
        case LaneRoadMarkType::DashedDashed:  return "broken broken";
        case LaneRoadMarkType::Broken:        return "broken";
        case LaneRoadMarkType::BrokenBroken:  return "broken broken";
        case LaneRoadMarkType::Curb:          return "curb";
        case LaneRoadMarkType::Edge:          return "edge";
    }
    return "none";
}

// ═══════════════════════════════════════════════════════════
// LaneRoadMark — Lane boundary marking
// ═══════════════════════════════════════════════════════════
//
// Describes the visual marking on the outer boundary of a lane
// (the side farther from the reference line).
//
struct LaneRoadMark {
    double sOffset = 0.0;               // s-offset from lane section start
    LaneRoadMarkType type = LaneRoadMarkType::Dashed;
    std::string color = "white";        // "white", "yellow", "red"
    double width = 0.15;                // marking width in meters
    double laneChange = 0.0;            // -1=no left, +1=no right, 0=both

    LaneRoadMark() = default;
    LaneRoadMark(LaneRoadMarkType t, std::string c, double w = 0.15)
        : type(t), color(std::move(c)), width(w) {}
};

// ═══════════════════════════════════════════════════════════
// Lane — A single lane within a lane section
// ═══════════════════════════════════════════════════════════
//
// @section Lane ID Convention
//   id = 0   → center lane (virtual, type=Border, width=0)
//   id < 0   → left lane (oncoming direction), |id| increases outward
//   id > 0   → right lane (forward direction), id increases outward
//
struct Lane {
    int id = 0;                              // 0=center, negative=left, positive=right
    LaneType type = LaneType::Driving;
    Polynomial3 width;                       // width as function of ds
    std::vector<LaneRoadMark> roadMarks;     // markings on this lane

    Lane() = default;
    Lane(int id_, LaneType type_, Polynomial3 width_)
        : id(id_), type(type_), width(width_) {}

    // Evaluate width at ds (delta-s from lane section start)
    double widthAt(double ds) const {
        return width.evaluate(ds);
    }

    // Whether this is the center lane
    bool isCenter() const { return id == 0; }

    // Whether this is a left lane
    bool isLeft() const { return id < 0; }

    // Whether this is a right lane
    bool isRight() const { return id > 0; }

    // Whether this lane is drivable
    bool isDrivable() const {
        return type == LaneType::Driving ||
               type == LaneType::Acceleration ||
               type == LaneType::Deceleration ||
               type == LaneType::Bus ||
               type == LaneType::Taxi ||
               type == LaneType::HOV;
    }
};

// ═══════════════════════════════════════════════════════════
// LaneValidation — Result of LaneSection::validate()
// ═══════════════════════════════════════════════════════════
//
struct LaneValidation {
    bool valid = true;
    std::vector<std::string> errors;

    void addError(const std::string& msg) {
        valid = false;
        errors.push_back(msg);
    }
};

// ═══════════════════════════════════════════════════════════
// LaneSection — A span of road with a fixed lane configuration
// ═══════════════════════════════════════════════════════════
//
// A LaneSection defines the lane layout for a span of road
// [startS, nextSection.startS). It contains all lanes (left,
// center, right) and their width polynomials.
//
// @section Invariants (checked by validate())
//   1. At most one center lane (id=0)
//   2. Lane IDs are unique
//   3. Lane IDs are contiguous (no gaps: -2, -1, 0, 1, 2)
//   4. Lane widths are non-negative at ds=0
//   5. Left lanes have negative IDs, right lanes have positive
//
// @section Encapsulation
// Lanes are accessed through addLane()/findLane()/leftLanes()/etc.
// The internal lanes_ vector is private to maintain invariants.
//
struct LaneSection {
    double startS = 0.0;                     // s-position where section starts

    LaneSection() = default;
    explicit LaneSection(double s) : startS(s) {}

    // ─── Lane mutation (encapsulated) ───

    // Add a lane. Does NOT check for duplicate IDs — call validate().
    void addLane(Lane lane) {
        lanes_.push_back(std::move(lane));
    }

    // Remove lane by ID. Returns true if found.
    bool removeLane(int id) {
        auto it = std::find_if(lanes_.begin(), lanes_.end(),
            [id](const Lane& l) { return l.id == id; });
        if (it == lanes_.end()) return false;
        lanes_.erase(it);
        return true;
    }

    // ─── Lane queries ───

    // Number of lanes (all types)
    int numLanes() const { return static_cast<int>(lanes_.size()); }

    // Find lane by ID. Returns nullptr if not found.
    const Lane* findLane(int id) const {
        auto it = std::find_if(lanes_.begin(), lanes_.end(),
            [id](const Lane& l) { return l.id == id; });
        return (it != lanes_.end()) ? &(*it) : nullptr;
    }

    // Find lane by ID (mutable).
    Lane* findLaneMut(int id) {
        auto it = std::find_if(lanes_.begin(), lanes_.end(),
            [id](const Lane& l) { return l.id == id; });
        return (it != lanes_.end()) ? &(*it) : nullptr;
    }

    // Center lane (id=0). Returns nullptr if not present.
    const Lane* center() const { return findLane(0); }

    // Left lanes (id < 0), sorted by |id| ascending (innermost first).
    // Returns copies to avoid dangling pointers.
    std::vector<const Lane*> leftLanes() const {
        std::vector<const Lane*> result;
        for (const auto& l : lanes_) {
            if (l.id < 0) result.push_back(&l);
        }
        std::sort(result.begin(), result.end(),
            [](const Lane* a, const Lane* b) {
                return std::abs(a->id) < std::abs(b->id);
            });
        return result;
    }

    // Right lanes (id > 0), sorted by id ascending (innermost first).
    std::vector<const Lane*> rightLanes() const {
        std::vector<const Lane*> result;
        for (const auto& l : lanes_) {
            if (l.id > 0) result.push_back(&l);
        }
        std::sort(result.begin(), result.end(),
            [](const Lane* a, const Lane* b) { return a->id < b->id; });
        return result;
    }

    // Maximum left lane ID (most negative, e.g., -3 for 3 left lanes).
    // Returns 0 if no left lanes.
    int maxLeftLaneId() const {
        int minId = 0;
        for (const auto& l : lanes_) {
            if (l.id < minId) minId = l.id;
        }
        return minId;
    }

    // Maximum right lane ID (e.g., 3 for 3 right lanes).
    // Returns 0 if no right lanes.
    int maxRightLaneId() const {
        int maxId = 0;
        for (const auto& l : lanes_) {
            if (l.id > maxId) maxId = l.id;
        }
        return maxId;
    }

    // ─── Width queries ───

    // Total road width at ds (sum of all lane widths).
    // For a road with left + center + right lanes, this is
    // the total paved width.
    double totalWidthAt(double ds) const {
        double total = 0;
        for (const auto& l : lanes_) {
            double w = l.widthAt(ds);
            if (w > 0) total += w;  // ignore zero-width center
        }
        return total;
    }

    // Total width of right side at ds (sum of right lane widths, id > 0).
    double rightWidthAt(double ds) const {
        double total = 0;
        for (const auto& l : lanes_) {
            if (l.id > 0) {
                double w = l.widthAt(ds);
                if (w > 0) total += w;
            }
        }
        return total;
    }

    // Total width of left side at ds (sum of left lane widths, id < 0).
    double leftWidthAt(double ds) const {
        double total = 0;
        for (const auto& l : lanes_) {
            if (l.id < 0) {
                double w = l.widthAt(ds);
                if (w > 0) total += w;
            }
        }
        return total;
    }

    // ─── Lane offset queries (pure lateral math, no world-space) ───
    //
    // These compute lateral offsets from the reference line (center).
    // Positive offset = right side, negative = left side.
    // World-space conversion (applying normal vector) is deferred to 2.3.
    //

    // Lateral offset of the boundary between lane `laneId` and the
    // next lane outward (laneId+1 for right, laneId-1 for left).
    // For right lanes (id > 0): offset = sum(width(1..laneId))
    // For left lanes (id < 0): offset = -sum(width(-1..laneId))
    // For center (id = 0): offset = 0
    //
    // Example for a 4-lane road:
    //   boundaryOffset(0, ds)  = 0           (center line)
    //   boundaryOffset(1, ds)  = w1          (between lane 1 and 2)
    //   boundaryOffset(2, ds)  = w1 + w2     (outer right edge)
    //   boundaryOffset(-1, ds) = -w_left1    (between lane -1 and -2)
    //
    double boundaryOffset(int laneId, double ds) const {
        if (laneId == 0) return 0.0;

        if (laneId > 0) {
            // Right side: sum widths of lanes 1..laneId
            double offset = 0;
            for (int i = 1; i <= laneId; i++) {
                const Lane* lane = findLane(i);
                if (lane) offset += lane->widthAt(ds);
            }
            return offset;
        } else {
            // Left side: negative offset, sum widths of lanes -1..laneId
            double offset = 0;
            for (int i = -1; i >= laneId; i--) {
                const Lane* lane = findLane(i);
                if (lane) offset += lane->widthAt(ds);
            }
            return -offset;
        }
    }

    // Lateral offset of the CENTER of lane `laneId` from the reference line.
    // For right lanes: boundary(laneId-1) + width(laneId)/2
    // For left lanes:  boundary(laneId+1) - width(laneId)/2
    // For center (0):  0
    //
    double laneCenterOffset(int laneId, double ds) const {
        if (laneId == 0) return 0.0;

        const Lane* lane = findLane(laneId);
        if (!lane) return 0.0;

        double halfWidth = lane->widthAt(ds) / 2.0;

        if (laneId > 0) {
            // Right: inner boundary + half width
            double innerBoundary = boundaryOffset(laneId - 1, ds);
            return innerBoundary + halfWidth;
        } else {
            // Left: inner boundary - half width
            // boundaryOffset(laneId+1) gives the boundary between this lane
            // and the next one toward center
            double innerBoundary = boundaryOffset(laneId + 1, ds);
            return innerBoundary - halfWidth;
        }
    }

    // Outer edge offset of lane `laneId` (the boundary farthest from center).
    // For right lanes: boundaryOffset(laneId)
    // For left lanes:  boundaryOffset(laneId)
    // Same as boundaryOffset — provided for API clarity.
    double laneOuterEdgeOffset(int laneId, double ds) const {
        return boundaryOffset(laneId, ds);
    }

    // Inner edge offset of lane `laneId` (the boundary closest to center).
    // For right lanes (id > 0): boundaryOffset(laneId - 1)
    // For left lanes (id < 0):  boundaryOffset(laneId + 1)
    // For center (0): 0
    double laneInnerEdgeOffset(int laneId, double ds) const {
        if (laneId == 0) return 0.0;
        if (laneId > 0) return boundaryOffset(laneId - 1, ds);
        return boundaryOffset(laneId + 1, ds);
    }

    // Number of drivable lanes (any side).
    int drivingLaneCount() const {
        int count = 0;
        for (const auto& l : lanes_) {
            if (l.isDrivable()) count++;
        }
        return count;
    }

    // ─── Validation ───

    // Check lane section invariants.
    // Returns LaneValidation with errors list.
    LaneValidation validate() const {
        LaneValidation result;

        // Check: at most one center lane
        int centerCount = 0;
        for (const auto& l : lanes_) {
            if (l.id == 0) centerCount++;
        }
        if (centerCount > 1) {
            result.addError("Multiple center lanes (id=0) found: " +
                std::to_string(centerCount));
        }

        // Check: unique lane IDs
        std::vector<int> ids;
        for (const auto& l : lanes_) ids.push_back(l.id);
        std::sort(ids.begin(), ids.end());
        for (size_t i = 1; i < ids.size(); i++) {
            if (ids[i] == ids[i - 1]) {
                result.addError("Duplicate lane ID: " + std::to_string(ids[i]));
            }
        }

        // Check: contiguous lane IDs (no gaps)
        // If we have lanes -2, -1, 0, 1, 3 — that's a gap (missing 2)
        if (!ids.empty()) {
            int minId = ids.front();
            int maxId = ids.back();
            for (int expected = minId; expected <= maxId; expected++) {
                if (std::find(ids.begin(), ids.end(), expected) == ids.end()) {
                    result.addError("Non-contiguous lane IDs: missing " +
                        std::to_string(expected));
                }
            }
        }

        // Check: non-negative widths at ds=0
        for (const auto& l : lanes_) {
            double w = l.widthAt(0.0);
            if (w < 0) {
                result.addError("Lane " + std::to_string(l.id) +
                    " has negative width at ds=0: " + std::to_string(w));
            }
        }

        // Check: polynomial coefficients are finite (not NaN/Inf)
        for (const auto& l : lanes_) {
            if (!l.width.isValid()) {
                result.addError("Lane " + std::to_string(l.id) +
                    " has non-finite polynomial coefficients (NaN/Inf)");
            }
        }

        // Check: center lane should have zero width
        if (centerCount == 1) {
            const Lane* c = center();
            if (c && c->widthAt(0.0) != 0.0) {
                result.addError("Center lane (id=0) must have zero width, got: " +
                    std::to_string(c->widthAt(0.0)));
            }
        }

        return result;
    }

    // ─── Direct access (for serialization and testing) ───
    // Returns const reference to internal lane vector.
    // This is for serialization only — do NOT mutate through this.
    const std::vector<Lane>& lanes() const { return lanes_; }

private:
    std::vector<Lane> lanes_;
};

// ═══════════════════════════════════════════════════════════
// synthesizeFromLegacy — Create LaneSection from width/laneCount
// ═══════════════════════════════════════════════════════════
//
// When a RoadV2 has no explicit LaneSections (legacy roads), this
// function synthesizes a LaneSection from the flat width and
// laneCount fields. The result is a constant-width lane section
// with equal lane widths.
//
// For odd laneCount: left gets one more lane than right.
//   e.g., laneCount=3 → left=2, right=1 (or left=1, right=2)
// We choose: left = ceil(laneCount/2), right = floor(laneCount/2)
// This matches the existing OpenDRIVE export logic.
//
inline LaneSection synthesizeFromLegacy(double roadWidth, int laneCount) {
    LaneSection ls;
    ls.startS = 0.0;

    if (laneCount <= 0) {
        // Degenerate: just a center lane
        Lane center;
        center.id = 0;
        center.type = LaneType::Border;
        center.width = Polynomial3(0.0);
        ls.addLane(center);
        return ls;
    }

    double laneWidth = roadWidth / laneCount;
    int rightCount = laneCount / 2;
    int leftCount = laneCount - rightCount;

    // Center lane (virtual, zero width)
    Lane center;
    center.id = 0;
    center.type = LaneType::Border;
    center.width = Polynomial3(0.0);
    ls.addLane(center);

    // Right lanes (positive IDs, forward direction)
    for (int i = 1; i <= rightCount; i++) {
        Lane lane;
        lane.id = i;
        lane.type = LaneType::Driving;
        lane.width = Polynomial3(laneWidth);
        ls.addLane(lane);
    }

    // Left lanes (negative IDs, oncoming direction)
    for (int i = 1; i <= leftCount; i++) {
        Lane lane;
        lane.id = -i;
        lane.type = LaneType::Driving;
        lane.width = Polynomial3(laneWidth);
        ls.addLane(lane);
    }

    return ls;
}

} // namespace geo
