#pragma once

// ═══════════════════════════════════════════════════════════
// Geometry Segment — Polymorphic road geometry primitives
// ═══════════════════════════════════════════════════════════
//
// @file geometry_segment.hpp
// @brief Public API: Polymorphic road geometry primitives
//
// This is the new polymorphic geometry system that will eventually
// replace the flat ControlPoint[] model. Each road segment is a
// GeometrySegment subclass (Line, Arc, Spiral, Bezier) that can
// evaluate its position, heading, and curvature at any arc-length
// distance s along the segment.
//
// @section API_Freeze Phase 1 API Freeze
// The following classes are FROZEN as of Phase 1 Complete:
//   - GeometryType (enum)
//   - GeometrySegment (abstract base)
//   - LineSegment
//   - ArcSegment
//   - SpiralSegment
//   - BezierSegment
//   - SegmentSequence
// Breaking changes to these classes require a major version bump.
//
// Design decisions (see docs/ROAD_ENGINE_MIGRATION_PLAN.md):
// - Heap-allocated via unique_ptr (Q1: simpler than variant, more extensible)
// - clone() virtual method for deep-copy (undo/redo compatibility)
// - Absolute coordinates (Q2: adapter handles relative→absolute at boundary)
// - s is arc-length in meters from segment start [0, length()]
// - All segments are planar (2D); elevation is handled separately

#include "geometry.hpp"
#include <memory>
#include <string>
#include <functional>
#include <algorithm>

namespace geo {

// ─── Sampling/Geometry Constants (from architecture doc Appendix B) ───
constexpr double MAX_GEOM_ERROR_HORIZONTAL = 0.25;  // m — max horizontal error from sampling
constexpr double MAX_GEOM_ERROR_VERTICAL = 0.1;     // m — max vertical error
constexpr double GEOM_TOLERANCE = 0.2;              // m — min distance between vertices
constexpr double MAX_GEOM_LENGTH = 50.0;            // m — max segment length
constexpr double MIN_GEOM_LENGTH = 0.1;             // m — min segment length
constexpr int ADAPTIVE_MAX_DEPTH = 20;              // recursion depth limit for adaptive sampling

// ─── Geometry Type Enum ────────────────────────────────────
enum class GeometryType {
    Line,
    Arc,
    Spiral,   // clothoid
    Bezier,   // cubic bezier
};

// ─── Geometry Segment (abstract base) ──────────────────────
//
// Contract:
// - s ∈ [0, length()] is arc-length in meters from the segment start
// - evaluateDS(s) returns (x, y, heading) at distance s
// - curvatureDS(s) returns signed curvature (1/radius, CCW positive)
// - tangent(s) returns unit direction vector at s
// - normal(s) returns unit normal vector (left = CCW 90° from tangent)
// - length() returns total arc length
// - clone() returns a deep copy (for undo/redo snapshot compatibility)
//
// ─── Curvature sign convention ─────────────────────────────
// Positive curvature = left turn (CCW), matching OpenDRIVE.
// Negative curvature = right turn (CW).
// This applies to ArcSegment (constant curvature) and SpiralSegment
// (linearly varying curvature). LineSegment curvature is always 0.
// BezierSegment reports signed curvature from the parametric formula.
//
// Parameterization (Line/Arc/Spiral):
//   All three store: startPoint, startHeading, [curvature...], length.
//   evaluateDS integrates heading from startHeading over arc-length:
//     heading(s) = startHeading + ∫₀ˢ κ(u) du
//     position(s) = startPoint + ∫₀ˢ [cos(heading(u)), sin(heading(u))] du
//   Line:  κ(s) = 0           → heading constant, position linear
//   Arc:   κ(s) = κ (constant) → heading linear, position circular
//   Spiral: κ(s) = κ₀ + (κ₁-κ₀)·s/L → heading quadratic, position Fresnel
//
//   Bezier is the exception (Q2): stores absolute P0–P3 control points,
//   not start+heading+length. It satisfies the same evaluateDS contract
//   but uses de Casteljau + arc-length lookup table internally.
//
class GeometrySegment {
public:
    virtual ~GeometrySegment() = default;

    // Core evaluation: position and heading at arc-length s
    // s must be in [0, length()]. Out-of-range s is clamped.
    virtual void evaluateDS(double s, double& x, double& y, double& heading) const = 0;

