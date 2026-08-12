// Diagnostic: 4-way junction LaneGraph audit
// Traces Road → LaneNetwork → LaneNode → LaneLink → path
//
// Build: cl /std:c++20 /EHsc /Fe:diag.exe /I. lane_graph_diagnostic.cpp && diag.exe

#define _USE_MATH_DEFINES
#include "road_engine/internal/lane_graph.hpp"
#include "lane_network.hpp"
#include "road_v2.hpp"
#include "road_adapter.hpp"
#include <cmath>
#include <cstdio>

using namespace geo;

static double dist2d(const Point2D& a, const Point2D& b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

// Create a simple straight road from (x0,y0) to (x1,y1) with 2 lanes (+1, -1)
static RoadV2 makeStraightRoad(const std::string& id,
                                double x0, double y0, double x1, double y1,
                                double laneWidth = 3.5) {
    RoadV2 road;
    road.id = id;

    // Geometry: single line segment
    road.addSegment<LineSegment>(Point2D(x0, y0), Point2D(x1, y1));

    // Lane section: 2 lanes (+1 right, -1 left)
    LaneSection ls;
    ls.startS = 0.0;

    Lane lane1;
    lane1.id = 1;
    lane1.type = LaneType::Driving;
    lane1.width = Polynomial3(laneWidth, 0, 0, 0);
    ls.addLane(lane1);

    Lane lane2;
    lane2.id = -1;
    lane2.type = LaneType::Driving;
    lane2.width = Polynomial3(laneWidth, 0, 0, 0);
    ls.addLane(lane2);

    road.addLaneSection(std::move(ls));

    return road;
}

int main() {
    printf("=== 4-WAY JUNCTION DIAGNOSTIC ===\n\n");

    // Junction at origin (0,0)
    // 4 roads, each 100m long, ending/starting at junction
    // Road A: from junction(0,0) to east(100,0)    — departs junction
    // Road B: from west(-100,0) to junction(0,0)   — arrives at junction
    // Road C: from junction(0,0) to north(0,100)   — departs junction
    // Road D: from south(0,-100) to junction(0,0)  — arrives at junction

    auto roadA = makeStraightRoad("roadA", 0, 0, 100, 0);
    auto roadB = makeStraightRoad("roadB", -100, 0, 0, 0);
    auto roadC = makeStraightRoad("roadC", 0, 0, 0, 100);
    auto roadD = makeStraightRoad("roadD", 0, -100, 0, 0);

    // Build LaneNetworks
    auto netA = generateLaneNetwork(roadA);
    auto netB = generateLaneNetwork(roadB);
    auto netC = generateLaneNetwork(roadC);
    auto netD = generateLaneNetwork(roadD);

    // Print centerline endpoints for each road
    printf("--- CENTERLINE ENDPOINTS ---\n");
    struct RoadInfo { std::string id; RoadV2* road; LaneNetwork* net; };
    RoadInfo roads[] = {
        {"roadA", &roadA, &netA},
        {"roadB", &roadB, &netB},
        {"roadC", &roadC, &netC},
        {"roadD", &roadD, &netD},
    };

    for (const auto& ri : roads) {
        printf("\n%s (len=%.1f):\n", ri.id.c_str(), ri.road->totalLength());
        for (const auto& cl : ri.net->centerlines) {
            if (cl.samples.empty()) continue;
            const auto& s0 = cl.samples.front();
            const auto& sN = cl.samples.back();
            printf("  lane %+d: start=(%.2f,%.2f) h=%.1f°  end=(%.2f,%.2f) h=%.1f°  samples=%zu\n",
                   cl.laneId,
                   s0.position.x, s0.position.y, s0.heading * 180.0 / M_PI,
                   sN.position.x, sN.position.y, sN.heading * 180.0 / M_PI,
                   cl.samples.size());
        }
    }

    // Build RoadGraph
    RoadGraph graph;

    // Junction node
    RoadNode junction;
    junction.id = "j0";
    junction.type = RoadNodeType::Junction;
    junction.position = {0, 0};
    junction.connectedRoadIds = {"roadA", "roadB", "roadC", "roadD"};

    // Endpoint nodes
    RoadNode east;  east.id = "east";  east.type = RoadNodeType::EndPoint;  east.position = {100, 0};
    RoadNode west;  west.id = "west";  west.type = RoadNodeType::EndPoint;  west.position = {-100, 0};
    RoadNode north; north.id = "north"; north.type = RoadNodeType::EndPoint; north.position = {0, 100};
    RoadNode south; south.id = "south"; south.type = RoadNodeType::EndPoint; south.position = {0, -100};

    graph.nodes.push_back(junction);
    graph.nodes.push_back(east);
    graph.nodes.push_back(west);
    graph.nodes.push_back(north);
    graph.nodes.push_back(south);

    // Edges — manually create
    RoadEdge ea;  ea.id = "edgeA";  ea.fromNodeId = "j0";    ea.toNodeId = "east";  ea.roadId = "roadA";
    RoadEdge eb;  eb.id = "edgeB";  eb.fromNodeId = "west";  eb.toNodeId = "j0";    eb.roadId = "roadB";
    RoadEdge ec;  ec.id = "edgeC";  ec.fromNodeId = "j0";    ec.toNodeId = "north"; ec.roadId = "roadC";
    RoadEdge ed;  ed.id = "edgeD";  ed.fromNodeId = "south"; ed.toNodeId = "j0";    ed.roadId = "roadD";
    graph.edges = {ea, eb, ec, ed};
    graph.rebuildAdjacency();

    // Update connectedRoadIds on junction
    graph.nodes[0].connectedRoadIds = {"roadA", "roadB", "roadC", "roadD"};

    // Build LaneNetwork map
    std::map<std::string, LaneNetwork> laneNets;
    laneNets["roadA"] = netA;
    laneNets["roadB"] = netB;
    laneNets["roadC"] = netC;
    laneNets["roadD"] = netD;

    // Build LaneGraph
    printf("\n--- LANE GRAPH BUILD ---\n");
    auto lg = LaneGraphBuilder::build(junction, graph, laneNets);

    // Print all LaneNodes
    printf("\n--- LANE NODES (%zu) ---\n", lg.nodes.size());
    for (const auto& node : lg.nodes) {
        printf("  %s: road=%s lane=%d type=%s pos=(%.2f,%.2f) heading=%.1f°\n",
               node.id.c_str(), node.roadId.c_str(), node.laneId,
               node.typeName().c_str(),
               node.position.x, node.position.y,
               node.heading * 180.0 / M_PI);
    }

    // Print all LaneLinks with path details
    printf("\n--- LANE LINKS (%zu) ---\n", lg.links.size());
    for (const auto& link : lg.links) {
        const auto* from = lg.findNode(link.fromLaneNodeId);
        const auto* to = lg.findNode(link.toLaneNodeId);
        if (!from || !to) continue;

        printf("\n  LINK %s: %s\n", link.id.c_str(), link.maneuverName().c_str());
        printf("    FROM: road=%s lane=%d pos=(%.2f,%.2f) heading=%.1f°\n",
               from->roadId.c_str(), from->laneId,
               from->position.x, from->position.y,
               from->heading * 180.0 / M_PI);
        printf("    TO:   road=%s lane=%d pos=(%.2f,%.2f) heading=%.1f°\n",
               to->roadId.c_str(), to->laneId,
               to->position.x, to->position.y,
               to->heading * 180.0 / M_PI);

        // Bezier control points (reconstruct from buildBezierPath logic)
        double d = dist2d(from->position, to->position);
        double cpDist = std::max(d * 0.4, 3.0);
        Point2D c0{ from->position.x + std::cos(from->heading) * cpDist,
                    from->position.y + std::sin(from->heading) * cpDist };
        Point2D c1{ to->position.x - std::cos(to->heading) * cpDist,
                    to->position.y - std::sin(to->heading) * cpDist };
        printf("    BEZIER: d=%.2f cpDist=%.2f\n", d, cpDist);
        printf("      P0=(%.2f,%.2f)  P1=(%.2f,%.2f)  P2=(%.2f,%.2f)  P3=(%.2f,%.2f)\n",
               from->position.x, from->position.y,
               c0.x, c0.y,
               c1.x, c1.y,
               to->position.x, to->position.y);

        // Check for degenerate cases
        if (d < 0.5) {
            printf("    *** DEGENERATE: from~to (d=%.2f) — BEZIER WILL LOOP! ***\n", d);
        }
        double pathLen = link.length;
        if (pathLen > d * 2.0 && d > 0.5) {
            printf("    *** WARNING: path length (%.2f) >> straight distance (%.2f) ***\n",
                   pathLen, d);
        }

        // Print path samples
        printf("    PATH (%zu samples, len=%.2f):\n", link.path.size(), pathLen);
        if (link.path.size() >= 3) {
            printf("      [0]   (%.2f,%.2f)\n", link.path[0].x, link.path[0].y);
            printf("      [1]   (%.2f,%.2f)\n", link.path[1].x, link.path[1].y);
            printf("      [mid] (%.2f,%.2f)\n",
                   link.path[link.path.size()/2].x, link.path[link.path.size()/2].y);
            printf("      [N-1] (%.2f,%.2f)\n",
                   link.path[link.path.size()-2].x, link.path[link.path.size()-2].y);
            printf("      [N]   (%.2f,%.2f)\n",
                   link.path.back().x, link.path.back().y);
        }
    }

    // Summary
    printf("\n--- SUMMARY ---\n");
    printf("  Nodes: %zu (in=%zu, out=%zu)\n",
           lg.nodes.size(),
           lg.incomingNodes().size(),
           lg.outgoingNodes().size());
    printf("  Links: %zu\n", lg.links.size());

    int straight = 0, left = 0, right = 0, uturn = 0;
    int degenerate = 0;
    for (const auto& link : lg.links) {
        switch (link.maneuver) {
            case ManeuverType::Straight: straight++; break;
            case ManeuverType::LeftTurn: left++; break;
            case ManeuverType::RightTurn: right++; break;
            case ManeuverType::UTurn: uturn++; break;
            default: break;
        }
        const auto* from = lg.findNode(link.fromLaneNodeId);
        const auto* to = lg.findNode(link.toLaneNodeId);
        if (from && to && dist2d(from->position, to->position) < 0.5)
            degenerate++;
    }
    printf("  Straight=%d  Left=%d  Right=%d  UTurn=%d\n", straight, left, right, uturn);
    printf("  Degenerate (from~to): %d\n", degenerate);

    printf("\n=== END DIAGNOSTIC ===\n");
    return 0;
}
