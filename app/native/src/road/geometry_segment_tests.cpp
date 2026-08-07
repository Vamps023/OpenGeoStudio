// ═══════════════════════════════════════════════════════════
// Geometry Segment Unit Tests (doctest)
// ═══════════════════════════════════════════════════════════
//
// Pure C++ unit tests for the geometry kernel.
// Run with: build and execute this test executable.
// doctest is header-only — no external dependency beyond the header.
//
// Test split (per docs/ROAD_ENGINE_MIGRATION_PLAN.md Q5):
// - doctest (this file): pure geometry math (evaluateDS, curvature, etc.)
// - vitest (TS via bridge): IPC round-trips, mesh output, store behavior

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "geometry_segment.hpp"
#include "geometry.hpp"
#include <cmath>

using geo::LineSegment;
using geo::Point2D;
using geo::Vec2;
using geo::GeometryType;
using geo::MAX_GEOM_ERROR_HORIZONTAL;
using geo::MAX_GEOM_ERROR_VERTICAL;
using geo::GEOM_TOLERANCE;
using geo::MAX_GEOM_LENGTH;
using geo::MIN_GEOM_LENGTH;
using geo::ADAPTIVE_MAX_DEPTH;

// ─── LineSegment Tests ─────────────────────────────────────

TEST_CASE("LineSegment: basic construction and type") {
    LineSegment seg({0, 0}, {10, 0});
    CHECK(seg.type() == GeometryType::Line);
    CHECK(seg.length() == doctest::Approx(10.0));
}

TEST_CASE("LineSegment: evaluateDS at s=0 returns start point") {
    LineSegment seg({1, 2}, {11, 2});
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(x == doctest::Approx(1.0));
    CHECK(y == doctest::Approx(2.0));
}

TEST_CASE("LineSegment: evaluateDS at s=length returns end point") {
    LineSegment seg({0, 0}, {10, 0});
    double x, y, h;
    seg.evaluateDS(10, x, y, h);
    CHECK(x == doctest::Approx(10.0));
    CHECK(y == doctest::Approx(0.0));
}

TEST_CASE("LineSegment: evaluateDS at midpoint") {
    LineSegment seg({0, 0}, {10, 0});
    double x, y, h;
    seg.evaluateDS(5, x, y, h);
    CHECK(x == doctest::Approx(5.0));
    CHECK(y == doctest::Approx(0.0));
}

TEST_CASE("LineSegment: heading is constant (east direction)") {
    LineSegment seg({0, 0}, {10, 0});
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(h == doctest::Approx(0.0));  // east = 0 radians
    seg.evaluateDS(5, x, y, h);
    CHECK(h == doctest::Approx(0.0));
    seg.evaluateDS(10, x, y, h);
    CHECK(h == doctest::Approx(0.0));
}

TEST_CASE("LineSegment: heading is constant (north direction)") {
    LineSegment seg({0, 0}, {0, 10});
    double x, y, h;
    seg.evaluateDS(5, x, y, h);
    CHECK(h == doctest::Approx(geo::HALF_PI));  // north = π/2
}

TEST_CASE("LineSegment: heading is constant (diagonal)") {
    LineSegment seg({0, 0}, {10, 10});
    double x, y, h;
    seg.evaluateDS(5, x, y, h);
    CHECK(h == doctest::Approx(geo::PI / 4.0));  // 45° = π/4
}

TEST_CASE("LineSegment: curvature is always 0") {
    LineSegment seg({0, 0}, {10, 5});
    CHECK(seg.curvatureDS(0) == doctest::Approx(0.0));
    CHECK(seg.curvatureDS(5) == doctest::Approx(0.0));
    CHECK(seg.curvatureDS(10) == doctest::Approx(0.0));
}

TEST_CASE("LineSegment: length matches Euclidean distance") {
    LineSegment seg({0, 0}, {3, 4});  // 3-4-5 triangle
    CHECK(seg.length() == doctest::Approx(5.0));
}

TEST_CASE("LineSegment: length for zero-length segment") {
    LineSegment seg({5, 5}, {5, 5});
    CHECK(seg.length() == doctest::Approx(0.0));
}

TEST_CASE("LineSegment: evaluateDS clamps out-of-range s") {
    LineSegment seg({0, 0}, {10, 0});
    double x, y, h;
    // s < 0 should clamp to start
    seg.evaluateDS(-5, x, y, h);
    CHECK(x == doctest::Approx(0.0));
    CHECK(y == doctest::Approx(0.0));
    // s > length should clamp to end
    seg.evaluateDS(15, x, y, h);
    CHECK(x == doctest::Approx(10.0));
    CHECK(y == doctest::Approx(0.0));
}

TEST_CASE("LineSegment: evaluateDS on zero-length segment doesn't crash") {
    LineSegment seg({5, 5}, {5, 5});
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(x == doctest::Approx(5.0));
    CHECK(y == doctest::Approx(5.0));
}

TEST_CASE("LineSegment: positionAt convenience method") {
    LineSegment seg({0, 0}, {10, 0});
    Point2D p = seg.positionAt(7);
    CHECK(p.x == doctest::Approx(7.0));
    CHECK(p.y == doctest::Approx(0.0));
}

TEST_CASE("LineSegment: startPoint and endPoint") {
    LineSegment seg({1, 2}, {11, 7});
    CHECK(seg.startPoint().x == doctest::Approx(1.0));
    CHECK(seg.startPoint().y == doctest::Approx(2.0));
    CHECK(seg.endPoint().x == doctest::Approx(11.0));
    CHECK(seg.endPoint().y == doctest::Approx(7.0));
}

TEST_CASE("LineSegment: tangentAt returns unit vector") {
    LineSegment seg({0, 0}, {10, 0});
    Vec2 t = seg.tangentAt(5);
    CHECK(t.x == doctest::Approx(1.0));
    CHECK(t.y == doctest::Approx(0.0));
    CHECK(t.norm() == doctest::Approx(1.0));
}

TEST_CASE("LineSegment: normalAt is perpendicular to tangent (left)") {
    LineSegment seg({0, 0}, {10, 0});  // east
    Vec2 n = seg.normalAt(5);
    CHECK(n.x == doctest::Approx(0.0));
    CHECK(n.y == doctest::Approx(1.0));  // north = left of east
}

TEST_CASE("LineSegment: sampleUniform returns correct count") {
    LineSegment seg({0, 0}, {10, 0});
    auto pts = seg.sampleUniform(5);
    CHECK(pts.size() == 5);
    CHECK(pts[0].x == doctest::Approx(0.0));
    CHECK(pts[4].x == doctest::Approx(10.0));
    CHECK(pts[2].x == doctest::Approx(5.0));  // midpoint
}

TEST_CASE("LineSegment: sampleUniform with min 2 samples") {
    LineSegment seg({0, 0}, {10, 0});
    auto pts = seg.sampleUniform(2);
    CHECK(pts.size() == 2);
    CHECK(pts[0].x == doctest::Approx(0.0));
    CHECK(pts[1].x == doctest::Approx(10.0));
}

TEST_CASE("LineSegment: clone produces equal segment") {
    LineSegment seg({3, 4}, {13, 9});
    auto cloned = seg.clone();
    CHECK(cloned->type() == GeometryType::Line);
    CHECK(cloned->length() == doctest::Approx(seg.length()));
    CHECK(cloned->startPoint().x == doctest::Approx(3.0));
    CHECK(cloned->startPoint().y == doctest::Approx(4.0));
    CHECK(cloned->endPoint().x == doctest::Approx(13.0));
    CHECK(cloned->endPoint().y == doctest::Approx(9.0));
}

TEST_CASE("LineSegment: clone is independent (modifying original doesn't affect clone)") {
    LineSegment seg({0, 0}, {10, 0});
    auto cloned = seg.clone();
    // Modify original
    seg.p1 = {20, 0};
    // Clone should be unchanged
    CHECK(cloned->length() == doctest::Approx(10.0));
    CHECK(seg.length() == doctest::Approx(20.0));
}

TEST_CASE("LineSegment: diagonal segment evaluation") {
    LineSegment seg({0, 0}, {6, 8});  // 6-8-10 triangle
    CHECK(seg.length() == doctest::Approx(10.0));
    double x, y, h;
    seg.evaluateDS(5, x, y, h);  // halfway
    CHECK(x == doctest::Approx(3.0));
    CHECK(y == doctest::Approx(4.0));
    CHECK(h == doctest::Approx(std::atan2(8, 6)));
}

