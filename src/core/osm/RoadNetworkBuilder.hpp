#pragma once

// ============================================================
// RoadNetworkBuilder — OSM ways → RoadV2 road network
// ============================================================
//
// Converts raw OSM data into the road engine's RoadV2 model.
//
// Pipeline:
//   OSM ways → filter highways → project coordinates →
//   simplify geometry → create LineSegments → build RoadV2 →
//   detect shared nodes → build topology
//
// The output is a set of RoadV2 roads + a node map that records
// which roads share endpoints (used by JunctionDetector).
//

#include "OsmTypes.hpp"
#include "CoordinateConverter.hpp"
#include "RoadClassifier.hpp"
#include "LaneGenerator.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/geometry_segment.hpp"
#include "../../engine/road/road_v2.hpp"
#include "../../engine/road/lane_engine.hpp"

#include <QString>
#include <QList>
#include <QDebug>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <cmath>
#include <algorithm>

namespace osm {

// ─── NetworkNode — a node in the road network topology ───
struct NetworkNode {
    qint64 osmNodeId = 0;
    double x = 0, y = 0, z = 0;    // local meters
    double lat = 0, lon = 0;       // original WGS84
    std::vector<QString> roadIds;  // roads that share this node
    bool isJunction = false;       // 3+ roads meet
    bool isEndPoint = false;       // dead-end
    bool isRoundabout = false;

    int degree() const { return int(roadIds.size()); }
};

// ─── RoadNetworkBuilder ───
class RoadNetworkBuilder {
public:
    // Network mode: highway (road) or railway (rail)
    enum class Mode { Highway, Railway };

    // Build parameters
    struct Params {
        Mode mode = Mode::Highway;
        double simplifyTolerance = 0.5;     // meters — Douglas-Peucker tolerance
        double minSegmentLength = 0.5;      // meters — discard segments shorter than this
        double duplicatePointTolerance = 0.3; // meters — merge points closer than this
        bool preserveJunctionNodes = true;  // keep nodes where roads meet
        bool preserveBridgeTunnel = true;   // keep bridge/tunnel boundary points
        bool preserveLayerChanges = true;   // keep points where layer changes
        bool fitCurves = false;             // fit arcs/bezier to OSM polylines (future)
    };

    // Build result
    struct Result {
        std::vector<geo::RoadV2> roads;
        std::unordered_map<qint64, NetworkNode> nodes;  // keyed by OSM node id
        std::vector<qint64> junctionNodeIds;             // nodes with 3+ roads
        std::vector<qint64> endPointNodeIds;             // dead-end nodes

        // Statistics
        int roadsCreated = 0;
        int nodesCreated = 0;
        int junctionsDetected = 0;
        int endPointsDetected = 0;
        int waysSkipped = 0;
        int segmentsCreated = 0;
    };

