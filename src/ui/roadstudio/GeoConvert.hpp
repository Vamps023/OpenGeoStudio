#pragma once

// ============================================================
// GeoConvert — Geographic coordinate conversion utilities
// ============================================================
//
// Mirrors the equirectangular projection used in SkiaViewport.tsx
// and types.ts (geoToLocal / localToGeo).
//
// Local coordinates are in meters relative to a reference origin
// (refLat, refLon). X is east, Y is north.
//

#include <cmath>

namespace roads {

// Earth circumference in meters (WGS84 equatorial)
constexpr double kEarthCircumference = 40075016.686;
constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;

// Convert geographic (lat, lon) to local meters (x=east, y=north)
// relative to a reference origin (refLat, refLon).
inline void geoToLocal(double lat, double lon,
                        double refLat, double refLon,
                        double& outX, double& outY) {
    const double latRad = lat * kDegToRad;
    const double refLatRad = refLat * kDegToRad;
    outX = (lon - refLon) * kDegToRad * kEarthCircumference * std::cos(refLatRad) / 360.0;
    outY = (lat - refLat) * kDegToRad * kEarthCircumference / 360.0;
}

// Convert local meters (x=east, y=north) back to geographic (lat, lon).
inline void localToGeo(double x, double y,
                        double refLat, double refLon,
                        double& outLat, double& outLon) {
    const double refLatRad = refLat * kDegToRad;
    outLat = refLat + y * 360.0 / (kEarthCircumference * kDegToRad);
    outLon = refLon + x * 360.0 / (kEarthCircumference * std::cos(refLatRad) * kDegToRad);
}

// Meters per pixel at a given zoom level and latitude.
// Used for the map scale in the 2D viewport.
inline double metersPerPixel(double zoom, double lat) {
    const double latRad = lat * kDegToRad;
    return kEarthCircumference * std::cos(latRad) / std::pow(2.0, zoom + 8);
}

// Pixels per meter (inverse of metersPerPixel).
inline double pixelsPerMeter(double zoom, double lat) {
    return 1.0 / metersPerPixel(zoom, lat);
}

// Haversine distance between two geographic points in meters.
inline double distanceMeters(double lat1, double lon1,
                              double lat2, double lon2) {
    const double dLat = (lat2 - lat1) * kDegToRad;
    const double dLon = (lon2 - lon1) * kDegToRad;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
        std::cos(lat1 * kDegToRad) * std::cos(lat2 * kDegToRad) *
        std::sin(dLon / 2) * std::sin(dLon / 2);
    const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    constexpr double kEarthRadius = 6371000.0; // meters
    return kEarthRadius * c;
}

} // namespace roads
