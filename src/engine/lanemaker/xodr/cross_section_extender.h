#pragma once

// ═══════════════════════════════════════════════════════════
// CrossSectionExtender — Add shoulder/sidewalk/curb/bike/bus/
// parking lanes to a generated LaneMaker road.
//
// The LaneMaker LaneProfile system generates "driving" and "median"
// lanes. This extender adds additional non-driving lanes (shoulder,
// sidewalk, curb, bike, bus, parking) as real odr::Lane objects in
// the road's LaneSections after Generate() is called.
//
// The added lanes:
//   - Have proper lane types ("shoulder", "sidewalk", etc.)
//   - Have proper widths via CubicSpline
//   - Have proper inner/outer borders
//   - Survive save/load (stored in odr::Road::s_to_lanesection)
//   - Are included in mesh generation
//   - Are included in validation
// ═══════════════════════════════════════════════════════════

#include "road.h"
#include "LaneSection.h"
#include "Geometries/CubicSpline.h"

#include <memory>
#include <string>

namespace LM
{

// ─── CrossSectionConfig ────────────────────────────────────
// Configuration for non-driving lane additions.
struct CrossSectionConfig
{
    // Right side (positive lane IDs, direction of travel)
    bool rightHasShoulder = false;
    double rightShoulderWidth = 1.5;    // meters
    bool rightHasSidewalk = false;
    double rightSidewalkWidth = 1.5;    // meters
    bool rightHasCurb = false;
    double rightCurbWidth = 0.25;       // meters
    bool rightHasBikeLane = false;
    double rightBikeLaneWidth = 1.2;    // meters
    bool rightHasParking = false;
    double rightParkingWidth = 2.5;     // meters
    bool rightHasBusLane = false;
    double rightBusLaneWidth = 3.5;     // meters

    // Left side (negative lane IDs, opposite direction)
    bool leftHasShoulder = false;
    double leftShoulderWidth = 1.5;
    bool leftHasSidewalk = false;
    double leftSidewalkWidth = 1.5;
    bool leftHasCurb = false;
    double leftCurbWidth = 0.25;
    bool leftHasBikeLane = false;
    double leftBikeLaneWidth = 1.2;
    bool leftHasParking = false;
    double leftParkingWidth = 2.5;
    bool leftHasBusLane = false;
    double leftBusLaneWidth = 3.5;

    // Median (already handled by LaneProfile offset, but we can
    // add a median type lane explicitly)
    bool hasMedian = false;
    double medianWidth = 2.0;
};

// ─── CrossSectionExtender ──────────────────────────────────
class CrossSectionExtender
{
public:
    // Apply cross-section extensions to a road after Generate().
    // This adds shoulder/sidewalk/curb/bike/bus/parking lanes
    // as real odr::Lane objects in each LaneSection.
    //
    // The lanes are added from the center outward:
    //   center | driving lanes | bike | bus | parking | shoulder | curb | sidewalk
    //
    // Lane IDs follow OpenDRIVE convention:
    //   - Right side: positive IDs (1, 2, 3, ...)
    //   - Left side: negative IDs (-1, -2, -3, ...)
    //   - Center: ID 0
    //
    // The existing driving lanes keep their IDs. New lanes get
    // IDs beyond the existing driving lanes.
    static void Apply(std::shared_ptr<Road>& road, const CrossSectionConfig& config);

    // Get the current cross-section config from a road (reads
    // existing non-driving lanes and reconstructs the config).
    static CrossSectionConfig Extract(const std::shared_ptr<Road>& road);

    // Remove all non-driving lanes (except median) from a road.
    // Used before re-applying a new cross-section config.
    static void RemoveNonDrivingLanes(std::shared_ptr<Road>& road);

private:
    // Add a single non-driving lane to a lane section on the right side.
    // laneType: "shoulder", "sidewalk", "curb", "biking", "bus", "parking"
    // The lane is added at the outer edge of the existing lanes.
    static void AddRightLane(odr::LaneSection& section, double s0, double sEnd,
                             const std::string& laneType, double width);

    // Add a single non-driving lane to a lane section on the left side.
    static void AddLeftLane(odr::LaneSection& section, double s0, double sEnd,
                            const std::string& laneType, double width);

    // Get the highest positive lane ID in a section (outermost right lane).
    static int GetMaxRightLaneId(const odr::LaneSection& section);

    // Get the lowest negative lane ID in a section (outermost left lane).
    static int GetMinLeftLaneId(const odr::LaneSection& section);
};

} // namespace LM
