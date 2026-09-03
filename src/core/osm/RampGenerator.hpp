#pragma once

// ============================================================
// RampGenerator — Generate ramps and forks at junctions
// ============================================================
//
// RoadBuilder-inspired ramp/fork generation:
//   - Detects where a main road meets a side road at a shallow
//     angle suitable for an exit/entry ramp.
//   - Generates a smooth connecting ramp road using arc or
//     clothoid geometry for G1/G2 continuity.
//   - Creates fork connections where a road splits into two
//     branches (e.g., highway fork).
//
// The generated ramps are added to the road network as new
// RoadV2 entries with appropriate metadata.
//

#include "OsmTypes.hpp"
#include "RoadNetworkBuilder.hpp"
#include "JunctionDetector.hpp"
#include "CoordinateConverter.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"
#include "../../engine/road/geometry_segment.hpp"
#include "../../engine/road/curvature_blending.hpp"

#include <QString>
#include "../logger/Logger.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace osm {

// ─── Ramp generation parameters ─────────────────────────────
struct RampParams {
    // Minimum angle between roads to consider ramp generation (degrees)
    double minRampAngleDeg = 15.0;

    // Maximum angle between roads to consider ramp generation (degrees)
    // Beyond this, a normal junction connection is used instead.
    double maxRampAngleDeg = 75.0;

    // Ramp radius (m) — the circular arc radius for the ramp curve
    double rampRadius = 50.0;

    // Ramp lane width (m)
    double rampLaneWidth = 4.0;

    // Ramp speed limit (km/h)
    double rampSpeedLimit = 50.0;

    // Offset from junction center to start the ramp (m)
    double rampStartOffset = 30.0;

    // Whether to apply curvature blending (clothoid transitions) to ramps
    bool applyCurvatureBlending = true;

    // Minimum main road length to consider ramp generation (m)
    double minMainRoadLength = 100.0;
};

// ─── Generated ramp ─────────────────────────────────────────
struct GeneratedRamp {
    geo::RoadV2 road;
    QString fromRoadId;
    QString toRoadId;
    bool isExitRamp = true;  // true = exit (main→side), false = entry (side→main)
};