    // Build a road network from OSM data
    static Result build(const OsmData& osm,
                        const CoordinateConverter& converter,
                        const Params& params = {})
    {
        Result result;
        int roadCounter = 0;

        // ─── Step 1: Collect ways and build node map ───
        // Track which roads use each node
        std::unordered_map<qint64, std::vector<QString>> nodeToRoads;

        auto isRelevantWay = [&params](const Way& way) {
            if (!way.visible || way.isArea()) return false;
            return params.mode == Mode::Railway ? way.isRailway() : way.isHighway();
        };

        for (const auto& [wayId, way] : osm.ways) {
            if (!isRelevantWay(way)) {
                result.waysSkipped++;
                continue;
            }
            if (way.nodeRefs.size() < 2) {
                result.waysSkipped++;
                continue;
            }

            QString roadId = QString("osm_way_%1").arg(wayId);
            for (qint64 nodeRef : way.nodeRefs) {
                nodeToRoads[nodeRef].push_back(roadId);
            }
        }

        // ─── Step 2: Build network nodes ───
        for (const auto& [nodeId, roadIds] : nodeToRoads) {
            const Node* osmNode = osm.getNode(nodeId);
            if (!osmNode) continue;

            NetworkNode nn;
            nn.osmNodeId = nodeId;
            nn.lat = osmNode->lat;
            nn.lon = osmNode->lon;
            nn.z = osmNode->hasElevation ? osmNode->ele : 0.0;

            // Project to local coordinates
            converter.toLocal(osmNode->lat, osmNode->lon, nn.x, nn.y);

            nn.roadIds = roadIds;
            nn.isJunction = roadIds.size() >= 3;
            nn.isEndPoint = roadIds.size() == 1;

            result.nodes[nodeId] = nn;

            if (nn.isJunction) {
                result.junctionNodeIds.push_back(nodeId);
                result.junctionsDetected++;
            }
            if (nn.isEndPoint) {
                result.endPointNodeIds.push_back(nodeId);
                result.endPointsDetected++;
            }
            result.nodesCreated++;
        }

        // ─── Step 3: Build roads from ways ───
        for (const auto& [wayId, way] : osm.ways) {
            if (!isRelevantWay(way)) continue;
            if (way.nodeRefs.size() < 2) continue;

            // Get classified info (road mode uses RoadClassifier; rail mode uses defaults)
            RoadClassInfo classInfo;
            if (params.mode == Mode::Railway) {
                // Rail defaults: single track, standard gauge
                classInfo.cls = RoadClass::Unclassified;
                classInfo.defaultLanes = 1;
                classInfo.defaultLaneWidth = 3.5;  // gauge + ballast
                classInfo.defaultSpeed = 120;
                classInfo.osmValue = way.railwayType();
                classInfo.displayName = way.railwayType().isEmpty() ? "Rail" : way.railwayType();
            } else {
                classInfo = RoadClassifier::classifyAndGet(way.highwayType());
                if (!RoadClassifier::isDrivable(classInfo.cls)) {
                    // Skip non-drivable ways (footways, paths)
                    continue;
                }
            }

            // Collect projected points
            std::vector<geo::Point2D> points;
            std::vector<qint64> pointNodeIds;
            std::vector<double> elevations;

            for (qint64 nodeRef : way.nodeRefs) {
                const Node* osmNode = osm.getNode(nodeRef);
                if (!osmNode) continue;

                double x, y;
                converter.toLocal(osmNode->lat, osmNode->lon, x, y);

                // Skip duplicate points
                if (!points.empty()) {
                    double dx = x - points.back().x;
                    double dy = y - points.back().y;
                    if (std::sqrt(dx*dx + dy*dy) < params.duplicatePointTolerance) {
                        continue;
                    }
                }

                points.emplace_back(x, y);
                pointNodeIds.push_back(nodeRef);
                elevations.push_back(osmNode->hasElevation ? osmNode->ele : 0.0);
            }

            if (points.size() < 2) {
                result.waysSkipped++;
                continue;
            }

            // ─── Simplify geometry (preserve junction nodes) ───
            std::vector<size_t> keepIndices;
            simplifyPolyline(points, pointNodeIds, nodeToRoads, params, keepIndices);

            std::vector<geo::Point2D> simplifiedPoints;
            std::vector<double> simplifiedElev;
            for (size_t idx : keepIndices) {
                simplifiedPoints.push_back(points[idx]);
                simplifiedElev.push_back(elevations[idx]);
            }

            if (simplifiedPoints.size() < 2) {
                result.waysSkipped++;
                continue;
            }

            // ─── Create RoadV2 ───
            geo::RoadV2 road;
            road.id = QString("osm_way_%1").arg(wayId).toStdString();
            QString defaultName = (params.mode == Mode::Railway)
                ? QString("Rail_%1").arg(roadCounter)
                : QString("Road_%1").arg(roadCounter);
            road.name = way.name().isEmpty() ? defaultName.toStdString()
                                              : way.name().toStdString();

            // Set road metadata from OSM tags + class defaults
            RoadClassInfo info = classInfo;

            // Override with explicit OSM tags
            int osmLanes = way.lanes();
            double osmWidth = way.width();
            double osmSpeed = way.maxspeed();

            if (osmLanes > 0) road.laneCount = osmLanes;
            else road.laneCount = info.defaultLanes;

            if (osmWidth > 0) road.width = osmWidth;
            else road.width = info.defaultLanes * info.defaultLaneWidth;

            // One-way handling
            if (way.isOneWay()) {
                road.laneCount = std::max(1, road.laneCount);
            }

            // Color based on class or railway type
            road.color = (params.mode == Mode::Railway)
                ? railColorForType(way.railwayType())
                : roadColorForClass(info.cls);

            // Profile name
            road.profileName = info.osmValue.toStdString();

            // ─── Add line segments ───
            road.reserveSegments(simplifiedPoints.size() - 1);
            for (size_t i = 1; i < simplifiedPoints.size(); i++) {
                double segLen = simplifiedPoints[i-1].distanceTo(simplifiedPoints[i]);
                if (segLen < params.minSegmentLength) continue;

                road.addSegment<geo::LineSegment>(
                    simplifiedPoints[i-1], simplifiedPoints[i]);
                result.segmentsCreated++;
            }

            if (road.numSegments() == 0) {
                result.waysSkipped++;
                continue;
            }

            // ─── Create lane section ───
            if (params.mode == Mode::Railway) {
                // Rail: simple single-lane section (one track)
                geo::LaneSection laneSection;
                geo::Lane lane;
                lane.id = 1;
                lane.type = geo::LaneType::Driving;
                lane.width = geo::Polynomial3(road.width);
                laneSection.addLane(lane);
                road.addLaneSection(laneSection);
            } else {
                // Road: use LaneGenerator for proper lane configuration
                geo::LaneSection laneSection;
                LaneGenerationResult laneResult = LaneGenerator::generate(
                    way, info, laneSection);
                road.laneCount = laneResult.totalLanes;
                if (way.width() <= 0) {
                    road.width = LaneGenerator::estimateRoadWidth(
                        laneResult, info.defaultLaneWidth);
                }
                road.addLaneSection(laneSection);
            }

            // Store OSM metadata in a side map (not in RoadV2 directly)
            // This will be used by the inspector and validator
            roadMetadata[road.id] = {
                wayId,
                params.mode == Mode::Railway ? way.railwayType() : way.highwayType(),
                way.name(),
                way.ref(),
                way.isOneWay(),
                way.isBridge(),
                way.isTunnel(),
                way.layer(),
                way.surface(),
                way.maxspeed(),
                way.tag("sidewalk"),
                way.tag("shoulder"),
                way.tag("median"),
                way.tag("lit"),
                way.tag("cycleway"),
                way.tag("turn:lanes"),
                way.tag("turn:lanes:forward"),
                way.tag("turn:lanes:backward"),
                way.tag("change:lanes"),
                way.tag("access"),
                way.tag("vehicle")
            };

            result.roads.push_back(std::move(road));
            roadCounter++;
            result.roadsCreated++;
        }

        const char* label = (params.mode == Mode::Railway) ? "RailNetworkBuilder" : "RoadNetworkBuilder";
        qDebug() << "[" << label << "] Created" << result.roadsCreated << "tracks/roads,"
                 << result.segmentsCreated << "segments,"
                 << result.junctionsDetected << "junctions,"
                 << result.endPointsDetected << "endpoints,"
                 << result.waysSkipped << "ways skipped";

        return result;
    }

