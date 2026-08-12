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
#include "LaneSection.h"
#include "Lane.h"
#include "RefLine.h"
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

        for (const auto& road : roads) {
            if (road.points.size() < 2) continue;

            // Compute local coordinates and total length
            std::vector<double> localX, localY;
            double totalLen = 0;
            for (int i = 0; i < road.points.size(); ++i) {
                double lx, ly;
                roads::geoToLocal(road.points[i].lat, road.points[i].lon,
                                  refLat, refLon, lx, ly);
                localX.push_back(lx);
                localY.push_back(ly);
                if (i > 0) {
                    totalLen += std::sqrt((localX[i]-localX[i-1])*(localX[i]-localX[i-1]) +
                                          (localY[i]-localY[i-1])*(localY[i]-localY[i-1]));
                }
            }
            if (totalLen < 1e-6) continue;

            // Create road with proper constructor
            odr::Road odrRoad(road.id.toStdString(), totalLen, "-1", road.name.toStdString());

            // Build refline with Line geometry segments between control points
            odr::RefLine refLine(road.id.toStdString(), totalLen);

            double s0 = 0;
            for (int i = 1; i < road.points.size(); ++i) {
                double dx = localX[i] - localX[i-1];
                double dy = localY[i] - localY[i-1];
                double segLen = std::sqrt(dx*dx + dy*dy);
                if (segLen < 1e-9) continue;

                double hdg = std::atan2(dy, dx);
                auto lineGeo = std::make_unique<odr::Line>(s0, localX[i-1], localY[i-1], hdg, segLen);
                refLine.s0_to_geometry[s0] = std::move(lineGeo);
                s0 += segLen;
            }

            // Set elevation profile from control point z values
            for (int i = 0; i < road.points.size(); ++i) {
                double cpS = 0;
                for (int j = 1; j <= i; ++j) {
                    cpS += std::sqrt((localX[j]-localX[j-1])*(localX[j]-localX[j-1]) +
                                     (localY[j]-localY[j-1])*(localY[j]-localY[j-1]));
                }
                refLine.elevation_profile.set(cpS, road.points[i].z);
            }

            odrRoad.ref_line = std::move(refLine);

            // Add lane section with proper lanes
            odr::LaneSection laneSection(road.id.toStdString(), 0.0);

            int laneCount = road.laneCount > 0 ? road.laneCount : 2;
            double laneWidth = road.width / laneCount;

            // Center lane (id=0)
            odr::Lane centerLane(road.id.toStdString(), 0.0, 0, true, "center");
            laneSection.id_to_lane[0] = centerLane;

            // Driving lanes: negative = right side, positive = left side
            int lanesPerSide = laneCount / 2;
            if (lanesPerSide < 1) lanesPerSide = 1;

            for (int i = 1; i <= lanesPerSide; ++i) {
                // Left lanes (positive IDs)
                odr::Lane leftLane(road.id.toStdString(), 0.0, i, false, "driving");
                leftLane.lane_width.set(0, laneWidth);
                laneSection.id_to_lane[i] = leftLane;

                // Right lanes (negative IDs)
                odr::Lane rightLane(road.id.toStdString(), 0.0, -i, false, "driving");
                rightLane.lane_width.set(0, laneWidth);
                laneSection.id_to_lane[-i] = rightLane;
            }

            odrRoad.s_to_lanesection[0.0] = laneSection;
            odrRoad.s_to_type[0.0] = "town";

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
