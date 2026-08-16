#pragma once

// ============================================================
// OsmExporter — Export road network to OpenDRIVE and GeoJSON
// ============================================================
//
// Exports the OSM-derived road network to standard formats:
//   - OpenDRIVE (.xodr) — for simulation and driving simulators
//   - GeoJSON (.geojson) — for GIS tools and web maps
//
// Both exports preserve the structured road/lane/junction data.
//

#include "OsmTypes.hpp"
#include "RoadNetworkBuilder.hpp"
#include "JunctionDetector.hpp"
#include "RoundaboutGenerator.hpp"
#include "CoordinateConverter.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"
#include "../../engine/road/lane_engine.hpp"

#include <QFile>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include "../logger/Logger.hpp"
#include <vector>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace osm {

// ─── OsmExporter ───
class OsmExporter {
public:
    // ─── Export to OpenDRIVE (.xodr) ───
    struct OpenDriveParams {
        int geoSamples = 100;       // samples per road geometry
        double defaultLaneWidth = 3.5;
        bool includeJunctions = true;
        bool includeSignals = true;
    };

    static bool exportToOpenDrive(const QString& path,
                                   const RoadNetworkBuilder::Result& network,
                                   const std::vector<DetectedJunction>& junctions,
                                   const CoordinateConverter& converter,
                                   const OpenDriveParams& params = {},
                                   QString* errorMsg = nullptr)
    {
        std::ostringstream xml;
        xml << std::fixed << std::setprecision(6);

        // Header
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
        xml << "<OpenDRIVE>\n";
        xml << "  <header revMajor=\"1\" revMinor=\"7\" name=\"OSM Export\""
            << " version=\"1.00\" north=\"0\" south=\"0\" east=\"0\" west=\"0\">\n";
        xml << "    <geoReference refLat=\"" << converter.refLat
            << "\" refLon=\"" << converter.refLon << "\"/>\n";
        xml << "  </header>\n";

        // Build road ID → index map
        std::unordered_map<std::string, int> roadIdToIndex;
        for (int r = 0; r < int(network.roads.size()); r++) {
            roadIdToIndex[network.roads[r].id] = r;
        }

        // Roads
        int roadIdx = 0;
        for (const auto& road : network.roads) {
            double totalLen = road.totalLength();
            if (totalLen <= 0) { roadIdx++; continue; }

            xml << "  <road name=\"" << escapeXml(road.name)
                << "\" length=\"" << totalLen
                << "\" id=\"" << roadIdx
                << "\" junction=\"-1\">\n";

            // Plan view (geometry) — must be wrapped in <planView> per OpenDRIVE spec
            xml << "    <planView>\n";
            double s = 0;
            for (int i = 0; i < road.numSegments(); i++) {
                const auto& seg = road.segment(i);
                geo::Point2D start = seg.startPoint();
                geo::Point2D end = seg.endPoint();
                double segLen = seg.length();
                double heading = std::atan2(end.y - start.y, end.x - start.x);

                xml << "      <geometry s=\"" << s
                    << "\" x=\"" << start.x
                    << "\" y=\"" << start.y
                    << "\" hdg=\"" << heading
                    << "\" length=\"" << segLen << "\">\n";
                xml << "        <line/>\n";
                xml << "      </geometry>\n";
                s += segLen;
            }
            xml << "    </planView>\n";

            // Elevation profile (flat for now)
            xml << "    <elevationProfile>\n";
            xml << "      <elevation s=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/>\n";
            xml << "    </elevationProfile>\n";

            // Lateral profile (flat)
            xml << "    <lateralProfile>\n";
            xml << "    </lateralProfile>\n";

            // Lanes
            xml << "    <lanes>\n";
            if (road.numLaneSections() > 0) {
                const auto& ls = road.laneSection(0);
                xml << "      <laneSection s=\"0\">\n";

                // Center lane (required by OpenDRIVE spec)
                xml << "        <center>\n";
                xml << "          <lane id=\"0\" type=\"border\" level=\"0\">\n";
                xml << "            <width sOffset=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                xml << "          </lane>\n";
                xml << "        </center>\n";

                // Right lanes (negative ids in OpenDRIVE)
                xml << "        <right>\n";
                for (const auto& lane : ls.lanes()) {
                    if (lane.id > 0) {
                        double w = lane.widthAt(0);
                        // OpenDRIVE: right lanes have negative ids
                        xml << "          <lane id=\"-" << lane.id
                            << "\" type=\"" << laneTypeToString(lane.type)
                            << "\" level=\"0\">\n";
                        xml << "            <width sOffset=\"0\" a=\"" << w
                            << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                        xml << "          </lane>\n";
                    }
                }
                xml << "        </right>\n";

                // Left lanes (positive ids in OpenDRIVE)
                if (ls.numLanes() > 1) {
                    bool hasLeft = false;
                    for (const auto& lane : ls.lanes()) {
                        if (lane.id < 0) { hasLeft = true; break; }
                    }
                    if (hasLeft) {
                        xml << "        <left>\n";
                        for (const auto& lane : ls.lanes()) {
                            if (lane.id < 0) {
                                double w = lane.widthAt(0);
                                // OpenDRIVE: left lanes have positive ids
                                int positiveId = -lane.id;
                                xml << "          <lane id=\"" << positiveId
                                    << "\" type=\"" << laneTypeToString(lane.type)
                                    << "\" level=\"0\">\n";
                                xml << "            <width sOffset=\"0\" a=\"" << w
                                    << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                                xml << "          </lane>\n";
                            }
                        }
                        xml << "        </left>\n";
                    }
                }

                xml << "      </laneSection>\n";
            } else {
                // Minimal lane section with just center lane
                xml << "      <laneSection s=\"0\">\n";
                xml << "        <center>\n";
                xml << "          <lane id=\"0\" type=\"border\" level=\"0\">\n";
                xml << "            <width sOffset=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                xml << "          </lane>\n";
                xml << "        </center>\n";
                xml << "      </laneSection>\n";
            }
            xml << "    </lanes>\n";

            // LaneMaker custom profile (required by libOpenDRIVE's loader)
            // Stores lane plans (count + offset) for left and right sides
            xml << "    <roadRunnerProfile>\n";
            if (road.numLaneSections() > 0) {
                const auto& ls = road.laneSection(0);
                int rightLanes = 0, leftLanes = 0;
                for (const auto& lane : ls.lanes()) {
                    if (lane.id > 0) rightLanes++;
                    else if (lane.id < 0) leftLanes++;
                }
                if (leftLanes > 0) {
                    xml << "      <left>\n";
                    xml << "        <section type_s=\"0\" laneCount=\""
                        << leftLanes << "\" offsetX2=\"0\"/>\n";
                    xml << "      </left>\n";
                }
                if (rightLanes > 0) {
                    xml << "      <right>\n";
                    xml << "        <section type_s=\"0\" laneCount=\""
                        << rightLanes << "\" offsetX2=\"0\"/>\n";
                    xml << "      </right>\n";
                }
            }
            xml << "    </roadRunnerProfile>\n";

            xml << "  </road>\n";
            roadIdx++;
        }

        // Junctions — create connecting roads and proper connections
        if (params.includeJunctions) {
            // Connecting road IDs start high to avoid collision with real roads
            const int connectingRoadIdBase = 100000;
            int connectingRoadId = connectingRoadIdBase;
            int juncIdx = 0;

            struct JunctionConnection {
                int connectionId;
                int incomingRoadIdx;
                int connectingRoadIdx;
                std::string contactPoint; // "start" or "end"
            };

            for (const auto& j : junctions) {
                if (j.type == JunctionType::Overpass) { juncIdx++; continue; }
                if (j.roadIds.size() < 2) { juncIdx++; continue; }

                // Collect valid incoming roads and their endpoints at the junction
                struct IncomingRoadInfo {
                    int roadIdx;
                    geo::Point2D endpointAtJunction;  // road endpoint closest to junction
                    std::string contactPoint;         // which end of road meets junction
                };

                std::vector<IncomingRoadInfo> incoming;
                for (const auto& rid : j.roadIds) {
                    auto it = roadIdToIndex.find(rid.toStdString());
                    if (it == roadIdToIndex.end()) continue;

                    const auto& road = network.roads[it->second];
                    double totalLen = road.totalLength();
                    if (totalLen <= 0) continue;

                    geo::Point2D roadStart = road.geometry().positionAt(0);
                    geo::Point2D roadEnd = road.geometry().positionAt(totalLen);
                    double distStart = std::hypot(roadStart.x - j.center.x,
                                                  roadStart.y - j.center.y);
                    double distEnd = std::hypot(roadEnd.x - j.center.x,
                                                roadEnd.y - j.center.y);

                    IncomingRoadInfo info;
                    info.roadIdx = it->second;
                    if (distStart <= distEnd) {
                        info.endpointAtJunction = roadStart;
                        info.contactPoint = "start";
                    } else {
                        info.endpointAtJunction = roadEnd;
                        info.contactPoint = "end";
                    }
                    incoming.push_back(info);
                }

                if (incoming.size() < 2) { juncIdx++; continue; }

                // Create connecting roads: one per incoming road,
                // going from the road endpoint to the junction center
                std::vector<int> connectingRoadIds;
                std::vector<std::string> connectingContactPoints;
                for (const auto& inc : incoming) {
                    double connLen = std::hypot(
                        inc.endpointAtJunction.x - j.center.x,
                        inc.endpointAtJunction.y - j.center.y);
                    if (connLen < 0.1) connLen = 0.1; // minimum length

                    double hdg = std::atan2(
                        j.center.y - inc.endpointAtJunction.y,
                        j.center.x - inc.endpointAtJunction.x);

                    xml << "  <road name=\"junction_" << juncIdx
                        << "_conn_" << connectingRoadId
                        << "\" length=\"" << connLen
                        << "\" id=\"" << connectingRoadId
                        << "\" junction=\"" << juncIdx << "\">\n";
                    // Plan view (required by OpenDRIVE spec)
                    xml << "    <planView>\n";
                    xml << "      <geometry s=\"0\""
                        << " x=\"" << inc.endpointAtJunction.x
                        << "\" y=\"" << inc.endpointAtJunction.y
                        << "\" hdg=\"" << hdg
                        << "\" length=\"" << connLen << "\">\n";
                    xml << "        <line/>\n";
                    xml << "      </geometry>\n";
                    xml << "    </planView>\n";
                    xml << "    <lanes>\n";
                    xml << "      <laneSection s=\"0\">\n";
                    xml << "        <center>\n";
                    xml << "          <lane id=\"0\" type=\"border\" level=\"0\">\n";
                    xml << "            <width sOffset=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                    xml << "          </lane>\n";
                    xml << "        </center>\n";
                    xml << "        <right>\n";
                    xml << "          <lane id=\"-1\" type=\"driving\" level=\"0\">\n";
                    xml << "            <width sOffset=\"0\" a=\""
                        << params.defaultLaneWidth
                        << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                    xml << "          </lane>\n";
                    xml << "        </right>\n";
                    xml << "      </laneSection>\n";
                    xml << "    </lanes>\n";
                    // LaneMaker custom profile (required by libOpenDRIVE)
                    xml << "    <roadRunnerProfile>\n";
                    xml << "      <right>\n";
                    xml << "        <section type_s=\"0\" laneCount=\"1\" offsetX2=\"0\"/>\n";
                    xml << "      </right>\n";
                    xml << "    </roadRunnerProfile>\n";
                    xml << "  </road>\n";

                    connectingRoadIds.push_back(connectingRoadId);
                    // The connecting road starts at the incoming road's endpoint,
                    // so the incoming road connects at the START of the connecting road
                    connectingContactPoints.push_back("start");
                    connectingRoadId++;
                }

                // Write junction element with connections
                xml << "  <junction name=\"" << escapeXml(j.typeString().toStdString())
                    << "\" id=\"" << juncIdx << "\">\n";

                for (size_t i = 0; i < incoming.size(); i++) {
                    xml << "    <connection id=\"" << i
                        << "\" incomingRoad=\"" << incoming[i].roadIdx
                        << "\" connectingRoad=\"" << connectingRoadIds[i]
                        << "\" contactPoint=\"" << connectingContactPoints[i]
                        << "\">\n";
                    xml << "      <laneLink from=\"-1\" to=\"1\"/>\n";
                    xml << "    </connection>\n";
                }

                xml << "  </junction>\n";
                juncIdx++;
            }
        }

        xml << "</OpenDRIVE>\n";

        // Write to file
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QString("Cannot open file: %1").arg(path);
            return false;
        }
        QTextStream stream(&file);
        stream << QString::fromStdString(xml.str());
        file.close();