    // ─── Road metadata (OSM tags preserved for inspection) ───
    struct RoadMetadata {
        qint64 osmWayId = 0;
        QString highwayType;
        QString name;
        QString ref;
        bool isOneWay = false;
        bool isBridge = false;
        bool isTunnel = false;
        QString layer;
        QString surface;
        double maxspeed = -1;
        QString sidewalk;
        QString shoulder;
        QString median;
        QString lit;
        QString cycleway;
        QString turnLanes;
        QString turnLanesForward;
        QString turnLanesBackward;
        QString changeLanes;
        QString access;
        QString vehicle;
    };

    // Static map of road metadata (persists after build)
    // Keyed by road id (string)
    static inline std::unordered_map<std::string, RoadMetadata> roadMetadata;

    // Get metadata for a road
    static const RoadMetadata* getMetadata(const std::string& roadId) {
        auto it = roadMetadata.find(roadId);
        return it != roadMetadata.end() ? &it->second : nullptr;
    }

private:
    // ─── Douglas-Peucker simplification with junction preservation ───
    static void simplifyPolyline(const std::vector<geo::Point2D>& points,
                                  const std::vector<qint64>& pointNodeIds,
                                  const std::unordered_map<qint64, std::vector<QString>>& nodeToRoads,
                                  const Params& params,
                                  std::vector<size_t>& outKeep)
    {
        if (points.size() <= 2) {
            outKeep = {0, points.size() - 1};
            return;
        }

        // Mark points that must be preserved (junctions, endpoints)
        std::vector<bool> mustKeep(points.size(), false);
        mustKeep[0] = true;
        mustKeep[points.size() - 1] = true;

        if (params.preserveJunctionNodes) {
            for (size_t i = 1; i < points.size() - 1; i++) {
                auto it = nodeToRoads.find(pointNodeIds[i]);
                if (it != nodeToRoads.end() && it->second.size() >= 3) {
                    mustKeep[i] = true;
                }
                // Also keep endpoints of ways (2-road nodes where one road ends)
                if (it != nodeToRoads.end() && it->second.size() >= 2) {
                    // Keep it — it's a potential junction
                    mustKeep[i] = true;
                }
            }
        }

        // Douglas-Peucker with forced keep points
        outKeep.clear();
        douglasPeucker(points, mustKeep, 0, points.size() - 1,
                        params.simplifyTolerance, outKeep);

        // Sort and deduplicate
        std::sort(outKeep.begin(), outKeep.end());
        outKeep.erase(std::unique(outKeep.begin(), outKeep.end()), outKeep.end());
    }

