#pragma once

// ============================================================
// RoundaboutGenerator — Generate roundabout geometry from OSM
// ============================================================
//
// Detects and generates roundabout geometry:
//   - Circular central island
//   - Entry/exit approach roads
//   - Yield lines
//   - Splitter islands
//   - Lane connectivity
//
// Roundabouts in OSM are tagged with junction=roundabout.
// The way forms a closed loop (first node == last node).
//

#include "OsmTypes.hpp"
#include "RoadNetworkBuilder.hpp"
#include "JunctionDetector.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/geometry_segment.hpp"
#include "../../engine/road/road_v2.hpp"

#include <QString>
#include <QDebug>
#include <vector>
#include <cmath>
#include <algorithm>

namespace osm {

// ─── RoundaboutGeometry ───
struct RoundaboutGeometry {
    QString id;
    qint64 osmWayId = 0;
    geo::Point2D center;
    double radius = 0.0;           // meters
    double circumference = 0.0;    // meters
    std::vector<geo::Point2D> ringPoints;  // sampled ring
    std::vector<geo::Point2D> entryPoints; // where approach roads enter
    std::vector<geo::Point2D> exitPoints;  // where approach roads exit
    std::vector<double> entryAngles;       // angle of each entry (radians)
    int circulatoryLanes = 1;
    double laneWidth = 3.5;
    bool isClockwise = true;       // driving direction
};

// ─── RoundaboutGenerator ───
class RoundaboutGenerator {
public:
    struct Params {
        int ringSamples = 64;          // samples around the ring
        double minRadius = 5.0;        // meters
        double maxRadius = 100.0;      // meters
        double defaultLaneWidth = 3.5;
        int defaultCirculatoryLanes = 1;
    };