TEST_CASE("LineSegment: negative direction segment") {
    LineSegment seg({10, 10}, {0, 0});  // going SW
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(x == doctest::Approx(10.0));
    CHECK(y == doctest::Approx(10.0));
    seg.evaluateDS(14.1421356, x, y, h);  // sqrt(200) ≈ 14.14
    CHECK(x == doctest::Approx(0.0).epsilon(0.01));
    CHECK(y == doctest::Approx(0.0).epsilon(0.01));
    // Heading should be SW = -3π/4
    CHECK(h == doctest::Approx(-3.0 * geo::PI / 4.0).epsilon(0.01));
}

// ═══════════════════════════════════════════════════════════
// ArcSegment Tests
// ═══════════════════════════════════════════════════════════

using geo::ArcSegment;

TEST_CASE("ArcSegment: basic construction and type") {
    ArcSegment seg({0, 0}, 0.0, 0.1, 10.0);  // start at origin, heading east, κ=0.1, L=10
    CHECK(seg.type() == GeometryType::Arc);
    CHECK(seg.length() == doctest::Approx(10.0));
    CHECK(seg.curvatureDS(0) == doctest::Approx(0.1));
    CHECK(seg.curvatureDS(5) == doctest::Approx(0.1));  // constant curvature
}

TEST_CASE("ArcSegment: radius is 1/|curvature|") {
    ArcSegment seg({0, 0}, 0.0, 0.1, 10.0);
    CHECK(seg.radius() == doctest::Approx(10.0));  // 1/0.1 = 10
    ArcSegment seg2({0, 0}, 0.0, -0.2, 5.0);
    CHECK(seg2.radius() == doctest::Approx(5.0));  // 1/0.2 = 5
}

TEST_CASE("ArcSegment: sweepAngle = curvature * length") {
    ArcSegment seg({0, 0}, 0.0, 0.1, 10.0);
    CHECK(seg.sweepAngle() == doctest::Approx(1.0));  // 0.1 * 10 = 1.0 rad
    ArcSegment seg2({0, 0}, 0.0, -0.1, 10.0);
    CHECK(seg2.sweepAngle() == doctest::Approx(-1.0));  // negative = right turn
}

TEST_CASE("ArcSegment: center is to the left for positive curvature (CCW)") {
    // Start at origin, heading east (0), curvature = 0.1 (left turn)
    // Center should be at (0, 10) — 10m to the left (north)
    ArcSegment seg({0, 0}, 0.0, 0.1, 10.0);
    Point2D c = seg.center();
    CHECK(c.x == doctest::Approx(0.0).epsilon(0.001));
    CHECK(c.y == doctest::Approx(10.0).epsilon(0.001));
}

TEST_CASE("ArcSegment: center is to the right for negative curvature (CW)") {
    // Start at origin, heading east (0), curvature = -0.1 (right turn)
    // Center should be at (0, -10) — 10m to the right (south)
    ArcSegment seg({0, 0}, 0.0, -0.1, 10.0);
    Point2D c = seg.center();
    CHECK(c.x == doctest::Approx(0.0).epsilon(0.001));
    CHECK(c.y == doctest::Approx(-10.0).epsilon(0.001));
}

TEST_CASE("ArcSegment: evaluateDS at s=0 returns start point and heading") {
    ArcSegment seg({5, 3}, 0.5, 0.1, 10.0);
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(x == doctest::Approx(5.0));
    CHECK(y == doctest::Approx(3.0));
    CHECK(h == doctest::Approx(0.5));  // start heading
}

TEST_CASE("ArcSegment: evaluateDS heading changes linearly") {
    // Start heading east (0), curvature 0.1, length 10
    // At s=5: heading = 0 + 0.1*5 = 0.5
    // At s=10: heading = 0 + 0.1*10 = 1.0
    ArcSegment seg({0, 0}, 0.0, 0.1, 10.0);
    double x, y, h;
    seg.evaluateDS(5, x, y, h);
    CHECK(h == doctest::Approx(0.5));
    seg.evaluateDS(10, x, y, h);
    CHECK(h == doctest::Approx(1.0));
}

TEST_CASE("ArcSegment: evaluateDS position traces a circle") {
    // Quarter circle: start at (10, 0), heading north (π/2), curvature 0.1 (left)
    // Center at (0, 0), radius 10
    // At s = quarter circumference = π*R/2 = 5π ≈ 15.708
    // End should be at (0, 10) heading west (π)
    double R = 10.0;
    double kappa = 1.0 / R;  // 0.1
    double quarterLen = geo::PI * R / 2.0;  // 5π ≈ 15.708
    ArcSegment seg({R, 0}, geo::HALF_PI, kappa, quarterLen);

    Point2D c = seg.center();
    CHECK(c.x == doctest::Approx(0.0).epsilon(0.01));
    CHECK(c.y == doctest::Approx(0.0).epsilon(0.01));

    // End point should be at (0, R) = (0, 10)
    Point2D end = seg.endPoint();
    CHECK(end.x == doctest::Approx(0.0).epsilon(0.01));
    CHECK(end.y == doctest::Approx(R).epsilon(0.01));

    // End heading should be π (west)
    double x, y, h;
    seg.evaluateDS(quarterLen, x, y, h);
    CHECK(h == doctest::Approx(geo::PI).epsilon(0.01));
}

TEST_CASE("ArcSegment: evaluateDS midpoint of quarter circle") {
    // Quarter circle from (10,0) heading north, center (0,0), R=10
    // Midpoint at s = quarterLen/2 = 5π/2
    // Angle from center = 90° + 45° = 135° (measuring from center)
    // Position = (10*cos(135°), 10*sin(135°)) = (-7.07, 7.07)
    double R = 10.0;
    double kappa = 1.0 / R;
    double quarterLen = geo::PI * R / 2.0;
    ArcSegment seg({R, 0}, geo::HALF_PI, kappa, quarterLen);

    double s = quarterLen / 2.0;
    double x, y, h;
    seg.evaluateDS(s, x, y, h);
    // At midpoint, heading = π/2 + κ*s = π/2 + 0.1 * 5π/2 = π/2 + π/4 = 3π/4
    CHECK(h == doctest::Approx(3.0 * geo::PI / 4.0).epsilon(0.01));
    // Position: angle from center = startAngle + κ*s
    //   startAngle = atan2(0-0, 10-0) = 0
    //   angle = 0 + 0.1 * 5π/2 = π/4
    //   pos = (10*cos(π/4), 10*sin(π/4)) = (7.07, 7.07)
    CHECK(x == doctest::Approx(7.071).epsilon(0.01));
    CHECK(y == doctest::Approx(7.071).epsilon(0.01));
}

TEST_CASE("ArcSegment: right turn (negative curvature)") {
    // Start at (0, 0), heading east (0), curvature = -0.1 (right turn)
    // Center at (0, -10)
    // At s = quarterLen = 5π, heading = 0 + (-0.1)*5π = -π/2 (south)
    // End position: angle from center = 0 + (-0.1)*5π = -π/2
    //   pos = center + R*(cos(-π/2), sin(-π/2)) = (0,-10) + 10*(0,-1) = (0, -20)
    // Wait, let me recalculate. Start at (0,0), center at (0,-10).
    // startAngle from center = atan2(0-(-10), 0-0) = atan2(10, 0) = π/2
    // angle at end = π/2 + (-0.1)*5π = π/2 - π/2 = 0
    // pos = (0 + 10*cos(0), -10 + 10*sin(0)) = (10, -10)
    double R = 10.0;
    double kappa = -1.0 / R;
    double quarterLen = geo::PI * R / 2.0;
    ArcSegment seg({0, 0}, 0.0, kappa, quarterLen);

    Point2D c = seg.center();
    CHECK(c.x == doctest::Approx(0.0).epsilon(0.01));
    CHECK(c.y == doctest::Approx(-10.0).epsilon(0.01));

    // End heading = 0 + (-0.1)*5π = -π/2 (south)
    double x, y, h;
    seg.evaluateDS(quarterLen, x, y, h);
    CHECK(h == doctest::Approx(-geo::HALF_PI).epsilon(0.01));

    // End position = (10, -10)
    CHECK(x == doctest::Approx(10.0).epsilon(0.01));
    CHECK(y == doctest::Approx(-10.0).epsilon(0.01));
}

