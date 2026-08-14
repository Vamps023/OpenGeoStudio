#pragma once

// ============================================================
// CRS — Coordinate Reference System (inspired by QgsCoordinateReferenceSystem)
// Supports EPSG:4326 (WGS84 lat/lon) and EPSG:3857 (Web Mercator)
// without requiring PROJ library for the common cases.
// ============================================================

#include <string>
#include <cmath>
#include <optional>

namespace map {

enum class CRSId {
    Invalid,
    EPSG_4326,  // WGS84 geographic (lat/lon in degrees)
    EPSG_3857,  // Web Mercator (x/y in meters)
};

enum class Units {
    Degrees,
    Meters,
};

class CoordinateReferenceSystem {
public:
    CoordinateReferenceSystem() = default;
    explicit CoordinateReferenceSystem(CRSId id) : m_id(id) {}

    static CoordinateReferenceSystem fromEpsg(int epsg) {
        if (epsg == 4326) return CoordinateReferenceSystem(CRSId::EPSG_4326);
        if (epsg == 3857) return CoordinateReferenceSystem(CRSId::EPSG_3857);
        return CoordinateReferenceSystem();
    }

    static CoordinateReferenceSystem fromAuthId(const std::string& authId) {
        if (authId == "EPSG:4326") return fromEpsg(4326);
        if (authId == "EPSG:3857") return fromEpsg(3857);
        return CoordinateReferenceSystem();
    }

    bool isValid() const { return m_id != CRSId::Invalid; }
    CRSId id() const { return m_id; }

    std::string authId() const {
        switch (m_id) {
            case CRSId::EPSG_4326: return "EPSG:4326";
            case CRSId::EPSG_3857: return "EPSG:3857";
            default: return "EPSG:0";
        }
    }

    std::string description() const {
        switch (m_id) {
            case CRSId::EPSG_4326: return "WGS 84 (geographic)";
            case CRSId::EPSG_3857: return "WGS 84 / Pseudo-Mercator";
            default: return "Invalid";
        }
    }

    Units units() const {
        return (m_id == CRSId::EPSG_4326) ? Units::Degrees : Units::Meters;
    }

    bool isGeographic() const { return m_id == CRSId::EPSG_4326; }

    bool operator==(const CoordinateReferenceSystem& other) const {
        return m_id == other.m_id;
    }

private:
    CRSId m_id = CRSId::Invalid;
};

} // namespace map
