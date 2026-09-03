#pragma once

// ============================================================
// RoadMarkingGenerator — Generate road markings from lane model
// ============================================================
//
// Generates road markings for the road network:
//   - Center lines (solid/dashed)
//   - Lane divider lines (dashed)
//   - Edge lines (solid)
//   - Stop lines at junctions
//   - Yield markings
//   - Crosswalk markings
//   - Turn arrows
//
// Markings are generated as parametric data (not baked meshes).
// Each marking has a type, position, and style that can be
// rendered by the 3D viewport.
//

#include "OsmTypes.hpp"
#include "RoadNetworkBuilder.hpp"
#include "JunctionDetector.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"
#include "../../engine/road/lane_engine.hpp"

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include "../logger/Logger.hpp"
#include <vector>
#include <cmath>
#include <unordered_map>

namespace osm {

// ─── MarkingType ───
enum class MarkingType {
    CenterLine,         // solid or dashed line at road center
    LaneDivider,        // dashed line between lanes
    EdgeLine,           // solid line at road edge
    StopLine,           // solid line at junction entry
    YieldLine,          // dashed yield marking
    Crosswalk,          // pedestrian crossing
    TurnArrow,          // directional arrow
    RoundaboutArrow,    // circular flow arrow
    Chevron,            // chevron marking
    BikeLaneSymbol,     // bicycle symbol
    BusLaneSymbol,      // bus lane symbol
    ParkingMarking,     // parking space
    GoreArea,           // hatched gore area at ramp divergence
    RampArrow           // exit/entry ramp directional arrow
};

// ─── MarkingStyle ───
enum class MarkingStyle {
    Solid,          // continuous line
    Dashed,         // dashed line
    DoubleSolid,    // two parallel solid lines
    DoubleDashed,   // two parallel dashed lines
    SolidDashed,    // solid + dashed (no passing one direction)
    DashedSolid,    // dashed + solid (no passing other direction)
    BoldDashed,     // wider dashes (warning)
    Dotted          // dotted line
};

// ─── RoadMarking ───
struct RoadMarking {
    MarkingType type = MarkingType::CenterLine;
    MarkingStyle style = MarkingStyle::Solid;
    QString roadId;
    double startS = 0.0;       // start position along road (meters)
    double endS = 0.0;         // end position along road (meters)
    double lateralOffset = 0.0; // offset from center (meters)
    double width = 0.1;        // marking width (meters)
    QString color = "white";   // white, yellow, red
    QString turnDirection;     // for turn arrows: left, right, through, etc.

    // For point markings (arrows, symbols)
    bool isPointMarking = false;
    double position = 0.0;     // s-position for point markings

    QJsonObject toJson() const {
        QJsonObject j;
        j["type"] = int(type);
        j["style"] = int(style);
        j["roadId"] = roadId;
        j["startS"] = startS;
        j["endS"] = endS;
        j["lateralOffset"] = lateralOffset;
        j["width"] = width;
        j["color"] = color;
        j["turnDirection"] = turnDirection;
        j["isPointMarking"] = isPointMarking;
        j["position"] = position;
        return j;
    }

    static RoadMarking fromJson(const QJsonObject& j) {
        RoadMarking m;
        m.type = MarkingType(j["type"].toInt());
        m.style = MarkingStyle(j["style"].toInt());
        m.roadId = j["roadId"].toString();
        m.startS = j["startS"].toDouble();
        m.endS = j["endS"].toDouble();
        m.lateralOffset = j["lateralOffset"].toDouble();
        m.width = j["width"].toDouble();
        m.color = j["color"].toString();
        m.turnDirection = j["turnDirection"].toString();
        m.isPointMarking = j["isPointMarking"].toBool();
        m.position = j["position"].toDouble();
        return m;
    }

    QString typeString() const {
        switch (type) {
        case MarkingType::CenterLine:       return "CenterLine";
        case MarkingType::LaneDivider:      return "LaneDivider";
        case MarkingType::EdgeLine:         return "EdgeLine";
        case MarkingType::StopLine:         return "StopLine";
        case MarkingType::YieldLine:        return "YieldLine";
        case MarkingType::Crosswalk:        return "Crosswalk";
        case MarkingType::TurnArrow:        return "TurnArrow";
        case MarkingType::RoundaboutArrow:  return "RoundaboutArrow";
        case MarkingType::Chevron:          return "Chevron";
        case MarkingType::BikeLaneSymbol:   return "BikeLaneSymbol";
        case MarkingType::BusLaneSymbol:    return "BusLaneSymbol";
        case MarkingType::ParkingMarking:   return "ParkingMarking";
        case MarkingType::GoreArea:         return "GoreArea";
        case MarkingType::RampArrow:        return "RampArrow";
        }
        return "Unknown";
    }
};

// ─── RoadMarkingGenerator ───
class RoadMarkingGenerator {
public:
    struct Params {
        double dashLength = 3.0;        // meters
        double dashGap = 3.0;           // meters
        double defaultWidth = 0.1;      // meters
        double edgeLineWidth = 0.15;    // meters
        double centerLineWidth = 0.15;  // meters
        double stopLineDistance = 5.0;  // meters before junction
        bool generateCrosswalks = true;
        bool generateTurnArrows = true;
        bool generateGoreAreas = true;  // RoadBuilder-inspired gore area generation
        double goreLength = 30.0;       // gore area length (m)
        double goreWidth = 2.0;         // gore area width at widest point (m)
    };

