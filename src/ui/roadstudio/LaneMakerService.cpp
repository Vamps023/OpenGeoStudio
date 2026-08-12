// LaneMakerService — Bridge between Road Studio and LaneMaker implementation

#include "LaneMakerService.hpp"
#include "RoadTypes.hpp"
#include "GeoConvert.hpp"

#include <cmath>
#include <QUuid>

#ifdef ENABLE_LANEMAKER
#include "curve_fitting.h"
#include "Geometries/RoadGeometry.h"
#include "Geometries/Line.h"
#include "Geometries/Arc.h"
#include "Geometries/Spiral.h"
#include "Geometries/ParamPoly3.h"
#endif

roads::Road LaneMakerService::generateRoad(
    double startLat, double startLon,
    double startDirX, double startDirY,
    double endLat, double endLon,
    double endDirX, double endDirY,
    double refLat, double refLon,
    double width, int laneCount, int numSamples) {

    roads::Road road;
    road.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    road.name = "LaneMaker Road";
    road.width = width;
    road.laneCount = laneCount;
    road.color = "#4488ff";
    road.formatVersion = 1;

#ifdef ENABLE_LANEMAKER
    // Convert geo positions to local meters
    double startLocalX, startLocalY;
    roads::geoToLocal(startLat, startLon, refLat, refLon, startLocalX, startLocalY);

    double endLocalX, endLocalY;
    roads::geoToLocal(endLat, endLon, refLat, refLon, endLocalX, endLocalY);

    // Normalize directions
    double dirLen = std::sqrt(startDirX * startDirX + startDirY * startDirY);
    if (dirLen < 1e-9) dirLen = 1.0;
    odr::Vec2D startHdg = {startDirX / dirLen, startDirY / dirLen};

    dirLen = std::sqrt(endDirX * endDirX + endDirY * endDirY);
    if (dirLen < 1e-9) dirLen = 1.0;
    odr::Vec2D endHdg = {endDirX / dirLen, endDirY / dirLen};

    odr::Vec2D startPos = {startLocalX, startLocalY};
    odr::Vec2D endPos = {endLocalX, endLocalY};

    // Use LaneMaker's ConnectRays to generate the road geometry
    auto geometry = LM::ConnectRays(startPos, startHdg, endPos, endHdg);

    if (geometry) {
        // Sample the generated geometry
        double length = geometry->length;
        if (length > 0) {
            for (int i = 0; i <= numSamples; ++i) {
                double s = length * i / numSamples;
                auto pt = geometry->get_point(s);

                // Convert local meters back to geo
                double lat, lon;
                roads::localToGeo(pt[0], pt[1], refLat, refLon, lat, lon);

                roads::ControlPoint cp;
                cp.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                cp.lat = lat;
                cp.lon = lon;
                cp.z = 0;
                cp.type = roads::ControlPoint::Type::Smooth;
                road.points.append(cp);
            }
            return road;
        }
    }
#endif

    // Fallback: straight line between start and end
    roads::ControlPoint startCp;
    startCp.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    startCp.lat = startLat;
    startCp.lon = startLon;
    startCp.type = roads::ControlPoint::Type::Smooth;
    road.points.append(startCp);

    roads::ControlPoint endCp;
    endCp.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    endCp.lat = endLat;
    endCp.lon = endLon;
    endCp.type = roads::ControlPoint::Type::Smooth;
    road.points.append(endCp);

    return road;
}

roads::Road LaneMakerService::generateSimpleRoad(
    double startLat, double startLon,
    double startDirX, double startDirY,
    double endLat, double endLon,
    double refLat, double refLon,
    double width, int laneCount, int numSamples) {

    // For simple road, use the end direction as the direction from start to end
    double startLocalX, startLocalY;
    roads::geoToLocal(startLat, startLon, refLat, refLon, startLocalX, startLocalY);
    double endLocalX, endLocalY;
    roads::geoToLocal(endLat, endLon, refLat, refLon, endLocalX, endLocalY);

    double endDirX = endLocalX - startLocalX;
    double endDirY = endLocalY - startLocalY;

    return generateRoad(startLat, startLon, startDirX, startDirY,
                        endLat, endLon, endDirX, endDirY,
                        refLat, refLon, width, laneCount, numSamples);
}

QString LaneMakerService::version() {
#ifdef ENABLE_LANEMAKER
    return "LaneMaker 1.0 (libOpenDRIVE + xodr)";
#else
    return "LaneMaker not available (built without ENABLE_LANEMAKER)";
#endif
}

bool LaneMakerService::exportOpenDrive(const QString& filePath, const QList<roads::Road>& roads,
                                       double refLat, double refLon) {
#ifdef ENABLE_LANEMAKER
    try {
        odr::OpenDriveMap odrMap;

        // Convert each road to OpenDRIVE format
        for (const auto& road : roads) {
            odr::Road odrRoad;
            odrRoad.id = road.id.toStdString();
            odrRoad.name = road.name.toStdString();
            odrRoad.junction = "-1"; // Not a connecting road

            // Build refline from control points
            odr::RefLine refLine;
            refLine.length = 0;

            for (int i = 0; i < road.points.size(); ++i) {
                double localX, localY;
                roads::geoToLocal(road.points[i].lat, road.points[i].lon,
                                  refLat, refLon, localX, localY);

                if (i == 0) {
                    refLine.x_offset = localX;
                    refLine.y_offset = localY;
                } else if (i == 1) {
                    double prevX, prevY;
                    roads::geoToLocal(road.points[i-1].lat, road.points[i-1].lon,
                                      refLat, refLon, prevX, prevY);
                    double dx = localX - prevX;
                    double dy = localY - prevY;
                    double len = std::sqrt(dx*dx + dy*dy);
                    refLine.heading = std::atan2(dy, dx);
                    refLine.length = len;
                } else {
                    double prevX, prevY;
                    roads::geoToLocal(road.points[i-1].lat, road.points[i-1].lon,
                                      refLat, refLon, prevX, prevY);
                    double dx = localX - prevX;
                    double dy = localY - prevY;
                    refLine.length += std::sqrt(dx*dx + dy*dy);
                }
            }

            odrRoad.ref_line = refLine;
            odrMap.id_to_road[odrRoad.id] = odrRoad;
        }

        odrMap.export_file(filePath.toStdString());
        return true;
    } catch (const std::exception& e) {
        return false;
    }
#else
    return false;
#endif
}
