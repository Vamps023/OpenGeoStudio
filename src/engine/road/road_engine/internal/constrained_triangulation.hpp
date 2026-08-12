#pragma once

// ═══════════════════════════════════════════════════════════
// Constrained Delaunay Triangulation (Bowyer-Watson)
// ═══════════════════════════════════════════════════════════
//
// @file constrained_triangulation.hpp
// @brief Header-only C++20 constrained Delaunay triangulation.
//
// Implements the Bowyer-Watson incremental Delaunay algorithm with
// constraint edge insertion. Constraint edges (e.g. lane stripes,
// boundary polygon edges) are forced into the triangulation by
// splitting intersecting triangles and re-triangulating the
// resulting cavities.
//
// Algorithm overview:
//   1. Create a super-triangle containing all input points.
//   2. Insert points one at a time (Bowyer-Watson):
//      - Find all triangles whose circumcircle contains the point.
//      - Remove them (cavity).
//      - Re-triangulate cavity with the new point.
//   3. Insert constraint edges:
//      - For each constraint edge, find triangles that intersect it.
//      - Split them and re-triangulate so the edge is present.
//   4. Remove triangles connected to super-triangle vertices.
//   5. Remove triangles outside the boundary polygon (optional).
//
// All code is header-only and depends only on geometry.hpp (Point2D)
// and the C++ standard library.

#include "../geometry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace geo {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── Triangle (indices into vertex array) ──────────────────
struct CTriangle {
    int v[3];   // vertex indices
    bool bad = false;  // marked for deletion during cavity

    CTriangle() : v{0, 0, 0} {}
    CTriangle(int a, int b, int c) : v{a, b, c} {}

    bool hasVertex(int idx) const {
        return v[0] == idx || v[1] == idx || v[2] == idx;
    }

    bool hasEdge(int a, int b) const {
        return (v[0] == a && v[1] == b) || (v[1] == a && v[2] == b) ||
               (v[2] == a && v[0] == b) || (v[1] == a && v[0] == b) ||
               (v[2] == a && v[1] == b) || (v[0] == a && v[2] == b);
    }

    // Get the edge opposite to vertex idx as (a, b)
    std::pair<int, int> edgeOpposite(int idx) const {
        if (v[0] == idx) return {v[1], v[2]};
        if (v[1] == idx) return {v[2], v[0]};
        return {v[0], v[1]};
    }

    // Get the vertex index that is not a or b
    int oppositeVertex(int a, int b) const {
        for (int i = 0; i < 3; i++) {
            if (v[i] != a && v[i] != b) return v[i];
        }
        return -1;
    }
};

// ─── Edge (pair of vertex indices) ─────────────────────────
struct CEdge {
    int a, b;
    CEdge(int a_, int b_) : a(std::min(a_, b_)), b(std::max(a_, b_)) {}
    bool operator==(const CEdge& o) const { return a == o.a && b == o.b; }
    bool operator<(const CEdge& o) const {
        if (a != o.a) return a < o.a;
        return b < o.b;
    }
};

// ─── ConstrainedTriangulation ──────────────────────────────
//
// Builds a constrained Delaunay triangulation from boundary polygon
// points and internal constraint edges.
//
class ConstrainedTriangulation {
public:
    // Output vertices (input points only, no super-triangle)
    std::vector<Point2D> vertices;

    // Output triangles (indices into vertices)
    std::vector<CTriangle> triangles;

    // Constraint edges that must appear in the triangulation
    std::set<CEdge> constraintEdges;

    // Internal: all vertices including super-triangle (indices 0,1,2)
    std::vector<Point2D> allVerts;

    // Super-triangle vertex indices (first 3 vertices)
    int superVerts[3] = {0, 1, 2};

