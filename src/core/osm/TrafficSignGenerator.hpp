#pragma once

// ============================================================
// TrafficSignGenerator — Generate traffic signs from OSM data
// ============================================================
//
// Generates traffic signs, signals, and roadside markers:
//   - Stop signs
//   - Yield signs
//   - Speed limit signs
//   - Traffic signals
//   - Pedestrian crossing signs
//   - Turn restriction signs
//   - Roundabout signs
//   - Mileage/distance signs
//
// Signs are placed as parametric objects with position,
// orientation, and type — not baked into the mesh.
//

#include "OsmTypes.hpp"
#include "RoadNetworkBuilder.hpp"
#include "JunctionDetector.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include "../logger/Logger.hpp"
#include <vector>
#include <cmath>

namespace osm {

// ─── SignType ───
enum class SignType {
    Stop,
    Yield,
    SpeedLimit,
    SpeedLimitEnd,
    TrafficSignal,
    PedestrianCrossing,
    NoEntry,
    OneWay,
    TurnRestriction,
    RoundaboutAhead,
    Roundabout,
    LaneEnds,
    Merge,
    SlipperyRoad,
    SteepHill,
    CurveWarning,
    NarrowBridge,
    HeightLimit,
    WeightLimit,
    NoParking,
    NoStopping,
    BusStop,
    TaxiStand,
    PedestrianSignal,
    BicycleCrossing,
    SchoolZone,
    Hospital,
    FuelStation,
    Parking,
    Custom
};

// ─── TrafficSign ───
struct TrafficSign {
    SignType type = SignType::Stop;
    QString roadId;
    qint64 osmNodeId = 0;       // OSM node this sign is attached to
    double sPosition = 0.0;     // position along road (meters)
    double lateralOffset = 0.0; // offset from road center (meters, positive=right)
    double heading = 0.0;       // facing direction (radians)
    double height = 2.5;        // sign height above road (meters)
    QString text;               // text on sign (e.g., speed limit value)
    QString value;              // associated value (e.g., "50" for 50km/h)
    QString side = "right";     // "left" or "right" side of road

    QJsonObject toJson() const {
        QJsonObject j;
        j["type"] = int(type);
        j["roadId"] = roadId;
        j["osmNodeId"] = qint64(osmNodeId);
        j["sPosition"] = sPosition;
        j["lateralOffset"] = lateralOffset;
        j["heading"] = heading;
        j["height"] = height;
        j["text"] = text;
        j["value"] = value;
        j["side"] = side;
        return j;
    }

    static TrafficSign fromJson(const QJsonObject& j) {
        TrafficSign s;
        s.type = SignType(j["type"].toInt());
        s.roadId = j["roadId"].toString();
        s.osmNodeId = j["osmNodeId"].toVariant().toLongLong();
        s.sPosition = j["sPosition"].toDouble();
        s.lateralOffset = j["lateralOffset"].toDouble();
        s.heading = j["heading"].toDouble();
        s.height = j["height"].toDouble();
        s.text = j["text"].toString();
        s.value = j["value"].toString();
        s.side = j["side"].toString();
        return s;
    }

