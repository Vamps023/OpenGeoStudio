#include "DrapeOntoTerrain.hpp"

#include "../../core/map/CoordinateTransform.hpp"
#include "../../core/osm/DemElevationSampler.hpp"
#include "../../engine/lanemaker/libOpenDRIVE/include/RefLine.h"

#include <QFileInfo>
#include <cmath>
#include <algorithm>

namespace roadstudio {

DrapeResult drapeRoadOntoTerrain(
    const odr::RefLine& refLine,
    double mapCenterLat, double mapCenterLon,
    const QString& demPath, int samples)
{
    DrapeResult result;

    if (refLine.length <= 0.0)
    {
        result.error = QStringLiteral("Road has zero length");
        return result;
    }
    if (demPath.isEmpty() || !QFileInfo::exists(demPath))
    {
        result.error = QStringLiteral("No terrain heightmap found — export terrain first");
        return result;
    }

    osm::DemElevationSampler sampler;
    if (!sampler.loadFromGeoTiff(demPath))
    {
        result.error = QStringLiteral("Failed to load terrain heightmap: %1").arg(demPath);
        return result;
    }

    using map::CoordinateTransform;
    // LaneMaker world = Web Mercator meter offsets from the map center
    const auto center = CoordinateTransform::lonLatToMercator(mapCenterLon, mapCenterLat);

    const int n = std::max(2, samples);
    for (int i = 0; i < n; ++i)
    {
        const double s = refLine.length * static_cast<double>(i) / static_cast<double>(n - 1);
        const odr::Vec3D pos = refLine.get_xyz(s);

        const auto ll = CoordinateTransform::mercatorToLonLat(center.x + pos[0], center.y + pos[1]);

        const double z = sampler.sampleLonLat(ll.x, ll.y);  // x=lon, y=lat
        if (std::isnan(z))
        {
            result.error = QStringLiteral("Terrain does not cover the road extent");
            return result;
        }
        result.points[s] = z;
    }

    result.ok = true;
    return result;
}

} // namespace roadstudio
