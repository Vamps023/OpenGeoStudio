#pragma once

// ============================================================
// JunctionDetector — Detect intersections from road topology
// ============================================================
//
// Detects junctions from the road network by analyzing shared
// nodes between roads. Uses the existing road engine's
// intersection generation for geometry.
//
// Junction types detected:
//   - T junction (3 roads)
//   - X intersection (4 roads)
//   - Y intersection (3 roads, acute angle)
//   - Multi-way (5+ roads)
//   - Roundabout (junction=roundabout in OSM)
//   - Overpass (roads cross at different layers)
//

#include "OsmTypes.hpp"
#include "RoadNetworkBuilder.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"

#include <QString>
#include <QList>
#include <QDebug>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace osm {

// ─── JunctionType ───
enum class JunctionType {
    T_Junction,       // 3 roads, one perpendicular
    X_Intersection,   // 4 roads, roughly perpendicular
    Y_Junction,       // 3 roads, acute angle
    MultiWay,         // 5+ roads
    Roundabout,       // circular
    Overpass,         // roads cross at different layers
    Staggered,        // two T-junctions close together
    Unknown
};

// ─── DetectedJunction ───
struct DetectedJunction {
    QString id;
    qint64 osmNodeId = 0;
    JunctionType type = JunctionType::Unknown;
    geo::Point2D center;
    double z = 0.0;
    std::vector<QString> roadIds;       // roads meeting at this junction
    std::vector<double> roadHeadings;   // heading of each road at junction
    bool isRoundabout = false;
    bool isBridge = false;
    bool isTunnel = false;
    int layer = 0;

    QString typeString() const {
        switch (type) {
        case JunctionType::T_Junction:     return "T-Junction";
        case JunctionType::X_Intersection: return "X-Intersection";
        case JunctionType::Y_Junction:     return "Y-Junction";
        case JunctionType::MultiWay:       return "Multi-Way";
        case JunctionType::Roundabout:     return "Roundabout";
        case JunctionType::Overpass:       return "Overpass";
        case JunctionType::Staggered:      return "Staggered";
        default:                           return "Unknown";
        }
    }
};

// ─── JunctionDetector ───
class JunctionDetector {
public:
    struct Params {
        double mergeTolerance = 5.0;        // merge junctions within this distance
        double overpassLayerDiff = 1;       // layer difference for overpass
        double staggeredDistance = 15.0;    // max distance for staggered junction
        double minAngle = 15.0;             // degrees — minimum angle between roads
    };

    // Detect junctions from the road network
    static std::vector<DetectedJunction> detect(
        const RoadNetworkBuilder::Result& network,
        const OsmData& osm,
        const Params& params = {})
    {
        std::vector<DetectedJunction> junctions;

        // ─── Step 1: Find junction nodes (3+ roads) ───
        for (qint64 nodeId : network.junctionNodeIds) {
            auto it = network.nodes.find(nodeId);
            if (it == network.nodes.end()) continue;

            const NetworkNode& nn = it->second;
            if (nn.roadIds.size() < 3) continue;

            DetectedJunction j;
            j.id = QString("junction_%1").arg(nodeId);
            j.osmNodeId = nodeId;
            j.center = geo::Point2D(nn.x, nn.y);
            j.z = nn.z;
            j.roadIds = nn.roadIds;

            // Check if this is a roundabout
            for (const auto& rid : nn.roadIds) {
                // Extract OSM way ID from road ID
                QString roadIdStr = QString::fromStdString(rid.toStdString());
                if (roadIdStr.startsWith("osm_way_")) {
                    qint64 wayId = roadIdStr.mid(8).toLongLong();
                    const Way* way = osm.getWay(wayId);
                    if (way && way->isRoundabout()) {
                        j.isRoundabout = true;
                    }
                    if (way && way->isBridge()) j.isBridge = true;
                    if (way && way->isTunnel()) j.isTunnel = true;
                    if (way) {
                        QString layer = way->layer();
                        if (!layer.isEmpty()) j.layer = layer.toInt();
                    }
                }
            }

            // Compute headings of each road at the junction
            for (const auto& rid : nn.roadIds) {
                double heading = computeRoadHeadingAtNode(network, rid, nodeId);
                j.roadHeadings.push_back(heading);
            }

            // Classify junction type
            j.type = classifyJunction(j, params);

            junctions.push_back(j);
        }

        // ─── Step 2: Check for overpasses (2-road nodes at same position, different layers) ───
        // This is handled by checking if any 2-road nodes have roads on different layers
        for (qint64 nodeId : network.endPointNodeIds) {
            auto it = network.nodes.find(nodeId);
            if (it == network.nodes.end()) continue;
            // Endpoints are not junctions — skip
        }

        // Also check 2-road nodes that might be overpasses
        for (const auto& [nid, nn] : network.nodes) {
            if (nn.roadIds.size() != 2) continue;

            // Check if the two roads cross at different layers
            bool differentLayers = false;
            int layers[2] = {0, 0};
            for (int i = 0; i < 2; i++) {
                QString roadIdStr = QString::fromStdString(nn.roadIds[i].toStdString());
                if (roadIdStr.startsWith("osm_way_")) {
                    qint64 wayId = roadIdStr.mid(8).toLongLong();
                    const Way* way = osm.getWay(wayId);
                    if (way) {
                        QString l = way->layer();
                        layers[i] = l.isEmpty() ? 0 : l.toInt();
                    }
                }
            }
            if (std::abs(layers[0] - layers[1]) >= params.overpassLayerDiff) {
                differentLayers = true;
            }

            if (differentLayers) {
                DetectedJunction j;
                j.id = QString("overpass_%1").arg(nid);
                j.osmNodeId = nid;
                j.center = geo::Point2D(nn.x, nn.y);
                j.z = nn.z;
                j.roadIds = nn.roadIds;
                j.type = JunctionType::Overpass;
                j.layer = std::max(layers[0], layers[1]);
                junctions.push_back(j);
            }
        }

        // ─── Step 3: Merge nearby junctions (staggered intersections) ───
        if (params.mergeTolerance > 0) {
            mergeNearbyJunctions(junctions, params.mergeTolerance);
        }

        qDebug() << "[JunctionDetector] Detected" << junctions.size() << "junctions";
        return junctions;
    }

private:
    // Compute the heading of a road at a specific node
    static double computeRoadHeadingAtNode(const RoadNetworkBuilder::Result& network,
                                            const QString& roadId,
                                            qint64 nodeId)
    {
        // Find the road
        std::string rid = roadId.toStdString();
        const geo::RoadV2* road = nullptr;
        for (const auto& r : network.roads) {
            if (r.id == rid) { road = &r; break; }
        }
        if (!road || road->numSegments() == 0) return 0;

        // Find the position of this node along the road
        auto nodeIt = network.nodes.find(nodeId);
        if (nodeIt == network.nodes.end()) return 0;
        geo::Point2D nodePos(nodeIt->second.x, nodeIt->second.y);

        // Sample the road to find the closest point
        double totalLen = road->totalLength();
        double bestS = 0;
        double bestDist = 1e18;

        for (int i = 0; i <= 100; i++) {
            double s = totalLen * i / 100.0;
            geo::Point2D p = road->geometry().positionAt(s);
            double d = p.distanceTo(nodePos);
            if (d < bestDist) { bestDist = d; bestS = s; }
        }

        // Get heading at that point
        return road->geometry().headingAt(bestS);
    }

