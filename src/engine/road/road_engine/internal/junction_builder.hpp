#pragma once

// ═══════════════════════════════════════════════════════════
// Junction Builder — Phase 3.3: Lane-Based Junction Geometry
// ═══════════════════════════════════════════════════════════
//
// @file junction_builder.hpp
// @brief Generates junction geometry from lane connections, replacing
//        the legacy polygon-based intersection generator.
//
// @section Architecture
//
//   road_graph.hpp       = Road network topology
//   lane_graph.hpp       = Lane connectivity at junctions
//   junction_builder.hpp = Junction geometry from lane links (THIS FILE)
//
// @section Responsibility
// Converts a LaneGraph (lane-to-lane connectivity) into renderable
// mesh geometry. Instead of a flat polygon, the junction has:
//   - Lane stripes that flow naturally from each approach's lane boundaries
//   - Asphalt mesh filling the area between stripes
//   - Marking meshes on top of the stripes
//
// @section Replaces
// This replaces the legacy generateEdgeBasedPolygon() in intersection.hpp.
// The legacy generator produced a single polygon with fillet corners.
// This generator produces per-lane geometry with proper connectivity.
//
// @section API Freeze
// NOT YET FROZEN. Will be frozen at Phase 3 Complete.

#include "constrained_triangulation.hpp"
#include "../geometry.hpp"
#include "lane_graph.hpp"
#include "../lane_network.hpp"
#include "road_graph.hpp"
#include "../road_mesh_generator.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace geo {

// ═══════════════════════════════════════════════════════════
// JunctionLaneStripe — A lane boundary stripe through the junction
// ═══════════════════════════════════════════════════════════
//
// Connects lane boundaries from adjacent approaches with a smooth curve.
// Solid stripes are road edges; dashed stripes are lane dividers.
//
struct JunctionLaneStripe {
    Point2D fromPoint;                      // start point (on approach boundary)
    Point2D toPoint;                        // end point (on approach boundary)
    std::vector<Point2D> controlPoints;     // bezier control points
    std::vector<Point2D> samples;           // sampled curve points
    enum class Type { Solid, Dashed, None } type = Type::None;
    std::string color = "white";

    bool isSolid() const  { return type == Type::Solid; }
    bool isDashed() const { return type == Type::Dashed; }
};

// ═══════════════════════════════════════════════════════════
// JunctionMeshData — Renderable junction geometry (typed arrays)
// ═══════════════════════════════════════════════════════════
//
// Mirrors the MeshSection format from road_mesh_generator.hpp
// but uses flat arrays for direct GPU upload.
//
struct JunctionMeshData {
    std::vector<float> positions;   // x, y, z interleaved
    std::vector<float> normals;     // nx, ny, nz
    std::vector<float> uvs;         // u, v
    std::vector<uint32_t> indices;

    int vertexCount = 0;
    int triangleCount = 0;

    bool empty() const { return indices.empty(); }
};

// ═══════════════════════════════════════════════════════════
// JunctionResult — Complete junction build output
// ═══════════════════════════════════════════════════════════
//
// Contains all geometry and metadata for a single junction.
//
struct JunctionResult {
    std::string junctionId;
    Point2D center;
    std::vector<Point2D> polygon;           // boundary polygon (for culling)
    std::vector<JunctionLaneStripe> laneStripes;
    JunctionMeshData asphaltMesh;
    JunctionMeshData markingMesh;
    int numLaneConnections = 0;
    int numApproaches = 0;

    bool isValid() const { return !polygon.empty(); }
};

// ═══════════════════════════════════════════════════════════
// JunctionBuilder — Builds junction geometry from lane graph
// ═══════════════════════════════════════════════════════════

struct JunctionBuilder {

    // ═══════════════════════════════════════════════════════════
    // LaneCorridor — A single lane's corridor through the junction
    // ═══════════════════════════════════════════════════════════
    //
    // Each LaneLink becomes one corridor: a left boundary, a right
    // boundary, and the maneuver type. Corridors are grouped by
    // (fromRoad, toRoad) to form asphalt patches.
    //
    struct LaneCorridor {
        std::vector<Point2D> leftBoundary;    // offset path by -width/2
        std::vector<Point2D> rightBoundary;   // offset path by +width/2
        ManeuverType maneuver = ManeuverType::Straight;
        const LaneLink* link = nullptr;
        std::string fromRoadId;
        std::string toRoadId;
        double width = 3.5;
    };

    // ─── Build a single junction (esmini-inspired strip-based) ───
    //
    // Pipeline (esmini-inspired):
    //   1. For each LaneLink, build a lane surface strip mesh
    //      - Sample the link path (already a bezier curve)
    //      - Offset left/right by laneWidth/2 to get boundaries
    //      - Create triangle strip: L[0],R[0],L[1],R[1],... → quads → triangles
    //   2. Combine all lane surface strips into one asphalt mesh
    //   3. Generate lane markings from corridor boundaries
    //   4. Build culling polygon from outer boundary points
    //
    // This replaces the old polygon-based approach. No polygon triangulation,
    // no CDT, no ear-clipping, no fan fallback. Each lane surface is a simple
    // strip mesh that follows the actual driving path.
    //
    static JunctionResult build(const RoadNode& junction,
                                const RoadGraph& graph,
                                const LaneGraph& laneGraph,
                                const std::map<std::string, LaneNetwork>& laneNets) {
        JunctionResult result;
        result.junctionId = junction.id;
        result.center = junction.position;
        result.numLaneConnections = laneGraph.numLinks();
        result.numApproaches = static_cast<int>(junction.connectedRoadIds.size());

        printf("[JunctionMesh] junction=%s\n", junction.id.c_str());
        printf("[JunctionMesh] approaches=%d\n", result.numApproaches);
        printf("[JunctionMesh] laneConnections=%d\n", result.numLaneConnections);

        if (!junction.isJunction()) {
            printf("[JunctionMesh] EARLY RETURN: not a junction\n");
            return result;
        }

        // Step 1: Build approach edges. Try lane-graph first; if that fails
        // (no lane nodes), fall back to road-graph-based approach edges.
        auto approaches = buildApproachEdges(junction, graph, laneGraph);
        printf("[JunctionMesh] approach edges (lane-based): %zu\n", approaches.size());

        if (approaches.size() < 2) {
            // Fallback: build approach edges from road graph edges directly.
            // This handles the case where lane networks haven't been built yet
            // or the lane graph is empty (common with auto-detected junctions).
            approaches = buildApproachEdgesFromRoadGraph(junction, graph);
            printf("[JunctionMesh] approach edges (road-graph fallback): %zu\n", approaches.size());
        }

        if (approaches.size() < 2) {
            printf("[JunctionMesh] EARLY RETURN: fewer than 2 approach edges\n");
            return result;
        }

        // Step 2: Build the junction boundary as straight road-edge
        // segments joined by rounded fillet corners (same algorithm class
        // as the legacy generateEdgeBasedPolygon() in intersection.hpp and
        // esmini-style junction pavement), sorted by angle around center.
        std::vector<Point2D> polygon = buildFilletPolygon(approaches, junction.position);
        printf("[JunctionMesh]   filletPolygon=%zu pts\n", polygon.size());
        if (polygon.size() < 3) {
            printf("[JunctionMesh] EARLY RETURN: fillet polygon degenerate\n");
            return result;
        }
        result.polygon = polygon;

        // Step 3: Triangulate the polygon using simple ear-clipping.
        // This is more robust than CDT for junction boundary polygons
        // which may have near-collinear or closely spaced vertices.
        result.asphaltMesh = triangulatePolygonEarClip(polygon);

        printf("[JunctionMesh] RESULT\n");
        printf("[JunctionMesh]   polygon=%zu pts\n", polygon.size());
        printf("[JunctionMesh]   vertices=%d\n", result.asphaltMesh.vertexCount);
        printf("[JunctionMesh]   triangles=%d\n", result.asphaltMesh.triangleCount);

        // Step 4: Generate solid edge stripes (road boundary lines) through
        // the junction. These connect the left/right edges of each approach
        // road to the next approach's edges, following the fillet polygon.
        result.laneStripes = buildEdgeStripes(approaches, polygon, junction.position);
        result.markingMesh = buildStripeMesh(result.laneStripes);

        // Step 5: Validate mesh
        auto validation = validateMesh(result.asphaltMesh);
        printf("[JunctionMesh] VALIDATION\n");
        printf("[JunctionMesh]   valid=%s\n", validation.valid ? "YES" : "NO");
        printf("[JunctionMesh]   degenerateTriangles=%d\n", validation.degenerateTriangles);
        printf("[JunctionMesh]   nanVertices=%d\n", validation.nanVertices);
        printf("[JunctionMesh]   outOfBoundsIndices=%d\n", validation.outOfBoundsIndices);

        return result;
    }