TEST_CASE("ArcSegment: curvature is constant for all s") {
    ArcSegment seg({1, 2}, 0.3, 0.05, 50.0);
    CHECK(seg.curvatureDS(0) == doctest::Approx(0.05));
    CHECK(seg.curvatureDS(25) == doctest::Approx(0.05));
    CHECK(seg.curvatureDS(50) == doctest::Approx(0.05));
}

TEST_CASE("ArcSegment: evaluateDS clamps out-of-range s") {
    ArcSegment seg({0, 0}, 0.0, 0.1, 10.0);
    double x, y, h;
    // s < 0 should clamp to start
    seg.evaluateDS(-5, x, y, h);
    CHECK(x == doctest::Approx(0.0));
    CHECK(y == doctest::Approx(0.0));
    CHECK(h == doctest::Approx(0.0));
    // s > length should clamp to end
    seg.evaluateDS(15, x, y, h);
    CHECK(h == doctest::Approx(1.0));  // 0.1 * 10 = 1.0
}

TEST_CASE("ArcSegment: clone produces equal and independent segment") {
    ArcSegment seg({3, 4}, 0.5, 0.1, 20.0);
    auto cloned = seg.clone();
    CHECK(cloned->type() == GeometryType::Arc);
    CHECK(cloned->length() == doctest::Approx(20.0));
    CHECK(cloned->curvatureDS(0) == doctest::Approx(0.1));
    CHECK(cloned->startPoint().x == doctest::Approx(3.0));
    CHECK(cloned->startPoint().y == doctest::Approx(4.0));

    // Modify original — clone should be unchanged
    seg.curvature_ = 0.2;
    CHECK(cloned->curvatureDS(0) == doctest::Approx(0.1));
    CHECK(seg.curvatureDS(0) == doctest::Approx(0.2));
}

TEST_CASE("ArcSegment: tangentAt is unit vector consistent with heading") {
    ArcSegment seg({0, 0}, 0.0, 0.1, 10.0);
    Vec2 t = seg.tangentAt(5);
    // heading at s=5 = 0.5, so tangent = (cos(0.5), sin(0.5))
    CHECK(t.x == doctest::Approx(std::cos(0.5)));
    CHECK(t.y == doctest::Approx(std::sin(0.5)));
    CHECK(t.norm() == doctest::Approx(1.0));
}

TEST_CASE("ArcSegment: normalAt is left of tangent") {
    ArcSegment seg({0, 0}, 0.0, 0.1, 10.0);
    Vec2 n = seg.normalAt(0);
    // heading at s=0 = 0 (east), normal = (-sin(0), cos(0)) = (0, 1) = north = left
    CHECK(n.x == doctest::Approx(0.0));
    CHECK(n.y == doctest::Approx(1.0));
}

TEST_CASE("ArcSegment: sampleUniform traces arc correctly") {
    // Quarter circle, R=10, center (0,0), start (10,0) heading north
    double R = 10.0;
    double kappa = 1.0 / R;
    double quarterLen = geo::PI * R / 2.0;
    ArcSegment seg({R, 0}, geo::HALF_PI, kappa, quarterLen);

    auto pts = seg.sampleUniform(11);  // 11 points
    CHECK(pts.size() == 11);
    // First point = start
    CHECK(pts[0].x == doctest::Approx(R).epsilon(0.01));
    CHECK(pts[0].y == doctest::Approx(0.0).epsilon(0.01));
    // Last point = end (0, R)
    CHECK(pts[10].x == doctest::Approx(0.0).epsilon(0.01));
    CHECK(pts[10].y == doctest::Approx(R).epsilon(0.01));
    // All points should be on the circle of radius R centered at origin
    for (const auto& p : pts) {
        double dist = std::hypot(p.x, p.y);
        CHECK(dist == doctest::Approx(R).epsilon(0.01));
    }
}

TEST_CASE("ArcSegment: full circle (sweep = 2π)") {
    // Full circle: start at (10, 0), heading north (π/2), curvature 0.1, length = 2π*10
    double R = 10.0;
    double kappa = 1.0 / R;
    double fullLen = 2.0 * geo::PI * R;
    ArcSegment seg({R, 0}, geo::HALF_PI, kappa, fullLen);

    CHECK(seg.sweepAngle() == doctest::Approx(2.0 * geo::PI).epsilon(0.01));

    // End point should be back at start
    Point2D end = seg.endPoint();
    CHECK(end.x == doctest::Approx(R).epsilon(0.01));
    CHECK(end.y == doctest::Approx(0.0).epsilon(0.01));

    // End heading should be start heading + 2π = π/2 + 2π = 5π/2
    // Normalized: π/2 (same direction)
    double h = seg.endHeading();
    // Allow for 2π wrapping
    double hNorm = std::fmod(h, 2.0 * geo::PI);
    CHECK(hNorm == doctest::Approx(geo::HALF_PI).epsilon(0.01));
}

TEST_CASE("ArcSegment: half circle (sweep = π)") {
    // Half circle: start at (10, 0), heading north (π/2), curvature 0.1, length = π*10
    // End should be at (-10, 0), heading south (-π/2)
    double R = 10.0;
    double kappa = 1.0 / R;
    double halfLen = geo::PI * R;
    ArcSegment seg({R, 0}, geo::HALF_PI, kappa, halfLen);

    Point2D end = seg.endPoint();
    CHECK(end.x == doctest::Approx(-R).epsilon(0.01));
    CHECK(end.y == doctest::Approx(0.0).epsilon(0.01));

    double h = seg.endHeading();
    // heading = π/2 + 0.1 * π*10 = π/2 + π = 3π/2
    // Normalized: 3π/2 = -π/2 (south)
    CHECK(h == doctest::Approx(3.0 * geo::PI / 2.0).epsilon(0.01));
}

// ═══════════════════════════════════════════════════════════
// SpiralSegment Tests
// ═══════════════════════════════════════════════════════════

using geo::SpiralSegment;

TEST_CASE("SpiralSegment: basic construction and type") {
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.1, 50.0);  // straight → arc transition
    CHECK(seg.type() == GeometryType::Spiral);
    CHECK(seg.length() == doctest::Approx(50.0));
}

TEST_CASE("SpiralSegment: curvatureDS varies linearly") {
    // κ₀=0, κ₁=0.1, L=50
    // κ(s) = 0 + (0.1-0)·s/50 = 0.002·s
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.1, 50.0);
    CHECK(seg.curvatureDS(0) == doctest::Approx(0.0));
    CHECK(seg.curvatureDS(25) == doctest::Approx(0.05));   // midpoint
    CHECK(seg.curvatureDS(50) == doctest::Approx(0.1));    // end
}

TEST_CASE("SpiralSegment: curvatureDS with negative curvature (right turn)") {
    // κ₀=0.1, κ₁=-0.1, L=40 — left to right transition
    SpiralSegment seg({0, 0}, 0.0, 0.1, -0.1, 40.0);
    CHECK(seg.curvatureDS(0) == doctest::Approx(0.1));
    CHECK(seg.curvatureDS(20) == doctest::Approx(0.0));    // midpoint = 0
    CHECK(seg.curvatureDS(40) == doctest::Approx(-0.1));
}

TEST_CASE("SpiralSegment: totalAngleChange = L·(κ₀+κ₁)/2") {
    // κ₀=0, κ₁=0.1, L=50 → totalAngle = 50·(0+0.1)/2 = 2.5
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.1, 50.0);
    CHECK(seg.totalAngleChange() == doctest::Approx(2.5));
}

TEST_CASE("SpiralSegment: heading at end = startHeading + totalAngleChange") {
    SpiralSegment seg({0, 0}, 0.3, 0.0, 0.1, 50.0);
    double x, y, h;
    seg.evaluateDS(50, x, y, h);
    // h = 0.3 + 50·(0+0.1)/2 = 0.3 + 2.5 = 2.8
    CHECK(h == doctest::Approx(2.8).epsilon(0.001));
}

TEST_CASE("SpiralSegment: heading at midpoint") {
    // κ₀=0, κ₁=0.1, L=50
    // h(s) = 0 + 0·s + (0.1-0)·s²/(2·50) = 0.001·s²
    // h(25) = 0.001·625 = 0.625
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.1, 50.0);
    double x, y, h;
    seg.evaluateDS(25, x, y, h);
    CHECK(h == doctest::Approx(0.625).epsilon(0.001));
}

TEST_CASE("SpiralSegment: evaluateDS at s=0 returns start point and heading") {
    SpiralSegment seg({5, 3}, 0.7, 0.0, 0.1, 50.0);
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(x == doctest::Approx(5.0));
    CHECK(y == doctest::Approx(3.0));
    CHECK(h == doctest::Approx(0.7));
}