    QString typeString() const {
        switch (type) {
        case SignType::Stop:                return "Stop";
        case SignType::Yield:               return "Yield";
        case SignType::SpeedLimit:          return "SpeedLimit";
        case SignType::SpeedLimitEnd:       return "SpeedLimitEnd";
        case SignType::TrafficSignal:       return "TrafficSignal";
        case SignType::PedestrianCrossing:  return "PedestrianCrossing";
        case SignType::NoEntry:             return "NoEntry";
        case SignType::OneWay:              return "OneWay";
        case SignType::TurnRestriction:     return "TurnRestriction";
        case SignType::RoundaboutAhead:     return "RoundaboutAhead";
        case SignType::Roundabout:          return "Roundabout";
        case SignType::LaneEnds:            return "LaneEnds";
        case SignType::Merge:               return "Merge";
        case SignType::SlipperyRoad:        return "SlipperyRoad";
        case SignType::SteepHill:           return "SteepHill";
        case SignType::CurveWarning:        return "CurveWarning";
        case SignType::NarrowBridge:        return "NarrowBridge";
        case SignType::HeightLimit:         return "HeightLimit";
        case SignType::WeightLimit:         return "WeightLimit";
        case SignType::NoParking:           return "NoParking";
        case SignType::NoStopping:          return "NoStopping";
        case SignType::BusStop:             return "BusStop";
        case SignType::TaxiStand:           return "TaxiStand";
        case SignType::PedestrianSignal:    return "PedestrianSignal";
        case SignType::BicycleCrossing:     return "BicycleCrossing";
        case SignType::SchoolZone:          return "SchoolZone";
        case SignType::Hospital:            return "Hospital";
        case SignType::FuelStation:         return "FuelStation";
        case SignType::Parking:             return "Parking";
        case SignType::Custom:              return "Custom";
        }
        return "Unknown";
    }
};

// ─── TrafficSignGenerator ───
class TrafficSignGenerator {
public:
    struct Params {
        double signOffset = 2.0;     // lateral offset from road edge (meters)
        double signHeight = 2.5;     // height above road surface (meters)
        double signalHeight = 4.0;   // traffic signal height
        double stopSignDistance = 5.0; // distance before junction
    };

    // Generate signs from OSM node tags
    static std::vector<TrafficSign> generateFromOsmNodes(
        const OsmData& osm,
        const RoadNetworkBuilder::Result& network,
        const Params& params = {})
    {
        std::vector<TrafficSign> signs;

        for (const auto& [nodeId, node] : osm.nodes) {
            // Check for traffic sign tags
            QString highway = node.tag("highway");
            QString trafficSign = node.tag("traffic_sign");
            QString crossing = node.tag("crossing");

            TrafficSign sign;
            sign.osmNodeId = nodeId;
            sign.height = params.signHeight;
            sign.side = "right";

            // Find which road this node belongs to
            auto nodeIt = network.nodes.find(nodeId);
            if (nodeIt != network.nodes.end()) {
                if (!nodeIt->second.roadIds.empty()) {
                    sign.roadId = nodeIt->second.roadIds[0];
                }
                sign.lateralOffset = params.signOffset;
            }

            bool isSign = false;

            // Traffic signals
            if (highway == "traffic_signals") {
                sign.type = SignType::TrafficSignal;
                sign.height = params.signalHeight;
                isSign = true;
            }
            // Stop signs
            else if (highway == "stop") {
                sign.type = SignType::Stop;
                isSign = true;
            }
            // Give way / yield
            else if (highway == "give_way") {
                sign.type = SignType::Yield;
                isSign = true;
            }
            // Speed limit signs
            else if (highway == "speed_camera" || highway == "enforcement") {
                sign.type = SignType::SpeedLimit;
                QString maxspeed = node.tag("maxspeed");
                if (!maxspeed.isEmpty()) {
                    sign.value = maxspeed;
                    sign.text = "Speed Limit " + maxspeed;
                }
                isSign = true;
            }
            // Pedestrian crossing
            else if (highway == "crossing" || crossing == "marked") {
                sign.type = SignType::PedestrianCrossing;
                isSign = true;
            }
            // Bus stop
            else if (highway == "bus_stop") {
                sign.type = SignType::BusStop;
                sign.side = "right";
                isSign = true;
            }
            // Traffic sign tag
            else if (!trafficSign.isEmpty()) {
                if (trafficSign.contains("stop", Qt::CaseInsensitive)) {
                    sign.type = SignType::Stop;
                } else if (trafficSign.contains("yield", Qt::CaseInsensitive) ||
                           trafficSign.contains("give_way", Qt::CaseInsensitive)) {
                    sign.type = SignType::Yield;
                } else if (trafficSign.contains("speed_limit", Qt::CaseInsensitive) ||
                           trafficSign.contains("maxspeed", Qt::CaseInsensitive)) {
                    sign.type = SignType::SpeedLimit;
                    sign.value = trafficSign;
                } else if (trafficSign.contains("no_parking", Qt::CaseInsensitive)) {
                    sign.type = SignType::NoParking;
                } else if (trafficSign.contains("no_stopping", Qt::CaseInsensitive)) {
                    sign.type = SignType::NoStopping;
                } else {
                    sign.type = SignType::Custom;
                    sign.text = trafficSign;
                }
                isSign = true;
            }

            if (isSign) {
                // Compute s-position on the road
                if (!sign.roadId.isEmpty()) {
                    sign.sPosition = computeSPosition(network, sign.roadId, node);
                }
                signs.push_back(sign);
            }
        }

        appLog().info("[TrafficSignGenerator] Generated", signs.size(), "signs from OSM nodes");
        return signs;
    }