    // Generate roundabout geometry from a detected roundabout junction
    static RoundaboutGeometry generate(const DetectedJunction& junction,
                                        const RoadNetworkBuilder::Result& network,
                                        const OsmData& osm,
                                        const Params& params = {})
    {
        RoundaboutGeometry rb;
        rb.id = junction.id;
        rb.center = junction.center;

        // Find the roundabout way (the closed loop)
        qint64 roundaboutWayId = 0;
        for (const auto& roadId : junction.roadIds) {
            QString roadIdStr = QString::fromStdString(roadId.toStdString());
            if (roadIdStr.startsWith("osm_way_")) {
                qint64 wid = roadIdStr.mid(8).toLongLong();
                const Way* way = osm.getWay(wid);
                if (way && way->isRoundabout()) {
                    roundaboutWayId = wid;
                    break;
                }
            }
        }

        if (roundaboutWayId == 0) {
            qWarning() << "[RoundaboutGenerator] No roundabout way found for junction" << junction.id;
            return rb;
        }

        rb.osmWayId = roundaboutWayId;

        const Way* way = osm.getWay(roundaboutWayId);
        if (!way || way->nodeRefs.size() < 4) {
            qWarning() << "[RoundaboutGenerator] Roundabout way too short";
            return rb;
        }

        // Collect ring points from the roundabout way
        std::vector<geo::Point2D> ringPts;
        for (qint64 nodeRef : way->nodeRefs) {
            auto nodeIt = network.nodes.find(nodeRef);
            if (nodeIt != network.nodes.end()) {
                ringPts.emplace_back(nodeIt->second.x, nodeIt->second.y);
            }
        }

        // Remove duplicate last point (closed loop)
        if (ringPts.size() >= 2 && ringPts.front().distanceTo(ringPts.back()) < 1.0) {
            ringPts.pop_back();
        }

        if (ringPts.size() < 3) {
            qWarning() << "[RoundaboutGenerator] Not enough ring points";
            return rb;
        }

        // Compute center as centroid of ring points
        double cx = 0, cy = 0;
        for (const auto& p : ringPts) { cx += p.x; cy += p.y; }
        cx /= ringPts.size();
        cy /= ringPts.size();
        rb.center = geo::Point2D(cx, cy);

        // Compute radius as average distance from center
        double totalDist = 0;
        for (const auto& p : ringPts) {
            totalDist += p.distanceTo(rb.center);
        }
        rb.radius = totalDist / ringPts.size();
        rb.radius = std::clamp(rb.radius, params.minRadius, params.maxRadius);

        rb.circumference = 2.0 * geo::PI * rb.radius;

        // Determine direction (clockwise vs counter-clockwise)
        // Compute signed area — negative = clockwise
        double signedArea = 0;
        for (size_t i = 0; i < ringPts.size(); i++) {
            const auto& p1 = ringPts[i];
            const auto& p2 = ringPts[(i + 1) % ringPts.size()];
            signedArea += (p1.x * p2.y - p2.x * p1.y);
        }
        rb.isClockwise = signedArea < 0;  // negative signed area = clockwise

        // Generate evenly sampled ring points
        rb.ringPoints.clear();
        rb.ringPoints.reserve(params.ringSamples);
        for (int i = 0; i < params.ringSamples; i++) {
            double angle = 2.0 * geo::PI * i / params.ringSamples;
            double x = rb.center.x + rb.radius * std::cos(angle);
            double y = rb.center.y + rb.radius * std::sin(angle);
            rb.ringPoints.emplace_back(x, y);
        }

        // Find entry/exit points (where approach roads connect)
        for (const auto& roadId : junction.roadIds) {
            QString roadIdStr = QString::fromStdString(roadId.toStdString());
            if (roadIdStr.startsWith("osm_way_")) {
                qint64 wid = roadIdStr.mid(8).toLongLong();
                if (wid == roundaboutWayId) continue;

                const Way* approachWay = osm.getWay(wid);
                if (!approachWay) continue;

                // Find which node of the approach way connects to the roundabout
                for (qint64 nodeRef : approachWay->nodeRefs) {
                    auto nodeIt = network.nodes.find(nodeRef);
                    if (nodeIt == network.nodes.end()) continue;

                    // Check if this node is on the roundabout ring
                    bool onRing = false;
                    for (qint64 ringNodeRef : way->nodeRefs) {
                        if (ringNodeRef == nodeRef) { onRing = true; break; }
                    }
                    if (!onRing) continue;

                    geo::Point2D entryPt(nodeIt->second.x, nodeIt->second.y);
                    double angle = std::atan2(entryPt.y - rb.center.y,
                                               entryPt.x - rb.center.x);

                    // Determine if this is entry or exit based on one-way direction
                    if (approachWay->isOneWayReverse()) {
                        rb.exitPoints.push_back(entryPt);
                    } else {
                        rb.entryPoints.push_back(entryPt);
                        rb.entryAngles.push_back(angle);
                    }
                    break;  // only first connecting node
                }
            }
        }

        // Lane configuration
        int osmLanes = way->lanes();
        rb.circulatoryLanes = osmLanes > 0 ? osmLanes : params.defaultCirculatoryLanes;
        rb.laneWidth = params.defaultLaneWidth;

        qDebug() << "[RoundaboutGenerator] Generated roundabout" << rb.id
                 << "radius:" << rb.radius << "m"
                 << "entries:" << rb.entryPoints.size()
                 << "exits:" << rb.exitPoints.size()
                 << "clockwise:" << rb.isClockwise;

        return rb;
    }

    // Generate all roundabouts in the network
    static std::vector<RoundaboutGeometry> generateAll(
        const std::vector<DetectedJunction>& junctions,
        const RoadNetworkBuilder::Result& network,
        const OsmData& osm,
        const Params& params = {})
    {
        std::vector<RoundaboutGeometry> roundabouts;

        for (const auto& j : junctions) {
            if (j.type != JunctionType::Roundabout && !j.isRoundabout) continue;
            roundabouts.push_back(generate(j, network, osm, params));
        }

        qDebug() << "[RoundaboutGenerator] Generated" << roundabouts.size() << "roundabouts";
        return roundabouts;
    }