    // Signed curvature at arc-length s (1/radius, CCW positive, 0 for line)
    virtual double curvatureDS(double s) const = 0;

    // Total arc length of this segment (meters)
    virtual double length() const = 0;

    // Geometry type discriminator
    virtual GeometryType type() const = 0;

    // Deep copy (required for undo/redo — RoadV2 must be copyable)
    virtual std::unique_ptr<GeometrySegment> clone() const = 0;

    // ─── Convenience methods (non-virtual, derived from evaluateDS) ───

    // Position at arc-length s
    Point2D positionAt(double s) const {
        double x, y, h;
        evaluateDS(s, x, y, h);
        return {x, y};
    }

    // Unit tangent vector at arc-length s
    Vec2 tangentAt(double s) const {
        double x, y, h;
        evaluateDS(s, x, y, h);
        return {std::cos(h), std::sin(h)};
    }

    // Unit normal vector at arc-length s (left = 90° CCW from tangent)
    Vec2 normalAt(double s) const {
        double x, y, h;
        evaluateDS(s, x, y, h);
        return {-std::sin(h), std::cos(h)};
    }

    // Heading at arc-length s (radians)
    double headingAt(double s) const {
        double x, y, h;
        evaluateDS(s, x, y, h);
        return h;
    }

    // Start point (s=0)
    Point2D startPoint() const { return positionAt(0.0); }

    // End point (s=length)
    Point2D endPoint() const { return positionAt(length()); }

    // Start heading (s=0)
    double startHeading() const { return headingAt(0.0); }

    // End heading (s=length)
    double endHeading() const { return headingAt(length()); }

    // ─── Adaptive sampling (curvature-based recursive midpoint displacement) ───
    // Subdivides recursively until the chord error (distance from midpoint
    // of the chord to the actual curve point) is below maxError.
    // Uses MAX_GEOM_ERROR_HORIZONTAL (0.25m) as default tolerance.
    // Recursion depth limited by ADAPTIVE_MAX_DEPTH (20) to prevent stack
    // overflow on pathological inputs.
    virtual std::vector<Point2D> sampleAdaptive(
        double maxError = MAX_GEOM_ERROR_HORIZONTAL,
        int minSamples = 2,
        int maxSamples = 10000
    ) const;

