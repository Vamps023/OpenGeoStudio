#pragma once

// ============================================================
// LaneMakerService — Bridge between Road Studio and LaneMaker
// ============================================================
//
// Wraps the LaneMaker library (LM::ConnectRays, LM::FitSpiral, etc.)
// to generate proper road geometry from the 3-click workflow.
//
// The Road Studio's LaneMaker workflow:
//   1. Click start point → sets start position
//   2. Click end point → sets end position and direction
//   3. Click finish → generates road using ConnectRays
//
// ConnectRays composes: Line + Arc + Line, or Spiral, or Bezier
// to connect two rays with G1/G2 continuity.
//

#include "../roadstudio/RoadTypes.hpp"
#include "GeoConvert.hpp"

#include <memory>
#include <vector>
#include <string>

// Forward declarations from LaneMaker
namespace odr {
    struct Vec2D { double x, y; };
    class RoadGeometry;
}

namespace LM {
    std::unique_ptr<odr::RoadGeometry> ConnectRays(
        const odr::Vec2D& startPos, const odr::Vec2D& startHdg,
        const odr::Vec2D& endPos, const odr::Vec2D& endHdg);
}

class LaneMakerService {
public:
    // Generate a road from two rays using LaneMaker's ConnectRays
    // Returns a list of control points in geographic coordinates
    // that represent the generated road geometry.
    //
    // startLat/startLon: Start position in geographic coordinates
    // startDirX/startDirY: Start direction in local meters (normalized)
    // endLat/endLon: End position in geographic coordinates
    // endDirX/endDirY: End direction in local meters (normalized)
    // refLat/refLon: Reference origin for geo/local conversion
    // numSamples: Number of samples to take from the generated geometry
    static roads::Road generateRoad(
        double startLat, double startLon,
        double startDirX, double startDirY,
        double endLat, double endLon,
        double endDirX, double endDirY,
        double refLat, double refLon,
        double width = 8.0, int laneCount = 2,
        int numSamples = 32);

    // Simple 2-point road (no direction at end point)
    // Uses FitArcOrLine which fits either an arc or a line
    static roads::Road generateSimpleRoad(
        double startLat, double startLon,
        double startDirX, double startDirY,
        double endLat, double endLon,
        double refLat, double refLon,
        double width = 8.0, int laneCount = 2,
        int numSamples = 32);

    // Get the LaneMaker version string
    static QString version();
};
