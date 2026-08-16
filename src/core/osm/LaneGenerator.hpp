#pragma once

// ============================================================
// LaneGenerator — OSM lane tags → LaneSection model
// ============================================================
//
// Converts OSM lane-related tags into the road engine's
// LaneSection/Lane model. Handles:
//   - lanes, lanes:forward, lanes:backward
//   - oneway
//   - turn:lanes, turn:lanes:forward, turn:lanes:backward
//   - change:lanes
//   - lanes:bus, lanes:psv
//   - cycleway
//   - sidewalk
//   - shoulder
//   - median
//   - lane width from road width
//

#include "OsmTypes.hpp"
#include "RoadClassifier.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/lane_engine.hpp"
#include "../../engine/road/road_v2.hpp"

#include <QString>
#include <QStringList>
#include "../logger/Logger.hpp"
#include <vector>
#include <cmath>

namespace osm {

// ─── LaneGenerationResult ───
struct LaneGenerationResult {
    bool success = true;
    QString warning;
    int totalLanes = 0;
    int forwardLanes = 0;
    int backwardLanes = 0;
    int busLanes = 0;
    int cycleLanes = 0;
    bool hasSidewalk = false;
    bool hasShoulder = false;
    bool hasMedian = false;
};

// ─── LaneGenerator ───
class LaneGenerator {
public:
    // Generate a LaneSection from OSM way tags
    // Returns the lane section and metadata about what was generated
    static LaneGenerationResult generate(const Way& way,
                                          const RoadClassInfo& classInfo,
                                          geo::LaneSection& outSection)
    {
        LaneGenerationResult result;
        outSection = geo::LaneSection();
        outSection.startS = 0.0;

        // ─── Determine lane counts ───
        int totalLanes = way.lanes();
        int fwdLanes = way.lanesForward();
        int bwdLanes = way.lanesBackward();
        bool isOneWay = way.isOneWay();
        bool isOneWayReverse = way.isOneWayReverse();

        // If no explicit lane count, use class defaults
        if (totalLanes <= 0) {
            totalLanes = classInfo.defaultLanes;
            if (isOneWay) {
                fwdLanes = totalLanes;
                bwdLanes = 0;
            } else {
                fwdLanes = totalLanes / 2;
                bwdLanes = totalLanes - fwdLanes;
            }
        } else {
            // Explicit lane count from OSM
            if (fwdLanes <= 0 && bwdLanes <= 0) {
                // No forward/backward split specified
                if (isOneWay) {
                    fwdLanes = totalLanes;
                    bwdLanes = 0;
                } else {
                    fwdLanes = totalLanes / 2;
                    bwdLanes = totalLanes - fwdLanes;
                }
            } else if (fwdLanes <= 0) {
                fwdLanes = totalLanes - bwdLanes;
            } else if (bwdLanes <= 0) {
                bwdLanes = totalLanes - fwdLanes;
            }
        }

        // ─── Determine lane width ───
        double roadWidth = way.width();
        double laneWidth = classInfo.defaultLaneWidth;

        if (roadWidth > 0 && totalLanes > 0) {
            laneWidth = roadWidth / totalLanes;
            // Clamp to reasonable values
            laneWidth = std::clamp(laneWidth, 1.8, 5.0);
        }

        // ─── Center lane (virtual, zero width) ───
        geo::Lane centerLane;
        centerLane.id = 0;
        centerLane.type = geo::LaneType::Border;
        centerLane.width = geo::Polynomial3(0.0);
        outSection.addLane(centerLane);

        // ─── Bus/PSV lanes ───
        int busLanes = 0;
        QString busTag = way.tag("lanes:bus");
        if (!busTag.isEmpty()) {
            bool ok;
            int n = busTag.toInt(&ok);
            if (ok && n > 0) busLanes = n;
        }
        QString psvTag = way.tag("lanes:psv");
        if (!psvTag.isEmpty()) {
            bool ok;
            int n = psvTag.toInt(&ok);
            if (ok && n > 0) busLanes = std::max(busLanes, n);
        }

        // ─── Turn lanes ───
        QStringList turnLanesFwd = parseTurnLanes(way.tag("turn:lanes:forward"));
        if (turnLanesFwd.isEmpty() && isOneWay) {
            turnLanesFwd = parseTurnLanes(way.tag("turn:lanes"));
        }
        QStringList turnLanesBwd = parseTurnLanes(way.tag("turn:lanes:backward"));

        // ─── Right (forward) lanes ───
        // If one-way reverse, lanes go on the left side
        if (isOneWayReverse) {
            // All lanes on left side (negative IDs)
            int lanesToCreate = isOneWay ? totalLanes : bwdLanes;
            for (int i = 1; i <= lanesToCreate; i++) {
                geo::Lane lane;
                lane.id = -i;
                lane.type = geo::LaneType::Driving;
                lane.width = geo::Polynomial3(laneWidth);
                outSection.addLane(lane);
            }
            result.backwardLanes = lanesToCreate;
        } else {
            // Forward lanes on right side (positive IDs)
            for (int i = 1; i <= fwdLanes; i++) {
                geo::Lane lane;
                lane.id = i;
                lane.type = geo::LaneType::Driving;
                lane.width = geo::Polynomial3(laneWidth);

                outSection.addLane(lane);
            }
            result.forwardLanes = fwdLanes;

            // Backward lanes on left side (negative IDs)
            if (!isOneWay && bwdLanes > 0) {
                for (int i = 1; i <= bwdLanes; i++) {
                    geo::Lane lane;
                    lane.id = -i;
                    lane.type = geo::LaneType::Driving;
                    lane.width = geo::Polynomial3(laneWidth);

                    outSection.addLane(lane);
                }
                result.backwardLanes = bwdLanes;
            }
        }

        // ─── Bus lanes ───
        if (busLanes > 0) {
            for (int i = 1; i <= busLanes; i++) {
                geo::Lane lane;
                lane.id = fwdLanes + i;
                lane.type = geo::LaneType::Bus;
                lane.width = geo::Polynomial3(laneWidth);
                outSection.addLane(lane);
            }
            result.busLanes = busLanes;
        }

        // ─── Cycleway ───
        QString cycleway = way.tag("cycleway");
        QString cyclewayLeft = way.tag("cycleway:left");
        QString cyclewayRight = way.tag("cycleway:right");
        if (cycleway == "lane" || cyclewayLeft == "lane" || cyclewayRight == "lane") {
            // Add cycle lane on right side
            geo::Lane cycleLane;
            cycleLane.id = fwdLanes + busLanes + 1;
            cycleLane.type = geo::LaneType::Biking;
            cycleLane.width = geo::Polynomial3(1.5);  // standard cycle lane width
            outSection.addLane(cycleLane);
            result.cycleLanes = 1;

            // Also on left if specified
            if (cyclewayLeft == "lane" && !isOneWay) {
                geo::Lane leftCycle;
                leftCycle.id = -(bwdLanes + 1);
                leftCycle.type = geo::LaneType::Biking;
                leftCycle.width = geo::Polynomial3(1.5);
                outSection.addLane(leftCycle);
                result.cycleLanes = 2;
            }
        }

        // ─── Sidewalk ───
        QString sidewalk = way.tag("sidewalk");
        if (sidewalk == "both" || sidewalk == "right" || sidewalk == "yes") {
            result.hasSidewalk = true;
        }
        if (sidewalk == "both" || sidewalk == "left") {
            result.hasSidewalk = true;
        }

        // ─── Shoulder ───
        QString shoulder = way.tag("shoulder");
        if (shoulder == "yes" || shoulder == "both" || shoulder == "right") {
            result.hasShoulder = true;
        }

        // ─── Median ───
        QString median = way.tag("median");
        if (median == "yes" || median == "raised" || median == "lined") {
            result.hasMedian = true;
        }

        result.totalLanes = result.forwardLanes + result.backwardLanes +
                            result.busLanes + result.cycleLanes;
        result.success = true;

        return result;
    }

    // ─── Parse turn:lanes tag ───
    // Format: "left|right;left|through;through|right"
    // Each lane separated by |, multiple turns separated by ;
    static QStringList parseTurnLanes(const QString& tag) {
        if (tag.isEmpty()) return {};
        return tag.split('|', Qt::SkipEmptyParts);
    }

    // ─── Estimate road width from lane configuration ───
    static double estimateRoadWidth(const LaneGenerationResult& lanes,
                                     double laneWidth)
    {
        double width = 0;
        width += lanes.forwardLanes * laneWidth;
        width += lanes.backwardLanes * laneWidth;
        width += lanes.busLanes * laneWidth;
        width += lanes.cycleLanes * 1.5;  // cycle lanes are narrower
        if (lanes.hasMedian) width += 1.0;  // median barrier
        if (lanes.hasShoulder) width += 2.0;  // shoulder on each side
        return width;
    }
};

} // namespace osm
