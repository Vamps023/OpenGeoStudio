#pragma once

// ═══════════════════════════════════════════════════════════
// Road Graph — Phase 3: Road Network Topology
// ═══════════════════════════════════════════════════════════
//
// @file road_graph.hpp
// @brief Road network topology: nodes (junctions/endpoints) and
//        edges (road segments) forming a directed graph.
//
// @section Architecture
//
//   geometry.hpp       = Math kernel (Point2D, Vec2)
//   road.hpp           = Road data model (GeneratedIntersection, ApproachRoad)
//   road_v2.hpp        = RoadV2 — segment-based road model
//   lane_network.hpp   = Persistent lane representation (Phase 2.5)
//   road_graph.hpp     = Road network topology (THIS FILE)
//
// @section Responsibility
// RoadGraph captures the topological relationships between roads
// and intersections. It is consumed by:
//   - Pathfinding / routing (AI navigation)
//   - Traffic simulation
//   - Junction rendering / LOD
//   - OpenDRIVE junction export
//
// @section Node Detection
// Nodes are detected from two sources:
//   1. Intersection centers → Junction nodes (roads meeting at a junction)
//   2. Road start/end points → EndPoint nodes (dead-ends)
// Nodes are merged when positions match within a tolerance.
//
// @section Edge Construction
// Each road becomes one edge connecting its start node to its end node.
// If a road connects two intersections, the edge links two Junction nodes.
// If a road is a dead-end, one or both endpoints become EndPoint nodes.
//
// @section API Freeze
// NOT YET FROZEN. Will be frozen at Phase 3 Complete.

#include "../geometry.hpp"
#include "../road.hpp"
#include "../road_v2.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace geo {

// ═══════════════════════════════════════════════════════════
// RoadNodeType — Classification of graph nodes
// ═══════════════════════════════════════════════════════════

enum class RoadNodeType {
    Junction,    // intersection — 3+ roads meet
    EndPoint,    // dead-end — road terminates without intersection
    Merge,       // lane merge — two roads combine into one
    Split        // lane split — one road divides into two
};

// ═══════════════════════════════════════════════════════════
// RoadNode — A junction/intersection node in the road graph
// ═══════════════════════════════════════════════════════════
//
// Represents a point where roads meet or terminate.
// For Junction nodes, connectedRoadIds lists all roads at the junction.
// For EndPoint nodes, connectedRoadIds has exactly one road.
//
struct RoadNode {
    std::string id;                          // unique node identifier
    Point2D position;                        // node center in world space
    double z = 0.0;                          // elevation
    std::vector<std::string> connectedRoadIds;  // roads meeting at this node
    RoadNodeType type = RoadNodeType::EndPoint;

    // ─── Queries ───

    // Number of roads connected at this node
    int degree() const { return static_cast<int>(connectedRoadIds.size()); }

    // Whether this node is a junction (3+ roads)
    bool isJunction() const { return type == RoadNodeType::Junction; }

    // Whether this node is a dead-end
    bool isEndPoint() const { return type == RoadNodeType::EndPoint; }

    // Human-readable description for debugging
    std::string typeName() const {
        switch (type) {
            case RoadNodeType::Junction:  return "Junction";
            case RoadNodeType::EndPoint:  return "EndPoint";
            case RoadNodeType::Merge:     return "Merge";
            case RoadNodeType::Split:     return "Split";
        }
        return "Unknown";
    }
};

// ═══════════════════════════════════════════════════════════
// RoadEdge — A road segment connecting two nodes
// ═══════════════════════════════════════════════════════════
//
// Represents a directed road segment from one node to another.
// The edge id is the same as the road id (one edge per road).
// For two-way roads, the graph may contain a reverse edge.
//
struct RoadEdge {
    std::string id;           // edge id (= road id)
    std::string fromNodeId;   // source node id
    std::string toNodeId;     // destination node id
    std::string roadId;       // associated road id
    double length = 0.0;      // road length in meters
    int laneCount = 0;        // number of lanes
    double width = 0.0;       // road width in meters
    bool isOneWay = false;    // whether the road is one-way

    // ─── Queries ───

    // Whether this edge connects two distinct nodes
    bool isValid() const { return !fromNodeId.empty() && !toNodeId.empty(); }

    // Whether this edge is a self-loop (from == to)
    bool isSelfLoop() const { return fromNodeId == toNodeId && !fromNodeId.empty(); }
};