    // Generate markings for a single road
    static std::vector<RoadMarking> generateForRoad(const geo::RoadV2& road,
                                                     const Params& params = {})
    {
        std::vector<RoadMarking> markings;
        QString roadId = QString::fromStdString(road.id);
        double totalLen = road.totalLength();

        if (totalLen <= 0 || road.numLaneSections() == 0) return markings;

        const auto& ls = road.laneSection(0);

        // ─── Center line ───
        // If there are both left and right lanes, draw a center line
        bool hasLeftLanes = false, hasRightLanes = false;
        for (const auto& lane : ls.lanes()) {
            if (lane.isLeft()) hasLeftLanes = true;
            if (lane.isRight()) hasRightLanes = true;
        }

        if (hasLeftLanes && hasRightLanes) {
            RoadMarking center;
            center.type = MarkingType::CenterLine;
            center.style = MarkingStyle::DoubleSolid;  // typical for divided roads
            center.roadId = roadId;
            center.startS = 0;
            center.endS = totalLen;
            center.lateralOffset = 0.0;
            center.width = params.centerLineWidth;
            center.color = "yellow";
            markings.push_back(center);
        } else if (hasRightLanes && !hasLeftLanes) {
            // One-way road — no center line, just edge lines
        }

        // ─── Lane dividers ───
        // Draw dashed lines between adjacent driving lanes on the same side
        std::vector<int> rightLaneIds, leftLaneIds;
        for (const auto& lane : ls.lanes()) {
            if (lane.type == geo::LaneType::Driving) {
                if (lane.isRight()) rightLaneIds.push_back(lane.id);
                if (lane.isLeft()) leftLaneIds.push_back(lane.id);
            }
        }

        // Sort by ID
        std::sort(rightLaneIds.begin(), rightLaneIds.end());
        std::sort(leftLaneIds.begin(), leftLaneIds.end(), std::greater<int>());

        // Right side dividers (between lane 1&2, 2&3, etc.)
        for (size_t i = 0; i + 1 < rightLaneIds.size(); i++) {
            const geo::Lane* l1 = ls.findLane(rightLaneIds[i]);
            if (!l1) continue;
            double offset = 0;
            for (int id = 1; id <= rightLaneIds[i]; id++) {
                const geo::Lane* l = ls.findLane(id);
                if (l) offset += l->widthAt(0);
            }

            RoadMarking div;
            div.type = MarkingType::LaneDivider;
            div.style = MarkingStyle::Dashed;
            div.roadId = roadId;
            div.startS = 0;
            div.endS = totalLen;
            div.lateralOffset = offset;
            div.width = params.defaultWidth;
            div.color = "white";
            markings.push_back(div);
        }

        // Left side dividers
        for (size_t i = 0; i + 1 < leftLaneIds.size(); i++) {
            const geo::Lane* l1 = ls.findLane(leftLaneIds[i]);
            if (!l1) continue;
            double offset = 0;
            for (int id = -1; id >= leftLaneIds[i]; id--) {
                const geo::Lane* l = ls.findLane(id);
                if (l) offset -= l->widthAt(0);
            }

            RoadMarking div;
            div.type = MarkingType::LaneDivider;
            div.style = MarkingStyle::Dashed;
            div.roadId = roadId;
            div.startS = 0;
            div.endS = totalLen;
            div.lateralOffset = offset;
            div.width = params.defaultWidth;
            div.color = "white";
            markings.push_back(div);
        }

        // ─── Edge lines ───
        // Right edge
        double rightEdge = 0;
        for (const auto& lane : ls.lanes()) {
            if (lane.isRight()) rightEdge += lane.widthAt(0);
        }
        if (rightEdge > 0) {
            RoadMarking rightEdgeLine;
            rightEdgeLine.type = MarkingType::EdgeLine;
            rightEdgeLine.style = MarkingStyle::Solid;
            rightEdgeLine.roadId = roadId;
            rightEdgeLine.startS = 0;
            rightEdgeLine.endS = totalLen;
            rightEdgeLine.lateralOffset = rightEdge;
            rightEdgeLine.width = params.edgeLineWidth;
            rightEdgeLine.color = "white";
            markings.push_back(rightEdgeLine);
        }

        // Left edge
        double leftEdge = 0;
        for (const auto& lane : ls.lanes()) {
            if (lane.isLeft()) leftEdge -= lane.widthAt(0);
        }
        if (leftEdge < 0) {
            RoadMarking leftEdgeLine;
            leftEdgeLine.type = MarkingType::EdgeLine;
            leftEdgeLine.style = MarkingStyle::Solid;
            leftEdgeLine.roadId = roadId;
            leftEdgeLine.startS = 0;
            leftEdgeLine.endS = totalLen;
            leftEdgeLine.lateralOffset = leftEdge;
            leftEdgeLine.width = params.edgeLineWidth;
            leftEdgeLine.color = "white";
            markings.push_back(leftEdgeLine);
        }

        // ─── Bike lane markings ───
        for (const auto& lane : ls.lanes()) {
            if (lane.type == geo::LaneType::Biking) {
                RoadMarking bike;
                bike.type = MarkingType::BikeLaneSymbol;
                bike.roadId = roadId;
                bike.isPointMarking = true;
                bike.position = totalLen / 2;
                bike.lateralOffset = lane.id > 0 ?
                    rightEdge - lane.widthAt(0) / 2 :
                    leftEdge + lane.widthAt(0) / 2;
                bike.color = "white";
                markings.push_back(bike);
            }
        }

        // ─── Bus lane markings ───
        for (const auto& lane : ls.lanes()) {
            if (lane.type == geo::LaneType::Bus) {
                RoadMarking bus;
                bus.type = MarkingType::BusLaneSymbol;
                bus.roadId = roadId;
                bus.isPointMarking = true;
                bus.position = totalLen / 2;
                bus.lateralOffset = lane.id > 0 ?
                    rightEdge - lane.widthAt(0) / 2 :
                    leftEdge + lane.widthAt(0) / 2;
                bus.color = "white";
                markings.push_back(bus);
            }
        }

        return markings;
    }