TEST_CASE("SpiralSegment: evaluateDS clamps out-of-range s") {
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.1, 50.0);
    double x, y, h;
    seg.evaluateDS(-10, x, y, h);
    CHECK(x == doctest::Approx(0.0));
    CHECK(y == doctest::Approx(0.0));
    CHECK(h == doctest::Approx(0.0));
    // s > length clamps to end
    seg.evaluateDS(60, x, y, h);
    CHECK(h == doctest::Approx(2.5).epsilon(0.001));  // totalAngleChange
}

TEST_CASE("SpiralSegment: clone produces equal and independent segment") {
    SpiralSegment seg({1, 2}, 0.5, 0.0, 0.1, 50.0);
    auto cloned = seg.clone();
    CHECK(cloned->type() == GeometryType::Spiral);
    CHECK(cloned->length() == doctest::Approx(50.0));
    CHECK(cloned->curvatureDS(0) == doctest::Approx(0.0));
    CHECK(cloned->curvatureDS(50) == doctest::Approx(0.1));

    // Modify original — clone unchanged
    seg.curvatureEnd_ = 0.2;
    CHECK(cloned->curvatureDS(50) == doctest::Approx(0.1));
    CHECK(seg.curvatureDS(50) == doctest::Approx(0.2));
}

TEST_CASE("SpiralSegment: clothoidA derived from κ₀,κ₁,L") {
    // A² = L / |κ₁ - κ₀| = 50 / 0.1 = 500, A = √500 ≈ 22.36
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.1, 50.0);
    CHECK(seg.clothoidA() == doctest::Approx(std::sqrt(500.0)).epsilon(0.01));
}

TEST_CASE("SpiralSegment: clothoidA with negative curvature rate (right turn)") {
    // κ₀=0.1, κ₁=-0.1, L=40 → |dk| = 0.2, A = √(40/0.2) = √200 ≈ 14.14
    // Must not return NaN (bug: sqrt of negative if |dk| not used)
    SpiralSegment seg({0, 0}, 0.0, 0.1, -0.1, 40.0);
    double a = seg.clothoidA();
    CHECK(std::isfinite(a));
    CHECK(a == doctest::Approx(std::sqrt(200.0)).epsilon(0.01));
}

TEST_CASE("SpiralSegment: clothoidA returns infinity when κ₀ == κ₁ (degenerate = arc)") {
    SpiralSegment seg({0, 0}, 0.0, 0.1, 0.1, 50.0);  // κ₀ = κ₁
    double a = seg.clothoidA();
    CHECK(std::isinf(a));
    // Must not crash — this is a legitimate tested state (cross-type tests use it)
}

TEST_CASE("SpiralSegment: clothoidA returns infinity when κ₀ == κ₁ == 0 (degenerate = line)") {
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.0, 50.0);
    double a = seg.clothoidA();
    CHECK(std::isinf(a));
}

TEST_CASE("SpiralSegment: curvatureRate = (κ₁-κ₀)/L") {
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.1, 50.0);
    CHECK(seg.curvatureRate() == doctest::Approx(0.002));  // 0.1/50
}

TEST_CASE("SpiralSegment: curvatureRate is 0 when κ₀ == κ₁ (degenerate)") {
    SpiralSegment seg({0, 0}, 0.0, 0.1, 0.1, 50.0);
    CHECK(seg.curvatureRate() == doctest::Approx(0.0));
}

TEST_CASE("SpiralSegment: position moves forward (no backward motion)") {
    // Start heading east, κ₀=0, κ₁=0.02, L=50
    // Total angle change = 50·(0+0.02)/2 = 0.5 rad ≈ 28.6°
    // Heading stays within (-π/2, π/2), so x should always increase
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.02, 50.0);
    double prevX = 0.0;
    for (int i = 1; i <= 10; i++) {
        double s = i * 5.0;
        Point2D p = seg.positionAt(s);
        CHECK(p.x > prevX);  // x strictly increasing
        prevX = p.x;
    }
}

TEST_CASE("SpiralSegment: tangentAt is unit vector") {
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.1, 50.0);
    for (int i = 0; i <= 5; i++) {
        double s = i * 10.0;
        Vec2 t = seg.tangentAt(s);
        CHECK(t.norm() == doctest::Approx(1.0).epsilon(0.001));
    }
}

// ═══════════════════════════════════════════════════════════
// Cross-Type Consistency Tests
// ═══════════════════════════════════════════════════════════
// These verify that SpiralSegment degenerates correctly to
// ArcSegment (when κ₀=κ₁) and LineSegment (when κ₀=κ₁=0).
// This catches sign/normalization mismatches between classes
// that isolated tests can't see.
// ═══════════════════════════════════════════════════════════

TEST_CASE("Cross-type: Spiral(κ₀=κ₁=κ) matches Arc(κ) at multiple s values") {
    // Same start point, heading, curvature, length
    Point2D start(5, 3);
    double heading = 0.4;
    double kappa = 0.08;
    double L = 30.0;

    ArcSegment arc(start, heading, kappa, L);
    SpiralSegment spiral(start, heading, kappa, kappa, L);  // κ₀ = κ₁ = κ

    // Check at several s values
    double sValues[] = {0, 5, 10, 15, 20, 25, 30};
    for (double s : sValues) {
        double ax, ay, ah, sx, sy, sh;
        arc.evaluateDS(s, ax, ay, ah);
        spiral.evaluateDS(s, sx, sy, sh);

        // Heading should match very closely (both use same formula)
        CHECK(sh == doctest::Approx(ah).epsilon(0.001));

        // Position should match within numerical integration tolerance
        CHECK(sx == doctest::Approx(ax).epsilon(0.01));
        CHECK(sy == doctest::Approx(ay).epsilon(0.01));
    }
}

TEST_CASE("Cross-type: Spiral(κ₀=κ₁=κ) matches Arc(κ) with negative curvature") {
    Point2D start(0, 0);
    double heading = 0.0;
    double kappa = -0.05;  // right turn
    double L = 40.0;

    ArcSegment arc(start, heading, kappa, L);
    SpiralSegment spiral(start, heading, kappa, kappa, L);

    double sValues[] = {0, 10, 20, 30, 40};
    for (double s : sValues) {
        double ax, ay, ah, sx, sy, sh;
        arc.evaluateDS(s, ax, ay, ah);
        spiral.evaluateDS(s, sx, sy, sh);

        CHECK(sh == doctest::Approx(ah).epsilon(0.001));
        CHECK(sx == doctest::Approx(ax).epsilon(0.01));
        CHECK(sy == doctest::Approx(ay).epsilon(0.01));
    }
}

TEST_CASE("Cross-type: Spiral(κ₀=κ₁=0) matches Line at multiple s values") {
    // Spiral with zero curvature should match a line segment
    Point2D start(2, 7);
    double heading = 0.6;
    double L = 25.0;

    // LineSegment: need to compute end point from heading and length
    Point2D end(start.x + L * std::cos(heading), start.y + L * std::sin(heading));
    LineSegment line(start, end);
    SpiralSegment spiral(start, heading, 0.0, 0.0, L);

    double sValues[] = {0, 5, 10, 15, 20, 25};
    for (double s : sValues) {
        double lx, ly, lh, sx, sy, sh;
        line.evaluateDS(s, lx, ly, lh);
        spiral.evaluateDS(s, sx, sy, sh);

        // Heading should be constant and equal
        CHECK(sh == doctest::Approx(lh).epsilon(0.001));
        CHECK(sh == doctest::Approx(heading).epsilon(0.001));

        // Position should match
        CHECK(sx == doctest::Approx(lx).epsilon(0.01));
        CHECK(sy == doctest::Approx(ly).epsilon(0.01));
    }
}

TEST_CASE("Cross-type: Spiral(κ₀=κ₁=0) matches Line with different heading") {
    Point2D start(10, -5);
    double heading = -0.8;  // pointing down-right
    double L = 15.0;

    Point2D end(start.x + L * std::cos(heading), start.y + L * std::sin(heading));
    LineSegment line(start, end);
    SpiralSegment spiral(start, heading, 0.0, 0.0, L);

    for (int i = 0; i <= 5; i++) {
        double s = i * 3.0;
        double lx, ly, lh, sx, sy, sh;
        line.evaluateDS(s, lx, ly, lh);
        spiral.evaluateDS(s, sx, sy, sh);

        CHECK(sh == doctest::Approx(lh).epsilon(0.001));
        CHECK(sx == doctest::Approx(lx).epsilon(0.01));
        CHECK(sy == doctest::Approx(ly).epsilon(0.01));
    }
}