    // Uniform sampling at fixed count
    std::vector<Point2D> sampleUniform(int numSamples) const {
        std::vector<Point2D> pts;
        if (numSamples < 2) numSamples = 2;
        double len = length();
        for (int i = 0; i < numSamples; i++) {
            double s = len * static_cast<double>(i) / (numSamples - 1);
            pts.push_back(positionAt(s));
        }
        return pts;
    }
};

// ─── Adaptive sampling (recursive midpoint displacement) ───
//
// Algorithm:
//   1. Start with endpoints (s=0, s=length)
//   2. For each pair of adjacent samples, compute the midpoint of the chord
//      and the actual curve point at the midpoint s
//   3. If the distance between them exceeds maxError, subdivide (insert the
//      actual midpoint) and recurse on both halves
//   4. Stop when error < maxError, or recursion depth exceeds ADAPTIVE_MAX_DEPTH,
//      or sample count exceeds maxSamples
//
// This naturally produces more samples in high-curvature regions (where the
// chord deviates most from the curve) and fewer in straight regions.
//
inline std::vector<Point2D> GeometrySegment::sampleAdaptive(
    double maxError, int minSamples, int maxSamples
) const {
    if (length() < EPSILON) {
        return {positionAt(0.0)};
    }

    // Start with minSamples uniformly distributed
    std::vector<Point2D> samples;
    samples.reserve(minSamples);
    for (int i = 0; i < minSamples; i++) {
        double s = length() * static_cast<double>(i) / (minSamples - 1);
        samples.push_back(positionAt(s));
    }
    // Track the s-value for each sample (needed for subdivision)
    std::vector<double> sValues;
    sValues.reserve(minSamples);
    for (int i = 0; i < minSamples; i++) {
        sValues.push_back(length() * static_cast<double>(i) / (minSamples - 1));
    }

    // Recursive subdivision helper
    // Subdivides the range [sLo, sHi] if chord error exceeds maxError.
    // Inserts points into samples/sValues in-place.
    std::function<void(double sLo, double sHi, int depth)> subdivide =
        [&](double sLo, double sHi, int depth) {
            if (depth >= ADAPTIVE_MAX_DEPTH) return;
            if (static_cast<int>(samples.size()) >= maxSamples) return;

            double sMid = (sLo + sHi) / 2.0;
            Point2D pMid = positionAt(sMid);
            Point2D pLo = positionAt(sLo);
            Point2D pHi = positionAt(sHi);

            // Chord midpoint (linear interpolation)
            Point2D chordMid = Point2D::lerp(pLo, pHi, 0.5);

            // Chord error = distance from actual curve to chord midpoint
            double error = pMid.distanceTo(chordMid);

            if (error < maxError) return;

            // Need to subdivide: insert pMid, then recurse on both halves.
            // Find insertion index (after the sample at sLo).
            // We search for sHi in sValues and insert before it.
            auto it = std::lower_bound(sValues.begin(), sValues.end(), sHi);
            int idx = static_cast<int>(it - sValues.begin());
            sValues.insert(sValues.begin() + idx, sMid);
            samples.insert(samples.begin() + idx, pMid);

            // Recurse
            subdivide(sLo, sMid, depth + 1);
            subdivide(sMid, sHi, depth + 1);
        };

    // Subdivide each initial segment
    for (int i = 0; i < static_cast<int>(sValues.size()) - 1; i++) {
        subdivide(sValues[i], sValues[i + 1], 0);
    }

    return samples;
}

// ─── SegmentSequence ───────────────────────────────────────
//
// A non-owning view over a sequence of GeometrySegments that provides
// unified access across segment boundaries.
//
// This is the primary geometry interface for RoadV2. It handles:
//   - Arc-length to (segment, s) lookup
//   - Position/heading/curvature at any arc-length
//   - Sampling across multiple segments
//
// SegmentSequence does not own the segments — the owner (RoadV2) retains
// ownership and guarantees lifetime. This allows cheap construction from
// a vector of pointers.
//
class SegmentSequence {
public:
    // Empty sequence
    SegmentSequence() = default;

    // Construct from a vector of non-owning pointers.
    // The pointers must remain valid for the lifetime of SegmentSequence.
    explicit SegmentSequence(std::vector<const GeometrySegment*> segments)
        : segments_(std::move(segments)) {
        computeLengths();
    }

    // Number of segments
    int numSegments() const { return static_cast<int>(segments_.size()); }

    // Access a segment by index (read-only)
    const GeometrySegment& segment(int idx) const {
        return *segments_[idx];
    }

    // Total length (meters)
    double totalLength() const { return totalLength_; }

    // Find the segment containing arc-length s, and the local s within that segment.
    // Returns (segmentIndex, localS), where localS ∈ [0, segment.length()].
    // s is clamped to valid range.
    std::pair<int, double> locate(double s) const {
        if (segments_.empty()) return {0, 0.0};

        if (s <= 0.0) return {0, 0.0};
        if (s >= totalLength_) return {numSegments() - 1, segments_.back()->length()};

        // Binary search over cumulative lengths
        int lo = 0, hi = numSegments() - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (cumulativeLengths_[mid] < s) lo = mid + 1;
            else hi = mid;
        }

        int segIdx = lo;
        double segStart = (segIdx > 0) ? cumulativeLengths_[segIdx - 1] : 0.0;
        double localS = s - segStart;

        return {segIdx, localS};
    }

    // Position at arc-length s
    Point2D positionAt(double s) const {
        auto [segIdx, localS] = locate(s);
        return segments_[segIdx]->positionAt(localS);
    }

    // Heading at arc-length s (radians)
    double headingAt(double s) const {
        auto [segIdx, localS] = locate(s);
        return segments_[segIdx]->headingAt(localS);
    }

    // Tangent at arc-length s
    Vec2 tangentAt(double s) const {
        auto [segIdx, localS] = locate(s);
        return segments_[segIdx]->tangentAt(localS);
    }

    // Curvature at arc-length s (1/radius)
    double curvatureAt(double s) const {
        auto [segIdx, localS] = locate(s);
        return segments_[segIdx]->curvatureDS(localS);
    }

