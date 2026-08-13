// RoadGeometryHelper — Shared geometry generation implementation

#include "RoadGeometryHelper.hpp"
#include "../GeoConvert.hpp"

#include <cmath>

#ifdef ENABLE_LANEMAKER
#include "lanemaker_curve.hpp"
#endif

roads::StagedGeometry RoadGeometryHelper::connectRays(
    roads::Point2D start, roads::Vec2 startDir,
    roads::Point2D end, roads::Vec2 endDir,
    int numSamples) {

    roads::StagedGeometry geo;
    geo.startPos = start;
    geo.startDir = startDir;
    geo.endPos = end;
    geo.endDir = endDir;

#ifdef ENABLE_LANEMAKER
    odr::Vec2D startPos = {start.x, start.y};
    odr::Vec2D startHdg = {startDir.x, startDir.y};
    odr::Vec2D endPos = {end.x, end.y};
    odr::Vec2D endHdg = {endDir.x, endDir.y};

    try {
        auto geometry = LM::ConnectRays(startPos, startHdg, endPos, endHdg);
        if (geometry && geometry->length > 0) {
            geo.length = geometry->length;
            for (int i = 0; i <= numSamples; ++i) {
                double s = geo.length * i / numSamples;
                auto pt = geometry->get_point(s);
                geo.samples.append({pt[0], pt[1]});
            }
            auto grad = geometry->get_grad(geo.length);
            double len = std::sqrt(grad[0]*grad[0] + grad[1]*grad[1]);
            if (len > 1e-9) {
                geo.endDir = {grad[0]/len, grad[1]/len};
            }
            return geo;
        }
    } catch (...) {
        // Fall through to line fallback
    }
#endif

    // Fallback: straight line
    double dx = end.x - start.x;
    double dy = end.y - start.y;
    geo.length = std::sqrt(dx*dx + dy*dy);
    for (int i = 0; i <= numSamples; ++i) {
        double t = static_cast<double>(i) / numSamples;
        geo.samples.append({start.x + dx * t, start.y + dy * t});
    }
    if (geo.length > 1e-6) {
        geo.endDir = {dx/geo.length, dy/geo.length};
    }
    return geo;
}

roads::Vec2 RoadGeometryHelper::directionBetween(roads::Point2D from, roads::Point2D to) {
    double dx = to.x - from.x;
    double dy = to.y - from.y;
    double len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-9) return {1.0, 0.0};
    return {dx/len, dy/len};
}

QList<roads::Point2D> RoadGeometryHelper::roadToLocal(
    const roads::Road& road, double refLat, double refLon) {
    QList<roads::Point2D> result;
    for (const auto& cp : road.points) {
        double lx, ly;
        roads::geoToLocal(cp.lat, cp.lon, refLat, refLon, lx, ly);
        result.append({lx, ly});
    }
    return result;
}

bool RoadGeometryHelper::snapToEndpoints(
    const roads::Road& road,
    roads::Point2D query,
    double refLat, double refLon,
    double thresholdMeters,
    roads::Point2D& outPoint,
    roads::Vec2& outDir,
    bool& outIsStart) {

    if (road.points.size() < 2) return false;

    // Check start endpoint
    double startLx, startLy;
    roads::geoToLocal(road.points[0].lat, road.points[0].lon,
                       refLat, refLon, startLx, startLy);
    double startDist = std::hypot(startLx - query.x, startLy - query.y);

    // Check end endpoint
    int n = road.points.size();
    double endLx, endLy;
    roads::geoToLocal(road.points[n-1].lat, road.points[n-1].lon,
                       refLat, refLon, endLx, endLy);
    double endDist = std::hypot(endLx - query.x, endLy - query.y);

    if (startDist < thresholdMeters && startDist <= endDist) {
        outPoint = {startLx, startLy};
        // Direction from first to second point
        double p2lx, p2ly;
        roads::geoToLocal(road.points[1].lat, road.points[1].lon,
                           refLat, refLon, p2lx, p2ly);
        outDir = directionBetween({startLx, startLy}, {p2lx, p2ly});
        outIsStart = true;
        return true;
    }

    if (endDist < thresholdMeters) {
        outPoint = {endLx, endLy};
        // Direction from second-to-last to last point
        double p2lx, p2ly;
        roads::geoToLocal(road.points[n-2].lat, road.points[n-2].lon,
                           refLat, refLon, p2lx, p2ly);
        outDir = directionBetween({p2lx, p2ly}, {endLx, endLy});
        outIsStart = false;
        return true;
    }

    return false;
}