    // ─── Main entry point ───
    // @param boundaryPoints  Points forming the boundary polygon (CCW)
    // @param internalPoints  Additional Steiner points (e.g. lane stripe samples)
    // @param edges           Constraint edges (pairs of point indices into the
    //                        combined array: [boundaryPoints | internalPoints])
    // @return                Triangles as index triples into vertices
    //
    void triangulate(const std::vector<Point2D>& boundaryPoints,
                     const std::vector<Point2D>& internalPoints,
                     const std::vector<std::pair<int,int>>& edges)
    {
        vertices.clear();
        triangles.clear();
        constraintEdges.clear();
        allVerts.clear();

        // Combine all input points
        std::vector<Point2D> inputPoints;
        inputPoints.reserve(boundaryPoints.size() + internalPoints.size());
        for (const auto& p : boundaryPoints) inputPoints.push_back(p);
        for (const auto& p : internalPoints) inputPoints.push_back(p);

        if (inputPoints.size() < 3) return;

        int nInput = static_cast<int>(inputPoints.size());

        // Create super-triangle (allVerts[0..2] = super, allVerts[3..] = input)
        createSuperTriangle(inputPoints);

        // Push all input points into allVerts BEFORE inserting
        for (int i = 0; i < nInput; i++) {
            allVerts.push_back(inputPoints[i]);
        }

        // Insert all points using Bowyer-Watson
        // Points start at index 3 in allVerts
        for (int i = 0; i < nInput; i++) {
            insertPoint(i + 3);
        }

        // Insert constraint edges (indices are into inputPoints, need +3 offset)
        // Limit the number of constraint edges to prevent O(n²) blowup
        const int MAX_CONSTRAINT_EDGES = 100;
        int edgeCount = 0;
        for (const auto& [ea, eb] : edges) {
            if (edgeCount >= MAX_CONSTRAINT_EDGES) {
                printf("[CDT] WARNING: reached max constraint edges (%d), skipping rest\n",
                       MAX_CONSTRAINT_EDGES);
                break;
            }
            // Skip degenerate edges (same point)
            if (ea == eb) continue;
            // Skip edges with out-of-range indices
            if (ea < 0 || ea >= nInput || eb < 0 || eb >= nInput) continue;
            insertConstraintEdge(ea + 3, eb + 3);
            edgeCount++;
        }

        // Add boundary polygon edges as constraints
        int nBound = static_cast<int>(boundaryPoints.size());
        for (int i = 0; i < nBound; i++) {
            int a = i + 3;
            int b = ((i + 1) % nBound) + 3;
            insertConstraintEdge(a, b);
        }

        // Remove triangles connected to super-triangle vertices
        removeSuperTriangles();

        // Remove triangles outside the boundary polygon
        removeOutsideTriangles(nBound);

        // Copy input points to output vertices
        vertices = inputPoints;

        // Adjust triangle indices: subtract 3 to map from allVerts to vertices
        for (auto& t : triangles) {
            t.v[0] -= 3;
            t.v[1] -= 3;
            t.v[2] -= 3;
        }
    }

    // ─── Get result as flat index array ───
    std::vector<uint32_t> getIndices() const {
        std::vector<uint32_t> result;
        result.reserve(triangles.size() * 3);
        for (const auto& t : triangles) {
            result.push_back(static_cast<uint32_t>(t.v[0]));
            result.push_back(static_cast<uint32_t>(t.v[1]));
            result.push_back(static_cast<uint32_t>(t.v[2]));
        }
        return result;
    }

    // ─── Get vertices (without super-triangle) ───
    const std::vector<Point2D>& getVertices() const {
        return vertices;
    }