    // Sample position at N points across the entire sequence
    std::vector<Point2D> sample(int numSamples) const {
        if (numSamples < 2) numSamples = 2;
        std::vector<Point2D> result;
        result.reserve(numSamples);
        for (int i = 0; i < numSamples; i++) {
            double s = totalLength_ * static_cast<double>(i) / (numSamples - 1);
            result.push_back(positionAt(s));
        }
        return result;
    }

    // Sample with uniform arc-length spacing
    std::vector<Point2D> sampleAdaptive(
        double maxError = MAX_GEOM_ERROR_HORIZONTAL,
        int minSamples = 2,
        int maxSamples = 10000
    ) const {
        std::vector<Point2D> result;
        if (segments_.empty()) return result;

        // Sample each segment adaptively, then concatenate (with overlap at boundaries)
        for (int i = 0; i < numSegments(); i++) {
            auto segSamples = segments_[i]->sampleAdaptive(maxError, minSamples, maxSamples);
            // Append (avoid duplicating the first point, which is the last point of previous segment)
            for (size_t j = (i == 0 ? 0 : 1); j < segSamples.size(); j++) {
                result.push_back(segSamples[j]);
            }
        }
        return result;
    }

private:
    std::vector<const GeometrySegment*> segments_;
    std::vector<double> cumulativeLengths_;
    double totalLength_ = 0.0;

    void computeLengths() {
        cumulativeLengths_.resize(segments_.size());
        totalLength_ = 0.0;
        for (size_t i = 0; i < segments_.size(); i++) {
            double len = segments_[i]->length();
            totalLength_ += len;
            cumulativeLengths_[i] = totalLength_;
        }
    }
};

// ─── Line Segment ──────────────────────────────────────────
//
// Straight line from startPoint to endPoint.
// Curvature is always 0. Heading is constant.
//
class LineSegment : public GeometrySegment {
public:
    Point2D p0;  // start point (absolute)
    Point2D p1;  // end point (absolute)

    LineSegment() = default;
    LineSegment(const Point2D& start, const Point2D& end) : p0(start), p1(end) {}

    void evaluateDS(double s, double& x, double& y, double& heading) const override {
        double len = length();
        double t = (len > EPSILON) ? s / len : 0.0;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
        x = p0.x + (p1.x - p0.x) * t;
        y = p0.y + (p1.y - p0.y) * t;
        heading = std::atan2(p1.y - p0.y, p1.x - p0.x);
    }

    double curvatureDS(double /*s*/) const override { return 0.0; }

    double length() const override { return p0.distanceTo(p1); }

    GeometryType type() const override { return GeometryType::Line; }

    std::unique_ptr<GeometrySegment> clone() const override {
        return std::make_unique<LineSegment>(*this);
    }
};

// ─── Arc Segment ───────────────────────────────────────────
//
// Circular arc with constant curvature.
//
// Storage (OpenDRIVE-style): startPoint, startHeading, curvature, arcLength.
//   - Positive curvature = left turn (CCW)
//   - Negative curvature = right turn (CW)
//   - curvature = 0 is invalid (use LineSegment instead)
//
// Derived accessors (computed from stored fields, not a second source of truth):
//   - radius() = 1 / |curvature|
//   - center() = startPoint + normalLeft * radius, where normalLeft is
//     90° CCW from startHeading, sign-adjusted for curvature direction
//
// evaluateDS(s):
//   heading(s) = startHeading + curvature * s
//   The arc sweeps angle = curvature * arcLength total.
//   Position is computed by integrating along the circle:
//     For a circle of radius R centered at C, starting at angle θ₀:
//       angle(s) = θ₀ + curvature * s
//       x(s) = C.x + R * cos(angle(s))
//       y(s) = C.y + R * sin(angle(s))
//
class ArcSegment : public GeometrySegment {
public:
    Point2D startPoint_;
    double startHeading_;
    double curvature_;   // signed: + = left/CCW, - = right/CW
    double arcLength_;

    ArcSegment() = default;
    ArcSegment(const Point2D& start, double heading, double curvature, double length)
        : startPoint_(start), startHeading_(heading), curvature_(curvature), arcLength_(length) {}

    // ─── Derived accessors (geometric view, not stored) ───

    // Radius (always positive)
    double radius() const {
        return std::abs(1.0 / curvature_);
    }

