#pragma once

// ============================================================
// CRS.hpp — Coordinate Reference System definition
//
// Stores the COMPLETE CRS definition, not just an EPSG code.
// Backed by PROJ's database (proj.db) for metadata, WKT2,
// PROJJSON, and coordinate operations.
//
// This is the canonical CRS type for OpenGeoStudio. All
// geospatial datasets entering the World Model must carry a
// valid CRSDefinition.
// ============================================================

#include <string>
#include <optional>
#include <cstdint>

namespace gis {

// CRS kind — matches PROJ/OGC classification
enum class CRSKind {
    Unknown,
    Geographic,       // lat/lon (e.g. EPSG:4326)
    Projected,        // x/y in projected units (e.g. UTM, EPSG:32643)
    Geocentric,       // X/Y/Z from earth center
    Vertical,         // height/depth (e.g. EGM96 height)
    Compound,         // horizontal + vertical (e.g. EPSG:32643 + EGM96)
    Engineering,      // local/Engineering frame
    Bound,            // CRS with a bound transformation
    Derived,          // derived from another CRS
};

// Unit of measure
enum class Unit {
    Unknown,
    Degree,            // geographic angular degree
    Radian,            // geographic angular radian
    Metre,             // linear metre
    Foot,              // international foot
    USFoot,            // US survey foot
    Kilometre,
    NauticalMile,
};

// Bounding box for area of use (in WGS84 lat/lon degrees)
struct BoundingBox {
    double west = 0.0;    // min longitude
    double south = 0.0;   // min latitude
    double east = 0.0;    // max longitude
    double north = 0.0;   // max latitude

    bool isValid() const {
        return west != 0.0 || south != 0.0 || east != 0.0 || north != 0.0;
    }

    bool contains(double lon, double lat) const {
        return lon >= west && lon <= east && lat >= south && lat <= north;
    }

    double centerLon() const { return (west + east) * 0.5; }
    double centerLat() const { return (south + north) * 0.5; }
};

// ============================================================
// CRSDefinition — the full CRS definition
//
// Stores authority/code, name, WKT2, PROJJSON, kind, units,
// area of use, datum, ellipsoid, and vertical component info.
// This is what gets persisted in project files and used for
// transformations.
// ============================================================
struct CRSDefinition {
    // Authority + code (e.g. "EPSG", 4326)
    std::string authority;       // "EPSG", "ESRI", "IGNF", "OGC", ...
    int code = 0;                // 4326, 32643, ...

    // Human-readable name (e.g. "WGS 84 / UTM zone 43N")
    std::string name;

    // Full CRS definitions from PROJ
    std::string wkt2;            // WKT2:2019 string
    std::string projJson;        // PROJJSON representation
    std::string projString;      // PROJ string (legacy, if available)

    // Classification
    CRSKind kind = CRSKind::Unknown;
    Unit unit = Unit::Unknown;

    // Area of use
    BoundingBox areaOfUse;
    std::string areaOfUseName;   // e.g. "India", "World", "Europe"

    // Datum / ellipsoid metadata
    std::string datum;           // e.g. "WGS 84"
    std::string ellipsoid;       // e.g. "WGS 84"
    double semiMajorAxis = 0.0;  // ellipsoid semi-major axis (metres)
    double inverseFlattening = 0.0;

    // Vertical component
    bool hasVerticalComponent = false;
    std::string verticalCrsName; // e.g. "EGM96 height"
    int verticalCrsCode = 0;     // e.g. 5773

    // Misc
    bool dynamic = false;        // dynamic CRS (plate-fixed)
    bool deprecated = false;

    // --------------------------------------------------------
    // Convenience
    // --------------------------------------------------------

    bool isValid() const {
        return !authority.empty() && code > 0;
    }

    bool isGeographic() const { return kind == CRSKind::Geographic; }
    bool isProjected() const { return kind == CRSKind::Projected; }
    bool isGeocentric() const { return kind == CRSKind::Geocentric; }
    bool isVertical() const { return kind == CRSKind::Vertical; }
    bool isCompound() const { return kind == CRSKind::Compound; }
    bool isEngineering() const { return kind == CRSKind::Engineering; }

    // Authority:code identifier (e.g. "EPSG:4326")
    std::string authId() const {
        if (authority.empty() || code <= 0) return "";
        return authority + ":" + std::to_string(code);
    }

    // Unit as string
    static const char* unitToString(Unit u) {
        switch (u) {
            case Unit::Degree:       return "degree";
            case Unit::Radian:       return "radian";
            case Unit::Metre:        return "metre";
            case Unit::Foot:         return "foot";
            case Unit::USFoot:       return "US survey foot";
            case Unit::Kilometre:    return "kilometre";
            case Unit::NauticalMile: return "nautical mile";
            default:                 return "unknown";
        }
    }

    static const char* kindToString(CRSKind k) {
        switch (k) {
            case CRSKind::Geographic:   return "Geographic";
            case CRSKind::Projected:    return "Projected";
            case CRSKind::Geocentric:   return "Geocentric";
            case CRSKind::Vertical:     return "Vertical";
            case CRSKind::Compound:     return "Compound";
            case CRSKind::Engineering:  return "Engineering";
            case CRSKind::Bound:        return "Bound";
            case CRSKind::Derived:      return "Derived";
            default:                    return "Unknown";
        }
    }

    bool operator==(const CRSDefinition& other) const {
        return authority == other.authority && code == other.code;
    }

    bool operator!=(const CRSDefinition& other) const {
        return !(*this == other);
    }
};

// Common CRS constants
namespace CRS {
    constexpr int EPSG_WGS84 = 4326;           // WGS 84 geographic
    constexpr int EPSG_WEB_MERCATOR = 3857;    // WGS 84 / Pseudo-Mercator
    constexpr int EPSG_UTM_NORTH_BASE = 32600; // UTM northern hemisphere base
    constexpr int EPSG_UTM_SOUTH_BASE = 32700; // UTM southern hemisphere base
}

} // namespace gis
