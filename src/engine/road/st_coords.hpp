#pragma once

// ═══════════════════════════════════════════════════════════
// s/t Coordinate System — Segment composition and lateral offsets
// ═══════════════════════════════════════════════════════════
//
// SegmentSequence composes ordered GeometrySegments into one continuous
// curve with a global arc-length coordinate (s). It is a non-owning view
// — RoadV2 owns the segments; SegmentSequence just provides geometry
// queries over the composition.
//
// Design (per review):
// - Non-owning: stores const GeometrySegment* pointers. RoadV2 owns
//   the unique_ptr<GeometrySegment> objects. This avoids duplicating
//   clone/copy/move/lifetime logic.
// - API mirrors GeometrySegment: evaluateDS(s), positionAt(s), etc.
//   From the outside, SegmentSequence IS one continuous geometry.
// - Binary search O(log N) for globalS → (segment, localS).
// - clampS() centralizes clamping; all eval methods use it.
// - validateContinuity() returns diagnostics, not bool.
// - Empty sequence: assert in debug, not silent (0,0).
// - projectToST() is intentionally not declared. A stub returning {0,0}
//   would be worse than a compile error. Implement when needed.
//
// ─── Boundary semantics ───────────────────────────────────
// At a segment boundary (e.g., s=10 when seg0.length=10, seg1.length=5):
//   globalSToLocal(10) → {segmentIndex=1, localS=0}
// Boundaries belong to the NEXT segment (upper_bound convention).
// This is consistent across all methods (evaluateDS, positionAt, etc.).
//
// Edge cases:
//   s < 0              → clamped to 0 → {0, 0.0}
//   s > totalLength    → clamped to totalLength → {lastSeg, lastSeg.length}
//   s = 0              → {0, 0.0}
//   s = totalLength    → {lastSeg, lastSeg.length}
//   s = boundary       → {nextSeg, 0.0}
//
// ─── Thread safety ────────────────────────────────────────
// SegmentSequence is read-only after construction. No lazy initialization,
// no mutable caches, no internal state changes during evaluation. This
// makes it naturally usable from future mesh generation jobs (multi-threaded).
//
// ─── Separation of concerns ───────────────────────────────
// SegmentSequence does NOT provide sampleAdaptive(). Sampling is owned by
// GeometrySegment (the primitive). The sequence owns composition only.
// RoadV2 will concatenate per-segment adaptive samples when it needs a
// full centerline polyline. This keeps responsibilities clean:
//   GeometrySegment → sampling
//   SegmentSequence → composition + coordinate mapping
//   RoadV2 → ownership + road metadata + lane queries
//
// Coordinate convention:
// - s ∈ [0, totalLength()] is cumulative arc-length from sequence start
// - t is lateral offset: positive = left (CCW from tangent)
// - positionAtST(s, t) = positionAt(s) + t * normalAt(s)

#include "geometry_segment.hpp"
#include <cassert>
#include <optional>
#include <vector>

namespace geo {

// ─── Segment Sequence (non-owning view) ────────────────────
//
// Composes ordered GeometrySegments into one continuous curve.
// Does NOT own the segments — caller must keep them alive for the
// lifetime of the SegmentSequence.
//
class SegmentSequence {
public:
    struct LocalCoord {
        int segmentIndex;
        double localS;
    };

    struct ContinuityError {
        int segmentA;          // index of first segment
        int segmentB;          // index of second segment (segmentA + 1)
        double positionError;  // distance between end of A and start of B
        double headingError;   // angle between end heading of A and start heading of B
        // Detailed diagnostics for editor highlighting (no recomputation needed):
        Point2D expectedEnd;   // end point of segment A
        Point2D actualStart;   // start point of segment B
        double expectedHeading; // end heading of segment A
        double actualHeading;   // start heading of segment B
    };

private:
    std::vector<const GeometrySegment*> segments_;
    std::vector<double> offsets_;  // offsets_[i] = cumulative length before segment i
    double totalLength_ = 0.0;

    void rebuildOffsets() {
        offsets_.clear();
        offsets_.reserve(segments_.size() + 1);
        offsets_.push_back(0.0);
        double cum = 0.0;
        for (const auto* seg : segments_) {
            cum += seg->length();
            offsets_.push_back(cum);
        }
        totalLength_ = cum;
    }

public:
    SegmentSequence() = default;

    // Construct from non-owning pointers. Caller retains ownership.
    explicit SegmentSequence(std::vector<const GeometrySegment*> segs)
        : segments_(std::move(segs)) {
        rebuildOffsets();
    }

    // ─── Accessors ───

    int numSegments() const { return static_cast<int>(segments_.size()); }
    double totalLength() const { return totalLength_; }

    const GeometrySegment& segment(int idx) const {
        assert(idx >= 0 && idx < static_cast<int>(segments_.size()));
        return *segments_[idx];
    }

    // ─── Coordinate mapping ───