TEST_CASE("Cross-type: curvatureDS consistency — Spiral(κ₀=κ₁) = Arc(κ)") {
    double kappa = 0.07;
    SpiralSegment spiral({0, 0}, 0.0, kappa, kappa, 30.0);
    ArcSegment arc({0, 0}, 0.0, kappa, 30.0);

    for (int i = 0; i <= 6; i++) {
        double s = i * 5.0;
        CHECK(spiral.curvatureDS(s) == doctest::Approx(arc.curvatureDS(s)));
    }
}

TEST_CASE("Cross-type: curvatureDS consistency — Spiral(0,0) = Line(0)") {
    SpiralSegment spiral({0, 0}, 0.0, 0.0, 0.0, 30.0);
    LineSegment line({0, 0}, {30, 0});

    for (int i = 0; i <= 6; i++) {
        double s = i * 5.0;
        CHECK(spiral.curvatureDS(s) == doctest::Approx(line.curvatureDS(s)));
        CHECK(spiral.curvatureDS(s) == doctest::Approx(0.0));
    }
}

TEST_CASE("Cross-type: clone preserves type across all segment kinds") {
    LineSegment line({0, 0}, {10, 0});
    ArcSegment arc({0, 0}, 0.0, 0.1, 10.0);
    SpiralSegment spiral({0, 0}, 0.0, 0.0, 0.1, 50.0);

    auto lc = line.clone();
    auto ac = arc.clone();
    auto sc = spiral.clone();

    CHECK(lc->type() == GeometryType::Line);
    CHECK(ac->type() == GeometryType::Arc);
    CHECK(sc->type() == GeometryType::Spiral);

    // All clones should produce same positions as originals
    double s = 5.0;
    CHECK(lc->positionAt(s).x == doctest::Approx(line.positionAt(s).x));
    CHECK(ac->positionAt(s).x == doctest::Approx(arc.positionAt(s).x).epsilon(0.001));
    CHECK(sc->positionAt(s).x == doctest::Approx(spiral.positionAt(s).x).epsilon(0.01));
}

// ═══════════════════════════════════════════════════════════
// BezierSegment Tests
// ═══════════════════════════════════════════════════════════

using geo::BezierSegment;

TEST_CASE("BezierSegment: basic construction and type") {
    BezierSegment seg({0, 0}, {3, 5}, {7, 5}, {10, 0});
    CHECK(seg.type() == GeometryType::Bezier);
    CHECK(seg.length() > 0.0);
}

TEST_CASE("BezierSegment: evaluateDS at s=0 returns P0") {
    BezierSegment seg({1, 2}, {4, 6}, {8, 6}, {11, 2});
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(x == doctest::Approx(1.0));
    CHECK(y == doctest::Approx(2.0));
}

TEST_CASE("BezierSegment: evaluateDS at s=length returns P3") {
    BezierSegment seg({0, 0}, {3, 5}, {7, 5}, {10, 0});
    double len = seg.length();
    double x, y, h;
    seg.evaluateDS(len, x, y, h);
    CHECK(x == doctest::Approx(10.0).epsilon(0.01));
    CHECK(y == doctest::Approx(0.0).epsilon(0.01));
}

TEST_CASE("BezierSegment: evaluateDS midpoint is near curve midpoint") {
    // Symmetric bezier: midpoint should be at (5, 3.75) by symmetry
    // B(0.5) = 0.125*P0 + 0.375*P1 + 0.375*P2 + 0.125*P3
    //        = 0.125*(0,0) + 0.375*(3,5) + 0.375*(7,5) + 0.125*(10,0)
    //        = (0+1.125+2.625+1.25, 0+1.875+1.875+0) = (5, 3.75)
    BezierSegment seg({0, 0}, {3, 5}, {7, 5}, {10, 0});
    double len = seg.length();
    double x, y, h;
    seg.evaluateDS(len / 2.0, x, y, h);
    CHECK(x == doctest::Approx(5.0).epsilon(0.02));
    CHECK(y == doctest::Approx(3.75).epsilon(0.02));
}

TEST_CASE("BezierSegment: heading at start points from P0 toward P1") {
    BezierSegment seg({0, 0}, {10, 0}, {20, 0}, {30, 0});
    // All collinear east — heading should be 0 everywhere
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(h == doctest::Approx(0.0));
}

TEST_CASE("BezierSegment: curvature is 0 for collinear control points") {
    BezierSegment seg({0, 0}, {10, 0}, {20, 0}, {30, 0});
    double len = seg.length();
    CHECK(seg.curvatureDS(0) == doctest::Approx(0.0).epsilon(0.001));
    CHECK(seg.curvatureDS(len / 2.0) == doctest::Approx(0.0).epsilon(0.001));
    CHECK(seg.curvatureDS(len) == doctest::Approx(0.0).epsilon(0.001));
}

TEST_CASE("BezierSegment: evaluateDS clamps out-of-range s") {
    BezierSegment seg({0, 0}, {3, 5}, {7, 5}, {10, 0});
    double x, y, h;
    // s < 0 → start
    seg.evaluateDS(-5, x, y, h);
    CHECK(x == doctest::Approx(0.0));
    CHECK(y == doctest::Approx(0.0));
    // s > length → end
    seg.evaluateDS(999, x, y, h);
    CHECK(x == doctest::Approx(10.0).epsilon(0.01));
    CHECK(y == doctest::Approx(0.0).epsilon(0.01));
}

TEST_CASE("BezierSegment: clone produces equal and independent segment") {
    BezierSegment seg({0, 0}, {3, 5}, {7, 5}, {10, 0});
    auto cloned = seg.clone();
    CHECK(cloned->type() == GeometryType::Bezier);
    CHECK(cloned->length() == doctest::Approx(seg.length()));

    // Same position at midpoint
    double len = seg.length();
    CHECK(cloned->positionAt(len / 2).x == doctest::Approx(seg.positionAt(len / 2).x).epsilon(0.001));

    // Modify original significantly — clone should be unchanged
    double cloneMidX = cloned->positionAt(len / 2).x;
    seg.p1 = {0, 20};  // dramatic change: handle points straight up
    seg.rebuild();
    double segMidX = seg.positionAt(seg.length() / 2).x;
    // Positions should differ by more than 1.0 (absolute)
    CHECK(std::abs(segMidX - cloneMidX) > 1.0);
}

TEST_CASE("BezierSegment: tangentAt is unit vector") {
    BezierSegment seg({0, 0}, {3, 5}, {7, 5}, {10, 0});
    double len = seg.length();
    for (int i = 0; i <= 5; i++) {
        double s = len * i / 5.0;
        Vec2 t = seg.tangentAt(s);
        CHECK(t.norm() == doctest::Approx(1.0).epsilon(0.001));
    }
}

TEST_CASE("BezierSegment: sampleUniform returns correct count") {
    BezierSegment seg({0, 0}, {3, 5}, {7, 5}, {10, 0});
    auto pts = seg.sampleUniform(11);
    CHECK(pts.size() == 11);
    CHECK(pts[0].x == doctest::Approx(0.0));
    CHECK(pts[0].y == doctest::Approx(0.0));
    CHECK(pts[10].x == doctest::Approx(10.0).epsilon(0.01));
    CHECK(pts[10].y == doctest::Approx(0.0).epsilon(0.01));
}

// ─── Degenerate cases (standing rule: test division-by-magnitude guards) ───

TEST_CASE("BezierSegment: P1==P0 (zero handle at start) doesn't crash or produce NaN") {
    // Freshly-placed control point with no handle offset: P1 == P0
    BezierSegment seg({0, 0}, {0, 0}, {7, 5}, {10, 0});
    double len = seg.length();
    CHECK(len > 0.0);
    CHECK(std::isfinite(len));

    // evaluateDS at s=0 (the cusp point)
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(std::isfinite(x));
    CHECK(std::isfinite(y));
    CHECK(std::isfinite(h));

    // curvatureDS at s=0 — |B'(0)| = 0, must return 0 not NaN/Inf
    double k = seg.curvatureDS(0);
    CHECK(std::isfinite(k));
    CHECK(k == doctest::Approx(0.0));

    // Midpoint and end should be fine
    seg.evaluateDS(len / 2, x, y, h);
    CHECK(std::isfinite(x));
    CHECK(std::isfinite(y));
    CHECK(std::isfinite(h));
    CHECK(std::isfinite(seg.curvatureDS(len / 2)));
}