    // ─── Get triangle count ───
    int triangleCount() const {
        return static_cast<int>(triangles.size());
    }

private:
    // ─── Create super-triangle containing all points ───
    void createSuperTriangle(const std::vector<Point2D>& pts) {
        double minX = std::numeric_limits<double>::max();
        double minY = std::numeric_limits<double>::max();
        double maxX = std::numeric_limits<double>::lowest();
        double maxY = std::numeric_limits<double>::lowest();

        for (const auto& p : pts) {
            minX = std::min(minX, p.x);
            minY = std::min(minY, p.y);
            maxX = std::max(maxX, p.x);
            maxY = std::max(maxY, p.y);
        }

        double dx = maxX - minX;
        double dy = maxY - minY;
        double deltaMax = std::max(dx, dy);
        if (deltaMax < 1.0) deltaMax = 100.0;
        double delta = deltaMax * 10.0;

        double midX = (minX + maxX) / 2.0;
        double midY = (minY + maxY) / 2.0;

        // Super-triangle vertices (indices 0, 1, 2)
        allVerts.push_back({midX - 20 * delta, midY - delta});
        allVerts.push_back({midX, midY + 20 * delta});
        allVerts.push_back({midX + 20 * delta, midY - delta});

        // Initial triangle
        triangles.push_back(CTriangle(0, 1, 2));
    }

    // ─── Bowyer-Watson point insertion ───
    void insertPoint(int pIdx) {
        const Point2D& p = allVerts[pIdx];

        // Safety: prevent runaway triangle creation
        if (triangles.size() > 50000) {
            printf("[CDT] WARNING: too many triangles (%zu) during point insertion, skipping\n",
                   triangles.size());
            return;
        }

        // Find all triangles whose circumcircle contains p
        std::vector<int> badTriangles;
        for (int i = 0; i < static_cast<int>(triangles.size()); i++) {
            if (triangles[i].bad) continue;
            if (inCircumcircle(triangles[i], p)) {
                badTriangles.push_back(i);
                triangles[i].bad = true;
            }
        }

        // Find the boundary of the cavity (edges not shared by two bad triangles)
        std::vector<std::pair<int,int>> cavityEdges;
        for (int bi : badTriangles) {
            const auto& t = triangles[bi];
            for (int e = 0; e < 3; e++) {
                int a = t.v[e];
                int b = t.v[(e + 1) % 3];

                bool shared = false;
                for (int bj : badTriangles) {
                    if (bj == bi) continue;
                    if (triangles[bj].hasEdge(a, b)) {
                        shared = true;
                        break;
                    }
                }
                if (!shared) {
                    cavityEdges.push_back({a, b});
                }
            }
        }

        // Remove bad triangles
        std::vector<CTriangle> newTriangles;
        for (int i = 0; i < static_cast<int>(triangles.size()); i++) {
            if (!triangles[i].bad) {
                newTriangles.push_back(triangles[i]);
            }
        }
        triangles = std::move(newTriangles);

        // Re-triangulate the cavity with the new point
        for (const auto& [a, b] : cavityEdges) {
            triangles.push_back(CTriangle(a, b, pIdx));
        }
    }

    // ─── Check if point p is inside the circumcircle of triangle t ───
    bool inCircumcircle(const CTriangle& t, const Point2D& p) const {
        const Point2D& a = allVerts[t.v[0]];
        const Point2D& b = allVerts[t.v[1]];
        const Point2D& c = allVerts[t.v[2]];

        // Use the determinant test for in-circle
        double ax = a.x - p.x;
        double ay = a.y - p.y;
        double bx = b.x - p.x;
        double by = b.y - p.y;
        double cx = c.x - p.x;
        double cy = c.y - p.y;

        double det = (ax * ax + ay * ay) * (bx * cy - cx * by)
                   - (bx * bx + by * by) * (ax * cy - cx * ay)
                   + (cx * cx + cy * cy) * (ax * by - bx * ay);

        // If triangle is CCW, det > 0 means inside circumcircle
        double orient = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
        if (orient > 0) {
            return det > 0;
        } else {
            return det < 0;
        }
    }