// ═══════════════════════════════════════════════════════════
// RoadGraph — The full road network topology
// ═══════════════════════════════════════════════════════════
//
// Contains all nodes and edges of the road network, plus an
// adjacency map for efficient neighbor queries.
//
// The adjacency map maps nodeId → list of (edgeId, neighborNodeId).
// For one-way roads, only the forward direction is stored.
// For two-way roads, both directions are stored (the reverse
// edge shares the same edgeId but swaps from/to).
//
struct RoadGraph {
    std::vector<RoadNode> nodes;
    std::vector<RoadEdge> edges;

    // Adjacency: nodeId → vector of (edgeId, neighborNodeId)
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> adjacency;

    // ─── Queries ───

    int numNodes() const { return static_cast<int>(nodes.size()); }
    int numEdges() const { return static_cast<int>(edges.size()); }

    // Find a node by id (returns nullptr if not found)
    const RoadNode* findNode(const std::string& nodeId) const {
        for (const auto& n : nodes) {
            if (n.id == nodeId) return &n;
        }
        return nullptr;
    }

    // Find a node by id (mutable overload)
    RoadNode* findNode(const std::string& nodeId) {
        for (auto& n : nodes) {
            if (n.id == nodeId) return &n;
        }
        return nullptr;
    }

    // Find an edge by id (returns nullptr if not found)
    const RoadEdge* findEdge(const std::string& edgeId) const {
        for (const auto& e : edges) {
            if (e.id == edgeId) return &e;
        }
        return nullptr;
    }

    // Find an edge by id (mutable overload)
    RoadEdge* findEdge(const std::string& edgeId) {
        for (auto& e : edges) {
            if (e.id == edgeId) return &e;
        }
        return nullptr;
    }

    // Get neighbors of a node: vector of (edgeId, neighborNodeId)
    const std::vector<std::pair<std::string, std::string>>& neighbors(
        const std::string& nodeId
    ) const {
        static const std::vector<std::pair<std::string, std::string>> empty;
        auto it = adjacency.find(nodeId);
        if (it == adjacency.end()) return empty;
        return it->second;
    }

    // ─── Build ───

    // Build the road graph from roads and intersections.
    // Detects junction nodes from intersection centers,
    // endpoint nodes from road start/end points,
    // and builds edges from road segments.
    void buildFrom(
        const std::vector<RoadV2>& roads,
        const std::vector<GeneratedIntersection>& intersections
    );

    // Rebuild the adjacency map from current nodes and edges.
    // Called internally after buildFrom, but can be called
    // manually after modifying nodes/edges.
    void rebuildAdjacency();
};

// ═══════════════════════════════════════════════════════════
// buildFromRoads — Free function to build a RoadGraph
// ═══════════════════════════════════════════════════════════
//
// Convenience wrapper that creates a RoadGraph and calls buildFrom.
//
// @param roads  Vector of RoadV2 (road segments)
// @param intersections  Vector of GeneratedIntersection (junctions)
// @return RoadGraph containing the network topology
//
inline RoadGraph buildFromRoads(
    const std::vector<RoadV2>& roads,
    const std::vector<GeneratedIntersection>& intersections
) {
    RoadGraph graph;
    graph.buildFrom(roads, intersections);
    return graph;
}

// ═══════════════════════════════════════════════════════════
// Implementation — buildFrom / rebuildAdjacency
// ═══════════════════════════════════════════════════════════

// Tolerance for merging nearby points into the same node (meters).
// Roads whose start/end points are within this distance of an
// intersection center (or each other) are considered connected.
constexpr double ROAD_GRAPH_MERGE_TOLERANCE = 2.0;

// Tolerance for detecting that an intersection center lies on a road.
// If the closest point on the road centerline to the intersection
// center is within this distance, the road is considered to pass
// through the intersection and will be split there.
constexpr double ROAD_GRAPH_ON_ROUTE_TOLERANCE = 5.0;

// Number of samples used when searching for the closest point on a
// road to an intersection center.
constexpr int ROAD_GRAPH_SPLIT_SAMPLES = 200;

inline void RoadGraph::rebuildAdjacency() {
    adjacency.clear();
    for (const auto& edge : edges) {
        if (!edge.isValid()) continue;
        // Forward direction: from → to
        adjacency[edge.fromNodeId].emplace_back(edge.id, edge.toNodeId);
        // Reverse direction for two-way roads: to → from
        if (!edge.isOneWay && !edge.isSelfLoop()) {
            adjacency[edge.toNodeId].emplace_back(edge.id, edge.fromNodeId);
        }
    }
}