TEST_CASE("BezierSegment: P2==P3 (zero handle at end) doesn't crash or produce NaN") {
    BezierSegment seg({0, 0}, {3, 5}, {10, 0}, {10, 0});
    double len = seg.length();
    CHECK(len > 0.0);

    // evaluateDS at s=length (the cusp point at end)
    double x, y, h;
    seg.evaluateDS(len, x, y, h);
    CHECK(std::isfinite(x));
    CHECK(std::isfinite(y));
    CHECK(std::isfinite(h));

    // curvatureDS at end — |B'(1)| = 0, must return 0 not NaN/Inf
    double k = seg.curvatureDS(len);
    CHECK(std::isfinite(k));
    CHECK(k == doctest::Approx(0.0));
}

TEST_CASE("BezierSegment: both handles zero (P1==P0, P2==P3) — linear bezier") {
    // Degenerate to a line from P0 to P3
    BezierSegment seg({0, 0}, {0, 0}, {10, 5}, {10, 5});
    double len = seg.length();
    CHECK(len == doctest::Approx(std::hypot(10, 5)).epsilon(0.05));

    // Curvature should be ~0 everywhere
    for (int i = 0; i <= 5; i++) {
        double s = len * i / 5.0;
        CHECK(seg.curvatureDS(s) == doctest::Approx(0.0).epsilon(0.01));
    }
}

TEST_CASE("BezierSegment: all control points coincident (zero-length)") {
    BezierSegment seg({5, 5}, {5, 5}, {5, 5}, {5, 5});
    CHECK(seg.length() == doctest::Approx(0.0));

    // evaluateDS doesn't crash, returns the point
    double x, y, h;
    seg.evaluateDS(0, x, y, h);
    CHECK(x == doctest::Approx(5.0));
    CHECK(y == doctest::Approx(5.0));
    CHECK(std::isfinite(h));

    // curvatureDS doesn't crash
    CHECK(std::isfinite(seg.curvatureDS(0)));
}

TEST_CASE("BezierSegment: curvature sign is positive for left-turning curve") {
    // Curve that bends upward (left when traveling east)
    // P0=(0,0), P1=(3,5), P2=(7,5), P3=(10,0) — arch shape
    // At midpoint, curvature should be negative (curve bends right/down at apex)
    // Actually for this arch: going east then curving up = left turn initially
    BezierSegment seg({0, 0}, {3, 5}, {7, 5}, {10, 0});
    double len = seg.length();
    // At s=0, tangent is toward P1=(3,5) → heading atan2(5,3) ≈ 1.047 (NE)
    // The curve bends... let's just check it's finite and nonzero somewhere
    double kMid = seg.curvatureDS(len / 2.0);
    CHECK(std::isfinite(kMid));
    // For this symmetric arch, curvature at midpoint should be negative
    // (curve is concave down = right turn at apex)
    CHECK(kMid < 0.0);
}

// ─── Cross-type consistency: Bezier with collinear points = Line ───

TEST_CASE("Cross-type: Bezier(collinear) matches Line") {
    // All control points on the x-axis → bezier is a straight line
    Point2D p0(0, 0), p1(10, 0), p2(20, 0), p3(30, 0);
    BezierSegment bez(p0, p1, p2, p3);
    LineSegment line(p0, p3);

    double len = bez.length();
    // Length should match (bezier of collinear points = line)
    CHECK(len == doctest::Approx(line.length()).epsilon(0.01));

    // Position and heading at several s values
    double sValues[] = {0, len * 0.25, len * 0.5, len * 0.75, len};
    for (double s : sValues) {
        double bx, by, bh, lx, ly, lh;
        bez.evaluateDS(s, bx, by, bh);
        line.evaluateDS(s, lx, ly, lh);
        CHECK(bx == doctest::Approx(lx).epsilon(0.02));
        CHECK(by == doctest::Approx(ly).epsilon(0.02));
        CHECK(bh == doctest::Approx(lh).epsilon(0.01));
        // Curvature: both should be ~0
        CHECK(bez.curvatureDS(s) == doctest::Approx(0.0).epsilon(0.001));
        CHECK(line.curvatureDS(s) == doctest::Approx(0.0));
    }
}

TEST_CASE("Cross-type: Bezier(collinear) matches Line with diagonal") {
    Point2D p0(1, 2), p1(4, 6), p2(8, 12), p3(12, 18);
    BezierSegment bez(p0, p1, p2, p3);
    LineSegment line(p0, p3);

    double len = bez.length();
    CHECK(len == doctest::Approx(line.length()).epsilon(0.02));

    for (int i = 0; i <= 5; i++) {
        double s = len * i / 5.0;
        double bx, by, bh, lx, ly, lh;
        bez.evaluateDS(s, bx, by, bh);
        line.evaluateDS(s, lx, ly, lh);
        CHECK(bx == doctest::Approx(lx).epsilon(0.02));
        CHECK(by == doctest::Approx(ly).epsilon(0.02));
    }
}

TEST_CASE("Cross-type: clone preserves type across all 4 segment kinds") {
    LineSegment line({0, 0}, {10, 0});
    ArcSegment arc({0, 0}, 0.0, 0.1, 10.0);
    SpiralSegment spiral({0, 0}, 0.0, 0.0, 0.1, 50.0);
    BezierSegment bez({0, 0}, {3, 5}, {7, 5}, {10, 0});

    auto lc = line.clone();
    auto ac = arc.clone();
    auto sc = spiral.clone();
    auto bc = bez.clone();

    CHECK(lc->type() == GeometryType::Line);
    CHECK(ac->type() == GeometryType::Arc);
    CHECK(sc->type() == GeometryType::Spiral);
    CHECK(bc->type() == GeometryType::Bezier);

    // All clones produce same positions as originals
    double s = 5.0;
    CHECK(lc->positionAt(s).x == doctest::Approx(line.positionAt(s).x));
    CHECK(ac->positionAt(s).x == doctest::Approx(arc.positionAt(s).x).epsilon(0.001));
    CHECK(sc->positionAt(s).x == doctest::Approx(spiral.positionAt(s).x).epsilon(0.01));
    CHECK(bc->positionAt(s).x == doctest::Approx(bez.positionAt(s).x).epsilon(0.01));
}

// ═══════════════════════════════════════════════════════════
// Adaptive Sampling Tests (Task 1.6)
// ═══════════════════════════════════════════════════════════
// Recursive midpoint displacement with chord-error tolerance.
// Uses MAX_GEOM_ERROR_HORIZONTAL (0.25m) from architecture doc Appendix B.
// Recursion depth limited by ADAPTIVE_MAX_DEPTH (20) to prevent stack overflow.
// ═══════════════════════════════════════════════════════════

TEST_CASE("Adaptive sampling: LineSegment produces minimal samples") {
    // A straight line has zero chord error — should produce just minSamples
    LineSegment seg({0, 0}, {100, 0});
    auto pts = seg.sampleAdaptive(MAX_GEOM_ERROR_HORIZONTAL, 2, 1000);
    CHECK(pts.size() == 2);  // no subdivision needed
    CHECK(pts[0].x == doctest::Approx(0.0));
    CHECK(pts[1].x == doctest::Approx(100.0));
}

TEST_CASE("Adaptive sampling: ArcSegment produces more samples than line") {
    // A tight arc has high chord error — should subdivide
    double R = 5.0;
    double kappa = 1.0 / R;
    double quarterLen = geo::PI * R / 2.0;
    ArcSegment seg({R, 0}, geo::HALF_PI, kappa, quarterLen);
    auto pts = seg.sampleAdaptive(MAX_GEOM_ERROR_HORIZONTAL, 2, 1000);
    CHECK(pts.size() > 2);  // must subdivide
    CHECK(pts.size() <= 1000);  // respects maxSamples
}

TEST_CASE("Adaptive sampling: ArcSegment samples are within tolerance") {
    // Every sample point should be on the circle (within tolerance)
    double R = 10.0;
    double kappa = 1.0 / R;
    double quarterLen = geo::PI * R / 2.0;
    ArcSegment seg({R, 0}, geo::HALF_PI, kappa, quarterLen);
    auto pts = seg.sampleAdaptive(MAX_GEOM_ERROR_HORIZONTAL, 2, 1000);

    // All points should be on the circle of radius R centered at origin
    for (const auto& p : pts) {
        double dist = std::hypot(p.x, p.y);
        CHECK(dist == doctest::Approx(R).epsilon(0.01));
    }
}

