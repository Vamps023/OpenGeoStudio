// ═══════════════════════════════════════════════════════════
// CrossSectionExtender — Implementation
// ═══════════════════════════════════════════════════════════

#include "cross_section_extender.h"
#include "road_profile.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace LM
{

int CrossSectionExtender::GetMaxRightLaneId(const odr::LaneSection& section)
{
    int maxId = 0;
    for (const auto& [id, lane] : section.id_to_lane)
    {
        if (id > maxId && lane.type != "median")
            maxId = id;
    }
    return maxId;
}

int CrossSectionExtender::GetMinLeftLaneId(const odr::LaneSection& section)
{
    int minId = 0;
    for (const auto& [id, lane] : section.id_to_lane)
    {
        if (id < minId)
            minId = id;
    }
    return minId;
}

void CrossSectionExtender::AddRightLane(odr::LaneSection& section, double s0,
    double sEnd, const std::string& laneType, double width)
{
    int newId = GetMaxRightLaneId(section) + 1;
    if (newId <= 0) newId = 1; // safety

    odr::Lane newLane(section.road_id, s0, newId, false, laneType);

    // Set lane width as a constant CubicSpline
    odr::Poly3 widthPoly(s0, width, 0, 0, 0);
    newLane.lane_width.s0_to_poly.clear();
    newLane.lane_width.s0_to_poly.emplace(s0, widthPoly);

    // Set predecessor/successor for lane continuity
    newLane.predecessor = newId;
    newLane.successor = newId;

    section.id_to_lane.emplace(newId, std::move(newLane));
}

void CrossSectionExtender::AddLeftLane(odr::LaneSection& section, double s0,
    double sEnd, const std::string& laneType, double width)
{
    int newId = GetMinLeftLaneId(section) - 1;
    if (newId >= 0) newId = -1; // safety

    odr::Lane newLane(section.road_id, s0, newId, false, laneType);

    // Set lane width as a constant CubicSpline
    odr::Poly3 widthPoly(s0, width, 0, 0, 0);
    newLane.lane_width.s0_to_poly.clear();
    newLane.lane_width.s0_to_poly.emplace(s0, widthPoly);

    // Set predecessor/successor for lane continuity
    newLane.predecessor = newId;
    newLane.successor = newId;

    section.id_to_lane.emplace(newId, std::move(newLane));
}

void CrossSectionExtender::RemoveNonDrivingLanes(std::shared_ptr<Road>& road)
{
    if (!road) return;

    for (auto& [s0, section] : road->generated.s_to_lanesection)
    {
        std::vector<int> idsToRemove;
        for (auto& [id, lane] : section.id_to_lane)
        {
            // Keep driving, median, and center (id=0) lanes
            if (id == 0) continue;
            if (lane.type == "driving") continue;
            if (lane.type == "median") continue;
            idsToRemove.push_back(id);
        }
        for (int id : idsToRemove)
        {
            section.id_to_lane.erase(id);
        }
    }
}

void CrossSectionExtender::Apply(std::shared_ptr<Road>& road, const CrossSectionConfig& config)
{
    if (!road) return;

    // First remove existing non-driving lanes (except median)
    RemoveNonDrivingLanes(road);

    double roadLength = road->Length();

    // Apply to each lane section
    for (auto& [s0, section] : road->generated.s_to_lanesection)
    {
        // Find section end
        auto nextIt = road->generated.s_to_lanesection.upper_bound(s0);
        double sEnd = (nextIt != road->generated.s_to_lanesection.end()) ?
            nextIt->first : roadLength;

        // ─── Right side: add lanes from center outward ───
        // Order: driving | bike | bus | parking | shoulder | curb | sidewalk

        if (config.rightHasBikeLane)
        {
            AddRightLane(section, s0, sEnd, "biking", config.rightBikeLaneWidth);
        }
        if (config.rightHasBusLane)
        {
            AddRightLane(section, s0, sEnd, "bus", config.rightBusLaneWidth);
        }
        if (config.rightHasParking)
        {
            AddRightLane(section, s0, sEnd, "parking", config.rightParkingWidth);
        }
        if (config.rightHasShoulder)
        {
            AddRightLane(section, s0, sEnd, "shoulder", config.rightShoulderWidth);
        }
        if (config.rightHasCurb)
        {
            AddRightLane(section, s0, sEnd, "border", config.rightCurbWidth);
        }
        if (config.rightHasSidewalk)
        {
            AddRightLane(section, s0, sEnd, "sidewalk", config.rightSidewalkWidth);
        }

        // ─── Left side: add lanes from center outward ───
        // Order: driving | bike | bus | parking | shoulder | curb | sidewalk

        if (config.leftHasBikeLane)
        {
            AddLeftLane(section, s0, sEnd, "biking", config.leftBikeLaneWidth);
        }
        if (config.leftHasBusLane)
        {
            AddLeftLane(section, s0, sEnd, "bus", config.leftBusLaneWidth);
        }
        if (config.leftHasParking)
        {
            AddLeftLane(section, s0, sEnd, "parking", config.leftParkingWidth);
        }
        if (config.leftHasShoulder)
        {
            AddLeftLane(section, s0, sEnd, "shoulder", config.leftShoulderWidth);
        }
        if (config.leftHasCurb)
        {
            AddLeftLane(section, s0, sEnd, "border", config.leftCurbWidth);
        }
        if (config.leftHasSidewalk)
        {
            AddLeftLane(section, s0, sEnd, "sidewalk", config.leftSidewalkWidth);
        }
    }

    // Re-derive lane borders to update inner/outer borders for new lanes
    road->generated.DeriveLaneBorders();

    // Re-place markings for the new lane configuration
    road->generated.PlaceMarkings();

    spdlog::debug("CrossSectionExtender: Applied to road {}", road->ID());
}

CrossSectionConfig CrossSectionExtender::Extract(const std::shared_ptr<Road>& road)
{
    CrossSectionConfig config;
    if (!road) return config;

    // Scan the first lane section to determine what non-driving lanes exist
    if (road->generated.s_to_lanesection.empty()) return config;

    const auto& firstSection = road->generated.s_to_lanesection.begin()->second;

    for (const auto& [id, lane] : firstSection.id_to_lane)
    {
        if (id > 0) // Right side
        {
            if (lane.type == "shoulder") { config.rightHasShoulder = true; }
            else if (lane.type == "sidewalk") { config.rightHasSidewalk = true; }
            else if (lane.type == "border") { config.rightHasCurb = true; }
            else if (lane.type == "biking") { config.rightHasBikeLane = true; }
            else if (lane.type == "bus") { config.rightHasBusLane = true; }
            else if (lane.type == "parking") { config.rightHasParking = true; }
        }
        else if (id < 0) // Left side
        {
            if (lane.type == "shoulder") { config.leftHasShoulder = true; }
            else if (lane.type == "sidewalk") { config.leftHasSidewalk = true; }
            else if (lane.type == "border") { config.leftHasCurb = true; }
            else if (lane.type == "biking") { config.leftHasBikeLane = true; }
            else if (lane.type == "bus") { config.leftHasBusLane = true; }
            else if (lane.type == "parking") { config.leftHasParking = true; }
        }
    }

    return config;
}

} // namespace LM