inline void RoadGraph::buildFrom(
    const std::vector<RoadV2>& roads,
    const std::vector<GeneratedIntersection>& intersections
) {
    nodes.clear();
    edges.clear();
    adjacency.clear();

    // Track which nodes were explicitly created as junctions (from
    // intersection centers or intersection IDs). These should NOT be
    // downgraded to EndPoint during reclassification.
    std::unordered_map<std::string, bool> isExplicitJunction;

    // ─── Helper: find or create a node near a position ───
    // Returns the node id. If a node exists within merge tolerance,
    // it is reused (and the road id is added to connectedRoadIds).
    auto findOrCreateNode = [&](const Point2D& pos, double z,
                                 const std::string& roadId,
                                 RoadNodeType preferredType) -> std::string {
        // Search existing nodes for a match within tolerance
        for (auto& n : nodes) {
            if (n.position.distanceTo(pos) < ROAD_GRAPH_MERGE_TOLERANCE) {
                // Merge: add road id if not already present
                if (std::find(n.connectedRoadIds.begin(),
                              n.connectedRoadIds.end(),
                              roadId) == n.connectedRoadIds.end()) {
                    n.connectedRoadIds.push_back(roadId);
                }
                // Upgrade EndPoint to Junction if degree increases beyond 2
                if (n.connectedRoadIds.size() >= 3 &&
                    n.type == RoadNodeType::EndPoint) {
                    n.type = RoadNodeType::Junction;
                }
                return n.id;
            }
        }
        // Create new node
        std::string nodeId = "node_" + std::to_string(nodes.size());
        RoadNode node;
        node.id = nodeId;
        node.position = pos;
        node.z = z;
        node.connectedRoadIds.push_back(roadId);
        node.type = preferredType;
        nodes.push_back(std::move(node));
        return nodeId;
    };

    // ─── Step 1: Create junction nodes from intersection centers ───
    // Each intersection center becomes a Junction node.
    // We register them first so roads can snap to them.
    for (size_t i = 0; i < intersections.size(); i++) {
        const auto& intersection = intersections[i];
        std::string nodeId = "junction_" + std::to_string(i);
        RoadNode node;
        node.id = nodeId;
        node.position = intersection.center;
        node.z = 0.0;
        node.type = RoadNodeType::Junction;
        // connectedRoadIds will be populated when roads snap to this node
        nodes.push_back(std::move(node));
        isExplicitJunction[nodeId] = true;
    }

    // ─── Helper: snap to existing junction node if close ───
    auto snapToJunction = [&](const Point2D& pos,
                               const std::string& roadId) -> std::string {
        for (auto& n : nodes) {
            if (n.type == RoadNodeType::Junction &&
                n.position.distanceTo(pos) < ROAD_GRAPH_MERGE_TOLERANCE) {
                if (std::find(n.connectedRoadIds.begin(),
                              n.connectedRoadIds.end(),
                              roadId) == n.connectedRoadIds.end()) {
                    n.connectedRoadIds.push_back(roadId);
                }
                return n.id;
            }
        }
        return "";
    };

    // ─── Helper: find intersection centers that lie on a road ───
    // Samples the road geometry and finds the closest point to each
    // intersection center. Returns a sorted list of (sPosition, nodeId)
    // for intersections that are within on-route tolerance.
    auto findSplitPoints = [&](const RoadV2& road,
                                double totalLen) -> std::vector<std::pair<double, std::string>> {
        std::vector<std::pair<double, std::string>> splitPoints;
        if (intersections.empty() || totalLen <= 0.0) return splitPoints;

        const auto& geom = road.geometry();

        for (size_t i = 0; i < intersections.size(); i++) {
            const Point2D& center = intersections[i].center;

            // Sample the road to find the closest point to the intersection center
            double bestS = 0.0;
            double bestDist = std::numeric_limits<double>::max();

            int numSamples = ROAD_GRAPH_SPLIT_SAMPLES;
            for (int j = 0; j <= numSamples; j++) {
                double s = totalLen * static_cast<double>(j) / numSamples;
                Point2D pt = geom.positionAt(s);
                double dist = pt.distanceTo(center);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestS = s;
                }
            }

            // If the intersection center is close enough to the road,
            // this is a split point. Also check that it's not at the
            // very start or end (those are handled by endpoint logic).
            if (bestDist < ROAD_GRAPH_ON_ROUTE_TOLERANCE &&
                bestS > ROAD_GRAPH_MERGE_TOLERANCE &&
                bestS < totalLen - ROAD_GRAPH_MERGE_TOLERANCE) {
                std::string nodeId = "junction_" + std::to_string(i);
                splitPoints.emplace_back(bestS, nodeId);
            }
        }

        // Sort by s-position
        std::sort(splitPoints.begin(), splitPoints.end());
        return splitPoints;
    };

    // ─── Helper: create or snap a node for a road endpoint ───
    auto resolveEndpoint = [&](const Point2D& pos, double z,
                                const std::string& roadId,
                                const std::string& intersectionId,
                                RoadNodeType fallbackType) -> std::string {
        // First try to snap to an existing junction
        std::string nodeId = snapToJunction(pos, roadId);
        if (!nodeId.empty()) return nodeId;

        // Use explicit intersection ID if provided
        if (!intersectionId.empty()) {
            nodeId = intersectionId;
            if (!findNode(nodeId)) {
                RoadNode node;
                node.id = nodeId;
                node.position = pos;
                node.z = z;
                node.type = RoadNodeType::Junction;
                node.connectedRoadIds.push_back(roadId);
                nodes.push_back(std::move(node));
                isExplicitJunction[nodeId] = true;
            } else {
                RoadNode* n = findNode(nodeId);
                if (std::find(n->connectedRoadIds.begin(),
                              n->connectedRoadIds.end(),
                              roadId) == n->connectedRoadIds.end()) {
                    n->connectedRoadIds.push_back(roadId);
                }
            }
            return nodeId;
        }

        // Create an endpoint node
        return findOrCreateNode(pos, z, roadId, fallbackType);
    };

    // ─── Step 2: Build edges from roads ───
    // For each road, find split points (intersections on the route),
    // split the road into sub-segments, and create an edge per segment.
    int edgeCounter = 0;
    for (const auto& road : roads) {
        if (road.numSegments() == 0) continue;

        double totalLen = road.totalLength();
        if (totalLen <= 0.0) continue;

        const auto& geom = road.geometry();

        // Find intersection centers that lie on this road
        auto splitPoints = findSplitPoints(road, totalLen);

        // Build the list of node boundaries along the road:
        // [startPoint, splitPoint1, splitPoint2, ..., endPoint]
        struct Boundary {
            double s;           // s-position on road
            std::string nodeId; // node id at this boundary
        };
        std::vector<Boundary> boundaries;

        // Start boundary
        Point2D startPos = geom.positionAt(0.0);
        std::string startNodeId = resolveEndpoint(
            startPos, 0.0, road.id, road.startIntersectionId,
            RoadNodeType::EndPoint);
        boundaries.push_back({0.0, startNodeId});

        // Split point boundaries (intersections on the route)
        for (const auto& [s, nodeId] : splitPoints) {
            // Snap the road's position at s to the junction node
            // (register the road with the junction)
            Point2D posAtS = geom.positionAt(s);
            RoadNode* junction = findNode(nodeId);
            if (junction) {
                // Update junction position to the exact road point
                // (only if it hasn't been moved by another road)
                if (junction->connectedRoadIds.empty()) {
                    junction->position = posAtS;
                }
                if (std::find(junction->connectedRoadIds.begin(),
                              junction->connectedRoadIds.end(),
                              road.id) == junction->connectedRoadIds.end()) {
                    junction->connectedRoadIds.push_back(road.id);
                }
            }
            boundaries.push_back({s, nodeId});
        }

        // End boundary
        Point2D endPos = geom.positionAt(totalLen);
        std::string endNodeId = resolveEndpoint(
            endPos, 0.0, road.id, road.endIntersectionId,
            RoadNodeType::EndPoint);
        boundaries.push_back({totalLen, endNodeId});

        // Create an edge for each sub-segment between consecutive boundaries
        for (size_t i = 0; i < boundaries.size() - 1; i++) {
            double segStart = boundaries[i].s;
            double segEnd = boundaries[i + 1].s;
            double segLen = segEnd - segStart;
            if (segLen < EPSILON) continue;

            RoadEdge edge;
            // Edge id: roadId for single-segment roads, roadId_idx for split
            if (splitPoints.empty()) {
                edge.id = road.id;
            } else {
                edge.id = road.id + "_" + std::to_string(i);
            }
            edge.fromNodeId = boundaries[i].nodeId;
            edge.toNodeId = boundaries[i + 1].nodeId;
            edge.roadId = road.id;
            edge.length = segLen;
            edge.laneCount = road.laneCount;
            edge.width = road.width;
            // Heuristic: roads with 1 lane are one-way, 2+ are two-way
            edge.isOneWay = (road.laneCount <= 1);
            edges.push_back(std::move(edge));
            edgeCounter++;
        }
    }

    // ─── Step 3: Reclassify nodes based on degree ───
    // Only upgrade EndPoint → Junction. Never downgrade explicit junctions.
    for (auto& n : nodes) {
        if (n.connectedRoadIds.size() >= 3) {
            n.type = RoadNodeType::Junction;
        } else if (n.connectedRoadIds.size() == 2) {
            // Two roads meeting — could be a merge/split or a through junction
            if (n.type == RoadNodeType::EndPoint) {
                n.type = RoadNodeType::Junction;
            }
        } else if (n.connectedRoadIds.size() == 1) {
            // Don't downgrade explicit junctions (from intersection centers/IDs)
            if (!isExplicitJunction.count(n.id)) {
                n.type = RoadNodeType::EndPoint;
            }
        }
    }

    // ─── Step 4: Build adjacency map ───
    rebuildAdjacency();
}

} // namespace geo