    // ─── Build a lane surface strip mesh for a single corridor ───
    //
    // This is the esmini-inspired approach: instead of building a polygon
    // and triangulating it, we directly create a triangle strip along the
    // lane path. Each sample point generates two vertices (left + right
    // boundary), and consecutive samples form a quad (2 triangles).
    //
    //   L[0] ─── L[1] ─── L[2] ─── ... ─── L[n-1]
    //    |  \    |  \    |  \              |  \
    //    |   \   |   \   |   \             |   \
    //   R[0] ─── R[1] ─── R[2] ─── ... ─── R[n-1]
    //
    // Each quad (L[i], R[i], L[i+1], R[i+1]) becomes:
    //   Triangle 1: L[i], R[i], L[i+1]
    //   Triangle 2: R[i], R[i+1], L[i+1]
    //
    static JunctionMeshData buildLaneSurfaceStrip(const LaneCorridor& cor) {
        JunctionMeshData mesh;

        const auto& left = cor.leftBoundary;
        const auto& right = cor.rightBoundary;
        int n = static_cast<int>(left.size());

        if (n < 2 || static_cast<int>(right.size()) != n) return mesh;

        // Create vertices: for each sample, left and right boundary points
        // Vertex order: L[0], R[0], L[1], R[1], ..., L[n-1], R[n-1]
        for (int i = 0; i < n; i++) {
            // Left vertex
            mesh.positions.push_back(left[i].x);
            mesh.positions.push_back(0.02);   // slight offset above ground
            mesh.positions.push_back(left[i].y);
            mesh.normals.push_back(0);
            mesh.normals.push_back(0);
            mesh.normals.push_back(1);
            mesh.uvs.push_back(left[i].x / 10.0);
            mesh.uvs.push_back(left[i].y / 10.0);

            // Right vertex
            mesh.positions.push_back(right[i].x);
            mesh.positions.push_back(0.02);
            mesh.positions.push_back(right[i].y);
            mesh.normals.push_back(0);
            mesh.normals.push_back(0);
            mesh.normals.push_back(1);
            mesh.uvs.push_back(right[i].x / 10.0);
            mesh.uvs.push_back(right[i].y / 10.0);
        }

        // Create triangles: for each pair of consecutive samples, create 2 triangles
        // Vertex indices: L[i] = i*2, R[i] = i*2+1
        for (int i = 0; i < n - 1; i++) {
            uint32_t li = i * 2;       // L[i]
            uint32_t ri = i * 2 + 1;   // R[i]
            uint32_t li2 = (i + 1) * 2;     // L[i+1]
            uint32_t ri2 = (i + 1) * 2 + 1; // R[i+1]

            // Triangle 1: L[i], R[i], L[i+1]
            mesh.indices.push_back(li);
            mesh.indices.push_back(ri);
            mesh.indices.push_back(li2);

            // Triangle 2: R[i], R[i+1], L[i+1]
            mesh.indices.push_back(ri);
            mesh.indices.push_back(ri2);
            mesh.indices.push_back(li2);
        }

        mesh.vertexCount = n * 2;
        mesh.triangleCount = (n - 1) * 2;
        return mesh;
    }

