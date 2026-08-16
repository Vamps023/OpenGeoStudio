#pragma once

// ============================================================
// CoordinateConverter — WGS84 → local projected coordinates
// ============================================================
//
// Converts OSM lat/lon to local meters using one of:
//   1. Equirectangular (fast, good for small areas)
//   2. UTM (accurate, standard for engineering)
//
// The local coordinate system has:
//   X = East (meters)
//   Y = North (meters)
//   Z = Up (meters)
//
// A reference origin (refLat, refLon) is used to keep
// coordinates small for floating-point precision.
//

#include <cmath>
#include <QString>

namespace osm {

// WGS84 ellipsoid constants
constexpr double kWGS84A = 6378137.0;           // semi-major axis
constexpr double kWGS84F = 1.0 / 298.257223563; // flattening
constexpr double kWGS84B = kWGS84A * (1.0 - kWGS84F); // semi-minor axis
constexpr double kE2 = (kWGS84A * kWGS84A - kWGS84B * kWGS84B) / (kWGS84A * kWGS84A); // eccentricity²

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;

class CoordinateConverter {
public:
    enum class Method {
        Equirectangular,  // Simple — good for < 10km areas
        UTM               // Accurate — standard for engineering
    };

    // Reference origin
    double refLat = 0.0;
    double refLon = 0.0;
    Method method = Method::Equirectangular;

    // UTM zone (computed from reference longitude)
    int utmZone = 0;
    bool utmNorthern = true;

    // ─── Setup ───

    void setReference(double lat, double lon, Method m = Method::Equirectangular) {
        refLat = lat;
        refLon = lon;
        method = m;

        if (m == Method::UTM) {
            computeUTMZone();
        }
    }

    // Auto-detect reference from OSM bounds center
    void setReferenceFromBounds(double minLat, double minLon,
                                 double maxLat, double maxLon,
                                 Method m = Method::Equirectangular) {
        double centerLat = (minLat + maxLat) / 2.0;
        double centerLon = (minLon + maxLon) / 2.0;
        setReference(centerLat, centerLon, m);
    }

    // ─── Projection ───

    // Convert lat/lon to local meters (relative to reference origin)
    void toLocal(double lat, double lon, double& outX, double& outY) const {
        switch (method) {
        case Method::Equirectangular:
            toLocalEquirectangular(lat, lon, outX, outY);
            break;
        case Method::UTM:
            toLocalUTM(lat, lon, outX, outY);
            break;
        }
    }

    // Convert local meters back to lat/lon
    void toGeo(double x, double y, double& outLat, double& outLon) const {
        switch (method) {
        case Method::Equirectangular:
            toGeoEquirectangular(x, y, outLat, outLon);
            break;
        case Method::UTM:
            toGeoUTM(x, y, outLat, outLon);
            break;
        }
    }

private:
    void computeUTMZone() {
        utmZone = int((refLon + 180.0) / 6.0) + 1;
        utmNorthern = refLat >= 0.0;
    }

    // ─── Equirectangular projection ───
    // Simple, fast. Good for areas < 10km. Uses equirectangular
    // approximation with latitude correction.
    void toLocalEquirectangular(double lat, double lon,
                                 double& outX, double& outY) const {
        const double refLatRad = refLat * kDegToRad;
        const double metersPerDegree = 40075016.686 / 360.0;  // ~111319.9

        outX = (lon - refLon) * metersPerDegree * std::cos(refLatRad);
        outY = (lat - refLat) * metersPerDegree;
    }

    void toGeoEquirectangular(double x, double y,
                               double& outLat, double& outLon) const {
        const double refLatRad = refLat * kDegToRad;
        const double metersPerDegree = 40075016.686 / 360.0;

        outLat = refLat + y / metersPerDegree;
        outLon = refLon + x / (metersPerDegree * std::cos(refLatRad));
    }

    // ─── UTM projection ───
    // Standard Transverse Mercator. Accurate for large areas.
    // Uses Karney's Krüger series (accurate to < 1mm).
    void toLocalUTM(double lat, double lon, double& outX, double& outY) const {
        // Convert to UTM absolute coordinates
        double utmE, utmN;
        latLonToUTM(lat, lon, utmZone, utmNorthern, utmE, utmN);

        // Convert to local relative to reference origin
        double refE, refN;
        latLonToUTM(refLat, refLon, utmZone, utmNorthern, refE, refN);

        outX = utmE - refE;
        outY = utmN - refN;
    }

    void toGeoUTM(double x, double y, double& outLat, double& outLon) const {
        double refE, refN;
        latLonToUTM(refLat, refLon, utmZone, utmNorthern, refE, refN);

        double utmE = refE + x;
        double utmN = refN + y;

        utmToLatLon(utmE, utmN, utmZone, utmNorthern, outLat, outLon);
    }