// ═══════════════════════════════════════════════════════════
// doctest Unit Tests
// ═══════════════════════════════════════════════════════════
//
// Pure C++ unit tests for the road graph topology.
// Run with: build and execute this test executable.
// doctest is header-only — no external dependency beyond the header.
//

#ifdef DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "doctest.h"

using geo::RoadGraph;
using geo::RoadNode;
using geo::RoadEdge;
using geo::RoadNodeType;
using geo::Point2D;
using geo::RoadV2;
using geo::LineSegment;
using geo::GeneratedIntersection;
using geo::ApproachRoad;
using geo::buildFromRoads;

// ─── RoadNode Tests ────────────────────────────────────────

TEST_CASE("RoadNode: default type is EndPoint") {
    RoadNode node;
    CHECK(node.type == RoadNodeType::EndPoint);
    CHECK(node.isEndPoint());
    CHECK(!node.isJunction());
    CHECK(node.degree() == 0);
}

TEST_CASE("RoadNode: junction type") {
    RoadNode node;
    node.type = RoadNodeType::Junction;
    node.connectedRoadIds = {"road_a", "road_b", "road_c"};
    CHECK(node.isJunction());
    CHECK(node.degree() == 3);
    CHECK(node.typeName() == "Junction");
}

TEST_CASE("RoadNode: typeName returns correct strings") {
    RoadNode node;
    node.type = RoadNodeType::EndPoint;
    CHECK(node.typeName() == "EndPoint");
    node.type = RoadNodeType::Merge;
    CHECK(node.typeName() == "Merge");
    node.type = RoadNodeType::Split;
    CHECK(node.typeName() == "Split");
}

