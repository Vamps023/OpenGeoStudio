// RoadIntersection — Intersection generation in a separate TU
// to avoid duplicate geometry.hpp definitions between root-level
// and public-level engine headers.

#include "RoadEngineService.hpp"
#include "RoadTypes.hpp"
#include "GeoConvert.hpp"

// Include root-level engine headers — these are now identical to
// the public headers, so there are no duplicate definition errors.
#include "geometry.hpp"
#include "road.hpp"
#include "intersection.hpp"

RoadEngineService::IntersectionResult RoadEngineService::generateIntersection(
    const roads::Road& road1, const roads::Road& road2,
    double refLat, double refLon) {

    IntersectionResult result;
    result.valid = false;

    // Convert to engine roads
    geo::Road engineRoad1, engineRoad2;

    for (const auto& cp : road1.points) {
        double lx, ly;
        roads::geoToLocal(cp.lat, cp.lon, refLat, refLon, lx, ly);
        geo::ControlPoint gcp;
        gcp.position = {lx, ly};
        gcp.z = cp.z;
        gcp.type = (cp.type == roads::ControlPoint::Type::Smooth) ? "smooth" : "corner";
        gcp.hasHandleIn = false;
        gcp.hasHandleOut = false;
        engineRoad1.points.push_back(gcp);
    }
    engineRoad1.width = road1.width;
    engineRoad1.laneCount = road1.laneCount;

    for (const auto& cp : road2.points) {
        double lx, ly;
        roads::geoToLocal(cp.lat, cp.lon, refLat, refLon, lx, ly);
        geo::ControlPoint gcp;
        gcp.position = {lx, ly};
        gcp.z = cp.z;
        gcp.type = (cp.type == roads::ControlPoint::Type::Smooth) ? "smooth" : "corner";
        gcp.hasHandleIn = false;
        gcp.hasHandleOut = false;
        engineRoad2.points.push_back(gcp);
    }
    engineRoad2.width = road2.width;
    engineRoad2.laneCount = road2.laneCount;

    auto ix = geo::generateIntersection(engineRoad1, engineRoad2, refLat, refLon);

    if (ix.polygon.empty()) return result;

    result.center = {ix.center.x, ix.center.y};
    result.cornerRadius = ix.cornerRadius;
    result.intersectionAngle = ix.intersectionAngle;

    for (const auto& p : ix.polygon) {
        result.polygon.append({p.x, p.y});
    }

    for (const auto& c : ix.corners) {
        for (const auto& p : c.arcPoints) {
            result.filletArcPoints.append({p.x, p.y});
        }
    }

    for (const auto& t : ix.trimLines) {
        result.trimPoints.append({t.centerPt.x, t.centerPt.y});
    }

    for (const auto& p : ix.boundaryIntersections) {
        result.boundaryIntersections.append({p.x, p.y});
    }

    result.valid = true;
    return result;
}