// ─── RampGenerator ──────────────────────────────────────────
class RampGenerator {
public:
    // Generate ramps for the entire network
    static std::vector<GeneratedRamp> generate(
        const RoadNetworkBuilder::Result& network,
        const std::vector<DetectedJunction>& junctions,
        const RampParams& params = {})
    {
        std::vector<GeneratedRamp> ramps;
        std::unordered_map<std::string, const geo::RoadV2*> roadById;
        for (const auto& r : network.roads)
            roadById[r.id] = &r;

        for (const auto& j : junctions) {
            if (j.type == JunctionType::Overpass) continue;
            if (j.roadIds.size() < 2) continue;

            // Find the main road (longest) and side road(s)
            const geo::RoadV2* mainRoad = nullptr;
            double mainLen = 0;
            std::vector<const geo::RoadV2*> sideRoads;
            for (const auto& rid : j.roadIds) {
                auto it = roadById.find(rid.toStdString());
                if (it == roadById.end()) continue;
                const auto* r = it->second;
                if (r->totalLength() > mainLen && r->totalLength() >= params.minMainRoadLength) {
                    if (mainRoad) sideRoads.push_back(mainRoad);
                    mainRoad = r;
                    mainLen = r->totalLength();
                } else {
                    sideRoads.push_back(r);
                }
            }
            if (!mainRoad || sideRoads.empty()) continue;

            for (const auto* sideRoad : sideRoads) {
                // Compute the angle between main road and side road at junction
                const double mainTotal = mainRoad->totalLength();
                const double sideTotal = sideRoad->totalLength();

                // Find the junction point on each road
                double mainS = 0, mainBestDist = 1e18;
                for (int i = 0; i <= 50; ++i) {
                    const double s = mainTotal * i / 50.0;
                    const auto p = mainRoad->geometry().positionAt(s);
                    const double d = p.distanceTo(j.center);
                    if (d < mainBestDist) { mainBestDist = d; mainS = s; }
                }

                double sideS = 0, sideBestDist = 1e18;
                for (int i = 0; i <= 50; ++i) {
                    const double s = sideTotal * i / 50.0;
                    const auto p = sideRoad->geometry().positionAt(s);
                    const double d = p.distanceTo(j.center);
                    if (d < sideBestDist) { sideBestDist = d; sideS = s; }
                }

                // Headings at junction
                const double mainHeading = mainRoad->geometry().headingAt(mainS);
                const double sideHeading = sideRoad->geometry().headingAt(sideS);

                // Angle between roads
                double angleDiff = std::abs(geo::normalizeAnglePi(sideHeading - mainHeading));
                const double angleDeg = angleDiff * 180.0 / geo::PI;

                if (angleDeg < params.minRampAngleDeg || angleDeg > params.maxRampAngleDeg)
                    continue;

                // Generate ramp connection
                // Ramp starts on main road at rampStartOffset before junction
                const double rampMainS = std::max(0.0, mainS - params.rampStartOffset);
                const auto rampStart = mainRoad->geometry().positionAt(rampMainS);
                const double rampStartHeading = mainRoad->geometry().headingAt(rampMainS);

                // Ramp ends on side road at rampStartOffset from junction
                const double rampSideS = (sideS < sideTotal * 0.5)
                    ? std::min(sideTotal, sideS + params.rampStartOffset)
                    : std::max(0.0, sideS - params.rampStartOffset);
                const auto rampEnd = sideRoad->geometry().positionAt(rampSideS);
                const double rampEndHeading = sideRoad->geometry().headingAt(rampSideS);

                // Build ramp geometry: start → arc → end
                // Simple approach: use a single arc that connects start to end
                // with tangent matching at both ends
                const double dx = rampEnd.x - rampStart.x;
                const double dy = rampEnd.y - rampStart.y;
                const double dist = std::hypot(dx, dy);
                if (dist < 5.0) continue;

                // Determine if this is an exit or entry ramp
                // Exit: main road END is near junction → ramp goes from main to side
                // Entry: main road START is near junction → ramp goes from side to main
                const bool isExit = (mainS > mainTotal * 0.5);

                // Build the ramp road
                geo::RoadV2 ramp;
                ramp.id = "ramp_" + std::to_string(ramps.size() + 1) + "_" +
                          mainRoad->id + "_to_" + sideRoad->id;
                ramp.name = "Ramp " + std::to_string(ramps.size() + 1);
                ramp.profileName = "ramp_1x1";
                ramp.laneCount = 1;
                ramp.width = params.rampLaneWidth;

                // Try to build a two-segment ramp: line → arc → line
                // For simplicity, use a single arc from start to end
                // Compute the arc that passes through both points with given start heading
                const double startToEndAngle = std::atan2(dy, dx);
                const double headingDiff = geo::normalizeAnglePi(startToEndAngle - rampStartHeading);

                if (std::abs(headingDiff) < 0.01) {
                    // Nearly straight — use a line
                    ramp.addSegment<geo::LineSegment>(rampStart, rampEnd);
                } else {
                    // Use an arc
                    // Compute arc curvature from heading change and distance
                    const double curvature = 2.0 * std::sin(headingDiff) / dist;
                    const double arcLen = std::abs(headingDiff / curvature);

                    // First add a short line from the start
                    const double lineLen = std::max(5.0, dist * 0.2);
                    const auto lineEnd = mainRoad->geometry().positionAt(
                        rampMainS + (isExit ? lineLen : -lineLen));
                    if (lineLen > 1.0 && rampMainS + lineLen < mainTotal)
                        ramp.addSegment<geo::LineSegment>(rampStart, lineEnd);

                    // Then add the arc
                    const double arcStartHeading = (lineLen > 1.0)
                        ? rampStartHeading  // line continues straight
                        : rampStartHeading;
                    const auto arcStart = (lineLen > 1.0) ? lineEnd : rampStart;
                    ramp.addSegment<geo::ArcSegment>(arcStart, arcStartHeading,
                                                      curvature, arcLen);
                }

                // Add a lane section
                geo::Lane lane;
                lane.id = 1;
                lane.type = geo::LaneType::Driving;
                lane.width = geo::Polynomial3(params.rampLaneWidth);
                geo::LaneSection section;
                section.addLane(lane);
                ramp.addLaneSection(section);

                // Apply curvature blending if requested
                if (params.applyCurvatureBlending && ramp.numSegments() > 1) {
                    geo::applyCurvatureBlending(ramp);
                }

                GeneratedRamp gen;
                gen.road = std::move(ramp);
                gen.fromRoadId = QString::fromStdString(mainRoad->id);
                gen.toRoadId = QString::fromStdString(sideRoad->id);
                gen.isExitRamp = isExit;
                ramps.push_back(std::move(gen));
            }
        }

        appLog().info("[RampGenerator] Generated", ramps.size(), "ramps");
        return ramps;
    }