    // Recursive Douglas-Peucker
    static void douglasPeucker(const std::vector<geo::Point2D>& points,
                                const std::vector<bool>& mustKeep,
                                size_t start, size_t end,
                                double tolerance,
                                std::vector<size_t>& outKeep)
    {
        outKeep.push_back(start);
        outKeep.push_back(end);

        if (end - start <= 1) return;

        // Find point with maximum distance from the line start→end
        double maxDist = 0;
        size_t maxIdx = start;

        double dx = points[end].x - points[start].x;
        double dy = points[end].y - points[start].y;
        double segLen = std::sqrt(dx*dx + dy*dy);
        if (segLen < 1e-12) segLen = 1e-12;

        for (size_t i = start + 1; i < end; i++) {
            // If this point must be kept, always include it
            if (mustKeep[i]) {
                maxDist = tolerance + 1;  // Force inclusion
                maxIdx = i;
                break;
            }

            // Perpendicular distance from point to line
            double px = points[i].x - points[start].x;
            double py = points[i].y - points[start].y;
            double dist = std::abs(dx * py - dy * px) / segLen;

            if (dist > maxDist) {
                maxDist = dist;
                maxIdx = i;
            }
        }

        // If the max distance exceeds tolerance, recurse
        if (maxDist > tolerance) {
            std::vector<size_t> leftKeep, rightKeep;
            douglasPeucker(points, mustKeep, start, maxIdx, tolerance, leftKeep);
            douglasPeucker(points, mustKeep, maxIdx, end, tolerance, rightKeep);

            // Merge (remove duplicate maxIdx)
            outKeep = leftKeep;
            if (!rightKeep.empty()) {
                outKeep.insert(outKeep.end(), rightKeep.begin() + 1, rightKeep.end());
            }
        }
    }

    // ─── Color by road class ───
    static std::string roadColorForClass(RoadClass cls) {
        switch (cls) {
        case RoadClass::Motorway:    return "#e892a2";  // pink-red
        case RoadClass::Trunk:       return "#f9b29c";  // orange
        case RoadClass::Primary:     return "#fcd6a4";  // yellow-orange
        case RoadClass::Secondary:   return "#f7fabf";  // yellow
        case RoadClass::Tertiary:    return "#ffffff";  // white
        case RoadClass::Residential: return "#aaaaaa";  // gray
        case RoadClass::Service:     return "#888888";  // dark gray
        case RoadClass::LivingStreet:return "#cccccc";  // light gray
        case RoadClass::Unclassified:return "#dddddd";  // very light gray
        case RoadClass::Track:       return "#996633";  // brown
        default:                     return "#888888";
        }
    }

    // ─── Color for railway type ───
    static std::string railColorForType(const QString& type) {
        if (type == "rail")          return "#aa6622";  // brown
        if (type == "tram")          return "#e94e1b";  // red-orange
        if (type == "subway")        return "#1a73e8";  // blue
        if (type == "light_rail")    return "#34a853";  // green
        if (type == "monorail")      return "#9c27b0";  // purple
        if (type == "narrow_gauge")  return "#795548";  // dark brown
        return "#aa6622";  // default brown
    }
};

} // namespace osm