    // ─── Check if two segments intersect (proper intersection) ───
    static bool segmentsIntersect(const Point2D& p1, const Point2D& p2,
                                   const Point2D& p3, const Point2D& p4) {
        double d1 = cross2(p3, p4, p1);
        double d2 = cross2(p3, p4, p2);
        double d3 = cross2(p1, p2, p3);
        double d4 = cross2(p1, p2, p4);

        if (((d1 > EPSILON && d2 < -EPSILON) || (d1 < -EPSILON && d2 > EPSILON)) &&
            ((d3 > EPSILON && d4 < -EPSILON) || (d3 < -EPSILON && d4 > EPSILON))) {
            return true;
        }

        if (std::abs(d1) < EPSILON && onSegment(p3, p4, p1)) return true;
        if (std::abs(d2) < EPSILON && onSegment(p3, p4, p2)) return true;
        if (std::abs(d3) < EPSILON && onSegment(p1, p2, p3)) return true;
        if (std::abs(d4) < EPSILON && onSegment(p1, p2, p4)) return true;

        return false;
    }

    static double cross2(const Point2D& p1, const Point2D& p2, const Point2D& p3) {
        return (p2.x - p1.x) * (p3.y - p1.y) - (p3.x - p1.x) * (p2.y - p1.y);
    }

    static bool onSegment(const Point2D& a, const Point2D& b, const Point2D& p) {
        return p.x >= std::min(a.x, b.x) - EPSILON &&
               p.x <= std::max(a.x, b.x) + EPSILON &&
               p.y >= std::min(a.y, b.y) - EPSILON &&
               p.y <= std::max(a.y, b.y) + EPSILON;
    }

    // ─── Insert a constraint edge ───
    void insertConstraintEdge(int a, int b) {
        // Safety: if triangle count is exploding, skip further constraints
        if (triangles.size() > 50000) {
            printf("[CDT] WARNING: too many triangles (%zu), skipping constraint edge\n",
                   triangles.size());
            constraintEdges.insert(CEdge(a, b));
            return;
        }

        // If edge already exists, nothing to do
        if (edgeExists(a, b)) {
            constraintEdges.insert(CEdge(a, b));
            return;
        }

        const Point2D& pa = allVerts[a];
        const Point2D& pb = allVerts[b];

        // Find all triangles that intersect the constraint edge (a, b)
        // We do a simple scan of all triangles
        std::vector<int> intersectedTris;

        for (int i = 0; i < static_cast<int>(triangles.size()); i++) {
            const auto& t = triangles[i];

            // If this triangle has both endpoints, edge already exists
            if (t.hasVertex(a) && t.hasVertex(b)) {
                constraintEdges.insert(CEdge(a, b));
                return;
            }

            // Check if any edge of this triangle intersects (a, b)
            bool intersects = false;
            for (int e = 0; e < 3; e++) {
                int ea = t.v[e];
                int eb = t.v[(e + 1) % 3];

                // Skip edges that share an endpoint with the constraint edge
                if (ea == a || ea == b || eb == a || eb == b) continue;

                if (segmentsIntersect(pa, pb, allVerts[ea], allVerts[eb])) {
                    intersects = true;
                    break;
                }
            }

            // Also include triangles that share a vertex with the constraint edge
            // (they may need to be re-triangulated to include the edge)
            if (intersects || t.hasVertex(a) || t.hasVertex(b)) {
                intersectedTris.push_back(i);
            }
        }

        if (intersectedTris.empty()) {
            constraintEdges.insert(CEdge(a, b));
            return;
        }

        // Mark intersected triangles as bad
        for (int ti : intersectedTris) {
            triangles[ti].bad = true;
        }

        // Collect cavity boundary edges (edges not shared by two bad triangles)
        std::vector<std::pair<int,int>> cavityEdges;
        for (int ti : intersectedTris) {
            const auto& t = triangles[ti];
            for (int e = 0; e < 3; e++) {
                int ea = t.v[e];
                int eb = t.v[(e + 1) % 3];
                bool shared = false;
                for (int tj : intersectedTris) {
                    if (tj == ti) continue;
                    if (triangles[tj].hasEdge(ea, eb)) {
                        shared = true;
                        break;
                    }
                }
                if (!shared) {
                    cavityEdges.push_back({ea, eb});
                }
            }
        }

        // Remove bad triangles
        std::vector<CTriangle> newTriangles;
        for (int i = 0; i < static_cast<int>(triangles.size()); i++) {
            if (!triangles[i].bad) {
                newTriangles.push_back(triangles[i]);
            }
        }
        triangles = std::move(newTriangles);

        // Separate cavity edges into two chains: left and right of (a -> b)
        std::vector<std::pair<int,int>> leftEdges;
        std::vector<std::pair<int,int>> rightEdges;

        for (const auto& [ea, eb] : cavityEdges) {
            // Skip the constraint edge itself if it's in the boundary
            if ((ea == a && eb == b) || (ea == b && eb == a)) continue;

            // Determine which side of (a->b) the edge midpoint is on
            const Point2D& pea = allVerts[ea];
            double cross = (pb.x - pa.x) * (pea.y - pa.y) - (pb.y - pa.y) * (pea.x - pa.x);
            if (cross > EPSILON) {
                leftEdges.push_back({ea, eb});
            } else if (cross < -EPSILON) {
                rightEdges.push_back({ea, eb});
            } else {
                // Collinear - assign to left
                leftEdges.push_back({ea, eb});
            }
        }

        // Re-triangulate each side using ear clipping
        triangulateCavity(leftEdges, a, b);
        triangulateCavity(rightEdges, b, a);

        constraintEdges.insert(CEdge(a, b));
    }