    // ─── Generate gore areas at ramp divergence points ───────
    // A gore area is the hatched/chevroned triangular zone where
    // a ramp diverges from or merges into the main road. It is
    // generated at junction points where a main road meets a
    // ramp-type road, placed on the main road just before the
    // divergence point.
    static std::vector<RoadMarking> generateGoreAreas(
        const RoadNetworkBuilder::Result& network,
        const std::vector<DetectedJunction>& junctions,
        const Params& params = {})
    {
        std::vector<RoadMarking> gores;
        if (!params.generateGoreAreas) return gores;

        // Build road lookup
        std::unordered_map<std::string, const geo::RoadV2*> roadById;
        for (const auto& r : network.roads)
            roadById[r.id] = &r;

        for (const auto& j : junctions) {
            if (j.type == JunctionType::Overpass) continue;
            if (j.roadIds.size() < 2) continue;

            // Find the main road (longest) and ramp (shortest, or named "ramp")
            const geo::RoadV2* mainRoad = nullptr;
            const geo::RoadV2* rampRoad = nullptr;
            double mainLen = 0, rampLen = 1e18;
            for (const auto& rid : j.roadIds) {
                auto it = roadById.find(rid.toStdString());
                if (it == roadById.end()) continue;
                const auto* r = it->second;
                const double len = r->totalLength();
                const auto* meta = RoadNetworkBuilder::getMetadata(r->id);
                const bool isRamp = (meta && meta->highwayType.contains("ramp")) ||
                                     r->id.find("ramp") != std::string::npos ||
                                     r->name.find("ramp") != std::string::npos ||
                                    (j.roadIds.size() == 2 && len < mainLen * 0.3);
                if (isRamp) {
                    if (len < rampLen) { rampRoad = r; rampLen = len; }
                } else {
                    if (len > mainLen) { mainRoad = r; mainLen = len; }
                }
            }
            if (!mainRoad || !rampRoad) continue;

            // Find the divergence point on the main road (closest to junction center)
            const double mainTotal = mainRoad->totalLength();
            double bestS = 0, bestDist = 1e18;
            for (int i = 0; i <= 50; ++i) {
                const double s = mainTotal * i / 50.0;
                const auto p = mainRoad->geometry().positionAt(s);
                const double d = p.distanceTo(j.center);
                if (d < bestDist) { bestDist = d; bestS = s; }
            }

            // Place gore area just before the divergence point
            const double goreStart = std::max(0.0, bestS - params.goreLength);
            const double goreEnd = bestS;

            // Determine which side the ramp diverges
            const auto mainPt = mainRoad->geometry().positionAt(bestS);
            const auto rampPt = rampRoad->geometry().positionAt(
                rampRoad->totalLength() * 0.1);
            const auto mainNormal = mainRoad->geometry().normalAt(bestS);
            const double side = (rampPt.x - mainPt.x) * mainNormal.x +
                                (rampPt.y - mainPt.y) * mainNormal.y;
            const double lateralOffset = (side > 0 ? 1.0 : -1.0) * params.goreWidth * 0.5;

            RoadMarking gore;
            gore.type = MarkingType::GoreArea;
            gore.style = MarkingStyle::BoldDashed;  // hatched
            gore.roadId = QString::fromStdString(mainRoad->id);
            gore.startS = goreStart;
            gore.endS = goreEnd;
            gore.lateralOffset = lateralOffset;
            gore.width = params.goreWidth;
            gore.color = "white";
            gores.push_back(gore);

            // Add a ramp arrow at the gore point
            RoadMarking arrow;
            arrow.type = MarkingType::RampArrow;
            arrow.roadId = QString::fromStdString(mainRoad->id);
            arrow.isPointMarking = true;
            arrow.position = goreEnd;
            arrow.lateralOffset = lateralOffset;
            arrow.color = "white";
            gores.push_back(arrow);
        }
        return gores;
    }

