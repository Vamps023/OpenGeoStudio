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

#include "geometry.hpp"
#include "lane_graph.hpp"
#include "lane_network.hpp"
#include "road_graph.hpp"
#include "road_mesh_generator.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
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

    // ─── Build a single junction ───
    //
    // @param junction   The RoadNode (junction)
    // @param graph      The RoadGraph
    // @param laneGraph  The LaneGraph for this junction
    // @param laneNets   Map: roadId → LaneNetwork for each connected road
    // @return           JunctionResult with mesh + stripes
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

        if (!junction.isJunction() || laneGraph.numNodes() == 0) return result;

        // Step 1: Build junction boundary polygon from approach road edges
        result.polygon = buildBoundaryPolygon(junction, graph, laneNets);
        if (result.polygon.size() < 3) return result;

        // Step 2: Generate lane stripes connecting adjacent approaches
        result.laneStripes = buildLaneStripes(junction, laneGraph, laneNets);

        // Step 3: Triangulate the junction interior as asphalt mesh
        result.asphaltMesh = triangulateJunctionInterior(result.polygon, result.laneStripes);

        // Step 4: Generate marking meshes for solid/dashed stripes
        result.markingMesh = buildMarkingMesh(result.laneStripes);

        return result;
    }

private:
    // ─── Build junction boundary polygon ───
    // Collects road edge points from each approach and forms a polygon.
    static std::vector<Point2D> buildBoundaryPolygon(
        const RoadNode& junction,
        const RoadGraph& graph,
        const std::map<std::string, LaneNetwork>& laneNets)
    {
        // Collect boundary points from each road's road edges
        std::vector<std::pair<Point2D, double>> anglePoints; // point + angle from center

        for (const auto& roadId : junction.connectedRoadIds) {
            auto it = laneNets.find(roadId);
            if (it == laneNets.end()) continue;
            const auto& net = it->second;

            // Get road edge boundaries
            auto edges = net.roadEdges();
            for (const auto* edge : edges) {
                if (edge->samples.empty()) continue;

                // Find the endpoint closest to junction
                const auto& first = edge->samples.front().position;
                const auto& last  = edge->samples.back().position;
                double dFirst = dist(first, junction.position);
                double dLast  = dist(last,  junction.position);
                Point2D pt = (dFirst < dLast) ? first : last;

                // Skip if too far
                if (std::min(dFirst, dLast) > 50.0) continue;

                double angle = std::atan2(pt.y - junction.position.y,
                                          pt.x - junction.position.x);
                anglePoints.emplace_back(pt, angle);
            }
        }

        if (anglePoints.size() < 3) return {};

        // Sort by angle around junction center (CCW)
        std::sort(anglePoints.begin(), anglePoints.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

        std::vector<Point2D> polygon;
        for (const auto& [pt, _] : anglePoints) {
            polygon.push_back(pt);
        }
        return polygon;
    }

    // ─── Build lane stripes ───
    // Connects lane boundaries from adjacent approaches with bezier curves.
    static std::vector<JunctionLaneStripe> buildLaneStripes(
        const RoadNode& junction,
        const LaneGraph& laneGraph,
        const std::map<std::string, LaneNetwork>& laneNets)
    {
        std::vector<JunctionLaneStripe> stripes;

        // For each pair of adjacent approaches, connect their lane boundaries
        // Group LaneNodes by road
        std::map<std::string, std::vector<const LaneNode*>> nodesByRoad;
        for (const auto& node : laneGraph.nodes) {
            nodesByRoad[node.roadId].push_back(&node);
        }

        // For each road, get boundary endpoints near the junction
        struct RoadBoundaryEnds {
            std::string roadId;
            std::vector<std::pair<int, Point2D>> boundaries; // (boundaryIndex, position)
        };

        std::vector<RoadBoundaryEnds> roadEnds;
        for (const auto& roadId : junction.connectedRoadIds) {
            auto it = laneNets.find(roadId);
            if (it == laneNets.end()) continue;
            const auto& net = it->second;

            RoadBoundaryEnds ends;
            ends.roadId = roadId;

            for (int bi = 0; bi < net.numBoundaries(); bi++) {
                const auto& b = net.boundaries[bi];
                if (b.samples.empty()) continue;

                const auto& first = b.samples.front().position;
                const auto& last  = b.samples.back().position;
                double dFirst = dist(first, junction.position);
                double dLast  = dist(last,  junction.position);
                if (std::min(dFirst, dLast) > 50.0) continue;

                Point2D pt = (dFirst < dLast) ? first : last;
                ends.boundaries.emplace_back(bi, pt);
            }

            if (!ends.boundaries.empty()) roadEnds.push_back(std::move(ends));
        }

        // Connect boundaries between adjacent roads (sorted by angle)
        if (roadEnds.size() < 2) return stripes;

        // Sort roads by angle around junction
        std::sort(roadEnds.begin(), roadEnds.end(),
            [&](const RoadBoundaryEnds& a, const RoadBoundaryEnds& b) {
                Point2D pa = a.boundaries.front().second;
                Point2D pb = b.boundaries.front().second;
                double angA = std::atan2(pa.y - junction.position.y, pa.x - junction.position.x);
                double angB = std::atan2(pb.y - junction.position.y, pb.x - junction.position.x);
                return angA < angB;
            });

        // Connect each road's outer boundary to the next road's outer boundary
        for (size_t i = 0; i < roadEnds.size(); i++) {
            size_t nextI = (i + 1) % roadEnds.size();
            const auto& roadA = roadEnds[i];
            const auto& roadB = roadEnds[nextI];

            if (roadA.boundaries.empty() || roadB.boundaries.empty()) continue;

            // Connect the outermost boundary of roadA to the outermost of roadB
            const auto& [biA, ptA] = roadA.boundaries.back();
            const auto& [biB, ptB] = roadB.boundaries.front();

            JunctionLaneStripe stripe;
            stripe.fromPoint = ptA;
            stripe.toPoint = ptB;
            stripe.type = JunctionLaneStripe::Type::Solid;
            stripe.color = "white";

            // Build bezier control points
            double d = dist(ptA, ptB);
            double cpDist = std::max(d * 0.3, 2.0);

            // Tangent directions (toward junction center)
            Vec2 dirA{ junction.position.x - ptA.x, junction.position.y - ptA.y };
            Vec2 dirB{ junction.position.x - ptB.x, junction.position.y - ptB.y };
            normalize(dirA);
            normalize(dirB);

            stripe.controlPoints = {
                ptA,
                { ptA.x + dirA.x * cpDist, ptA.y + dirA.y * cpDist },
                { ptB.x + dirB.x * cpDist, ptB.y + dirB.y * cpDist },
                ptB
            };

            // Sample the curve
            stripe.samples = sampleCubicBezier(stripe.controlPoints, 16);
            stripes.push_back(std::move(stripe));
        }

        return stripes;
    }

    // ─── Triangulate junction interior ───
    // Simple fan triangulation from centroid (for now).
    static JunctionMeshData triangulateJunctionInterior(
        const std::vector<Point2D>& polygon,
        const std::vector<JunctionLaneStripe>& stripes)
    {
        JunctionMeshData mesh;

        if (polygon.size() < 3) return mesh;

        // Compute centroid
        Point2D centroid{0, 0};
        for (const auto& p : polygon) {
            centroid.x += p.x;
            centroid.y += p.y;
        }
        centroid.x /= polygon.size();
        centroid.y /= polygon.size();

        // Fan triangulation: centroid + polygon edges
        // Vertices: centroid (0) + polygon points (1..N)
        mesh.vertexCount = static_cast<int>(polygon.size() + 1);

        // Centroid vertex
        mesh.positions.push_back(centroid.x);
        mesh.positions.push_back(0.0);  // y (flat)
        mesh.positions.push_back(centroid.y);  // z
        mesh.normals.push_back(0); mesh.normals.push_back(0); mesh.normals.push_back(1);
        mesh.uvs.push_back(0.5); mesh.uvs.push_back(0.5);

        // Polygon vertices
        for (size_t i = 0; i < polygon.size(); i++) {
            mesh.positions.push_back(polygon[i].x);
            mesh.positions.push_back(0.02);  // slight offset above ground
            mesh.positions.push_back(polygon[i].y);
            mesh.normals.push_back(0); mesh.normals.push_back(0); mesh.normals.push_back(1);

            // UV based on distance from centroid
            double dx = polygon[i].x - centroid.x;
            double dy = polygon[i].y - centroid.y;
            double d = std::sqrt(dx * dx + dy * dy);
            mesh.uvs.push_back(d / 10.0); // 10m tile
            mesh.uvs.push_back(0);
        }

        // Triangles (fan)
        for (size_t i = 0; i < polygon.size(); i++) {
            uint32_t i0 = 0;  // centroid
            uint32_t i1 = static_cast<uint32_t>(i + 1);
            uint32_t i2 = static_cast<uint32_t>((i + 1) % polygon.size() + 1);

            // CCW winding (viewed from above, +Z)
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
        }

        mesh.triangleCount = static_cast<int>(mesh.indices.size() / 3);
        return mesh;
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