    // ─── Triangulate a cavity (polygon) defined by edges + constraint edge ───
    void triangulateCavity(std::vector<std::pair<int,int>>& edges,
                            int startVert, int endVert)
    {
        if (edges.empty()) {
            // No intermediate vertices - shouldn't happen for a valid cavity
            return;
        }

        // Build ordered vertex list from edges
        // The cavity forms a polygon: startVert -> ... -> endVert
        std::vector<int> polyVerts;

        // Build adjacency from edges
        std::map<int, std::vector<int>> adj;
        for (const auto& [a, b] : edges) {
            adj[a].push_back(b);
            adj[b].push_back(a);
        }

        // Walk from startVert to endVert
        polyVerts.push_back(startVert);
        int prev = startVert;
        int curr = -1;

        // Find first neighbor of startVert (not endVert)
        if (adj.count(startVert)) {
            for (int n : adj[startVert]) {
                if (n != endVert) {
                    curr = n;
                    break;
                }
            }
        }

        if (curr < 0) {
            // startVert connects directly to endVert on this side
            // Just add the triangle
            return;
        }

        while (curr != endVert && curr >= 0) {
            polyVerts.push_back(curr);
            int next = -1;
            if (adj.count(curr)) {
                for (int n : adj[curr]) {
                    if (n != prev) {
                        next = n;
                        break;
                    }
                }
            }
            prev = curr;
            curr = next;
            if (curr < 0) break;

            // Safety guard: prevent infinite loop in cyclic adjacency
            if (static_cast<int>(polyVerts.size()) > static_cast<int>(edges.size()) + 2) {
                printf("[CDT] triangulateCavity: cycle detected, aborting (polyVerts=%zu, edges=%zu)\n",
                       polyVerts.size(), edges.size());
                return;
            }
        }
        polyVerts.push_back(endVert);

        if (polyVerts.size() < 3) return;

        // Ear clipping triangulation
        earClipTriangulate(polyVerts);
    }

    // ─── Ear clipping triangulation for a polygon ───
    void earClipTriangulate(std::vector<int>& polyVerts) {
        std::vector<int> verts = polyVerts;
        int n = static_cast<int>(verts.size());

        if (n < 3) return;
        if (n == 3) {
            triangles.push_back(CTriangle(verts[0], verts[1], verts[2]));
            return;
        }

        // Ensure CCW orientation
        double area = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            area += allVerts[verts[i]].x * allVerts[verts[j]].y;
            area -= allVerts[verts[j]].x * allVerts[verts[i]].y;
        }
        if (area < 0) {
            std::reverse(verts.begin(), verts.end());
        }