    // Generate signs from road tags (speed limits, etc.)
    static std::vector<TrafficSign> generateFromRoadTags(
        const RoadNetworkBuilder::Result& network,
        const Params& params = {})
    {
        std::vector<TrafficSign> signs;

        for (const auto& road : network.roads) {
            QString roadId = QString::fromStdString(road.id);
            const RoadNetworkBuilder::RoadMetadata* meta =
                RoadNetworkBuilder::getMetadata(road.id);

            if (!meta) continue;

            // Speed limit sign at start of road
            if (meta->maxspeed > 0) {
                TrafficSign speedSign;
                speedSign.type = SignType::SpeedLimit;
                speedSign.roadId = roadId;
                speedSign.sPosition = 5.0;  // 5m from start
                speedSign.lateralOffset = params.signOffset;
                speedSign.height = params.signHeight;
                speedSign.value = QString::number(meta->maxspeed);
                speedSign.text = "Speed Limit " + QString::number(meta->maxspeed);
                speedSign.side = "right";
                signs.push_back(speedSign);
            }

            // One-way sign
            if (meta->isOneWay) {
                TrafficSign oneWaySign;
                oneWaySign.type = SignType::OneWay;
                oneWaySign.roadId = roadId;
                oneWaySign.sPosition = 3.0;
                oneWaySign.lateralOffset = params.signOffset;
                oneWaySign.height = params.signHeight;
                oneWaySign.side = "right";
                signs.push_back(oneWaySign);
            }
        }

        return signs;
    }