TEST_CASE("Adaptive sampling: tighter tolerance produces more samples") {
    ArcSegment seg({10, 0}, geo::HALF_PI, 0.1, geo::PI * 5);
    auto loose = seg.sampleAdaptive(1.0, 2, 1000);   // 1m tolerance
    auto tight = seg.sampleAdaptive(0.001, 2, 1000); // 1mm tolerance
    CHECK(tight.size() > loose.size());
}

TEST_CASE("Adaptive sampling: respects maxSamples limit") {
    // Very tight tolerance on a long arc — would produce many samples
    // but should be capped at maxSamples
    ArcSegment seg({100, 0}, geo::HALF_PI, 0.01, geo::PI * 1000);
    auto pts = seg.sampleAdaptive(0.0001, 2, 50);
    CHECK(pts.size() <= 50);
}

TEST_CASE("Adaptive sampling: respects minSamples") {
    LineSegment seg({0, 0}, {10, 0});
    auto pts = seg.sampleAdaptive(MAX_GEOM_ERROR_HORIZONTAL, 5, 1000);
    CHECK(pts.size() >= 5);
}

TEST_CASE("Adaptive sampling: BezierSegment subdivides in high-curvature regions") {
    // Arch-shaped bezier — more samples near the peak
    BezierSegment seg({0, 0}, {3, 10}, {7, 10}, {10, 0});
    auto pts = seg.sampleAdaptive(MAX_GEOM_ERROR_HORIZONTAL, 2, 1000);
    CHECK(pts.size() > 2);
    // First and last points should be endpoints
    CHECK(pts.front().x == doctest::Approx(0.0));
    CHECK(pts.back().x == doctest::Approx(10.0).epsilon(0.01));
}

TEST_CASE("Adaptive sampling: SpiralSegment subdivides") {
    // Spiral with significant curvature change
    SpiralSegment seg({0, 0}, 0.0, 0.0, 0.2, 30.0);
    auto pts = seg.sampleAdaptive(MAX_GEOM_ERROR_HORIZONTAL, 2, 1000);
    CHECK(pts.size() > 2);
}

TEST_CASE("Adaptive sampling: zero-length segment doesn't crash") {
    LineSegment seg({5, 5}, {5, 5});
    auto pts = seg.sampleAdaptive(MAX_GEOM_ERROR_HORIZONTAL, 2, 1000);
    CHECK(pts.size() >= 1);
    CHECK(pts[0].x == doctest::Approx(5.0));
    CHECK(pts[0].y == doctest::Approx(5.0));
}

TEST_CASE("Adaptive sampling: near-zero-length segment doesn't blow stack") {
    // Pathological: very short segment with tight tolerance
    // Could cause deep recursion if not for depth guard
    LineSegment seg({0, 0}, {0.0001, 0});
    auto pts = seg.sampleAdaptive(0.000001, 2, 1000);
    // Should complete without stack overflow
    CHECK(pts.size() >= 1);
}

TEST_CASE("Adaptive sampling: pathological arc with tight tolerance doesn't blow stack") {
    // Very tight arc (small radius) with very tight tolerance
    // Could cause extreme subdivision if not for depth/maxSamples guards
    ArcSegment seg({0.001, 0}, geo::HALF_PI, 1000.0, 0.01);
    auto pts = seg.sampleAdaptive(1e-10, 2, 1000);
    CHECK(pts.size() <= 1000);  // maxSamples guard
    CHECK(pts.size() >= 1);
}

TEST_CASE("Adaptive sampling: all sample points are on the actual curve") {
    // Verify that every returned point matches positionAt(s) for some s
    ArcSegment seg({10, 0}, geo::HALF_PI, 0.1, geo::PI * 5);
    auto pts = seg.sampleAdaptive(MAX_GEOM_ERROR_HORIZONTAL, 2, 1000);

    // Each point should be on the circle of radius 10 centered at origin
    for (const auto& p : pts) {
        double dist = std::hypot(p.x, p.y);
        CHECK(dist == doctest::Approx(10.0).epsilon(0.01));
    }
}

TEST_CASE("Adaptive sampling: constants match architecture doc Appendix B") {
    // Verify the named constants are what the doc says
    CHECK(MAX_GEOM_ERROR_HORIZONTAL == 0.25);
    CHECK(MAX_GEOM_ERROR_VERTICAL == 0.1);
    CHECK(GEOM_TOLERANCE == 0.2);
    CHECK(MAX_GEOM_LENGTH == 50.0);
    CHECK(MIN_GEOM_LENGTH == 0.1);
    CHECK(ADAPTIVE_MAX_DEPTH == 20);
}

// ═══════════════════════════════════════════════════════════
// SegmentSequence Tests (Task 1.7)
// ═══════════════════════════════════════════════════════════
// Non-owning view over ordered GeometrySegments.
// Global s → (segment, localS) via binary search.
// API mirrors GeometrySegment: evaluateDS, positionAt, tangentAt, etc.
// ═══════════════════════════════════════════════════════════

#include "st_coords.hpp"

using geo::SegmentSequence;

TEST_CASE("SegmentSequence: basic construction with two line segments") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});
    CHECK(seq.numSegments() == 2);
    CHECK(seq.totalLength() == doctest::Approx(20.0));
}

TEST_CASE("SegmentSequence: globalSToLocal at start") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});
    auto loc = seq.globalSToLocal(0.0);
    CHECK(loc.segmentIndex == 0);
    CHECK(loc.localS == doctest::Approx(0.0));
}

TEST_CASE("SegmentSequence: globalSToLocal in first segment") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});
    auto loc = seq.globalSToLocal(5.0);
    CHECK(loc.segmentIndex == 0);
    CHECK(loc.localS == doctest::Approx(5.0));
}

TEST_CASE("SegmentSequence: globalSToLocal at segment boundary") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});
    auto loc = seq.globalSToLocal(10.0);
    // At exact boundary — should be in segment 0 with localS=10, or segment 1 with localS=0
    // Either is valid; check that position is correct
    CHECK((loc.segmentIndex == 0 || loc.segmentIndex == 1));
    Point2D p = seq.positionAt(10.0);
    CHECK(p.x == doctest::Approx(10.0));
    CHECK(p.y == doctest::Approx(0.0));
}

TEST_CASE("SegmentSequence: globalSToLocal in second segment") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});
    auto loc = seq.globalSToLocal(15.0);
    CHECK(loc.segmentIndex == 1);
    CHECK(loc.localS == doctest::Approx(5.0));
}

TEST_CASE("SegmentSequence: globalSToLocal at end") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});
    auto loc = seq.globalSToLocal(20.0);
    CHECK(loc.segmentIndex == 1);
    CHECK(loc.localS == doctest::Approx(10.0));
}

TEST_CASE("SegmentSequence: clampS") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});
    CHECK(seq.clampS(-5.0) == doctest::Approx(0.0));
    CHECK(seq.clampS(0.0) == doctest::Approx(0.0));
    CHECK(seq.clampS(10.0) == doctest::Approx(10.0));
    CHECK(seq.clampS(20.0) == doctest::Approx(20.0));
    CHECK(seq.clampS(25.0) == doctest::Approx(20.0));
}

TEST_CASE("SegmentSequence: evaluateDS delegates to correct segment") {
    LineSegment seg1({0, 0}, {10, 0});   // heading east
    LineSegment seg2({10, 0}, {10, 10}); // heading north
    SegmentSequence seq({{&seg1, &seg2}});

    // In first segment: heading should be 0 (east)
    double x, y, h;
    seq.evaluateDS(5, x, y, h);
    CHECK(x == doctest::Approx(5.0));
    CHECK(y == doctest::Approx(0.0));
    CHECK(h == doctest::Approx(0.0));

    // In second segment: heading should be π/2 (north)
    seq.evaluateDS(15, x, y, h);
    CHECK(x == doctest::Approx(10.0));
    CHECK(y == doctest::Approx(5.0));
    CHECK(h == doctest::Approx(geo::HALF_PI));
}