    // Generate markings for the entire network
    static std::vector<RoadMarking> generateAll(
        const RoadNetworkBuilder::Result& network,
        const std::vector<DetectedJunction>& junctions,
        const Params& params = {})
    {
        std::vector<RoadMarking> allMarkings;

        // Per-road markings
        for (const auto& road : network.roads) {
            auto roadMarkings = generateForRoad(road, params);
            allMarkings.insert(allMarkings.end(),
                               roadMarkings.begin(), roadMarkings.end());
        }

        // Gore areas at ramp divergence points (RoadBuilder-inspired)
        if (params.generateGoreAreas) {
            auto gores = generateGoreAreas(network, junctions, params);
            allMarkings.insert(allMarkings.end(), gores.begin(), gores.end());
        }

        // Junction markings (stop lines, yield lines, crosswalks)
        for (const auto& j : junctions) {
            if (j.type == JunctionType::Overpass) continue;

            for (const auto& roadId : j.roadIds) {
                QString rid = QString::fromStdString(roadId.toStdString());

                // Find the road
                const geo::RoadV2* road = nullptr;
                for (const auto& r : network.roads) {
                    if (r.id == roadId.toStdString()) { road = &r; break; }
                }
                if (!road) continue;

                // Find the s-position of the junction on this road
                double totalLen = road->totalLength();
                double bestS = 0;
                double bestDist = 1e18;
                for (int i = 0; i <= 50; i++) {
                    double s = totalLen * i / 50.0;
                    geo::Point2D p = road->geometry().positionAt(s);
                    double d = p.distanceTo(j.center);
                    if (d < bestDist) { bestDist = d; bestS = s; }
                }

                // Stop line (before junction)
                double stopS = bestS - params.stopLineDistance;
                if (stopS > 0) {
                    RoadMarking stop;
                    stop.type = MarkingType::StopLine;
                    stop.style = MarkingStyle::Solid;
                    stop.roadId = rid;
                    stop.startS = stopS;
                    stop.endS = stopS + 0.5;  // 0.5m wide
                    stop.width = 0.3;
                    stop.color = "white";
                    allMarkings.push_back(stop);
                }

                // Crosswalk (before stop line)
                if (params.generateCrosswalks) {
                    double cwS = stopS - 3.0;
                    if (cwS > 0) {
                        RoadMarking crosswalk;
                        crosswalk.type = MarkingType::Crosswalk;
                        crosswalk.style = MarkingStyle::BoldDashed;
                        crosswalk.roadId = rid;
                        crosswalk.startS = cwS;
                        crosswalk.endS = cwS + 2.0;
                        crosswalk.width = 2.0;
                        crosswalk.color = "white";
                        allMarkings.push_back(crosswalk);
                    }
                }
            }
        }

        appLog().info("[RoadMarkingGenerator] Generated", allMarkings.size(), "markings");
        return allMarkings;
    }

    // ─── Serialize all markings ───
    static QJsonArray toJsonArray(const std::vector<RoadMarking>& markings) {
        QJsonArray arr;
        for (const auto& m : markings) arr.append(m.toJson());
        return arr;
    }

    static std::vector<RoadMarking> fromJsonArray(const QJsonArray& arr) {
        std::vector<RoadMarking> markings;
        for (const auto& v : arr) {
            markings.push_back(RoadMarking::fromJson(v.toObject()));
        }
        return markings;
    }
};

} // namespace osm
