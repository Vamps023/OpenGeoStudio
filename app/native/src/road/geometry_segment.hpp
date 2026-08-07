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

} // namespace geo