    // ─── Count degenerate triangles (zero area) ───
    static int countDegenerateTriangles(const JunctionMeshData& mesh) {
        int count = 0;
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            uint32_t i0 = mesh.indices[i];
            uint32_t i1 = mesh.indices[i + 1];
            uint32_t i2 = mesh.indices[i + 2];

            // Get positions (x, y, z interleaved)
            double ax = mesh.positions[i0 * 3], ay = mesh.positions[i0 * 3 + 2];
            double bx = mesh.positions[i1 * 3], by = mesh.positions[i1 * 3 + 2];
            double cx = mesh.positions[i2 * 3], cy = mesh.positions[i2 * 3 + 2];

            // Check for NaN
            if (std::isnan(ax) || std::isnan(ay) || std::isnan(bx) || std::isnan(by) ||
                std::isnan(cx) || std::isnan(cy)) {
                count++;
                continue;
            }

            // Cross product (2D area)
            double cross = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
            if (std::abs(cross) < 1e-8) count++;
        }
        return count;
    }

    // ─── Offset a path by distance d perpendicular to travel direction ───
    // For each sample, tangent = direction to next point, normal = (-ty, tx).
    // Offset point = sample + normal * d.
    static std::vector<Point2D> offsetPath(const std::vector<Point2D>& path, double d) {
        std::vector<Point2D> result;
        int n = static_cast<int>(path.size());
        if (n == 0) return result;
        result.reserve(n);

        for (int i = 0; i < n; i++) {
            double tx, ty;
            if (i == 0) {
                tx = path[1].x - path[0].x;
                ty = path[1].y - path[0].y;
            } else if (i == n - 1) {
                tx = path[i].x - path[i - 1].x;
                ty = path[i].y - path[i - 1].y;
            } else {
                tx = path[i + 1].x - path[i - 1].x;
                ty = path[i + 1].y - path[i - 1].y;
            }
            double tlen = std::sqrt(tx * tx + ty * ty);
            if (tlen < 1e-10) {
                result.push_back(path[i]);
                continue;
            }
            // Normal = (-ty, tx) normalized
            double nx = -ty / tlen;
            double ny = tx / tlen;
            result.push_back({path[i].x + nx * d, path[i].y + ny * d});
        }
        return result;
    }

    // ─── Build corridors for all lane links ───
    // Uses actual lane boundaries from LaneNetwork (libOpenDRIVE-inspired).
    // Instead of offsetting a center path by width/2, we:
    //   1. Find the incoming lane's left/right boundaries from LaneNetwork
    //   2. Find the outgoing lane's left/right boundaries from LaneNetwork
    //   3. Get the boundary endpoints near the junction
    //   4. Interpolate (bezier) from incoming boundary endpoint to outgoing boundary endpoint
    //   5. The corridor's leftBoundary = interpolated left boundary
    //      The corridor's rightBoundary = interpolated right boundary
    // This ensures actual lane widths are used and shared boundaries are consistent.
    static std::vector<LaneCorridor> buildCorridors(
        const LaneGraph& laneGraph,
        const std::map<std::string, LaneNetwork>& laneNets,
        const RoadNode& junction)
    {
        std::vector<LaneCorridor> corridors;

        for (const auto& link : laneGraph.links) {
            if (link.path.size() < 2) {
                printf("[JunctionMesh]   link %s: path too short (%zu), skipping\n",
                       link.id.c_str(), link.path.size());
                continue;
            }

            const LaneNode* fromNode = laneGraph.findNode(link.fromLaneNodeId);
            const LaneNode* toNode = laneGraph.findNode(link.toLaneNodeId);
            if (!fromNode || !toNode) {
                printf("[JunctionMesh]   link %s: missing node, skipping\n",
                       link.id.c_str());
                continue;
            }

            LaneCorridor cor;
            cor.link = &link;
            cor.maneuver = link.maneuver;
            cor.fromRoadId = fromNode->roadId;
            cor.toRoadId = toNode->roadId;
            cor.width = (fromNode->width > 0.0) ? fromNode->width : 3.5;

            // Try to get actual lane boundaries from LaneNetwork
            // For lane ID N: left boundary = boundary(N-1, N) or boundary(0, N) if N=1
            //                right boundary = boundary(N, N+1) or road edge if N is outermost
            Point2D inLeftPt, inRightPt, outLeftPt, outRightPt;
            bool hasBoundaries = false;

            auto getLaneBoundaryPoints = [&](const std::string& roadId, int laneId,
                                              Point2D& leftPt, Point2D& rightPt) -> bool {
                auto it = laneNets.find(roadId);
                if (it == laneNets.end()) return false;
                const auto& net = it->second;

                // Find the boundary samples closest to the junction
                // Left boundary: between laneId and the lane closer to center
                // For laneId > 0: left boundary = boundary(laneId-1, laneId)
                // For laneId < 0: left boundary = boundary(laneId, laneId+1) (toward center)
                int innerId = (laneId > 0) ? laneId - 1 : (laneId < 0) ? laneId + 1 : 0;
                int outerId = laneId;

                // For negative lanes, the boundary is (laneId, laneId+1) where laneId+1 is closer to 0
                if (laneId < 0) {
                    innerId = laneId + 1;  // closer to center
                    outerId = laneId;
                }

                // Find left boundary (inner side, toward center)
                const LaneBoundary* leftBdy = nullptr;
                double minDistLeft = 1e18;
                for (const auto& b : net.boundaries) {
                    if (b.samples.empty()) continue;
                    // Check if this boundary is between innerId and outerId
                    bool matches = (b.innerLaneId == innerId && b.outerLaneId == outerId) ||
                                   (b.innerLaneId == outerId && b.outerLaneId == innerId);
                    if (!matches) continue;

                    // Find sample closest to junction
                    for (const auto& s : b.samples) {
                        double d = dist(s.position, junction.position);
                        if (d < minDistLeft) {
                            minDistLeft = d;
                            leftBdy = &b;
                            leftPt = s.position;
                        }
                    }
                }

                // Right boundary: between laneId and the lane farther from center
                int rightInner = laneId;
                int rightOuter = (laneId > 0) ? laneId + 1 : laneId - 1;

                const LaneBoundary* rightBdy = nullptr;
                double minDistRight = 1e18;
                for (const auto& b : net.boundaries) {
                    if (b.samples.empty()) continue;
                    bool matches = (b.innerLaneId == rightInner && b.outerLaneId == rightOuter) ||
                                   (b.innerLaneId == rightOuter && b.outerLaneId == rightInner) ||
                                   (b.isRoadEdge && (b.innerLaneId == laneId || b.outerLaneId == laneId));
                    if (!matches) continue;

                    for (const auto& s : b.samples) {
                        double d = dist(s.position, junction.position);
                        if (d < minDistRight) {
                            minDistRight = d;
                            rightBdy = &b;
                            rightPt = s.position;
                        }
                    }
                }

                return (leftBdy != nullptr && rightBdy != nullptr);
            };

            hasBoundaries = getLaneBoundaryPoints(fromNode->roadId, fromNode->laneId,
                                                  inLeftPt, inRightPt) &&
                            getLaneBoundaryPoints(toNode->roadId, toNode->laneId,
                                                  outLeftPt, outRightPt);

            if (hasBoundaries) {
                // Build boundaries by interpolating from incoming to outgoing boundary points
                // Use the link path's tangent at start/end for bezier control points
                int nSamples = static_cast<int>(link.path.size());
                int nBound = std::max(4, std::min(nSamples, 16));

                cor.leftBoundary = interpolateBoundary(inLeftPt, outLeftPt,
                                                       link.path.front(), link.path.back(),
                                                       fromNode->heading, toNode->heading,
                                                       nBound);
                cor.rightBoundary = interpolateBoundary(inRightPt, outRightPt,
                                                        link.path.front(), link.path.back(),
                                                        fromNode->heading, toNode->heading,
                                                        nBound);

                // Compute actual width from boundary points
                double wIn = dist(inLeftPt, inRightPt);
                double wOut = dist(outLeftPt, outRightPt);
                cor.width = (wIn + wOut) / 2.0;

                printf("[JunctionMesh]   %s: road=%s lane=%d → road=%s lane=%d, %s, width=%.2f (actual), path=%zu\n",
                       link.id.c_str(),
                       cor.fromRoadId.c_str(), fromNode->laneId,
                       cor.toRoadId.c_str(), toNode->laneId,
                       link.maneuverName().c_str(), cor.width,
                       link.path.size());
            } else {
                // Fallback: offset center path by width/2
                double halfW = cor.width / 2.0;
                cor.leftBoundary = offsetPath(link.path, -halfW);
                cor.rightBoundary = offsetPath(link.path, +halfW);

                printf("[JunctionMesh]   %s: road=%s lane=%d → road=%s lane=%d, %s, width=%.2f (fallback), path=%zu\n",
                       link.id.c_str(),
                       cor.fromRoadId.c_str(), fromNode->laneId,
                       cor.toRoadId.c_str(), toNode->laneId,
                       link.maneuverName().c_str(), cor.width,
                       link.path.size());
            }

            corridors.push_back(std::move(cor));
        }
        return corridors;
    }

    // ─── Interpolate a boundary from incoming to outgoing position ───
    // Creates a smooth curve from the incoming boundary endpoint to the
    // outgoing boundary endpoint, following the general direction of the
    // lane connection path.
    static std::vector<Point2D> interpolateBoundary(
        const Point2D& fromPt, const Point2D& toPt,
        const Point2D& pathStart, const Point2D& pathEnd,
        double fromHeading, double toHeading,
        int nSamples)
    {
        // Use the path endpoints as control points for the bezier curve
        // The boundary follows the same general curve as the lane path
        // but starts/ends at the actual boundary positions
        double d = dist(fromPt, toPt);
        double cpDist = std::max(d * 0.3, 2.0);

        // Tangent at start: direction from fromPt toward pathStart (toward junction)
        Vec2 dirFrom{ pathStart.x - fromPt.x, pathStart.y - fromPt.y };
        normalize(dirFrom);
        // Tangent at end: direction from pathEnd toward toPt (away from junction)
        Vec2 dirTo{ toPt.x - pathEnd.x, toPt.y - pathEnd.y };
        normalize(dirTo);

        // Cubic bezier control points
        Point2D cp1{ fromPt.x + dirFrom.x * cpDist, fromPt.y + dirFrom.y * cpDist };
        Point2D cp2{ toPt.x - dirTo.x * cpDist, toPt.y - dirTo.y * cpDist };

        std::vector<Point2D> controlPoints = { fromPt, cp1, cp2, toPt };
        return sampleCubicBezier(controlPoints, nSamples);
    }

    // ─── Group corridors by (fromRoad, toRoad) pair ───
    static std::map<std::pair<std::string, std::string>, std::vector<LaneCorridor>>
    groupCorridors(const std::vector<LaneCorridor>& corridors) {
        std::map<std::pair<std::string, std::string>, std::vector<LaneCorridor>> groups;
        for (const auto& cor : corridors) {
            auto key = std::make_pair(cor.fromRoadId, cor.toRoadId);
            groups[key].push_back(cor);
        }
        return groups;
    }

    // ─── Sort corridors by lateral position (perpendicular to travel) ───
    static std::vector<LaneCorridor> sortCorridorsByLateral(
        const std::vector<LaneCorridor>& group)
    {
        if (group.size() <= 1) return group;

        // Compute average travel direction from the first corridor
        Point2D avgDir{0, 0};
        const auto& ref = group[0];
        if (ref.leftBoundary.size() >= 2) {
            avgDir.x = ref.leftBoundary[1].x - ref.leftBoundary[0].x;
            avgDir.y = ref.leftBoundary[1].y - ref.leftBoundary[0].y;
            double len = std::sqrt(avgDir.x * avgDir.x + avgDir.y * avgDir.y);
            if (len > 1e-10) { avgDir.x /= len; avgDir.y /= len; }
        }

        // Perpendicular direction (left normal)
        Point2D perp{-avgDir.y, avgDir.x};

        auto sorted = group;
        std::sort(sorted.begin(), sorted.end(),
            [&](const LaneCorridor& a, const LaneCorridor& b) {
                double projA = 0, projB = 0;
                if (!a.leftBoundary.empty())
                    projA = a.leftBoundary[0].x * perp.x + a.leftBoundary[0].y * perp.y;
                if (!b.leftBoundary.empty())
                    projB = b.leftBoundary[0].x * perp.x + b.leftBoundary[0].y * perp.y;
                return projA < projB;
            });
        return sorted;
    }

    // ─── Convex hull (Andrew's monotone chain) ───
    // Returns hull points in CCW order. A convex hull is always a simple
    // (non-self-intersecting) polygon, which makes it safe to triangulate
    // directly — unlike the raw union of overlapping lane corridor strips.
    static std::vector<Point2D> convexHull(std::vector<Point2D> pts) {
        std::sort(pts.begin(), pts.end(), [](const Point2D& a, const Point2D& b) {
            return (a.x != b.x) ? (a.x < b.x) : (a.y < b.y);
        });
        pts.erase(std::unique(pts.begin(), pts.end(), [](const Point2D& a, const Point2D& b) {
            return a.distanceTo(b) < 1e-6;
        }), pts.end());

        int n = static_cast<int>(pts.size());
        if (n < 3) return pts;

        auto cross = [](const Point2D& o, const Point2D& a, const Point2D& b) {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        };

        std::vector<Point2D> hull(2 * n);
        int k = 0;
        for (int i = 0; i < n; i++) {
            while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) k--;
            hull[k++] = pts[i];
        }
        int lower = k + 1;
        for (int i = n - 2; i >= 0; i--) {
            while (k >= lower && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) k--;
            hull[k++] = pts[i];
        }
        hull.resize(k - 1);
        return hull;
    }

    // ─── Round every corner of a simple polygon into a circular fillet ───
    // For each vertex, replaces the sharp corner with a small tangent arc
    // (clamped so it never overshoots either adjacent edge), matching the
    // rounded-corner look of real-world/esmini-style junction pavement.
    // Works on the already-correct convex hull shape directly, so it does
    // not depend on independently re-deriving each road's approach
    // direction (which proved unreliable — see junction_builder history).
    static std::vector<Point2D> roundPolygonCorners(
        const std::vector<Point2D>& poly, double radius, int segments = 8)
    {
        int n = static_cast<int>(poly.size());
        if (n < 3 || radius <= 0.0) return poly;

        std::vector<Point2D> result;
        for (int i = 0; i < n; i++) {
            const Point2D& prev = poly[(i - 1 + n) % n];
            const Point2D& cur = poly[i];
            const Point2D& next = poly[(i + 1) % n];

            Vec2 toPrev = prev - cur;
            Vec2 toNext = next - cur;
            double lenPrev = toPrev.norm();
            double lenNext = toNext.norm();
            if (lenPrev < 1e-6 || lenNext < 1e-6) { result.push_back(cur); continue; }

            Vec2 dirPrev = toPrev.normalized();
            Vec2 dirNext = toNext.normalized();

            double cosPhi = std::clamp(dirPrev.dot(dirNext), -1.0, 1.0);
            double phi = std::acos(cosPhi);
            double halfPhi = phi / 2.0;
            double tanHalf = std::tan(halfPhi);
            if (tanHalf < 1e-4) { result.push_back(cur); continue; }  // nearly straight

            double desiredTanLen = radius / tanHalf;
            double maxTanLen = std::min(lenPrev, lenNext) * 0.45;
            double tanLen = std::min(desiredTanLen, maxTanLen);
            if (tanLen < 1e-3) { result.push_back(cur); continue; }
            double rEff = tanLen * tanHalf;

            Point2D pA = cur + dirPrev * tanLen;
            Point2D pB = cur + dirNext * tanLen;

            Vec2 bisSum = dirPrev + dirNext;
            double bisLen = bisSum.norm();
            Vec2 bisDir = (bisLen > 1e-6) ? (bisSum * (1.0 / bisLen)) : dirPrev.perp();
            double d = tanLen / std::cos(halfPhi);
            Point2D arcCenter = cur + bisDir * d;

            Vec2 vA = pA - arcCenter;
            Vec2 vB = pB - arcCenter;
            double angleA = std::atan2(vA.y, vA.x);
            double angleB = std::atan2(vB.y, vB.x);
            double sweep = angleB - angleA;
            while (sweep > PI) sweep -= 2 * PI;
            while (sweep < -PI) sweep += 2 * PI;

            result.push_back(pA);
            for (int s = 1; s < segments; s++) {
                double t = static_cast<double>(s) / segments;
                double a = angleA + sweep * t;
                result.push_back({ arcCenter.x + rEff * std::cos(a),
                                    arcCenter.y + rEff * std::sin(a) });
            }
            result.push_back(pB);
        }
        return result;
    }

    // ─── ApproachEdge — one connected road's edge data at the junction ───
    struct ApproachEdge {
        Point2D trimPoint;      // road-end centerline point next to the junction
        Vec2 tangentToCenter;   // unit direction from trimPoint toward junction center
        Vec2 normal;            // left normal (90° CCW from tangentToCenter)
        double halfWidth;
        std::string roadId;
    };

    // ─── Build solid edge stripes connecting road edges through the junction ───
    // For each pair of adjacent approaches (sorted by angle), connect the
    // LEFT edge of the current approach to the RIGHT edge of the next approach
    // with a bezier curve. These are the solid white lines that delimit the
    // asphalt area of the junction.
    static std::vector<JunctionLaneStripe> buildEdgeStripes(
        const std::vector<ApproachEdge>& approaches,
        const std::vector<Point2D>& polygon,
        const Point2D& center)
    {
        std::vector<JunctionLaneStripe> stripes;
        if (approaches.size() < 2) return stripes;

        // Sort approaches by angle around center (same as buildFilletPolygon)
        struct SortedApproach {
            Point2D leftAtTrim, rightAtTrim;
            double angle;
        };
        std::vector<SortedApproach> sorted;
        for (const auto& a : approaches) {
            Point2D left{ a.trimPoint.x + a.normal.x * a.halfWidth,
                          a.trimPoint.y + a.normal.y * a.halfWidth };
            Point2D right{ a.trimPoint.x - a.normal.x * a.halfWidth,
                           a.trimPoint.y - a.normal.y * a.halfWidth };
            double angle = std::atan2(a.tangentToCenter.y, a.tangentToCenter.x);
            sorted.push_back({ left, right, angle });
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const SortedApproach& a, const SortedApproach& b) { return a.angle < b.angle; });

        int n = static_cast<int>(sorted.size());
        for (int i = 0; i < n; i++) {
            const auto& cur = sorted[i];
            const auto& nxt = sorted[(i + 1) % n];

            // Solid stripe: cur.left → nxt.right (the outer boundary)
            JunctionLaneStripe stripe;
            stripe.fromPoint = cur.leftAtTrim;
            stripe.toPoint = nxt.rightAtTrim;
            stripe.type = JunctionLaneStripe::Type::Solid;
            stripe.color = "white";

            // Bezier control points: tangent outward from each edge
            Vec2 outDir = (cur.leftAtTrim - center).normalized();
            Vec2 inDir = (nxt.rightAtTrim - center).normalized();
            double dist = cur.leftAtTrim.distanceTo(nxt.rightAtTrim);
            double handleLen = dist * 0.3;

            Point2D cp1 = { cur.leftAtTrim.x + outDir.x * handleLen,
                            cur.leftAtTrim.y + outDir.y * handleLen };
            Point2D cp2 = { nxt.rightAtTrim.x + inDir.x * handleLen,
                            nxt.rightAtTrim.y + inDir.y * handleLen };

            stripe.controlPoints = { cur.leftAtTrim, cp1, cp2, nxt.rightAtTrim };

            // Sample the bezier curve
            int numSamples = 16;
            for (int s = 0; s <= numSamples; s++) {
                double t = static_cast<double>(s) / numSamples;
                double u = 1.0 - t;
                double x = u*u*u * cur.leftAtTrim.x +
                           3*u*u*t * cp1.x +
                           3*u*t*t * cp2.x +
                           t*t*t * nxt.rightAtTrim.x;
                double y = u*u*u * cur.leftAtTrim.y +
                           3*u*u*t * cp1.y +
                           3*u*t*t * cp2.y +
                           t*t*t * nxt.rightAtTrim.y;
                stripe.samples.push_back({ x, y });
            }

            stripes.push_back(stripe);
        }

        return stripes;
    }

    // ─── Build a thin quad strip mesh from lane stripes ───
    // Each stripe becomes a thin quad strip (like a road marking).
    static JunctionMeshData buildStripeMesh(const std::vector<JunctionLaneStripe>& stripes) {
        JunctionMeshData mesh;
        if (stripes.empty()) return mesh;

        double stripeWidth = 0.15;  // 15cm wide markings
        uint32_t vertOffset = 0;

        for (const auto& stripe : stripes) {
            if (stripe.samples.size() < 2) continue;

            for (size_t i = 0; i + 1 < stripe.samples.size(); i++) {
                const auto& p1 = stripe.samples[i];
                const auto& p2 = stripe.samples[i + 1];

                Vec2 dir = { p2.x - p1.x, p2.y - p1.y };
                double len = dir.norm();
                if (len < 1e-6) continue;
                Vec2 perp = { -dir.y / len, dir.x / len };
                double hw = stripeWidth / 2.0;

                // Four corners of the quad
                Point2D v0 = { p1.x + perp.x * hw, p1.y + perp.y * hw };
                Point2D v1 = { p1.x - perp.x * hw, p1.y - perp.y * hw };
                Point2D v2 = { p2.x - perp.x * hw, p2.y - perp.y * hw };
                Point2D v3 = { p2.x + perp.x * hw, p2.y + perp.y * hw };

                // Add vertices (z=0.03, slightly above asphalt)
                for (const auto& v : { v0, v1, v2, v3 }) {
                    mesh.positions.push_back(static_cast<float>(v.x));
                    mesh.positions.push_back(0.03f);
                    mesh.positions.push_back(static_cast<float>(v.y));
                    mesh.normals.push_back(0);
                    mesh.normals.push_back(1);
                    mesh.normals.push_back(0);
                    mesh.uvs.push_back(0);
                    mesh.uvs.push_back(0);
                }

                // Two triangles: (0,1,2) and (0,2,3)
                mesh.indices.push_back(vertOffset + 0);
                mesh.indices.push_back(vertOffset + 1);
                mesh.indices.push_back(vertOffset + 2);
                mesh.indices.push_back(vertOffset + 0);
                mesh.indices.push_back(vertOffset + 2);
                mesh.indices.push_back(vertOffset + 3);

                vertOffset += 4;
                mesh.vertexCount += 4;
                mesh.triangleCount += 2;
            }
        }

        return mesh;
    }

    // ─── Build one approach edge per road connected to the junction ───
    // The road-end position is the average of that road's lane-node
    // positions (they all sit at the trimmed road end next to the
    // junction), and the direction toward the junction is computed
    // positionally as normalize(center - endCenter). This is far more
    // robust than averaging lane headings, which previously produced
    // garbage directions and a degenerate boundary shape.
    static std::vector<ApproachEdge> buildApproachEdges(
        const RoadNode& junction,
        const RoadGraph& graph,
        const LaneGraph& laneGraph)
    {
        std::vector<ApproachEdge> approaches;

        for (const auto& roadId : junction.connectedRoadIds) {
            double width = 7.0;
            for (const auto& e : graph.edges) {
                if (e.roadId == roadId &&
                    (e.fromNodeId == junction.id || e.toNodeId == junction.id)) {
                    width = e.width;
                    break;
                }
            }
            double halfWidth = width / 2.0;

            // Average ALL lane-node positions for this road (incoming and
            // outgoing lanes all terminate at the same trimmed road end).
            double sumX = 0.0, sumY = 0.0;
            int count = 0;
            for (const auto& n : laneGraph.nodes) {
                if (n.roadId != roadId) continue;
                sumX += n.position.x;
                sumY += n.position.y;
                count++;
            }
            if (count == 0) continue;

            Point2D endCenter{ sumX / count, sumY / count };
            Vec2 toCenter = junction.position - endCenter;
            if (toCenter.norm() < 1e-3) continue;
            Vec2 tangentToCenter = toCenter.normalized();

            approaches.push_back({ endCenter, tangentToCenter, tangentToCenter.perp(),
                                    halfWidth, roadId });
        }

        return approaches;
    }

    // ─── Fallback: Build approach edges from road graph edges ───
    // Used when the lane graph is empty (auto-detected junctions without
    // lane networks). Estimates each road's end position by finding the
    // closest point on the road's centerline to the junction center,
    // then computing the tangent direction at that point.
    static std::vector<ApproachEdge> buildApproachEdgesFromRoadGraph(
        const RoadNode& junction,
        const RoadGraph& graph)
    {
        std::vector<ApproachEdge> approaches;

        for (const auto& roadId : junction.connectedRoadIds) {
            // Find the road edge connected to this junction
            const RoadEdge* edge = nullptr;
            for (const auto& e : graph.edges) {
                if (e.roadId == roadId &&
                    (e.fromNodeId == junction.id || e.toNodeId == junction.id)) {
                    edge = &e;
                    break;
                }
            }
            if (!edge) continue;

            double halfWidth = edge->width / 2.0;

            // The road end position is the node at the other end of the edge
            // (the non-junction end of this road segment), but we actually
            // want the position where the road meets the junction.
            // Since we don't have the road's sampled centerline here, we
            // approximate: the road end is at distance edge->length from
            // the other node, heading toward the junction.
            std::string otherNodeId = (edge->fromNodeId == junction.id)
                ? edge->toNodeId : edge->fromNodeId;
            const RoadNode* otherNode = nullptr;
            for (const auto& n : graph.nodes) {
                if (n.id == otherNodeId) { otherNode = &n; break; }
            }
            if (!otherNode) continue;

            // Direction from other node toward junction
            Vec2 toJunction = junction.position - otherNode->position;
            double dist = toJunction.norm();
            if (dist < 1e-3) continue;
            Vec2 tangentToCenter = toJunction.normalized();

            // Estimate the road end position: move from the junction center
            // back along the road by half the road width (trim distance).
            // This places the approach edge at the junction boundary.
            double trimDist = halfWidth + 3.0;  // small trim
            Point2D endCenter = {
                junction.position.x - tangentToCenter.x * trimDist,
                junction.position.y - tangentToCenter.y * trimDist
            };

            approaches.push_back({ endCenter, tangentToCenter, tangentToCenter.perp(),
                                    halfWidth, roadId });
        }

        return approaches;
    }

    // ─── Build junction boundary polygon with rounded fillet corners ───
    // Same algorithm as the legacy generateEdgeBasedPolygon() in
    // intersection.hpp: for each pair of adjacent approaches (sorted CCW
    // by angle around the center), the outer corner is formed by the
    // intersection of the LEFT edge line of the current approach with the
    // RIGHT edge line of the next, rounded with a circular fillet arc.
    // Because each approach's edge lines are anchored at the actual
    // trimmed road end, the boundary continues each road's edges exactly
    // — no notches at the road entries (unlike a convex hull).
    static std::vector<Point2D> buildFilletPolygon(
        const std::vector<ApproachEdge>& approaches,
        const Point2D& center)
    {
        if (approaches.size() < 2) return {};

        struct Edge {
            Point2D leftAtTrim, rightAtTrim;
            Vec2 tangent;   // toward center
            double angle;
            double halfWidth;
        };
        std::vector<Edge> edges;
        for (const auto& a : approaches) {
            Point2D leftAtTrim{ a.trimPoint.x + a.normal.x * a.halfWidth,
                                 a.trimPoint.y + a.normal.y * a.halfWidth };
            Point2D rightAtTrim{ a.trimPoint.x - a.normal.x * a.halfWidth,
                                  a.trimPoint.y - a.normal.y * a.halfWidth };
            // Sort key: direction toward the junction center (cyclically
            // equivalent to the outward direction, unlike atan2 of the
            // trim-point position which breaks for collinear opposite arms).
            double angle = std::atan2(a.tangentToCenter.y, a.tangentToCenter.x);
            edges.push_back({ leftAtTrim, rightAtTrim, a.tangentToCenter, angle, a.halfWidth });
        }

        std::sort(edges.begin(), edges.end(),
                  [](const Edge& x, const Edge& y) { return x.angle < y.angle; });

        double minHalfWidth = edges[0].halfWidth;
        for (const auto& e : edges) minHalfWidth = std::min(minHalfWidth, e.halfWidth);
        double cornerRadius = std::clamp(minHalfWidth, 3.0, 10.0);

        int n = static_cast<int>(edges.size());
        int filletSegments = 12;

        struct Fillet {
            Point2D corner, tangentIn, tangentOut;
            double radius = 0;
            std::vector<Point2D> arcPoints;
            bool valid = false;
        };
        std::vector<Fillet> fillets(n);

        for (int i = 0; i < n; i++) {
            const auto& cur = edges[i];
            const auto& nxt = edges[(i + 1) % n];

            Point2D corner = lineIntersection(cur.leftAtTrim, cur.tangent,
                                               nxt.rightAtTrim, nxt.tangent);
            if (!isValid(corner)) continue;

            Vec2 dirToCurTrim = (cur.leftAtTrim - corner).normalized();
            Vec2 dirToNxtTrim = (nxt.rightAtTrim - corner).normalized();
            Point2D tangentIn = corner + dirToCurTrim * cornerRadius;
            Point2D tangentOut = corner + dirToNxtTrim * cornerRadius;

            Vec2 perpCur = dirToCurTrim.perp();
            Vec2 perpNxt = dirToNxtTrim.perp();
            Vec2 dirToCenter = (center - corner).normalized();
            Vec2 normCur = perpCur.dot(dirToCenter) > 0 ? perpCur : perpCur * -1;
            Vec2 normNxt = perpNxt.dot(dirToCenter) > 0 ? perpNxt : perpNxt * -1;

            Point2D arcCenter = lineIntersection(tangentIn, normCur, tangentOut, normNxt);
            if (!isValid(arcCenter)) {
                arcCenter = { (tangentIn.x + tangentOut.x) / 2, (tangentIn.y + tangentOut.y) / 2 };
            }

            Vec2 vIn = tangentIn - arcCenter;
            Vec2 vOut = tangentOut - arcCenter;
            double angleIn = std::atan2(vIn.y, vIn.x);
            double angleOut = std::atan2(vOut.y, vOut.x);
            double sweep = angleOut - angleIn;
            while (sweep > PI) sweep -= 2 * PI;
            while (sweep < -PI) sweep += 2 * PI;

            double actualRadius = tangentIn.distanceTo(arcCenter);

            std::vector<Point2D> arcPts;
            for (int s = 0; s <= filletSegments; s++) {
                double t = static_cast<double>(s) / filletSegments;
                double a = angleIn + sweep * t;
                arcPts.push_back({ arcCenter.x + actualRadius * std::cos(a),
                                    arcCenter.y + actualRadius * std::sin(a) });
            }

            fillets[i] = Fillet{ corner, tangentIn, tangentOut, actualRadius, arcPts, true };
        }

        std::vector<Point2D> polygon;
        for (int i = 0; i < n; i++) {
            const auto& cur = edges[i];
            const auto& nxt = edges[(i + 1) % n];
            const auto& f = fillets[i];

            polygon.push_back(cur.leftAtTrim);
            if (f.valid && f.radius > 0.1) {
                polygon.push_back(f.tangentIn);
                for (size_t j = 1; j + 1 < f.arcPoints.size(); j++) polygon.push_back(f.arcPoints[j]);
                polygon.push_back(f.tangentOut);
            } else if (f.valid) {
                polygon.push_back(f.corner);
            }
            polygon.push_back(nxt.rightAtTrim);
        }

        std::vector<Point2D> cleaned;
        for (const auto& p : polygon) {
            if (cleaned.empty() || p.distanceTo(cleaned.back()) > EPSILON) cleaned.push_back(p);
        }
        if (cleaned.size() > 1 && cleaned.front().distanceTo(cleaned.back()) < EPSILON) {
            cleaned.pop_back();
        }
        return cleaned;
    }

    // ─── Ear-clipping polygon triangulation ───
    // Robust triangulation for simple polygons. Works reliably for
    // junction boundary polygons that may have near-collinear vertices.
    static JunctionMeshData triangulatePolygonEarClip(const std::vector<Point2D>& polygon) {
        JunctionMeshData mesh;
        int n = static_cast<int>(polygon.size());
        if (n < 3) return mesh;

        // Build vertex data for all polygon points
        for (const auto& p : polygon) {
            mesh.positions.push_back(static_cast<float>(p.x));
            mesh.positions.push_back(0.02f);  // slight Y offset for rendering
            mesh.positions.push_back(static_cast<float>(p.y));
            mesh.normals.push_back(0.0f);
            mesh.normals.push_back(1.0f);  // up-facing normal
            mesh.normals.push_back(0.0f);
            mesh.uvs.push_back(static_cast<float>(p.x / 10.0));
            mesh.uvs.push_back(static_cast<float>(p.y / 10.0));
        }
        mesh.vertexCount = n;

        // Determine polygon winding (CCW = positive area)
        double area = 0.0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            area += polygon[i].x * polygon[j].y;
            area -= polygon[j].x * polygon[i].y;
        }
        bool isCCW = (area > 0.0);

        // Create index list for ear-clipping
        std::vector<int> indices(n);
        if (isCCW) {
            for (int i = 0; i < n; i++) indices[i] = i;
        } else {
            for (int i = 0; i < n; i++) indices[i] = n - 1 - i;
        }

        // Ear-clipping loop
        int remaining = n;
        int maxIter = remaining * remaining;  // safety limit
        int iter = 0;
        int i = 0;

        while (remaining > 2 && iter < maxIter) {
            iter++;
            int prev = indices[((i - 1) % remaining + remaining) % remaining];
            int curr = indices[i % remaining];
            int next = indices[(i + 1) % remaining];

            const Point2D& pPrev = polygon[prev];
            const Point2D& pCurr = polygon[curr];
            const Point2D& pNext = polygon[next];

            // Check if this is a convex vertex (ear candidate)
            double cross = (pCurr.x - pPrev.x) * (pNext.y - pPrev.y) -
                           (pCurr.y - pPrev.y) * (pNext.x - pPrev.x);

            if (cross > 1e-10) {
                // Check no other vertex is inside this triangle
                bool isEar = true;
                for (int k = 0; k < remaining; k++) {
                    int idx = indices[k];
                    if (idx == prev || idx == curr || idx == next) continue;
                    const Point2D& p = polygon[idx];

                    // Point-in-triangle test
                    double d1 = (p.x - pPrev.x) * (pCurr.y - pPrev.y) - (pCurr.x - pPrev.x) * (p.y - pPrev.y);
                    double d2 = (p.x - pCurr.x) * (pNext.y - pCurr.y) - (pNext.x - pCurr.x) * (p.y - pCurr.y);
                    double d3 = (p.x - pNext.x) * (pPrev.y - pNext.y) - (pPrev.x - pNext.x) * (p.y - pNext.y);

                    bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
                    bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
                    if (!(hasNeg && hasPos)) {
                        isEar = false;
                        break;
                    }
                }

                if (isEar) {
                    // Emit triangle
                    mesh.indices.push_back(static_cast<uint32_t>(prev));
                    mesh.indices.push_back(static_cast<uint32_t>(curr));
                    mesh.indices.push_back(static_cast<uint32_t>(next));

                    // Remove the ear vertex
                    indices.erase(indices.begin() + (i % remaining));
                    remaining--;
                    if (remaining > 0) i = i % remaining;
                    continue;
                }
            }

            i = (i + 1) % remaining;
        }

        mesh.triangleCount = static_cast<int>(mesh.indices.size()) / 3;
        return mesh;
    }

    // ─── Build a flat mesh from a completed constrained triangulation ───
    static JunctionMeshData buildMeshFromTriangulation(const ConstrainedTriangulation& ct) {
        JunctionMeshData mesh;
        const auto& verts = ct.getVertices();
        if (verts.empty() || ct.triangleCount() == 0) return mesh;

        for (const auto& p : verts) {
            mesh.positions.push_back(static_cast<float>(p.x));
            mesh.positions.push_back(0.02f);
            mesh.positions.push_back(static_cast<float>(p.y));
            mesh.normals.push_back(0);
            mesh.normals.push_back(0);
            mesh.normals.push_back(1);
            mesh.uvs.push_back(static_cast<float>(p.x / 10.0));
            mesh.uvs.push_back(static_cast<float>(p.y / 10.0));
        }
        mesh.indices = ct.getIndices();
        mesh.vertexCount = static_cast<int>(verts.size());
        mesh.triangleCount = ct.triangleCount();
        return mesh;
    }

    // ─── Build culling polygon from corridor boundary points ───
    // Collects all boundary endpoints and sorts by angle around junction center.
    // This is used only for culling/visibility, not for mesh generation.
    static std::vector<Point2D> buildCullingPolygonFromBoundaries(
        const std::vector<LaneCorridor>& corridors,
        const Point2D& center)
    {
        std::vector<std::pair<Point2D, double>> anglePoints;
        for (const auto& cor : corridors) {
            for (const auto& p : cor.leftBoundary) {
                double angle = std::atan2(p.y - center.y, p.x - center.x);
                anglePoints.emplace_back(p, angle);
            }
            for (const auto& p : cor.rightBoundary) {
                double angle = std::atan2(p.y - center.y, p.x - center.x);
                anglePoints.emplace_back(p, angle);
            }
        }
        if (anglePoints.size() < 3) return {};

        // Sort by angle and deduplicate
        std::sort(anglePoints.begin(), anglePoints.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        std::vector<Point2D> result;
        for (const auto& [pt, _] : anglePoints) {
            if (result.empty() || dist(result.back(), pt) > 0.5)
                result.push_back(pt);
        }
        // Remove last if same as first
        if (result.size() > 1 && dist(result.front(), result.back()) < 0.5)
            result.pop_back();
        return result;
    }

    // ─── Mesh validation ───
    struct MeshValidation {
        bool valid = true;
        int degenerateTriangles = 0;
        int nanVertices = 0;
        int outOfBoundsIndices = 0;
    };

    static MeshValidation validateMesh(const JunctionMeshData& mesh) {
        MeshValidation v;
        int nVerts = mesh.vertexCount;
        for (int i = 0; i < nVerts; i++) {
            double x = mesh.positions[i * 3];
            double y = mesh.positions[i * 3 + 1];
            double z = mesh.positions[i * 3 + 2];
            if (std::isnan(x) || std::isnan(y) || std::isnan(z) ||
                std::isinf(x) || std::isinf(y) || std::isinf(z)) {
                v.nanVertices++;
                v.valid = false;
            }
        }
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            uint32_t i0 = mesh.indices[i];
            uint32_t i1 = mesh.indices[i + 1];
            uint32_t i2 = mesh.indices[i + 2];
            if (i0 >= static_cast<uint32_t>(nVerts) ||
                i1 >= static_cast<uint32_t>(nVerts) ||
                i2 >= static_cast<uint32_t>(nVerts)) {
                v.outOfBoundsIndices++;
                v.valid = false;
                continue;
            }
            double ax = mesh.positions[i0 * 3], ay = mesh.positions[i0 * 3 + 2];
            double bx = mesh.positions[i1 * 3], by = mesh.positions[i1 * 3 + 2];
            double cx = mesh.positions[i2 * 3], cy = mesh.positions[i2 * 3 + 2];
            double cross = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
            if (std::abs(cross) < 1e-8) {
                v.degenerateTriangles++;
                v.valid = false;
            }
        }
        return v;
    }