    // Arc center (computed from start point, heading, and curvature)
    // For left turn (κ>0): center is to the left of the start heading
    // For right turn (κ<0): center is to the right of the start heading
    Point2D center() const {
        double r = 1.0 / curvature_;  // signed radius (negative for right turn)
        // Left normal of start heading: (-sin(h), cos(h))
        // Center = start + leftNormal * r
        // For κ>0 (left turn): r>0, center is left ✓
        // For κ<0 (right turn): r<0, center is right ✓ (leftNormal * negative = right)
        double nx = -std::sin(startHeading_);
        double ny = std::cos(startHeading_);
        return {startPoint_.x + nx * r, startPoint_.y + ny * r};
    }

    // Total sweep angle (radians) — signed, same sign as curvature
    double sweepAngle() const {
        return curvature_ * arcLength_;
    }

    // ─── Core interface ───

    void evaluateDS(double s, double& x, double& y, double& heading) const override {
        // Clamp s to valid range
        if (s < 0.0) s = 0.0;
        if (s > arcLength_) s = arcLength_;

        heading = startHeading_ + curvature_ * s;

        // Compute position on circle
        // Start angle from center to startPoint:
        //   For left turn (κ>0): startAngle = startHeading - π/2
        //   For right turn (κ<0): startAngle = startHeading + π/2
        // Unified: startAngle = startHeading - sign(κ) * π/2
        // But using the center() approach is cleaner:
        Point2D c = center();
        double r = radius();
        // Angle from center to start point
        double startAngleFromCenter = std::atan2(startPoint_.y - c.y, startPoint_.x - c.x);
        // Current angle: startAngle + (curvature * s) — same sign as curvature
        // For left turn (κ>0): angle increases (CCW) ✓
        // For right turn (κ<0): angle decreases (CW) ✓
        double angle = startAngleFromCenter + curvature_ * s;
        x = c.x + r * std::cos(angle);
        y = c.y + r * std::sin(angle);
    }

    double curvatureDS(double /*s*/) const override { return curvature_; }

    double length() const override { return arcLength_; }

    GeometryType type() const override { return GeometryType::Arc; }

    std::unique_ptr<GeometrySegment> clone() const override {
        return std::make_unique<ArcSegment>(*this);
    }
};

// ─── Spiral Segment (Clothoid / Euler Spiral) ──────────────
//
// Curvature varies linearly from κ₀ to κ₁ over the segment length.
//
// Storage (OpenDRIVE-style): startPoint, startHeading, curvatureStart,
// curvatureEnd, segmentLength.
//   - Positive curvature = left turn (CCW)
//   - Negative curvature = right turn (CW)
//
// curvatureDS(s) = κ₀ + (κ₁ - κ₀) · s / L
// heading(s)     = startHeading + κ₀·s + (κ₁-κ₀)·s²/(2L)
// position(s)    = startPoint + R(θ₀) · ∫₀ˢ [cos(φ(t)), sin(φ(t))] dt
//   where φ(t) = κ₀·t + (κ₁-κ₀)·t²/(2L)  (local heading, before startHeading rotation)
//   and R(θ₀) is rotation by startHeading
//
// Degenerate cases (κ₀ == κ₁, or both zero) are handled at the factory/
// adapter boundary, NOT inside evaluateDS. SpiralSegment always does the
// general Fresnel math. The adapter constructs ArcSegment when κ₀ == κ₁
// (nonzero) or LineSegment when both are zero.
//
class SpiralSegment : public GeometrySegment {
public:
    Point2D startPoint_;
    double startHeading_;
    double curvatureStart_;  // κ₀
    double curvatureEnd_;    // κ₁
    double segmentLength_;   // L

    SpiralSegment() = default;
    SpiralSegment(const Point2D& start, double heading,
                  double kappa0, double kappa1, double length)
        : startPoint_(start), startHeading_(heading),
          curvatureStart_(kappa0), curvatureEnd_(kappa1), segmentLength_(length) {}

    // ─── Derived accessors ───

    // Clothoid parameter A (A² = L / |κ₁ - κ₀| when κ₁ ≠ κ₀)
    // Returns infinity when κ₀ == κ₁ (degenerate spiral = arc)
    double clothoidA() const {
        double dk = std::abs(curvatureEnd_ - curvatureStart_);
        if (dk < EPSILON) return std::numeric_limits<double>::infinity();
        return std::sqrt(segmentLength_ / dk);
    }