TEST_CASE("SegmentSequence: positionAt at various global s") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {10, 10});
    SegmentSequence seq({{&seg1, &seg2}});

    CHECK(seq.positionAt(0).x == doctest::Approx(0.0));
    CHECK(seq.positionAt(0).y == doctest::Approx(0.0));
    CHECK(seq.positionAt(10).x == doctest::Approx(10.0));
    CHECK(seq.positionAt(10).y == doctest::Approx(0.0));
    CHECK(seq.positionAt(15).x == doctest::Approx(10.0));
    CHECK(seq.positionAt(15).y == doctest::Approx(5.0));
    CHECK(seq.positionAt(20).x == doctest::Approx(10.0));
    CHECK(seq.positionAt(20).y == doctest::Approx(10.0));
}

TEST_CASE("SegmentSequence: tangentAt at various global s") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {10, 10});
    SegmentSequence seq({{&seg1, &seg2}});

    Vec2 t1 = seq.tangentAt(5);
    CHECK(t1.x == doctest::Approx(1.0));  // east
    CHECK(t1.y == doctest::Approx(0.0));

    Vec2 t2 = seq.tangentAt(15);
    CHECK(t2.x == doctest::Approx(0.0));  // north
    CHECK(t2.y == doctest::Approx(1.0));
}

TEST_CASE("SegmentSequence: normalAt at various global s") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {10, 10});
    SegmentSequence seq({{&seg1, &seg2}});

    Vec2 n1 = seq.normalAt(5);
    CHECK(n1.x == doctest::Approx(0.0));  // north = left of east
    CHECK(n1.y == doctest::Approx(1.0));

    Vec2 n2 = seq.normalAt(15);
    CHECK(n2.x == doctest::Approx(-1.0)); // west = left of north
    CHECK(n2.y == doctest::Approx(0.0));
}

TEST_CASE("SegmentSequence: curvatureAt delegates correctly") {
    LineSegment line({0, 0}, {10, 0});
    ArcSegment arc({10, 0}, 0.0, 0.1, 10.0);  // κ = 0.1
    SegmentSequence seq({{&line, &arc}});

    CHECK(seq.curvatureAt(5) == doctest::Approx(0.0));   // in line
    CHECK(seq.curvatureAt(15) == doctest::Approx(0.1));  // in arc
}

TEST_CASE("SegmentSequence: mixed segment types (line + arc + line)") {
    LineSegment seg1({0, 0}, {10, 0});
    ArcSegment seg2({10, 0}, 0.0, 0.1, 5.0);  // 5m arc
    LineSegment seg3({15, 0}, {25, 0});       // approximate — not exact continuity
    SegmentSequence seq({{&seg1, &seg2, &seg3}});

    CHECK(seq.numSegments() == 3);
    CHECK(seq.totalLength() == doctest::Approx(25.0));  // 10 + 5 + 10

    // Position at start, middle of arc, end
    Point2D p0 = seq.positionAt(0);
    CHECK(p0.x == doctest::Approx(0.0));

    Point2D pEnd = seq.positionAt(25);
    CHECK(pEnd.x == doctest::Approx(25.0));
}

TEST_CASE("SegmentSequence: clampS applied in evaluateDS") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});

    // Out-of-range s should clamp
    double x, y, h;
    seq.evaluateDS(-5, x, y, h);
    CHECK(x == doctest::Approx(0.0));
    seq.evaluateDS(25, x, y, h);
    CHECK(x == doctest::Approx(20.0));
}

// ─── Continuity validation ───

TEST_CASE("SegmentSequence: validateContinuity — continuous segments return no errors") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});  // exact position + heading continuity
    SegmentSequence seq({{&seg1, &seg2}});
    auto errors = seq.validateContinuity();
    CHECK(errors.empty());
}

TEST_CASE("SegmentSequence: validateContinuity — position gap detected") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({15, 0}, {25, 0});  // 5m gap
    SegmentSequence seq({{&seg1, &seg2}});
    auto errors = seq.validateContinuity();
    CHECK(errors.size() == 1);
    CHECK(errors[0].segmentA == 0);
    CHECK(errors[0].segmentB == 1);
    CHECK(errors[0].positionError == doctest::Approx(5.0));
}

TEST_CASE("SegmentSequence: validateContinuity — heading discontinuity detected") {
    LineSegment seg1({0, 0}, {10, 0});     // heading east (0)
    LineSegment seg2({10, 0}, {10, 10});   // heading north (π/2) — 90° turn
    SegmentSequence seq({{&seg1, &seg2}});
    auto errors = seq.validateContinuity(0.01, 0.1);  // 0.1 rad ≈ 5.7° tolerance
    CHECK(errors.size() == 1);
    CHECK(errors[0].headingError == doctest::Approx(geo::HALF_PI).epsilon(0.01));
}

TEST_CASE("SegmentSequence: validateContinuity — custom tolerances") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10.001, 0}, {20, 0});  // 1mm gap, same heading
    SegmentSequence seq({{&seg1, &seg2}});

    // With loose tolerance (0.1m): no error
    auto loose = seq.validateContinuity(0.1, 0.01);
    CHECK(loose.empty());

    // With tight tolerance (0.0001m): error detected
    auto tight = seq.validateContinuity(0.0001, 0.01);
    CHECK(tight.size() == 1);
}

TEST_CASE("SegmentSequence: validateContinuity — single segment returns no errors") {
    LineSegment seg1({0, 0}, {10, 0});
    SegmentSequence seq({{&seg1}});
    auto errors = seq.validateContinuity();
    CHECK(errors.empty());
}

// ─── Lateral offset (s, t) ───

TEST_CASE("positionAtST: t=0 returns centerline position") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});

    Point2D p = geo::positionAtST(seq, 5.0, 0.0);
    CHECK(p.x == doctest::Approx(5.0));
    CHECK(p.y == doctest::Approx(0.0));
}

TEST_CASE("positionAtST: positive t offsets to the left") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});

    // At s=5, heading is east. Left = north. t=3 → 3m north.
    Point2D p = geo::positionAtST(seq, 5.0, 3.0);
    CHECK(p.x == doctest::Approx(5.0));
    CHECK(p.y == doctest::Approx(3.0));
}

TEST_CASE("positionAtST: negative t offsets to the right") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});

    Point2D p = geo::positionAtST(seq, 5.0, -2.0);
    CHECK(p.x == doctest::Approx(5.0));
    CHECK(p.y == doctest::Approx(-2.0));
}

TEST_CASE("positionAtST: works across segment boundary") {
    LineSegment seg1({0, 0}, {10, 0});     // east
    LineSegment seg2({10, 0}, {10, 10});   // north
    SegmentSequence seq({{&seg1, &seg2}});

    // At s=15 (in second segment, heading north), left = west
    Point2D p = geo::positionAtST(seq, 15.0, 4.0);
    CHECK(p.x == doctest::Approx(6.0));   // 10 - 4 = 6 (west)
    CHECK(p.y == doctest::Approx(5.0));   // 5m along north segment
}

TEST_CASE("headingAtST: heading independent of t") {
    LineSegment seg1({0, 0}, {10, 0});
    LineSegment seg2({10, 0}, {20, 0});
    SegmentSequence seq({{&seg1, &seg2}});

    CHECK(geo::headingAtST(seq, 5.0, 0.0) == doctest::Approx(0.0));
    CHECK(geo::headingAtST(seq, 5.0, 10.0) == doctest::Approx(0.0));
    CHECK(geo::headingAtST(seq, 5.0, -10.0) == doctest::Approx(0.0));
}

// ─── Edge cases ───

TEST_CASE("SegmentSequence: single segment works") {
    LineSegment seg({0, 0}, {10, 0});
    SegmentSequence seq({{&seg}});
    CHECK(seq.numSegments() == 1);
    CHECK(seq.totalLength() == doctest::Approx(10.0));
    CHECK(seq.positionAt(5).x == doctest::Approx(5.0));
}

TEST_CASE("SegmentSequence: three segments binary search correctness") {
    LineSegment seg1({0, 0}, {10, 0});    // s ∈ [0, 10)
    LineSegment seg2({10, 0}, {25, 0});   // s ∈ [10, 25), length 15
    LineSegment seg3({25, 0}, {30, 0});   // s ∈ [25, 30), length 5
    SegmentSequence seq({{&seg1, &seg2, &seg3}});

    // Test at various s values
    auto check = [&](double s, int expectedIdx) {
        auto loc = seq.globalSToLocal(s);
        CHECK(loc.segmentIndex == expectedIdx);
    };
    check(0, 0);
    check(5, 0);
    check(9.9, 0);
    check(10, 1);
    check(15, 1);
    check(24.9, 1);
    check(25, 2);
    check(29.9, 2);
    check(30, 2);
}
