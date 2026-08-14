#pragma once

// ============================================================
// CoordinateTransform — Transform between CRS (inspired by QgsCoordinateTransform)
// Built-in formulas for EPSG:4326 ↔ EPSG:3857 (no PROJ dependency)
// ============================================================

#include "CoordinateReferenceSystem.hpp"
#include "MapRectangle.hpp"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace map {

constexpr double EARTH_RADIUS = 6378137.0;  // WGS84 semi-major axis (meters)
constexpr double ORIGIN_SHIFT = 3.14159265358979323846 * EARTH_RADIUS;  // 20037508.3427892
constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
constexpr double RAD_TO_DEG = 180.0 / 3.14159265358979323846;

struct MapPoint {
    double x = 0, y = 0;
    MapPoint() = default;
    MapPoint(double x_, double y_) : x(x_), y(y_) {}
};

class CoordinateTransform {
public:
    CoordinateTransform() = default;
    CoordinateTransform(CRSId source, CRSId destination)
        : m_source(source), m_destination(destination) {}

    CoordinateTransform(const CoordinateReferenceSystem& source,
                        const CoordinateReferenceSystem& destination)
        : m_source(source.id()), m_destination(destination.id()) {}

    bool isValid() const { return m_source != CRSId::Invalid && m_destination != CRSId::Invalid; }
    bool isShortCircuited() const { return m_source == m_destination; }

    MapPoint transform(const MapPoint& point) const {
        if (isShortCircuited()) return point;
        if (m_source == CRSId::EPSG_4326 && m_destination == CRSId::EPSG_3857)
            return lonLatToMercator(point.x, point.y);  // x=lon, y=lat
        if (m_source == CRSId::EPSG_3857 && m_destination == CRSId::EPSG_4326)
            return mercatorToLonLat(point.x, point.y);  // returns x=lon, y=lat
        return point;
    }

    MapRectangle transform(const MapRectangle& rect) const {
        if (isShortCircuited()) return rect;
        MapPoint p1 = transform(MapPoint(rect.xMin, rect.yMin));
        MapPoint p2 = transform(MapPoint(rect.xMax, rect.yMax));
        MapRectangle result;
        result.xMin = std::min(p1.x, p2.x);
        result.yMin = std::min(p1.y, p2.y);
        result.xMax = std::max(p1.x, p2.x);
        result.yMax = std::max(p1.y, p2.y);
        return result;
    }

    // EPSG:4326 (lon, lat in degrees) → EPSG:3857 (x, y in meters)
    static MapPoint lonLatToMercator(double lon, double lat) {
        double x = lon * DEG_TO_RAD * EARTH_RADIUS;
        // Clamp latitude to avoid singularity at poles
        double clampedLat = std::max(-85.05112878, std::min(85.05112878, lat));
        double y = std::log(std::tan((90.0 + clampedLat) * DEG_TO_RAD * 0.5)) * EARTH_RADIUS;
        return MapPoint(x, y);
    }

    // EPSG:3857 (x, y in meters) → EPSG:4326 (lon, lat in degrees)
    static MapPoint mercatorToLonLat(double x, double y) {
        double lon = (x / EARTH_RADIUS) * RAD_TO_DEG;
        double lat = (2.0 * std::atan(std::exp(y / EARTH_RADIUS)) - M_PI * 0.5) * RAD_TO_DEG;
        return MapPoint(lon, lat);
    }

private:
    CRSId m_source = CRSId::Invalid;
    CRSId m_destination = CRSId::Invalid;
};

} // namespace map