// ─── RoadEdge Tests ────────────────────────────────────────

TEST_CASE("RoadEdge: default values") {
    RoadEdge edge;
    CHECK(edge.length == 0.0);
    CHECK(edge.laneCount == 0);
    CHECK(edge.width == 0.0);
    CHECK(!edge.isOneWay);
    CHECK(!edge.isValid());
    CHECK(!edge.isSelfLoop());
}

TEST_CASE("RoadEdge: valid edge") {
    RoadEdge edge;
    edge.id = "road_1";
    edge.fromNodeId = "node_a";
    edge.toNodeId = "node_b";
    edge.length = 100.0;
    edge.laneCount = 2;
    edge.width = 8.0;
    edge.isOneWay = false;
    CHECK(edge.isValid());
    CHECK(!edge.isSelfLoop());
}

TEST_CASE("RoadEdge: self-loop") {
    RoadEdge edge;
    edge.fromNodeId = "node_a";
    edge.toNodeId = "node_a";
    CHECK(edge.isSelfLoop());
}

// ─── RoadGraph Tests ───────────────────────────────────────

TEST_CASE("RoadGraph: empty graph") {
    RoadGraph graph;
    CHECK(graph.numNodes() == 0);
    CHECK(graph.numEdges() == 0);
    CHECK(graph.findNode("nonexistent") == nullptr);
    CHECK(graph.findEdge("nonexistent") == nullptr);
}