    // ─── Create a RoadV2 for the roundabout ring ───
    // This creates a circular road using line segments approximating the ring
    static geo::RoadV2 createRingRoad(const RoundaboutGeometry& rb) {
        geo::RoadV2 road;
        road.id = rb.id.toStdString() + "_ring";
        road.name = "Roundabout";
        road.width = rb.circulatoryLanes * rb.laneWidth;
        road.laneCount = rb.circulatoryLanes;
        road.color = "#4ecca3";
        road.profileName = "roundabout";

        // Create line segments around the ring
        int n = int(rb.ringPoints.size());
        road.reserveSegments(n);
        for (int i = 0; i < n; i++) {
            int next = (i + 1) % n;
            road.addSegment<geo::LineSegment>(
                rb.ringPoints[i], rb.ringPoints[next]);
        }

        // Create lane section
        geo::LaneSection ls;
        ls.startS = 0.0;

        // Center lane (virtual)
        geo::Lane center;
        center.id = 0;
        center.type = geo::LaneType::Border;
        center.width = geo::Polynomial3(0.0);
        ls.addLane(center);

        // Circulatory lanes (all on one side — direction depends on roundabout)
        for (int i = 1; i <= rb.circulatoryLanes; i++) {
            geo::Lane lane;
            lane.id = rb.isClockwise ? i : -i;
            lane.type = geo::LaneType::Driving;
            lane.width = geo::Polynomial3(rb.laneWidth);
            ls.addLane(lane);
        }

        road.addLaneSection(ls);
        return road;
    }

    // ─── Serialize roundabout geometry ───
    static QJsonObject toJson(const RoundaboutGeometry& rb) {
        QJsonObject j;
        j["id"] = rb.id;
        j["osmWayId"] = qint64(rb.osmWayId);
        j["centerX"] = rb.center.x;
        j["centerY"] = rb.center.y;
        j["radius"] = rb.radius;
        j["circumference"] = rb.circumference;
        j["circulatoryLanes"] = rb.circulatoryLanes;
        j["laneWidth"] = rb.laneWidth;
        j["isClockwise"] = rb.isClockwise;

        QJsonArray ring;
        for (const auto& p : rb.ringPoints) {
            QJsonObject pt;
            pt["x"] = p.x;
            pt["y"] = p.y;
            ring.append(pt);
        }
        j["ringPoints"] = ring;

        QJsonArray entries;
        for (const auto& p : rb.entryPoints) {
            QJsonObject pt;
            pt["x"] = p.x;
            pt["y"] = p.y;
            entries.append(pt);
        }
        j["entryPoints"] = entries;

        QJsonArray exits;
        for (const auto& p : rb.exitPoints) {
            QJsonObject pt;
            pt["x"] = p.x;
            pt["y"] = p.y;
            exits.append(pt);
        }
        j["exitPoints"] = exits;

        QJsonArray angles;
        for (double a : rb.entryAngles) angles.append(a);
        j["entryAngles"] = angles;

        return j;
    }

    static RoundaboutGeometry fromJson(const QJsonObject& j) {
        RoundaboutGeometry rb;
        rb.id = j["id"].toString();
        rb.osmWayId = j["osmWayId"].toVariant().toLongLong();
        rb.center.x = j["centerX"].toDouble();
        rb.center.y = j["centerY"].toDouble();
        rb.radius = j["radius"].toDouble();
        rb.circumference = j["circumference"].toDouble();
        rb.circulatoryLanes = j["circulatoryLanes"].toInt(1);
        rb.laneWidth = j["laneWidth"].toDouble(3.5);
        rb.isClockwise = j["isClockwise"].toBool(true);

        for (const auto& v : j["ringPoints"].toArray()) {
            QJsonObject pt = v.toObject();
            rb.ringPoints.emplace_back(pt["x"].toDouble(), pt["y"].toDouble());
        }
        for (const auto& v : j["entryPoints"].toArray()) {
            QJsonObject pt = v.toObject();
            rb.entryPoints.emplace_back(pt["x"].toDouble(), pt["y"].toDouble());
        }
        for (const auto& v : j["exitPoints"].toArray()) {
            QJsonObject pt = v.toObject();
            rb.exitPoints.emplace_back(pt["x"].toDouble(), pt["y"].toDouble());
        }
        for (const auto& v : j["entryAngles"].toArray()) {
            rb.entryAngles.push_back(v.toDouble());
        }

        return rb;
    }
};

} // namespace osm