    // Generate signs at junctions (stop/yield at intersections)
    static std::vector<TrafficSign> generateAtJunctions(
        const std::vector<DetectedJunction>& junctions,
        const RoadNetworkBuilder::Result& network,
        const Params& params = {})
    {
        std::vector<TrafficSign> signs;

        for (const auto& j : junctions) {
            if (j.type == JunctionType::Overpass) continue;
            if (j.type == JunctionType::Roundabout) {
                // Roundabout sign on each approach
                for (const auto& roadId : j.roadIds) {
                    QString rid = QString::fromStdString(roadId.toStdString());
                    const geo::RoadV2* road = nullptr;
                    for (const auto& r : network.roads) {
                        if (r.id == roadId.toStdString()) { road = &r; break; }
                    }
                    if (!road) continue;

                    // Find junction position on road
                    double totalLen = road->totalLength();
                    double bestS = 0, bestDist = 1e18;
                    for (int i = 0; i <= 50; i++) {
                        double s = totalLen * i / 50.0;
                        geo::Point2D p = road->geometry().positionAt(s);
                        double d = p.distanceTo(j.center);
                        if (d < bestDist) { bestDist = d; bestS = s; }
                    }

                    TrafficSign rbSign;
                    rbSign.type = SignType::RoundaboutAhead;
                    rbSign.roadId = rid;
                    rbSign.sPosition = std::max(0.0, bestS - 20.0);
                    rbSign.lateralOffset = params.signOffset;
                    rbSign.height = params.signHeight;
                    rbSign.side = "right";
                    signs.push_back(rbSign);

                    // Yield sign at roundabout entry
                    TrafficSign yieldSign;
                    yieldSign.type = SignType::Yield;
                    yieldSign.roadId = rid;
                    yieldSign.sPosition = std::max(0.0, bestS - params.stopSignDistance);
                    yieldSign.lateralOffset = params.signOffset;
                    yieldSign.height = params.signHeight;
                    yieldSign.side = "right";
                    signs.push_back(yieldSign);
                }
            } else {
                // Stop signs at T and X junctions
                // Place stop sign on each approach road
                for (const auto& roadId : j.roadIds) {
                    QString rid = QString::fromStdString(roadId.toStdString());
                    const geo::RoadV2* road = nullptr;
                    for (const auto& r : network.roads) {
                        if (r.id == roadId.toStdString()) { road = &r; break; }
                    }
                    if (!road) continue;

                    double totalLen = road->totalLength();
                    double bestS = 0, bestDist = 1e18;
                    for (int i = 0; i <= 50; i++) {
                        double s = totalLen * i / 50.0;
                        geo::Point2D p = road->geometry().positionAt(s);
                        double d = p.distanceTo(j.center);
                        if (d < bestDist) { bestDist = d; bestS = s; }
                    }

                    TrafficSign stopSign;
                    stopSign.type = SignType::Stop;
                    stopSign.roadId = rid;
                    stopSign.sPosition = std::max(0.0, bestS - params.stopSignDistance);
                    stopSign.lateralOffset = params.signOffset;
                    stopSign.height = params.signHeight;
                    stopSign.side = "right";
                    signs.push_back(stopSign);
                }
            }
        }

        return signs;
    }

    // Generate all signs
    static std::vector<TrafficSign> generateAll(
        const OsmData& osm,
        const RoadNetworkBuilder::Result& network,
        const std::vector<DetectedJunction>& junctions,
        const Params& params = {})
    {
        std::vector<TrafficSign> signs;

        auto fromNodes = generateFromOsmNodes(osm, network, params);
        auto fromRoads = generateFromRoadTags(network, params);
        auto fromJunctions = generateAtJunctions(junctions, network, params);

        signs.insert(signs.end(), fromNodes.begin(), fromNodes.end());
        signs.insert(signs.end(), fromRoads.begin(), fromRoads.end());
        signs.insert(signs.end(), fromJunctions.begin(), fromJunctions.end());

        appLog().info("[TrafficSignGenerator] Total signs:", signs.size());
        return signs;
    }

    // ─── Serialize ───
    static QJsonArray toJsonArray(const std::vector<TrafficSign>& signs) {
        QJsonArray arr;
        for (const auto& s : signs) arr.append(s.toJson());
        return arr;
    }

    static std::vector<TrafficSign> fromJsonArray(const QJsonArray& arr) {
        std::vector<TrafficSign> signs;
        for (const auto& v : arr) {
            signs.push_back(TrafficSign::fromJson(v.toObject()));
        }
        return signs;
    }

private:
    static double computeSPosition(const RoadNetworkBuilder::Result& network,
                                    const QString& roadId,
                                    const Node& node)
    {
        const geo::RoadV2* road = nullptr;
        for (const auto& r : network.roads) {
            if (r.id == roadId.toStdString()) { road = &r; break; }
        }
        if (!road || road->numSegments() == 0) return 0;

        auto nodeIt = network.nodes.find(node.id);
        if (nodeIt == network.nodes.end()) return 0;

        geo::Point2D nodePos(nodeIt->second.x, nodeIt->second.y);
        double totalLen = road->totalLength();
        double bestS = 0, bestDist = 1e18;

        for (int i = 0; i <= 100; i++) {
            double s = totalLen * i / 100.0;
            geo::Point2D p = road->geometry().positionAt(s);
            double d = p.distanceTo(nodePos);
            if (d < bestDist) { bestDist = d; bestS = s; }
        }

        return bestS;
    }
};

} // namespace osm