TEST_CASE("RoadGraph: buildFrom with empty inputs") {
    RoadGraph graph;
    std::vector<RoadV2> roads;
    std::vector<GeneratedIntersection> intersections;
    graph.buildFrom(roads, intersections);
    CHECK(graph.numNodes() == 0);
    CHECK(graph.numEdges() == 0);
}

TEST_CASE("RoadGraph: single road creates two endpoint nodes") {
    RoadV2 road;
    road.id = "road_1";
    road.laneCount = 2;
    road.width = 8.0;
    road.addSegment<LineSegment>(Point2D(0, 0), Point2D(100, 0));

    RoadGraph graph;
    graph.buildFrom({road}, {});

    CHECK(graph.numEdges() == 1);
    CHECK(graph.numNodes() == 2);

    const RoadEdge* edge = graph.findEdge("road_1");
    REQUIRE(edge != nullptr);
    CHECK(edge->length == doctest::Approx(100.0));
    CHECK(edge->laneCount == 2);
    CHECK(edge->width == doctest::Approx(8.0));
    CHECK(!edge->isOneWay);

    // Both nodes should be endpoints
    const RoadNode* fromNode = graph.findNode(edge->fromNodeId);
    const RoadNode* toNode = graph.findNode(edge->toNodeId);
    REQUIRE(fromNode != nullptr);
    REQUIRE(toNode != nullptr);
    CHECK(fromNode->isEndPoint());
    CHECK(toNode->isEndPoint());
    CHECK(fromNode->degree() == 1);
    CHECK(toNode->degree() == 1);
}

TEST_CASE("RoadGraph: intersection creates junction node") {
    // Two roads crossing at origin — each is split into 2 edges at the junction
    RoadV2 road1;
    road1.id = "road_h";
    road1.laneCount = 2;
    road1.width = 8.0;
    road1.addSegment<LineSegment>(Point2D(-50, 0), Point2D(50, 0));

    RoadV2 road2;
    road2.id = "road_v";
    road2.laneCount = 2;
    road2.width = 8.0;
    road2.addSegment<LineSegment>(Point2D(0, -50), Point2D(0, 50));

    // Intersection at origin
    GeneratedIntersection intersection;
    intersection.center = Point2D(0, 0);

    RoadGraph graph;
    graph.buildFrom({road1, road2}, {intersection});

    // Each road is split at the junction → 4 edges total
    CHECK(graph.numEdges() == 4);

    // Should have 1 junction node at origin + 4 endpoint nodes
    int junctionCount = 0;
    int endpointCount = 0;
    for (const auto& n : graph.nodes) {
        if (n.isJunction()) junctionCount++;
        if (n.isEndPoint()) endpointCount++;
    }
    CHECK(junctionCount == 1);
    CHECK(endpointCount == 4);

    // Junction should have 2 connected roads
    const RoadNode* junction = nullptr;
    for (const auto& n : graph.nodes) {
        if (n.isJunction()) { junction = &n; break; }
    }
    REQUIRE(junction != nullptr);
    CHECK(junction->degree() == 2);
    CHECK(junction->position.x == doctest::Approx(0.0));
    CHECK(junction->position.y == doctest::Approx(0.0));
}

TEST_CASE("RoadGraph: adjacency map for two-way road") {
    RoadV2 road;
    road.id = "road_1";
    road.laneCount = 2;
    road.width = 8.0;
    road.addSegment<LineSegment>(Point2D(0, 0), Point2D(100, 0));

    RoadGraph graph;
    graph.buildFrom({road}, {});

    const RoadEdge* edge = graph.findEdge("road_1");
    REQUIRE(edge != nullptr);

    // Two-way road: both nodes should have neighbors
    const auto& fromNeighbors = graph.neighbors(edge->fromNodeId);
    const auto& toNeighbors = graph.neighbors(edge->toNodeId);
    CHECK(fromNeighbors.size() == 1);
    CHECK(toNeighbors.size() == 1);
    CHECK(fromNeighbors[0].second == edge->toNodeId);
    CHECK(toNeighbors[0].second == edge->fromNodeId);
}