    // Curvature rate of change (c_dot = (κ₁ - κ₀) / L)
    double curvatureRate() const {
        if (segmentLength_ < EPSILON) return 0.0;
        return (curvatureEnd_ - curvatureStart_) / segmentLength_;
    }

    // Total heading change = κ₀·L + (κ₁-κ₀)·L/2 = L·(κ₀ + κ₁)/2
    double totalAngleChange() const {
        return segmentLength_ * (curvatureStart_ + curvatureEnd_) / 2.0;
    }

    // ─── Core interface ───

    double curvatureDS(double s) const override {
        if (segmentLength_ < EPSILON) return curvatureStart_;
        return curvatureStart_ + (curvatureEnd_ - curvatureStart_) * s / segmentLength_;
    }

    double length() const override { return segmentLength_; }

    GeometryType type() const override { return GeometryType::Spiral; }

    void evaluateDS(double s, double& x, double& y, double& heading) const override {
        // Clamp s
        if (s < 0.0) s = 0.0;
        if (s > segmentLength_) s = segmentLength_;

        // Heading: startHeading + κ₀·s + (κ₁-κ₀)·s²/(2L)
        double L = segmentLength_;
        double k0 = curvatureStart_;
        double k1 = curvatureEnd_;
        double dk = k1 - k0;

        heading = startHeading_;
        if (L > EPSILON) {
            heading += k0 * s + dk * s * s / (2.0 * L);
        }

        // Position via shared Simpson's rule integration:
        //   φ(t) = k0·t + dk·t²/(2L)   (local heading, before startHeading rotation)
        //   localX = ∫₀ˢ cos(φ(t)) dt
        //   localY = ∫₀ˢ sin(φ(t)) dt
        // Then rotate by startHeading and translate by startPoint.
        auto [localX, localY] = simpsonIntegrate2D(
            [k0, dk, L](double t) -> std::pair<double, double> {
                double phi = (L > EPSILON) ? (k0 * t + dk * t * t / (2.0 * L)) : 0.0;
                return {std::cos(phi), std::sin(phi)};
            },
            s, 100
        );

        // Rotate local position by startHeading, translate to startPoint
        double c = std::cos(startHeading_);
        double sn = std::sin(startHeading_);
        x = startPoint_.x + localX * c - localY * sn;
        y = startPoint_.y + localX * sn + localY * c;
    }

    std::unique_ptr<GeometrySegment> clone() const override {
        return std::make_unique<SpiralSegment>(*this);
    }
};

// ─── Bezier Segment (cubic Bezier) ─────────────────────────
//
// Cubic Bezier with 4 absolute control points (P0, P1, P2, P3).
// This is the exception to the start+heading+curvature parameterization
// (Q2 decision): Bezier stores absolute control points because that's
// the natural representation for a cubic curve.
//
// Arc-length parameterization:
//   Bezier is naturally t ∈ [0,1], but evaluateDS takes s ∈ [0, length()].
//   A lookup table (N=100 samples) maps s → t at construction time.
//   Construction cost: ~100 bezier evals + 100 distance calcs ≈ microseconds.
//
// Curvature:
//   κ(t) = (B'(t) × B''(t)) / |B'(t)|³
//   B'(t)  = 3(1-t)²(P1-P0) + 6(1-t)t(P2-P1) + 3t²(P3-P2)
//   B''(t) = 6(1-t)(P2-2P1+P0) + 6t(P3-2P2+P1)
//   Guard: when |B'(t)| < epsilon, return curvature 0 (cusp/degenerate).
//   This happens when P1==P0 (zero handle at start) or P2==P3 (zero handle
//   at end) — normal editor states, not exotic edge cases.
//
class BezierSegment : public GeometrySegment {
public:
    Point2D p0, p1, p2, p3;  // absolute control points

private:
    // Arc-length lookup table (built at construction)
    std::vector<double> tTable_;        // t values [0, 1], N+1 entries
    std::vector<double> arcLengthTable_; // cumulative arc length, N+1 entries
    double totalLength_ = 0.0;

    static constexpr int TABLE_N = 100;

