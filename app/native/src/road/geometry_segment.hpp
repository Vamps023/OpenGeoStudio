#pragma once

// ═══════════════════════════════════════════════════════════
// Geometry Segment — Polymorphic road geometry primitives
// ═══════════════════════════════════════════════════════════
//
// This is the new polymorphic geometry system that will eventually
// replace the flat ControlPoint[] model. Each road segment is a
// GeometrySegment subclass (Line, Arc, Spiral, Bezier) that can
// evaluate its position, heading, and curvature at any arc-length
// distance s along the segment.
//
// Design decisions (see docs/ROAD_ENGINE_MIGRATION_PLAN.md):
// - Heap-allocated via unique_ptr (Q1: simpler than variant, more extensible)
// - clone() virtual method for deep-copy (undo/redo compatibility)
// - Absolute coordinates (Q2: adapter handles relative→absolute at boundary)
// - s is arc-length in meters from segment start [0, length()]
// - All segments are planar (2D); elevation is handled separately
//
// Phase 1.1: Abstract base + LineSegment only.
// Arc/Spiral/Bezier will be added in tasks 1.3–1.5.

#include "geometry.hpp"
#include <memory>
#include <string>

namespace geo {

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

    // ─── Adaptive sampling (task 1.6 will override with curvature-based) ───
    // Default: uniform sampling. Subclasses may override for better resolution.
    virtual std::vector<Point2D> sampleAdaptive(double maxError = 0.01, int minSamples = 2, int maxSamples = 1000) const;

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

// ─── Default adaptive sampling (uniform fallback) ──────────
inline std::vector<Point2D> GeometrySegment::sampleAdaptive(
    double /*maxError*/, int minSamples, int maxSamples
) const {
    // Default: uniform sampling with at least minSamples
    // Task 1.6 will replace this with curvature-based adaptive sampling
    int n = std::max(minSamples, std::min(maxSamples, 32));
    return sampleUniform(n);
}

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

} // namespace geo