TEST_CASE("RoadGraph: one-way road has single-direction adjacency") {
    RoadV2 road;
    road.id = "road_1";
    road.laneCount = 1;  // one-way
    road.width = 4.0;
    road.addSegment<LineSegment>(Point2D(0, 0), Point2D(100, 0));

    RoadGraph graph;
    graph.buildFrom({road}, {});

    const RoadEdge* edge = graph.findEdge("road_1");
    REQUIRE(edge != nullptr);
    CHECK(edge->isOneWay);

    // One-way: only from node has neighbors
    const auto& fromNeighbors = graph.neighbors(edge->fromNodeId);
    const auto& toNeighbors = graph.neighbors(edge->toNodeId);
    CHECK(fromNeighbors.size() == 1);
    CHECK(toNeighbors.size() == 0);
}

TEST_CASE("RoadGraph: buildFromRoads free function") {
    RoadV2 road;
    road.id = "road_1";
    road.laneCount = 2;
    road.width = 8.0;
    road.addSegment<LineSegment>(Point2D(0, 0), Point2D(50, 0));

    RoadGraph graph = buildFromRoads({road}, {});
    CHECK(graph.numEdges() == 1);
    CHECK(graph.numNodes() == 2);
}

TEST_CASE("RoadGraph: rebuildAdjacency after manual edge addition") {
    RoadGraph graph;

    // Manually add nodes
    RoadNode n1;
    n1.id = "n1";
    n1.type = RoadNodeType::EndPoint;
    RoadNode n2;
    n2.id = "n2";
    n2.type = RoadNodeType::EndPoint;
    graph.nodes = {n1, n2};

    // Manually add edge
    RoadEdge e;
    e.id = "e1";
    e.fromNodeId = "n1";
    e.toNodeId = "n2";
    e.isOneWay = false;
    graph.edges = {e};

    // Rebuild adjacency
    graph.rebuildAdjacency();

    CHECK(graph.neighbors("n1").size() == 1);
    CHECK(graph.neighbors("n2").size() == 1);
    CHECK(graph.neighbors("n1")[0].second == "n2");
    CHECK(graph.neighbors("n2")[0].second == "n1");
}

TEST_CASE("RoadGraph: neighbors of nonexistent node returns empty") {
    RoadGraph graph;
    CHECK(graph.neighbors("nonexistent").empty());
}

TEST_CASE("RoadGraph: road with startIntersectionId snaps to junction") {
    RoadV2 road;
    road.id = "road_1";
    road.laneCount = 2;
    road.width = 8.0;
    road.startIntersectionId = "int_a";
    road.addSegment<LineSegment>(Point2D(0, 0), Point2D(100, 0));

    RoadGraph graph;
    graph.buildFrom({road}, {});

    const RoadEdge* edge = graph.findEdge("road_1");
    REQUIRE(edge != nullptr);
    CHECK(edge->fromNodeId == "int_a");

    const RoadNode* startNode = graph.findNode("int_a");
    REQUIRE(startNode != nullptr);
    CHECK(startNode->type == RoadNodeType::Junction);
}

TEST_CASE("RoadGraph: T-junction with 3 roads") {
    // Three roads meeting at origin: one horizontal (passing through),
    // two vertical (terminating at origin)
    RoadV2 road1;
    road1.id = "road_h";
    road1.laneCount = 2;
    road1.width = 8.0;
    road1.addSegment<LineSegment>(Point2D(-50, 0), Point2D(50, 0));

    RoadV2 road2;
    road2.id = "road_v1";
    road2.laneCount = 2;
    road2.width = 8.0;
    road2.addSegment<LineSegment>(Point2D(0, 0), Point2D(0, 50));

    RoadV2 road3;
    road3.id = "road_v2";
    road3.laneCount = 2;
    road3.width = 8.0;
    road3.addSegment<LineSegment>(Point2D(0, -50), Point2D(0, 0));

    GeneratedIntersection intersection;
    intersection.center = Point2D(0, 0);

    RoadGraph graph;
    graph.buildFrom({road1, road2, road3}, {intersection});

    // road_h is split at junction → 2 edges
    // road_v1 and road_v2 terminate at junction → 1 edge each
    // Total: 4 edges
    CHECK(graph.numEdges() == 4);

    // Find the junction node with degree >= 3
    const RoadNode* junction = nullptr;
    for (const auto& n : graph.nodes) {
        if (n.isJunction() && n.degree() >= 3) {
            junction = &n;
            break;
        }
    }
    REQUIRE(junction != nullptr);
    CHECK(junction->degree() == 3);
}

#endif // DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