    void buildArcLengthTable() {
        tTable_.resize(TABLE_N + 1);
        arcLengthTable_.resize(TABLE_N + 1);
        arcLengthTable_[0] = 0.0;
        tTable_[0] = 0.0;
        Point2D prev = p0;
        double cumLen = 0.0;
        for (int i = 1; i <= TABLE_N; i++) {
            double t = static_cast<double>(i) / TABLE_N;
            tTable_[i] = t;
            Point2D pt = bezierCubic(p0, p1, p2, p3, t);
            cumLen += prev.distanceTo(pt);
            arcLengthTable_[i] = cumLen;
            prev = pt;
        }
        totalLength_ = cumLen;
    }

    // Binary search: s → t using the arc-length lookup table
    double sToT(double s) const {
        if (totalLength_ < EPSILON) return 0.0;
        if (s <= 0.0) return 0.0;
        if (s >= totalLength_) return 1.0;

        // Binary search in arcLengthTable_
        int lo = 0, hi = TABLE_N;
        while (lo < hi - 1) {
            int mid = (lo + hi) / 2;
            if (arcLengthTable_[mid] < s) lo = mid;
            else hi = mid;
        }
        // Linear interpolate between lo and hi
        double sLo = arcLengthTable_[lo];
        double sHi = arcLengthTable_[hi];
        double frac = (sHi > sLo) ? (s - sLo) / (sHi - sLo) : 0.0;
        return tTable_[lo] + frac * (tTable_[hi] - tTable_[lo]);
    }

    // First derivative B'(t)
    Point2D derivative(double t) const {
        double mt = 1.0 - t;
        return (p1 - p0) * (3.0 * mt * mt)
             + (p2 - p1) * (6.0 * mt * t)
             + (p3 - p2) * (3.0 * t * t);
    }

    // Second derivative B''(t)
    Point2D secondDerivative(double t) const {
        double mt = 1.0 - t;
        return (p2 - p1 * 2.0 + p0) * (6.0 * mt)
             + (p3 - p2 * 2.0 + p1) * (6.0 * t);
    }

public:
    BezierSegment() {
        buildArcLengthTable();
    }

    BezierSegment(const Point2D& cp0, const Point2D& cp1, const Point2D& cp2, const Point2D& cp3)
        : p0(cp0), p1(cp1), p2(cp2), p3(cp3) {
        buildArcLengthTable();
    }

    // Rebuild table if control points change (for mutable use)
    void rebuild() { buildArcLengthTable(); }

    void evaluateDS(double s, double& x, double& y, double& heading) const override {
        if (s < 0.0) s = 0.0;
        if (s > totalLength_) s = totalLength_;

        double t = sToT(s);
        Point2D pos = bezierCubic(p0, p1, p2, p3, t);
        x = pos.x;
        y = pos.y;

        Point2D d = derivative(t);
        double dMag = d.norm();
        if (dMag < EPSILON) {
            // Degenerate: zero velocity (cusp). Try neighboring t.
            double tEps = (t < 0.5) ? t + 0.001 : t - 0.001;
            tEps = std::max(0.0, std::min(1.0, tEps));
            d = derivative(tEps);
            dMag = d.norm();
        }
        heading = (dMag > EPSILON) ? std::atan2(d.y, d.x) : 0.0;
    }

    double curvatureDS(double s) const override {
        if (totalLength_ < EPSILON) return 0.0;
        if (s < 0.0) s = 0.0;
        if (s > totalLength_) s = totalLength_;

        double t = sToT(s);
        Point2D d1 = derivative(t);
        Point2D d2 = secondDerivative(t);
        double d1Mag = d1.norm();

        // Guard: |B'(t)|³ in denominator. When |B'(t)| ≈ 0 (cusp at P1==P0
        // or P2==P3), return 0 instead of dividing by zero.
        if (d1Mag < EPSILON) return 0.0;

        // 2D cross product: d1 × d2 = d1.x*d2.y - d1.y*d2.x
        double cross = d1.x * d2.y - d1.y * d2.x;
        return cross / (d1Mag * d1Mag * d1Mag);
    }

    double length() const override { return totalLength_; }

    GeometryType type() const override { return GeometryType::Bezier; }

    std::unique_ptr<GeometrySegment> clone() const override {
        // Copy constructor rebuilds the table (control points are the source
        // of truth, table is derived). This is correct but costs one rebuild.
        // If profiling shows this is hot, add a copy constructor that copies
        // the table directly.
        return std::make_unique<BezierSegment>(*this);
    }
};

} // namespace geo