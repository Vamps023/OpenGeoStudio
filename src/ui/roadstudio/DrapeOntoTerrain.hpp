// ============================================================
// DrapeOntoTerrain — sample the project DEM along a road's
// centerline and return (s, z) control points for the
// elevation profile editor. Lives in the app layer (not the
// LaneMaker engine) because it reads the project DEM.
// ============================================================
#pragma once

#include <map>
#include <QString>

namespace odr { struct RefLine; }

namespace roadstudio {

struct DrapeResult {
    bool ok = false;
    QString error;
    std::map<double, double> points;  // s → elevation (m)
};

// Samples `samples` elevations along the road (s = 0..length) at the
// world-space positions computed from LaneMaker's map center.
DrapeResult drapeRoadOntoTerrain(
    const odr::RefLine& refLine,
    double mapCenterLat, double mapCenterLon,
    const QString& demPath, int samples = 40);

} // namespace roadstudio