    // Clamp global s to [0, totalLength()]
    double clampS(double s) const {
        if (s < 0.0) return 0.0;
        if (s > totalLength_) return totalLength_;
        return s;
    }

    // Global s → (segment index, local s) via binary search over offsets.
    // Returns {0, 0.0} for empty sequence (asserts in debug).
    LocalCoord globalSToLocal(double globalS) const {
        assert(!segments_.empty() && "globalSToLocal on empty SegmentSequence");
        if (segments_.empty()) return {0, 0.0};

        globalS = clampS(globalS);

        // Binary search: find the segment whose offset range contains globalS.
        // offsets_ = [0, L0, L0+L1, ...], size = numSegments + 1
        // We want the largest i such that offsets_[i] <= globalS
        auto it = std::upper_bound(offsets_.begin(), offsets_.end(), globalS);
        int idx = static_cast<int>(it - offsets_.begin()) - 1;
        if (idx < 0) idx = 0;
        if (idx >= static_cast<int>(segments_.size())) idx = static_cast<int>(segments_.size()) - 1;

        double localS = globalS - offsets_[idx];
        // Clamp localS to segment length (handles floating-point edge at boundary)
        double segLen = segments_[idx]->length();
        if (localS > segLen) localS = segLen;
        if (localS < 0.0) localS = 0.0;

        return {idx, localS};
    }

    // ─── Evaluation (mirrors GeometrySegment API) ───

    void evaluateDS(double s, double& x, double& y, double& heading) const {
        assert(!segments_.empty() && "evaluateDS on empty SegmentSequence");
        if (segments_.empty()) { x = y = heading = 0.0; return; }
        auto loc = globalSToLocal(s);
        segments_[loc.segmentIndex]->evaluateDS(loc.localS, x, y, heading);
    }

    Point2D positionAt(double s) const {
        assert(!segments_.empty() && "positionAt on empty SegmentSequence");
        if (segments_.empty()) return {};
        auto loc = globalSToLocal(s);
        return segments_[loc.segmentIndex]->positionAt(loc.localS);
    }

    Vec2 tangentAt(double s) const {
        assert(!segments_.empty() && "tangentAt on empty SegmentSequence");
        if (segments_.empty()) return {};
        auto loc = globalSToLocal(s);
        return segments_[loc.segmentIndex]->tangentAt(loc.localS);
    }

    Vec2 normalAt(double s) const {
        assert(!segments_.empty() && "normalAt on empty SegmentSequence");
        if (segments_.empty()) return {};
        auto loc = globalSToLocal(s);
        return segments_[loc.segmentIndex]->normalAt(loc.localS);
    }

    double curvatureAt(double s) const {
        assert(!segments_.empty() && "curvatureAt on empty SegmentSequence");
        if (segments_.empty()) return 0.0;
        auto loc = globalSToLocal(s);
        return segments_[loc.segmentIndex]->curvatureDS(loc.localS);
    }

    double headingAt(double s) const {
        assert(!segments_.empty() && "headingAt on empty SegmentSequence");
        if (segments_.empty()) return 0.0;
        auto loc = globalSToLocal(s);
        return segments_[loc.segmentIndex]->headingAt(loc.localS);
    }

    // ─── Continuity validation ───
    // Returns diagnostics for each pair of adjacent segments where
    // position or heading discontinuity exceeds tolerance.
    // Empty result = fully continuous.
    std::vector<ContinuityError> validateContinuity(
        double posTol = GEOM_TOLERANCE,
        double headingTol = 1.0 * DEG_TO_RAD
    ) const {
        std::vector<ContinuityError> errors;
        for (int i = 0; i < static_cast<int>(segments_.size()) - 1; i++) {
            const GeometrySegment& a = *segments_[i];
            const GeometrySegment& b = *segments_[i + 1];

            Point2D endA = a.endPoint();
            Point2D startB = b.startPoint();
            double posErr = endA.distanceTo(startB);

            double hEndA = a.endHeading();
            double hStartB = b.startHeading();
            double hErr = std::abs(normalizeAnglePi(hEndA - hStartB));

            if (posErr > posTol || hErr > headingTol) {
                errors.push_back({
                    i, i + 1, posErr, hErr,
                    endA, startB, hEndA, hStartB
                });
            }
        }
        return errors;
    }
};

// ─── Lateral offset: (s, t) → world position ──────────────
//
// t is lateral offset from the centerline: positive = left (CCW from tangent).
// positionAtST(s, t) = positionAt(s) + t * normalAt(s)
//
inline Point2D positionAtST(const SegmentSequence& seq, double s, double t) {
    return seq.positionAt(s) + seq.normalAt(s) * t;
}

// headingAtST: heading is independent of lateral offset for a rigid cross-section,
// but included for completeness (useful for lane direction queries).
inline double headingAtST(const SegmentSequence& seq, double s, double /*t*/) {
    return seq.headingAt(s);
}

} // namespace geo
