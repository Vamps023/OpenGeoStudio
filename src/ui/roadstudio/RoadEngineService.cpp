// RoadEngineService — Direct C++ road engine integration implementation

#include "RoadEngineService.hpp"

// Note: intersection.hpp is included via a separate translation unit
// (RoadIntersection.cpp) to avoid duplicate geometry.hpp definitions
// between the root-level and public-level engine headers.

geo::Road RoadEngineService::toEngineRoad(const roads::Road& road,
                                           double refLat, double refLon) {
    geo::Road engineRoad;
    engineRoad.id = road.id.toStdString();
    engineRoad.name = road.name.toStdString();
    engineRoad.width = road.width;
    engineRoad.laneCount = road.laneCount;
    engineRoad.formatVersion = road.formatVersion;

    for (const auto& cp : road.points) {
        // Convert geo to local meters
        double localX, localY;
        roads::geoToLocal(cp.lat, cp.lon, refLat, refLon, localX, localY);

        geo::ControlPoint engineCp;
        engineCp.id = cp.id.toStdString();
        engineCp.position = geo::Point2D{localX, localY};
        engineCp.z = cp.z;
        engineCp.type = (cp.type == roads::ControlPoint::Type::Smooth)
            ? "smooth" : "corner";

        if (cp.handleIn) {
            engineCp.handleIn = geo::Point2D{cp.handleIn->x, cp.handleIn->y};
            engineCp.hasHandleIn = true;
        }
        if (cp.handleOut) {
            engineCp.handleOut = geo::Point2D{cp.handleOut->x, cp.handleOut->y};
            engineCp.hasHandleOut = true;
        }

        engineRoad.points.push_back(engineCp);
    }

    return engineRoad;
}

std::vector<roads::Point2D> RoadEngineService::sampleCenterline(
    const roads::Road& road, double refLat, double refLon, int numSamples) {

    if (road.points.size() < 2) return {};

    geo::Road engineRoad = toEngineRoad(road, refLat, refLon);
    // Use the member function (the free function geo::sampleCenterline is
    // declared but not implemented in the header-only engine)
    auto samples = engineRoad.sampleCenterline(numSamples);

    std::vector<roads::Point2D> result;
    result.reserve(samples.size());
    for (const auto& p : samples) {
        result.push_back(fromEnginePoint(p));
    }
    return result;
}

std::vector<roads::Point2D> RoadEngineService::sampleLeftEdge(
    const roads::Road& road, double refLat, double refLon, int numSamples) {

    // sampleLeftEdge/sampleRightEdge are declared but not implemented in the
    // header-only engine. Compute edges from centerline + width offset.
    auto centerline = sampleCenterline(road, refLat, refLon, numSamples);
    if (centerline.size() < 2) return {};

    const double halfWidth = road.width / 2.0;
    std::vector<roads::Point2D> result;
    result.reserve(centerline.size());

    for (size_t i = 0; i < centerline.size(); ++i) {
        // Compute tangent direction
        double dx, dy;
        if (i == 0) {
            dx = centerline[1].x - centerline[0].x;
            dy = centerline[1].y - centerline[0].y;
        } else if (i == centerline.size() - 1) {
            dx = centerline[i].x - centerline[i-1].x;
            dy = centerline[i].y - centerline[i-1].y;
        } else {
            dx = centerline[i+1].x - centerline[i-1].x;
            dy = centerline[i+1].y - centerline[i-1].y;
        }
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) len = 1e-9;
        // Left normal = (-dy, dx) / len
        double nx = -dy / len;
        double ny = dx / len;
        result.push_back({
            centerline[i].x + nx * halfWidth,
            centerline[i].y + ny * halfWidth
        });
    }
    return result;
}

std::vector<roads::Point2D> RoadEngineService::sampleRightEdge(
    const roads::Road& road, double refLat, double refLon, int numSamples) {

    auto centerline = sampleCenterline(road, refLat, refLon, numSamples);
    if (centerline.size() < 2) return {};

    const double halfWidth = road.width / 2.0;
    std::vector<roads::Point2D> result;
    result.reserve(centerline.size());

    for (size_t i = 0; i < centerline.size(); ++i) {
        double dx, dy;
        if (i == 0) {
            dx = centerline[1].x - centerline[0].x;
            dy = centerline[1].y - centerline[0].y;
        } else if (i == centerline.size() - 1) {
            dx = centerline[i].x - centerline[i-1].x;
            dy = centerline[i].y - centerline[i-1].y;
        } else {
            dx = centerline[i+1].x - centerline[i-1].x;
            dy = centerline[i+1].y - centerline[i-1].y;
        }
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-9) len = 1e-9;
        // Right normal = (dy, -dx) / len
        double nx = dy / len;
        double ny = -dx / len;
        result.push_back({
            centerline[i].x + nx * halfWidth,
            centerline[i].y + ny * halfWidth
        });
    }
    return result;
}

std::vector<std::vector<roads::Point2D>> RoadEngineService::generateLaneBoundaries(
    const roads::Road& road, double refLat, double refLon, int numSamples) {

    geo::Road engineRoad = toEngineRoad(road, refLat, refLon);
    auto boundaries = geo::generateLaneBoundaries(engineRoad, numSamples);

    std::vector<std::vector<roads::Point2D>> result;
    result.reserve(boundaries.size());
    for (const auto& boundary : boundaries) {
        std::vector<roads::Point2D> pts;
        pts.reserve(boundary.size());
        for (const auto& p : boundary) {
            pts.push_back(fromEnginePoint(p));
        }
        result.push_back(std::move(pts));
    }
    return result;
}

roads::MeshData RoadEngineService::generateMesh(
    const roads::Road& road, double refLat, double refLon, int numSamples) {

    geo::Road engineRoad = toEngineRoad(road, refLat, refLon);
    auto mesh = geo::generateRoadMesh(engineRoad, numSamples);

    roads::MeshData result;
    // Engine stores vertices as interleaved x,y,z floats
    result.positions = QList<float>(mesh.vertices.begin(), mesh.vertices.end());
    result.normals = QList<float>(mesh.normals.begin(), mesh.normals.end());
    result.uvs = QList<float>(mesh.uvs.begin(), mesh.uvs.end());
    result.indices.reserve(mesh.indices.size());
    for (auto idx : mesh.indices) {
        result.indices.push_back(static_cast<unsigned int>(idx));
    }
    result.vertexCount = static_cast<int>(mesh.vertexCount);

    return result;
}

QString RoadEngineService::exportOpenDrive(
    const QList<roads::Road>& roads, double refLat, double refLon) {

    // OpenDRIVE export is now handled by LaneMakerService::exportOpenDrive
    // which uses libOpenDRIVE directly. This stub remains for compatibility.
    Q_UNUSED(roads)
    Q_UNUSED(refLat)
    Q_UNUSED(refLon)
    return {};
}

// generateIntersection is implemented in RoadIntersection.cpp
// (separate TU to avoid duplicate geometry.hpp definitions)