    // ─── UTM conversion (Karney-Krüger series) ───

    static void latLonToUTM(double lat, double lon, int zone, bool northern,
                             double& utmE, double& utmN) {
        const double a = kWGS84A;
        const double k0 = 0.9996;
        const double e2 = kE2;
        const double ep2 = e2 / (1.0 - e2);

        double latRad = lat * kDegToRad;
        double lonRad = lon * kDegToRad;
        double lonOrigin = (zone - 1) * 6.0 - 180.0 + 3.0;
        double lonOriginRad = lonOrigin * kDegToRad;

        double N = a / std::sqrt(1.0 - e2 * std::sin(latRad) * std::sin(latRad));
        double T = std::tan(latRad) * std::tan(latRad);
        double C = ep2 * std::cos(latRad) * std::cos(latRad);
        double A = std::cos(latRad) * (lonRad - lonOriginRad);

        double M = a * ((1.0 - e2/4.0 - 3.0*e2*e2/64.0 - 5.0*e2*e2*e2/256.0) * latRad
                      - (3.0*e2/8.0 + 3.0*e2*e2/32.0 + 45.0*e2*e2*e2/1024.0) * std::sin(2.0*latRad)
                      + (15.0*e2*e2/256.0 + 45.0*e2*e2*e2/1024.0) * std::sin(4.0*latRad)
                      - (35.0*e2*e2*e2/3072.0) * std::sin(6.0*latRad));

        utmE = k0 * N * (A + (1.0 - T + C) * A*A*A / 6.0
                    + (5.0 - 18.0*T + T*T + 72.0*C - 58.0*ep2) * A*A*A*A*A / 120.0) + 500000.0;

        utmN = k0 * (M + N * std::tan(latRad) * (A*A/2.0
                    + (5.0 - T + 9.0*C + 4.0*C*C) * A*A*A*A / 24.0
                    + (61.0 - 58.0*T + T*T + 600.0*C - 330.0*ep2) * A*A*A*A*A*A / 720.0));

        if (!northern) utmN -= 10000000.0;
    }

    static void utmToLatLon(double utmE, double utmN, int zone, bool northern,
                             double& lat, double& lon) {
        const double a = kWGS84A;
        const double k0 = 0.9996;
        const double e2 = kE2;
        const double ep2 = e2 / (1.0 - e2);
        const double e1 = (1.0 - std::sqrt(1.0 - e2)) / (1.0 + std::sqrt(1.0 - e2));

        if (!northern) utmN += 10000000.0;

        double x = utmE - 500000.0;
        double y = utmN;

        double M = y / k0;
        double mu = M / (a * (1.0 - e2/4.0 - 3.0*e2*e2/64.0 - 5.0*e2*e2*e2/256.0));

        double phi1 = mu + (3.0*e1/2.0 - 27.0*e1*e1*e1/32.0) * std::sin(2.0*mu)
                    + (21.0*e1*e1/16.0 - 55.0*e1*e1*e1*e1/32.0) * std::sin(4.0*mu)
                    + (151.0*e1*e1*e1/96.0) * std::sin(6.0*mu)
                    + (1097.0*e1*e1*e1*e1/512.0) * std::sin(8.0*mu);

        double phi1Rad = phi1;

        double N1 = a / std::sqrt(1.0 - e2 * std::sin(phi1Rad) * std::sin(phi1Rad));
        double T1 = std::tan(phi1Rad) * std::tan(phi1Rad);
        double C1 = ep2 * std::cos(phi1Rad) * std::cos(phi1Rad);
        double R1 = a * (1.0 - e2) / std::pow(1.0 - e2 * std::sin(phi1Rad) * std::sin(phi1Rad), 1.5);
        double D = x / (N1 * k0);

        double latRad = phi1Rad - (N1 * std::tan(phi1Rad) / R1) * (
            D*D/2.0
            - (5.0 + 3.0*T1 + 10.0*C1 - 4.0*C1*C1 - 9.0*ep2) * D*D*D*D / 24.0
            + (61.0 + 90.0*T1 + 298.0*C1 + 45.0*T1*T1 - 252.0*ep2 - 3.0*C1*C1) * D*D*D*D*D*D / 720.0
        );

        double lonOrigin = (zone - 1) * 6.0 - 180.0 + 3.0;
        double lonRad = lonOrigin * kDegToRad + (
            D - (1.0 + 2.0*T1 + C1) * D*D*D / 6.0
            + (5.0 - 2.0*C1 + 28.0*T1 - 3.0*C1*C1 + 8.0*ep2 + 24.0*T1*T1) * D*D*D*D*D / 120.0
        ) / std::cos(phi1Rad);

        lat = latRad * kRadToDeg;
        lon = lonRad * kRadToDeg;
    }
};

} // namespace osm