        int remaining = n;
        int guard = 0;

        while (remaining > 3 && guard < n * n) {
            bool earFound = false;
            for (int i = 0; i < remaining; i++) {
                int prev = (i - 1 + remaining) % remaining;
                int next = (i + 1) % remaining;

                const Point2D& a = allVerts[verts[prev]];
                const Point2D& b = allVerts[verts[i]];
                const Point2D& c = allVerts[verts[next]];

                // Check convex (CCW)
                double cross = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
                if (cross <= 0) continue;

                // Check no other vertex inside this triangle
                bool pointInside = false;
                for (int j = 0; j < remaining; j++) {
                    if (j == prev || j == i || j == next) continue;
                    if (pointInTriangle(allVerts[verts[j]], a, b, c)) {
                        pointInside = true;
                        break;
                    }
                }
                if (pointInside) continue;

                // Found an ear
                triangles.push_back(CTriangle(verts[prev], verts[i], verts[next]));
                verts.erase(verts.begin() + i);
                remaining--;
                earFound = true;
                break;
            }
            if (!earFound) {
                // Fallback: just add a fan
                for (int i = 1; i < remaining - 1; i++) {
                    triangles.push_back(CTriangle(verts[0], verts[i], verts[i+1]));
                }
                break;
            }
            guard++;
        }

        if (remaining == 3) {
            triangles.push_back(CTriangle(verts[0], verts[1], verts[2]));
        }
    }

    static bool pointInTriangle(const Point2D& p, const Point2D& a,
                                 const Point2D& b, const Point2D& c) {
        double d1 = cross2(a, b, p);
        double d2 = cross2(b, c, p);
        double d3 = cross2(c, a, p);
        bool hasNeg = (d1 < -EPSILON) || (d2 < -EPSILON) || (d3 < -EPSILON);
        bool hasPos = (d1 > EPSILON) || (d2 > EPSILON) || (d3 > EPSILON);
        return !(hasNeg && hasPos);
    }

    bool edgeExists(int a, int b) const {
        for (const auto& t : triangles) {
            if (t.hasEdge(a, b)) return true;
        }
        return false;
    }

    // ─── Remove triangles connected to super-triangle vertices ───
    void removeSuperTriangles() {
        std::vector<CTriangle> kept;
        for (const auto& t : triangles) {
            if (t.hasVertex(superVerts[0]) ||
                t.hasVertex(superVerts[1]) ||
                t.hasVertex(superVerts[2])) {
                continue;
            }
            kept.push_back(t);
        }
        triangles = std::move(kept);
    }

    // ─── Remove triangles outside the boundary polygon ───
    void removeOutsideTriangles(int nBound) {
        if (nBound < 3) return;

        // Build boundary polygon points (indices 3..3+nBound-1 in allVerts)
        std::vector<Point2D> boundPoly;
        for (int i = 0; i < nBound; i++) {
            boundPoly.push_back(allVerts[i + 3]);
        }

        std::vector<CTriangle> kept;
        for (const auto& t : triangles) {
            const Point2D& a = allVerts[t.v[0]];
            const Point2D& b = allVerts[t.v[1]];
            const Point2D& c = allVerts[t.v[2]];
            Point2D centroid{(a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0};

            if (pointInPolygon(centroid, boundPoly)) {
                kept.push_back(t);
            }
        }
        triangles = std::move(kept);
    }

    static bool pointInPolygon(const Point2D& p, const std::vector<Point2D>& poly) {
        int n = static_cast<int>(poly.size());
        if (n < 3) return false;

        bool inside = false;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            const Point2D& pi = poly[i];
            const Point2D& pj = poly[j];

            if (((pi.y > p.y) != (pj.y > p.y)) &&
                (p.x < (pj.x - pi.x) * (p.y - pi.y) / (pj.y - pi.y) + pi.x)) {
                inside = !inside;
            }
        }
        return inside;
    }
};

} // namespace geo