    // ─── Generate fork connections ───────────────────────────
    // A fork is where a single road splits into two branches.
    // This detects potential fork points and generates the
    // connecting geometry.
    static std::vector<GeneratedRamp> generateForks(
        const RoadNetworkBuilder::Result& network,
        const std::vector<DetectedJunction>& junctions,
        const RampParams& params = {})
    {
        std::vector<GeneratedRamp> forks;
        std::unordered_map<std::string, const geo::RoadV2*> roadById;
        for (const auto& r : network.roads)
            roadById[r.id] = &r;

        // A fork is a Y-junction where the main road splits into two
        // roads of similar size going in similar directions
        for (const auto& j : junctions) {
            if (j.type != JunctionType::Y_Junction && j.type != JunctionType::T_Junction)
                continue;
            if (j.roadIds.size() != 3) continue;

            // Find the three roads and their headings
            struct RoadInfo {
                const geo::RoadV2* road = nullptr;
                double heading = 0;
                double s = 0;
                bool isEnd = false;
            };
            std::vector<RoadInfo> infos;
            for (const auto& rid : j.roadIds) {
                auto it = roadById.find(rid.toStdString());
                if (it == roadById.end()) continue;
                const auto* r = it->second;
                const double total = r->totalLength();
                if (total <= 0) continue;

                RoadInfo info;
                info.road = r;
                const auto start = r->geometry().positionAt(0);
                const auto end = r->geometry().positionAt(total);
                const double dStart = start.distanceTo(j.center);
                const double dEnd = end.distanceTo(j.center);
                if (dStart < dEnd) {
                    info.s = 0;
                    info.heading = r->geometry().headingAt(0);
                    info.isEnd = false;
                } else {
                    info.s = total;
                    info.heading = r->geometry().headingAt(total);
                    info.isEnd = true;
                }
                infos.push_back(info);
            }
            if (infos.size() != 3) continue;

            // Find the incoming road (one road ending at junction, two starting)
            // or the outgoing road (one starting, two ending)
            int endCount = 0;
            for (const auto& info : infos) if (info.isEnd) endCount++;

            // A fork has 1 road ending and 2 starting (or vice versa)
            if (endCount != 1 && endCount != 2) continue;

            const RoadInfo* mainInfo = nullptr;
            std::vector<const RoadInfo*> branchInfos;
            for (const auto& info : infos) {
                if (info.isEnd == (endCount == 1)) mainInfo = &info;
                else branchInfos.push_back(&info);
            }
            if (!mainInfo || branchInfos.size() != 2) continue;

            // Check that the two branches diverge at similar angles (fork pattern)
            const double branchAngleDiff = std::abs(
                geo::normalizeAnglePi(branchInfos[0]->heading - branchInfos[1]->heading));
            const double branchAngleDeg = branchAngleDiff * 180.0 / geo::PI;
            if (branchAngleDeg < 10.0 || branchAngleDeg > 90.0) continue;

            // Generate fork connecting roads (two ramps from main to each branch)
            for (const auto* branch : branchInfos) {
                const double mainTotal = mainInfo->road->totalLength();
                const double offsetS = params.rampStartOffset;
                const double forkMainS = mainInfo->isEnd
                    ? std::max(0.0, mainInfo->s - offsetS)
                    : std::min(mainTotal, mainInfo->s + offsetS);

                const auto forkStart = mainInfo->road->geometry().positionAt(forkMainS);
                const double forkStartHeading = mainInfo->road->geometry().headingAt(forkMainS);

                const double branchTotal = branch->road->totalLength();
                const double forkBranchS = branch->isEnd
                    ? std::max(0.0, branch->s - offsetS)
                    : std::min(branchTotal, branch->s + offsetS);
                const auto forkEnd = branch->road->geometry().positionAt(forkBranchS);

                geo::RoadV2 forkRoad;
                forkRoad.id = "fork_" + std::to_string(forks.size() + 1) + "_" +
                              mainInfo->road->id + "_to_" + branch->road->id;
                forkRoad.name = "Fork " + std::to_string(forks.size() + 1);
                forkRoad.profileName = "ramp_1x1";
                forkRoad.laneCount = 1;
                forkRoad.width = params.rampLaneWidth;

                // Build geometry: line → arc → line
                const double dx = forkEnd.x - forkStart.x;
                const double dy = forkEnd.y - forkStart.y;
                const double dist = std::hypot(dx, dy);
                if (dist < 5.0) continue;

                const double startToEndAngle = std::atan2(dy, dx);
                const double headingDiff = geo::normalizeAnglePi(startToEndAngle - forkStartHeading);

                if (std::abs(headingDiff) < 0.01) {
                    forkRoad.addSegment<geo::LineSegment>(forkStart, forkEnd);
                } else {
                    const double curvature = 2.0 * std::sin(headingDiff) / dist;
                    const double arcLen = std::abs(headingDiff / curvature);
                    forkRoad.addSegment<geo::ArcSegment>(forkStart, forkStartHeading,
                                                          curvature, arcLen);
                }

                geo::Lane lane;
                lane.id = 1;
                lane.type = geo::LaneType::Driving;
                lane.width = geo::Polynomial3(params.rampLaneWidth);
                geo::LaneSection section;
                section.addLane(lane);
                forkRoad.addLaneSection(section);

                if (params.applyCurvatureBlending && forkRoad.numSegments() > 1)
                    geo::applyCurvatureBlending(forkRoad);

                GeneratedRamp gen;
                gen.road = std::move(forkRoad);
                gen.fromRoadId = QString::fromStdString(mainInfo->road->id);
                gen.toRoadId = QString::fromStdString(branch->road->id);
                gen.isExitRamp = mainInfo->isEnd;
                forks.push_back(std::move(gen));
            }
        }

        appLog().info("[RampGenerator] Generated", forks.size(), "fork connections");
        return forks;
    }
};

} // namespace osm