    // Classify junction type based on road count and headings
    static JunctionType classifyJunction(const DetectedJunction& j, const Params& params) {
        if (j.isRoundabout) return JunctionType::Roundabout;

        int n = int(j.roadIds.size());

        if (n >= 5) return JunctionType::MultiWay;

        if (n == 4) {
            // Check if it's roughly perpendicular (X) or not
            // For 4 roads, check if opposite pairs have ~180° difference
            if (j.roadHeadings.size() >= 4) {
                // Sort headings
                auto headings = j.roadHeadings;
                std::sort(headings.begin(), headings.end());

                // Check if it's roughly X-shaped
                double diff1 = angleDiff(headings[0], headings[2]);
                double diff2 = angleDiff(headings[1], headings[3]);
                if (std::abs(diff1 - 180) < 30 && std::abs(diff2 - 180) < 30) {
                    return JunctionType::X_Intersection;
                }
            }
            return JunctionType::X_Intersection;
        }

        if (n == 3) {
            // T vs Y: check angles
            if (j.roadHeadings.size() >= 3) {
                // T-junction: one road is roughly perpendicular to the other two
                // Y-junction: all roads at acute angles
                auto headings = j.roadHeadings;
                std::sort(headings.begin(), headings.end());

                // Check for T shape: two roads roughly opposite, one perpendicular
                double diff01 = angleDiff(headings[0], headings[1]);
                double diff12 = angleDiff(headings[1], headings[2]);
                double diff02 = angleDiff(headings[0], headings[2]);

                // If two roads are roughly opposite (~180°) and third is ~90°
                if (std::abs(diff01 - 180) < 30 || std::abs(diff12 - 180) < 30 ||
                    std::abs(diff02 - 180) < 30) {
                    return JunctionType::T_Junction;
                }
            }
            return JunctionType::Y_Junction;
        }

        return JunctionType::Unknown;
    }

    // Angular difference (0-180)
    static double angleDiff(double a1, double a2) {
        double diff = std::abs(a1 - a2);
        while (diff > 360) diff -= 360;
        if (diff > 180) diff = 360 - diff;
        return diff;
    }

    // Merge junctions that are very close together
    static void mergeNearbyJunctions(std::vector<DetectedJunction>& junctions, double tolerance) {
        std::vector<bool> merged(junctions.size(), false);

        for (size_t i = 0; i < junctions.size(); i++) {
            if (merged[i]) continue;
            for (size_t j = i + 1; j < junctions.size(); j++) {
                if (merged[j]) continue;
                double dist = junctions[i].center.distanceTo(junctions[j].center);
                if (dist < tolerance) {
                    // Merge j into i
                    for (const auto& rid : junctions[j].roadIds) {
                        if (std::find(junctions[i].roadIds.begin(),
                                      junctions[i].roadIds.end(), rid) ==
                            junctions[i].roadIds.end()) {
                            junctions[i].roadIds.push_back(rid);
                        }
                    }
                    merged[j] = true;
                }
            }
        }

        // Remove merged junctions
        std::vector<DetectedJunction> result;
        for (size_t i = 0; i < junctions.size(); i++) {
            if (!merged[i]) result.push_back(std::move(junctions[i]));
        }
        junctions = std::move(result);
    }
};

} // namespace osm
