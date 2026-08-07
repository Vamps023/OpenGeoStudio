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
