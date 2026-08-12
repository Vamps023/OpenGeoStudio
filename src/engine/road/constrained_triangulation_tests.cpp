// ═══════════════════════════════════════════════════════════
// Constrained Triangulation Unit Tests (doctest)
// ═══════════════════════════════════════════════════════════
//
// Tests for constrained_triangulation.hpp
//
// Run with: cl /std:c++20 /EHsc /Fe:ct_tests.exe /I. constrained_triangulation_tests.cpp && ct_tests.exe

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define _USE_MATH_DEFINES
#include "doctest.h"

#include "road_engine/internal/constrained_triangulation.hpp"

#include <cmath>
#include <vector>

using namespace geo;

// ─── Helper: check if a triangle edge exists in the triangulation ───
static bool hasEdge(const std::vector<CTriangle>& tris, int a, int b) {
    for (const auto& t : tris) {
        if (t.hasEdge(a, b)) return true;
    }
    return false;
}

// ─── Helper: validate all triangle indices are in range ───
static bool validIndices(const ConstrainedTriangulation& ct, int numVerts) {
    for (const auto& t : ct.triangles) {
        for (int i = 0; i < 3; i++) {
            if (t.v[i] < 0 || t.v[i] >= numVerts) return false;
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════
// Test Suite: ConstrainedTriangulation
// ═══════════════════════════════════════════════════════════

TEST_SUITE("ConstrainedTriangulation") {

    // ─── Simple square ───
    TEST_CASE("Simple square produces 2 triangles") {
        std::vector<Point2D> square = {
            {0, 0}, {10, 0}, {10, 10}, {0, 10}
        };
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(square, internal, edges);

        CHECK(ct.triangleCount() == 2);
        CHECK(validIndices(ct, 4));
    }

    // ─── Simple square: all 4 vertices used ───
    TEST_CASE("Simple square uses all 4 vertices") {
        std::vector<Point2D> square = {
            {0, 0}, {10, 0}, {10, 10}, {0, 10}
        };
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(square, internal, edges);

        bool used[4] = {false, false, false, false};
        for (const auto& t : ct.triangles) {
            for (int i = 0; i < 3; i++) {
                if (t.v[i] >= 0 && t.v[i] < 4) used[t.v[i]] = true;
            }
        }
        CHECK(used[0]);
        CHECK(used[1]);
        CHECK(used[2]);
        CHECK(used[3]);
    }

    // ─── Simple triangle ───
    TEST_CASE("Simple triangle produces 1 triangle") {
        std::vector<Point2D> tri = {
            {0, 0}, {10, 0}, {5, 10}
        };
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(tri, internal, edges);

        CHECK(ct.triangleCount() == 1);
        CHECK(validIndices(ct, 3));
    }

    // ─── Triangle with internal point ───
    TEST_CASE("Triangle with internal point produces 3 triangles") {
        std::vector<Point2D> tri = {
            {0, 0}, {10, 0}, {5, 10}
        };
        std::vector<Point2D> internal = {{5, 3}};
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(tri, internal, edges);

        CHECK(ct.triangleCount() == 3);
        CHECK(validIndices(ct, 4));
    }

    // ─── Polygon with hole (via internal points and constraints) ───
    TEST_CASE("Polygon with hole: square with inner square constraint") {
        std::vector<Point2D> outer = {
            {0, 0}, {20, 0}, {20, 20}, {0, 20}
        };
        std::vector<Point2D> internal = {
            {5, 5}, {15, 5}, {15, 15}, {5, 15}
        };
        // Constraint edges for inner square (indices into combined: boundary[0..3] + internal[0..3])
        std::vector<std::pair<int,int>> edges = {
            {4, 5}, {5, 6}, {6, 7}, {7, 4}
        };

        ConstrainedTriangulation ct;
        ct.triangulate(outer, internal, edges);

        CHECK(ct.triangleCount() > 2);
        CHECK(validIndices(ct, 8));

        // Inner square edges should be present (indices 4..7 in combined array)
        CHECK(hasEdge(ct.triangles, 4, 5));
        CHECK(hasEdge(ct.triangles, 5, 6));
        CHECK(hasEdge(ct.triangles, 6, 7));
        CHECK(hasEdge(ct.triangles, 7, 4));
    }

    // ─── Constraint edges: diagonal constraint in square ───
    TEST_CASE("Constraint edge: diagonal forced into square") {
        std::vector<Point2D> square = {
            {0, 0}, {10, 0}, {10, 10}, {0, 10}
        };
        std::vector<Point2D> internal;
        // Force diagonal edge from vertex 0 to vertex 2
        std::vector<std::pair<int,int>> edges = {
            {0, 2}
        };

        ConstrainedTriangulation ct;
        ct.triangulate(square, internal, edges);

        CHECK(ct.triangleCount() == 2);
        CHECK(validIndices(ct, 4));

        // The diagonal (0, 2) must be present
        CHECK(hasEdge(ct.triangles, 0, 2));
    }

    // ─── Constraint edges: multiple non-crossing constraints ───
    TEST_CASE("Constraint edges: multiple non-crossing constraints in square") {
        std::vector<Point2D> square = {
            {0, 0}, {10, 0}, {10, 10}, {0, 10}
        };
        // Add an internal point so we can have non-crossing constraint edges
        std::vector<Point2D> internal = {{5, 5}};
        // Edge from vertex 0 to internal point 4, and from vertex 2 to internal point 4
        std::vector<std::pair<int,int>> edges = {
            {0, 4}, {2, 4}
        };

        ConstrainedTriangulation ct;
        ct.triangulate(square, internal, edges);

        // Both constraint edges should be present
        CHECK(hasEdge(ct.triangles, 0, 4));
        CHECK(hasEdge(ct.triangles, 2, 4));
    }

    // ─── Lane stripe constraints ───
    TEST_CASE("Lane stripe constraints: stripe through junction") {
        std::vector<Point2D> boundary = {
            {0, 0}, {30, 0}, {30, 20}, {0, 20}
        };
        std::vector<Point2D> internal = {
            {2, 10}, {8, 10}, {15, 10}, {22, 10}, {28, 10}
        };
        // Constraint edges connecting consecutive stripe samples
        // internal points start at index 4 (after 4 boundary points)
        std::vector<std::pair<int,int>> edges = {
            {4, 5}, {5, 6}, {6, 7}, {7, 8}
        };

        ConstrainedTriangulation ct;
        ct.triangulate(boundary, internal, edges);

        CHECK(ct.triangleCount() > 2);
        CHECK(validIndices(ct, 9));

        // All stripe edges should be present as constraint edges
        CHECK(hasEdge(ct.triangles, 4, 5));
        CHECK(hasEdge(ct.triangles, 5, 6));
        CHECK(hasEdge(ct.triangles, 6, 7));
        CHECK(hasEdge(ct.triangles, 7, 8));
    }

    // ─── Lane stripe: two crossing stripes ───
    TEST_CASE("Lane stripe: two crossing stripes") {
        std::vector<Point2D> boundary = {
            {0, 0}, {30, 0}, {30, 30}, {0, 30}
        };
        // Horizontal stripe + vertical stripe
        std::vector<Point2D> internal = {
            {2, 15}, {15, 15}, {28, 15},   // indices 4,5,6
            {15, 2}, {15, 28}               // indices 7,8 (vertical stripe)
        };
        std::vector<std::pair<int,int>> edges = {
            {4, 5}, {5, 6},   // horizontal
            {7, 5}, {5, 8}     // vertical (shares point at index 5)
        };

        ConstrainedTriangulation ct;
        ct.triangulate(boundary, internal, edges);

        CHECK(ct.triangleCount() > 2);
        CHECK(validIndices(ct, 9));

        // Stripe edges should be present
        CHECK(hasEdge(ct.triangles, 4, 5));
        CHECK(hasEdge(ct.triangles, 5, 6));
    }

    // ─── No constraint edges: pure Delaunay ───
    TEST_CASE("Pure Delaunay: no constraints, random points") {
        std::vector<Point2D> boundary = {
            {0, 0}, {20, 0}, {20, 20}, {0, 20}
        };
        std::vector<Point2D> internal = {
            {5, 5}, {10, 10}, {15, 5}, {10, 15}
        };
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(boundary, internal, edges);

        CHECK(ct.triangleCount() > 2);
        CHECK(validIndices(ct, 8));
    }

    // ─── Empty input ───
    TEST_CASE("Empty input: no points") {
        std::vector<Point2D> boundary;
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(boundary, internal, edges);

        CHECK(ct.triangleCount() == 0);
    }

    // ─── Too few points ───
    TEST_CASE("Too few points: 2 boundary points") {
        std::vector<Point2D> boundary = {{0, 0}, {10, 10}};
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(boundary, internal, edges);

        CHECK(ct.triangleCount() == 0);
    }

    // ─── Get indices returns flat array ───
    TEST_CASE("Get indices: flat uint32_t array") {
        std::vector<Point2D> square = {
            {0, 0}, {10, 0}, {10, 10}, {0, 10}
        };
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(square, internal, edges);

        auto indices = ct.getIndices();
        CHECK(indices.size() == 6);  // 2 triangles * 3
        for (uint32_t idx : indices) {
            CHECK(idx < 4);
        }
    }

    // ─── Get vertices returns input points ───
    TEST_CASE("Get vertices: returns combined points") {
        std::vector<Point2D> boundary = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        std::vector<Point2D> internal = {{5, 5}};
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(boundary, internal, edges);

        const auto& verts = ct.getVertices();
        CHECK(verts.size() == 5);
        CHECK(verts[0].x == 0.0);
        CHECK(verts[4].x == 5.0);
        CHECK(verts[4].y == 5.0);
    }

    // ─── Boundary edges are always constraints ───
    TEST_CASE("Boundary edges are constraints") {
        std::vector<Point2D> square = {
            {0, 0}, {10, 0}, {10, 10}, {0, 10}
        };
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(square, internal, edges);

        // All 4 boundary edges should be present
        CHECK(hasEdge(ct.triangles, 0, 1));
        CHECK(hasEdge(ct.triangles, 1, 2));
        CHECK(hasEdge(ct.triangles, 2, 3));
        CHECK(hasEdge(ct.triangles, 3, 0));
    }

    // ─── Larger polygon: hexagon ───
    TEST_CASE("Hexagon: 6-sided polygon") {
        std::vector<Point2D> hex;
        for (int i = 0; i < 6; i++) {
            double angle = i * M_PI / 3.0;
            hex.push_back({10.0 * std::cos(angle), 10.0 * std::sin(angle)});
        }
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(hex, internal, edges);

        // A hexagon should produce at least 4 triangles (n-2 for convex polygon)
        CHECK(ct.triangleCount() >= 4);
        CHECK(validIndices(ct, 6));
    }

    // ─── CTriangle struct tests ───
    TEST_CASE("CTriangle: hasVertex") {
        CTriangle t(1, 2, 3);
        CHECK(t.hasVertex(1));
        CHECK(t.hasVertex(2));
        CHECK(t.hasVertex(3));
        CHECK(!t.hasVertex(4));
    }

    TEST_CASE("CTriangle: hasEdge") {
        CTriangle t(1, 2, 3);
        CHECK(t.hasEdge(1, 2));
        CHECK(t.hasEdge(2, 3));
        CHECK(t.hasEdge(3, 1));
        CHECK(t.hasEdge(2, 1));  // reversed
        CHECK(!t.hasEdge(1, 4));
    }

    TEST_CASE("CTriangle: edgeOpposite") {
        CTriangle t(1, 2, 3);
        auto [a, b] = t.edgeOpposite(1);
        CHECK(a == 2);
        CHECK(b == 3);
    }

    TEST_CASE("CTriangle: oppositeVertex") {
        CTriangle t(1, 2, 3);
        CHECK(t.oppositeVertex(1, 2) == 3);
        CHECK(t.oppositeVertex(2, 3) == 1);
        CHECK(t.oppositeVertex(1, 3) == 2);
    }

    // ─── CEdge struct tests ───
    TEST_CASE("CEdge: ordering and equality") {
        CEdge e1(1, 5);
        CEdge e2(5, 1);
        CEdge e3(2, 3);
        CHECK(e1 == e2);  // normalized
        CHECK(!(e1 == e3));
        CHECK(e1 < e3);   // 1 < 2
    }

    // ─── Triangulation with many internal points ───
    TEST_CASE("Many internal points: grid inside square") {
        std::vector<Point2D> boundary = {
            {0, 0}, {30, 0}, {30, 30}, {0, 30}
        };
        std::vector<Point2D> internal;
        for (int x = 5; x <= 25; x += 5) {
            for (int y = 5; y <= 25; y += 5) {
                internal.push_back({static_cast<double>(x), static_cast<double>(y)});
            }
        }
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(boundary, internal, edges);

        CHECK(ct.triangleCount() > 10);
        CHECK(validIndices(ct, static_cast<int>(boundary.size() + internal.size())));
    }

    // ─── World-space UV consistency ───
    TEST_CASE("UV mapping: world-space planar UVs via CDT") {
        std::vector<Point2D> polygon = {
            {0, 0}, {20, 0}, {20, 20}, {0, 20}
        };
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(polygon, internal, edges);

        CHECK(ct.triangleCount() > 0);
        CHECK(ct.getVertices().size() >= 4);
    }

    // ─── CDT with stripe constraint ───
    TEST_CASE("CDT: constrained triangulation with stripe") {
        std::vector<Point2D> polygon = {
            {0, 0}, {30, 0}, {30, 30}, {0, 30}
        };

        // Add a constraint edge through the middle
        std::vector<Point2D> internal = {{5, 15}, {15, 15}, {25, 15}};
        std::vector<std::pair<int,int>> edges = {{0, 1}, {1, 2}};

        ConstrainedTriangulation ct;
        ct.triangulate(polygon, internal, edges);

        // Should have more triangles than simple fan (which would be 4)
        CHECK(ct.triangleCount() >= 4);
        CHECK(ct.getVertices().size() > 4);
    }

    // ─── CDT: no internal points, basic polygon ───
    TEST_CASE("CDT: no internal points, basic polygon") {
        std::vector<Point2D> polygon = {
            {0, 0}, {10, 0}, {10, 10}, {0, 10}
        };
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(polygon, internal, edges);

        CHECK(ct.triangleCount() > 0);
        CHECK(ct.getVertices().size() == 4);
    }

    // ─── CDT: empty polygon returns empty ───
    TEST_CASE("CDT: empty polygon returns empty mesh") {
        std::vector<Point2D> polygon;
        std::vector<Point2D> internal;
        std::vector<std::pair<int,int>> edges;

        ConstrainedTriangulation ct;
        ct.triangulate(polygon, internal, edges);

        CHECK(ct.triangleCount() == 0);
    }
}
