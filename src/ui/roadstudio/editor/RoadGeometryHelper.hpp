#pragma once

// ============================================================
// RoadGeometryHelper — Shared geometry generation for road editing
// ============================================================
//
// Extracts the generateFlexGeometry logic from RoadViewport2D
// so it can be used by both the viewport and edit sessions
// without duplicating code.
//
// Uses LaneMaker's ConnectRays when ENABLE_LANEMAKER is defined,
// falls back to a straight line otherwise.
//

#include "../RoadTypes.hpp"

class RoadGeometryHelper {
public:
    // Generate geometry between two rays (position + direction)
    // Returns a StagedGeometry with sampled points along the curve.
    static roads::StagedGeometry connectRays(
        roads::Point2D start, roads::Vec2 startDir,
        roads::Point2D end, roads::Vec2 endDir,
        int numSamples = 32);

    // Compute the direction from one point to another (normalized)
    static roads::Vec2 directionBetween(roads::Point2D from, roads::Point2D to);

    // Convert a road's control points to local meters
    static QList<roads::Point2D> roadToLocal(
        const roads::Road& road, double refLat, double refLon);

    // Find the nearest point on a road's centerline to a local point
    // Returns the s-position (arc length) and the distance
    template<typename Container>
    static void nearestPointOnRoad(
        const Container& samples,
        roads::Point2D query,
        double& outS, double& outDist, roads::Point2D& outNearest) {

        outS = 0;
        outDist = 1e18;
        outNearest = query;

        double cumS = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            double dist = std::hypot(samples[i].x - query.x, samples[i].y - query.y);
            if (dist < outDist) {
                outDist = dist;
                outS = cumS;
                outNearest = samples[i];
            }
            if (i + 1 < samples.size()) {
                cumS += std::hypot(samples[i+1].x - samples[i].x,
                                   samples[i+1].y - samples[i].y);
            }
        }
    }

    // Snap a query point to the endpoint of a road (within threshold)
    // Returns true if snapped, fills outPoint and outDir
    static bool snapToEndpoints(
        const roads::Road& road,
        roads::Point2D query,
        double refLat, double refLon,
        double thresholdMeters,
        roads::Point2D& outPoint,
        roads::Vec2& outDir,
        bool& outIsStart);
};
