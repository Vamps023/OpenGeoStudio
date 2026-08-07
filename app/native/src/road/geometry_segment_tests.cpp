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