private:
    // ─── Append one mesh into another (with index offset) ───
    static void appendMesh(JunctionMeshData& dest, const JunctionMeshData& src) {
        if (src.empty()) return;
        uint32_t offset = static_cast<uint32_t>(dest.positions.size() / 3);
        dest.positions.insert(dest.positions.end(), src.positions.begin(), src.positions.end());
        dest.normals.insert(dest.normals.end(), src.normals.begin(), src.normals.end());
        dest.uvs.insert(dest.uvs.end(), src.uvs.begin(), src.uvs.end());
        for (uint32_t idx : src.indices)
            dest.indices.push_back(idx + offset);
        dest.vertexCount = static_cast<int>(dest.positions.size() / 3);
        dest.triangleCount = static_cast<int>(dest.indices.size() / 3);
    }

private:
    // ─── Build lane stripes from corridor boundaries ───
    // For each group of corridors (sorted by lateral position):
    //   - Left boundary of leftmost corridor → Solid (road edge)
    //   - Right boundary of rightmost corridor → Solid (road edge)
    //   - All intermediate boundaries → Dashed (lane dividers)
    //
    // This produces N+1 stripes for N corridors (correct: N lanes have
    // N+1 boundaries).
    //
    static std::vector<JunctionLaneStripe> buildCorridorStripes(
        const std::vector<LaneCorridor>& corridors)
    {
        std::vector<JunctionLaneStripe> stripes;

        auto groups = groupCorridors(corridors);

        for (const auto& [key, group] : groups) {
            auto sorted = sortCorridorsByLateral(group);

            for (size_t i = 0; i < sorted.size(); i++) {
                const auto& cor = sorted[i];

                // Left boundary of this corridor
                if (cor.leftBoundary.size() >= 2) {
                    JunctionLaneStripe stripe;
                    stripe.fromPoint = cor.leftBoundary.front();
                    stripe.toPoint = cor.leftBoundary.back();
                    stripe.samples = cor.leftBoundary;
                    // Leftmost corridor's left boundary = road edge (solid)
                    // All others = lane divider (dashed)
                    stripe.type = (i == 0)
                        ? JunctionLaneStripe::Type::Solid
                        : JunctionLaneStripe::Type::Dashed;
                    stripe.color = "white";
                    stripes.push_back(std::move(stripe));
                }

                // Right boundary — only add for the rightmost corridor
                // (intermediate right boundaries are covered by the next
                //  corridor's left boundary as a dashed stripe)
                if (i == sorted.size() - 1 && cor.rightBoundary.size() >= 2) {
                    JunctionLaneStripe stripe;
                    stripe.fromPoint = cor.rightBoundary.front();
                    stripe.toPoint = cor.rightBoundary.back();
                    stripe.samples = cor.rightBoundary;
                    stripe.type = JunctionLaneStripe::Type::Solid;  // road edge
                    stripe.color = "white";
                    stripes.push_back(std::move(stripe));
                }
            }
        }

        return stripes;
    }

    // ─── Build marking mesh from stripes ───
    static JunctionMeshData buildMarkingMesh(
        const std::vector<JunctionLaneStripe>& stripes)
    {
        JunctionMeshData mesh;
        uint32_t vertexOffset = 0;

        for (const auto& stripe : stripes) {
            if (stripe.type == JunctionLaneStripe::Type::None) continue;
            if (stripe.samples.size() < 2) continue;

            const double markingWidth = 0.15;
            const double yOffset = 0.05;

            for (size_t i = 0; i < stripe.samples.size(); i++) {
                const auto& p = stripe.samples[i];

                // Compute tangent
                double tx, ty;
                if (i == 0) {
                    tx = stripe.samples[1].x - p.x;
                    ty = stripe.samples[1].y - p.y;
                } else if (i == stripe.samples.size() - 1) {
                    tx = p.x - stripe.samples[i-1].x;
                    ty = p.y - stripe.samples[i-1].y;
                } else {
                    tx = stripe.samples[i+1].x - stripe.samples[i-1].x;
                    ty = stripe.samples[i+1].y - stripe.samples[i-1].y;
                }
                double tlen = std::sqrt(tx * tx + ty * ty) || 1;
                double nx = -ty / tlen, ny = tx / tlen;
                double hw = markingWidth / 2;

                // Left vertex
                mesh.positions.push_back(p.x + nx * hw);
                mesh.positions.push_back(yOffset);
                mesh.positions.push_back(p.y + ny * hw);
                mesh.normals.push_back(0); mesh.normals.push_back(0); mesh.normals.push_back(1);
                mesh.uvs.push_back(static_cast<double>(i) / stripe.samples.size());
                mesh.uvs.push_back(0);

                // Right vertex
                mesh.positions.push_back(p.x - nx * hw);
                mesh.positions.push_back(yOffset);
                mesh.positions.push_back(p.y - ny * hw);
                mesh.normals.push_back(0); mesh.normals.push_back(0); mesh.normals.push_back(1);
                mesh.uvs.push_back(static_cast<double>(i) / stripe.samples.size());
                mesh.uvs.push_back(1);
            }

            // Triangles
            int n = static_cast<int>(stripe.samples.size());
            for (int i = 0; i < n - 1; i++) {
                uint32_t li = vertexOffset + i * 2;
                uint32_t ri = vertexOffset + i * 2 + 1;
                uint32_t li2 = vertexOffset + (i + 1) * 2;
                uint32_t ri2 = vertexOffset + (i + 1) * 2 + 1;

                mesh.indices.push_back(li);
                mesh.indices.push_back(ri);
                mesh.indices.push_back(li2);
                mesh.indices.push_back(ri);
                mesh.indices.push_back(ri2);
                mesh.indices.push_back(li2);
            }

            vertexOffset += n * 2;
        }

        mesh.vertexCount = static_cast<int>(mesh.positions.size() / 3);
        mesh.triangleCount = static_cast<int>(mesh.indices.size() / 3);
        return mesh;
    }

    // ─── Geometry helpers ───

    static double dist(const Point2D& a, const Point2D& b) {
        double dx = a.x - b.x, dy = a.y - b.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    static void normalize(Vec2& v) {
        double len = std::sqrt(v.x * v.x + v.y * v.y);
        if (len > 1e-10) { v.x /= len; v.y /= len; }
    }

    static std::vector<Point2D> sampleCubicBezier(
        const std::vector<Point2D>& cps, int numSamples)
    {
        std::vector<Point2D> result;
        if (cps.size() < 4) return result;
        for (int i = 0; i <= numSamples; i++) {
            double t = static_cast<double>(i) / numSamples;
            double u = 1.0 - t;
            double x = u*u*u*cps[0].x + 3*u*u*t*cps[1].x + 3*u*t*t*cps[2].x + t*t*t*cps[3].x;
            double y = u*u*u*cps[0].y + 3*u*u*t*cps[1].y + 3*u*t*t*cps[2].y + t*t*t*cps[3].y;
            result.push_back({x, y});
        }
        return result;
    }
};

} // namespace geo