        appLog().info("[OsmExporter] Exported OpenDRIVE to", path, "—", network.roads.size(), "roads");
        return true;
    }

    // ─── Export to GeoJSON ───
    struct GeoJsonParams {
        int coordinatePrecision = 7;  // decimal places for lat/lon
        bool includeProperties = true;
        bool includeJunctions = true;
    };

    static bool exportToGeoJson(const QString& path,
                                 const RoadNetworkBuilder::Result& network,
                                 const std::vector<DetectedJunction>& junctions,
                                 const CoordinateConverter& converter,
                                 const GeoJsonParams& params = {},
                                 QString* errorMsg = nullptr)
    {
        QJsonObject root;
        root["type"] = "FeatureCollection";

        QJsonArray features;

        // Road features (LineString)
        for (const auto& road : network.roads) {
            QJsonObject feature;
            feature["type"] = "Feature";

            // Geometry
            QJsonObject geometry;
            geometry["type"] = "LineString";
            QJsonArray coordinates;

            // Sample the road and convert back to lat/lon
            double totalLen = road.totalLength();
            int samples = std::max(2, std::min(100, int(totalLen / 5)));
            for (int i = 0; i <= samples; i++) {
                double s = totalLen * i / samples;
                geo::Point2D p = road.geometry().positionAt(s);

                double lat, lon;
                converter.toGeo(p.x, p.y, lat, lon);

                QJsonArray coord;
                coord.append(QString::number(lon, 'f', params.coordinatePrecision).toDouble());
                coord.append(QString::number(lat, 'f', params.coordinatePrecision).toDouble());
                coordinates.append(coord);
            }
            geometry["coordinates"] = coordinates;
            feature["geometry"] = geometry;

            // Properties
            if (params.includeProperties) {
                QJsonObject props;
                props["id"] = QString::fromStdString(road.id);
                props["name"] = QString::fromStdString(road.name);
                props["lanes"] = road.laneCount;
                props["width"] = road.width;
                props["length"] = totalLen;

                const RoadNetworkBuilder::RoadMetadata* meta =
                    RoadNetworkBuilder::getMetadata(road.id);
                if (meta) {
                    props["highway"] = meta->highwayType;
                    props["oneway"] = meta->isOneWay;
                    props["bridge"] = meta->isBridge;
                    props["tunnel"] = meta->isTunnel;
                    if (!meta->layer.isEmpty()) props["layer"] = meta->layer;
                    if (!meta->surface.isEmpty()) props["surface"] = meta->surface;
                    if (meta->maxspeed > 0) props["maxspeed"] = meta->maxspeed;
                }

                feature["properties"] = props;
            }

            features.append(feature);
        }

        // Junction features (Point)
        if (params.includeJunctions) {
            for (const auto& j : junctions) {
                QJsonObject feature;
                feature["type"] = "Feature";

                QJsonObject geometry;
                geometry["type"] = "Point";
                QJsonArray coord;

                double lat, lon;
                converter.toGeo(j.center.x, j.center.y, lat, lon);
                coord.append(QString::number(lon, 'f', params.coordinatePrecision).toDouble());
                coord.append(QString::number(lat, 'f', params.coordinatePrecision).toDouble());
                geometry["coordinates"] = coord;
                feature["geometry"] = geometry;

                if (params.includeProperties) {
                    QJsonObject props;
                    props["id"] = j.id;
                    props["type"] = j.typeString();
                    props["osmNodeId"] = qint64(j.osmNodeId);
                    props["roadCount"] = int(j.roadIds.size());
                    props["isRoundabout"] = j.isRoundabout;
                    props["isBridge"] = j.isBridge;
                    props["isTunnel"] = j.isTunnel;
                    feature["properties"] = props;
                }

                features.append(feature);
            }
        }

        root["features"] = features;

        // Write to file
        QJsonDocument doc(root);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QString("Cannot open file: %1").arg(path);
            return false;
        }
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();

        appLog().info("[OsmExporter] Exported GeoJSON to", path, "—", features.size(), "features");
        return true;
    }

    // ─── Validate exported file can be re-imported ───
    static bool validateExport(const QString& path, QString* errorMsg = nullptr) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = "Cannot open exported file";
            return false;
        }

        QByteArray data = file.readAll();
        file.close();

        // Try parsing as JSON (GeoJSON)
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error == QJsonParseError::NoError) {
            QJsonObject root = doc.object();
            if (root["type"].toString() == "FeatureCollection") {
                int featureCount = root["features"].toArray().size();
                if (featureCount > 0) {
                    appLog().info("[OsmExporter] Validation: GeoJSON OK,", featureCount, "features");
                    return true;
                }
            }
            if (errorMsg) *errorMsg = "Invalid GeoJSON structure";
            return false;
        }

        // Try as OpenDRIVE XML
        if (data.contains("<?xml") && data.contains("<OpenDRIVE>")) {
            if (data.contains("<road") && data.contains("</OpenDRIVE>")) {
                appLog().info("[OsmExporter] Validation: OpenDRIVE XML OK");
                return true;
            }
        }

        if (errorMsg) *errorMsg = "Unknown export format";
        return false;
    }

private:
    static std::string escapeXml(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '&':  result += "&amp;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default:   result += c;
            }
        }
        return result;
    }

    static std::string laneTypeToString(geo::LaneType type) {
        switch (type) {
        case geo::LaneType::Driving:       return "driving";
        case geo::LaneType::Border:        return "border";
        case geo::LaneType::Sidewalk:      return "sidewalk";
        case geo::LaneType::Biking:        return "biking";
        case geo::LaneType::Bus:           return "bus";
        case geo::LaneType::Taxi:          return "taxi";
        case geo::LaneType::Parking:       return "parking";
        case geo::LaneType::Stop:          return "stop";
        case geo::LaneType::Shoulder:      return "shoulder";
        case geo::LaneType::Acceleration:  return "acceleration";
        case geo::LaneType::Deceleration:  return "deceleration";
        case geo::LaneType::HOV:           return "hov";
        case geo::LaneType::Restricted:    return "restricted";
        case geo::LaneType::Tram:          return "tram";
        case geo::LaneType::None:          return "none";
        }
        return "driving";
    }
};

} // namespace osm