// ═══════════════════════════════════════════════════════════
// Tests
// ═══════════════════════════════════════════════════════════

#ifdef DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

TEST_SUITE("JunctionBuilder") {

    TEST_CASE("JunctionLaneStripe defaults") {
        geo::JunctionLaneStripe stripe;
        CHECK(stripe.type == geo::JunctionLaneStripe::Type::None);
        CHECK(stripe.color == "white");
        CHECK(!stripe.isSolid());
        CHECK(!stripe.isDashed());
    }

    TEST_CASE("JunctionMeshData empty") {
        geo::JunctionMeshData mesh;
        CHECK(mesh.empty());
        CHECK(mesh.vertexCount == 0);
        CHECK(mesh.triangleCount == 0);
    }

    TEST_CASE("JunctionResult defaults") {
        geo::JunctionResult result;
        CHECK(result.junctionId == "");
        CHECK(result.numLaneConnections == 0);
        CHECK(!result.isValid()); // empty polygon
    }

    TEST_CASE("JunctionBuilder: empty input") {
        geo::RoadNode junction;
        junction.type = geo::RoadNodeType::Junction;
        geo::RoadGraph graph;
        geo::LaneGraph laneGraph;
        std::map<std::string, geo::LaneNetwork> laneNets;

        auto result = geo::JunctionBuilder::build(junction, graph, laneGraph, laneNets);
        CHECK(result.junctionId == junction.id);
        CHECK(!result.isValid()); // no polygon
    }

    TEST_CASE("JunctionBuilder: non-junction returns empty") {
        geo::RoadNode node;
        node.type = geo::RoadNodeType::EndPoint;
        geo::RoadGraph graph;
        geo::LaneGraph laneGraph;
        std::map<std::string, geo::LaneNetwork> laneNets;

        auto result = geo::JunctionBuilder::build(node, graph, laneGraph, laneNets);
        CHECK(!result.isValid());
    }
}

#endif // DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
