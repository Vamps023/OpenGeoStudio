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
#include "RoadMarkingGenerator.hpp"
#include "TrafficSignGenerator.hpp"
#include "CoordinateConverter.hpp"
#include "DemElevationSampler.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"
#include "../../engine/road/lane_engine.hpp"

#include <QFile>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QSet>
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
#include <map>

namespace osm {

// ─── OsmExporter ───
class OsmExporter {
public:
    // ─── Export to OpenDRIVE (.xodr) ───
    struct OpenDriveParams {
        int geoSamples = 100;       // samples per road geometry
        double defaultLaneWidth = 3.5;
        int revisionMinor = 6;
        bool includeJunctions = true;
        bool includeSignals = true;
        bool includeRoadMarks = true;
        const std::vector<RoadMarking>* markings = nullptr;
        const std::vector<TrafficSign>* signs = nullptr;
        // Optional DEM sampler (project heightmap) for real elevation profiles
        const DemElevationSampler* elevation = nullptr;
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

        // Header — revMinor 1.6: the newest OpenDRIVE version widely
        // supported by simulators (SCANeR's importer accepts 1.4–1.6).
        // All tags we emit are 1.4-era, so 1.6 is valid.
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
        xml << "<OpenDRIVE>\n";

        // Bounding box in local meters, sampled along road centerlines
        double minX = 0, minY = 0, maxX = 0, maxY = 0;
        bool hasBounds = false;
        for (const auto& road : network.roads) {
            const double total = road.totalLength();
            if (total <= 0) continue;
            for (int i = 0; i <= 16; ++i) {
                const geo::Point2D p = road.geometry().positionAt(total * double(i) / 16.0);
                if (!hasBounds) {
                    minX = maxX = p.x; minY = maxY = p.y; hasBounds = true;
                } else {
                    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
                    minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
                }
            }
        }
        const int revisionMinor = params.revisionMinor >= 8 ? 8 : 6;
        xml << "  <header revMajor=\"1\" revMinor=\"" << revisionMinor << "\" name=\"OSM Export\""
            << " version=\"1.00\""
            << " north=\"" << (hasBounds ? maxY : 0.0)
            << "\" south=\"" << (hasBounds ? minY : 0.0)
            << "\" east=\"" << (hasBounds ? maxX : 0.0)
            << "\" west=\"" << (hasBounds ? minX : 0.0) << "\">\n";
        // Standard PROJ geoReference — lets SCANeR / esmini / odrviewer
        // geolocate the network. projString() matches toLocal() exactly.
        xml << "    <geoReference><![CDATA[" << converter.projString() << "]]></geoReference>\n";
        xml << "  </header>\n";

        // Build road ID → index map
        std::unordered_map<std::string, int> roadIdToIndex;
        for (int r = 0; r < int(network.roads.size()); r++) {
            roadIdToIndex[network.roads[r].id] = r;
        }

        // ── Junction topology (GeoTerrain converter pattern) ──
        // Roads whose END touches the junction are incoming; roads whose
        // START touches it are outgoing. One connecting road per
        // incoming→outgoing pair (skipping U-turns on the same OSM way),
        // with proper predecessor/successor links and lane links.
        struct ConnLaneLink { int from; int to; };
        struct JunctionConnectionX {
            int id = 0;
            int incomingRoadIdx = -1;
            int connectingRoadId = -1;
            std::vector<ConnLaneLink> laneLinks;
        };
        struct ConnectingRoad {
            int id = 0;
            int junctionIdx = 0;
            geo::Point2D from, to;
            double hdg = 0, len = 0;
            int predRoadIdx = -1, succRoadIdx = -1;
            double elevStart = 0, elevEnd = 0;
            bool hasElevation = false;
            bool hasLeft = false, hasRight = true;
        };
        struct JunctionOut {
            int idx = 0;
            std::string name;
            std::vector<JunctionConnectionX> connections;
        };

        std::vector<ConnectingRoad> connectingRoads;
        std::vector<JunctionOut> junctionOuts;
        std::vector<int> roadSuccessorJunction(network.roads.size(), -1);
        std::vector<int> roadPredecessorJunction(network.roads.size(), -1);

        if (params.includeJunctions) {
            constexpr int kMaxConnectionsPerJunction = 20; // GeoTerrain limit
            int junctionIdx = 0;
            int connectingRoadId = 100000;

            for (const auto& j : junctions) {
                if (j.type == JunctionType::Overpass) continue;
                if (j.roadIds.size() < 2) continue;

                struct RoadAt {
                    int idx;
                    std::string roadId;
                    geo::Point2D endpoint;
                    bool isEnd;  // true → road END here (incoming)
                };
                std::vector<RoadAt> entries;
                for (const auto& rid : j.roadIds) {
                    auto it = roadIdToIndex.find(rid.toStdString());
                    if (it == roadIdToIndex.end()) continue;
                    const auto& road = network.roads[it->second];
                    const double totalLen = road.totalLength();
                    if (totalLen <= 0) continue;

                    const geo::Point2D rs = road.geometry().positionAt(0);
                    const geo::Point2D re = road.geometry().positionAt(totalLen);
                    const double dS = std::hypot(rs.x - j.center.x, rs.y - j.center.y);
                    const double dE = std::hypot(re.x - j.center.x, re.y - j.center.y);

                    RoadAt ra;
                    ra.idx = it->second;
                    ra.roadId = rid.toStdString();
                    if (dS <= dE) { ra.endpoint = rs; ra.isEnd = false; }
                    else          { ra.endpoint = re; ra.isEnd = true;  }
                    entries.push_back(ra);
                }
                if (entries.size() < 2) continue;

                JunctionOut jo;
                jo.idx = junctionIdx;

                for (const auto& in : entries) {
                    if (!in.isEnd) continue;
                    for (const auto& out : entries) {
                        if (out.isEnd) continue;
                        if (in.roadId == out.roadId) continue; // U-turn on same way
                        if (int(jo.connections.size()) >= kMaxConnectionsPerJunction)
                            break;

                        ConnectingRoad cr;
                        cr.id = connectingRoadId++;
                        cr.junctionIdx = junctionIdx;
                        cr.from = in.endpoint;
                        cr.to = out.endpoint;
                        cr.len = std::max(std::hypot(cr.to.x - cr.from.x,
                                                     cr.to.y - cr.from.y), 1.0);
                        cr.hdg = std::atan2(cr.to.y - cr.from.y, cr.to.x - cr.from.x);
                        cr.predRoadIdx = in.idx;
                        cr.succRoadIdx = out.idx;
                        if (params.elevation && params.elevation->valid()) {
                            double lat, lon;
                            converter.toGeo(cr.from.x, cr.from.y, lat, lon);
                            cr.elevStart = params.elevation->sampleLonLat(lon, lat);
                            converter.toGeo(cr.to.x, cr.to.y, lat, lon);
                            cr.elevEnd = params.elevation->sampleLonLat(lon, lat);
                            cr.hasElevation = !std::isnan(cr.elevStart) &&
                                              !std::isnan(cr.elevEnd);
                        }
                        // Only link lanes the incoming road actually has
                        // (engine +id → OpenDRIVE right −id, engine −id → left +id)
                        if (network.roads[in.idx].numLaneSections() > 0) {
                            for (const auto& lane :
                                 network.roads[in.idx].laneSection(0).lanes()) {
                                if (lane.id > 0) cr.hasRight = true;
                                else if (lane.id < 0) cr.hasLeft = true;
                            }
                        }
                        connectingRoads.push_back(cr);

                        JunctionConnectionX c;
                        c.id = int(jo.connections.size()) + 1;
                        c.incomingRoadIdx = in.idx;
                        c.connectingRoadId = cr.id;
                        if (cr.hasRight) c.laneLinks.push_back({-1, -1});
                        if (cr.hasLeft)  c.laneLinks.push_back({1, 1});
                        if (c.laneLinks.empty()) c.laneLinks.push_back({-1, -1});
                        jo.connections.push_back(c);
                    }
                }

                if (jo.connections.empty()) continue;

                // Point the participating roads at the junction (required by
                // LaneMaker's loader to build junction graphics)
                for (const auto& ra : entries) {
                    if (ra.isEnd) roadSuccessorJunction[ra.idx] = junctionIdx;
                    else          roadPredecessorJunction[ra.idx] = junctionIdx;
                }
                jo.name = escapeXml(j.typeString().toStdString());
                junctionOuts.push_back(std::move(jo));
                junctionIdx++;
            }
        }

        // Roads
        for (int r = 0; r < int(network.roads.size()); r++) {
            const auto& road = network.roads[r];
            double totalLen = road.totalLength();
            if (totalLen <= 0) continue;

            xml << "  <road name=\"" << escapeXml(road.name)
                << "\" length=\"" << totalLen
                << "\" id=\"" << r
                << "\" junction=\"-1\">\n";

            // Junction links (successor: road END at junction; predecessor: START)
            if (r < int(roadPredecessorJunction.size()) &&
                (roadPredecessorJunction[r] >= 0 || roadSuccessorJunction[r] >= 0)) {
                xml << "    <link>\n";
                if (roadPredecessorJunction[r] >= 0)
                    xml << "      <predecessor elementType=\"junction\" id=\""
                        << roadPredecessorJunction[r] << "\"/>\n";
                if (roadSuccessorJunction[r] >= 0)
                    xml << "      <successor elementType=\"junction\" id=\""
                        << roadSuccessorJunction[r] << "\"/>\n";
                xml << "    </link>\n";
            }

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

            // Elevation profile — sampled from the project DEM when available
            // (GeoTerrain sampleElevationProfile pattern: ≤10 m spacing,
            // absolute meters), else flat.
            xml << "    <elevationProfile>\n";
            auto profile = buildElevationProfile(road, converter, params.elevation);
            if (profile.size() >= 2) {
                for (const auto& [sOff, elev] : profile) {
                    xml << "      <elevation s=\"" << sOff
                        << "\" a=\"" << elev << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                }
            } else {
                xml << "      <elevation s=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/>\n";
            }
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
                if (params.includeRoadMarks)
                    xml << "            <roadMark sOffset=\"0\" type=\"solid\" weight=\"standard\" color=\"white\" material=\"standard\" width=\"0.15\" laneChange=\"none\" height=\"0\"/>\n";
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
                        if (params.includeRoadMarks)
                            xml << "            <roadMark sOffset=\"0\" type=\"broken\" weight=\"standard\" color=\"white\" material=\"standard\" width=\"0.15\" laneChange=\"both\" height=\"0\"/>\n";
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
                                if (params.includeRoadMarks)
                                    xml << "            <roadMark sOffset=\"0\" type=\"broken\" weight=\"standard\" color=\"white\" material=\"standard\" width=\"0.15\" laneChange=\"both\" height=\"0\"/>\n";
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
                if (params.includeRoadMarks)
                    xml << "            <roadMark sOffset=\"0\" type=\"solid\" weight=\"standard\" color=\"white\" material=\"standard\" width=\"0.15\" laneChange=\"none\" height=\"0\"/>\n";
                xml << "          </lane>\n";
                xml << "        </center>\n";
                xml << "      </laneSection>\n";
            }
            xml << "    </lanes>\n";

            // LaneMaker custom profile (required by libOpenDRIVE's loader)
            // Stores lane plans (count + offset) for left and right sides
            if (revisionMinor <= 6) {
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
            }

            if (revisionMinor >= 8 && params.markings) {
                bool opened = false;
                int objectId = 0;
                for (const auto& marking : *params.markings) {
                    if (marking.roadId.toStdString() != road.id ||
                        (!marking.isPointMarking && marking.type != MarkingType::Crosswalk &&
                         marking.type != MarkingType::ParkingMarking)) continue;
                    if (!opened) { xml << "    <objects>\n"; opened = true; }
                    const double sPosition = marking.isPointMarking ? marking.position : marking.startS;
                    xml << "      <object id=\"mark_" << r << '_' << objectId++
                        << "\" name=\"" << escapeXml(marking.typeString().toStdString())
                        << "\" type=\"roadMark\" s=\"" << sPosition
                        << "\" t=\"" << marking.lateralOffset
                        << "\" zOffset=\"0.01\" hdg=\"0\" pitch=\"0\" roll=\"0\""
                        << " length=\"" << std::max(0.1, marking.endS - marking.startS)
                        << "\" width=\"" << marking.width
                        << "\" height=\"0.01\" orientation=\"none\"/>\n";
                }
                if (opened) xml << "    </objects>\n";
            }

            if (revisionMinor >= 8 && params.includeSignals && params.signs) {
                bool opened = false;
                int signalId = 0;
                for (const auto& sign : *params.signs) {
                    if (sign.roadId.toStdString() != road.id) continue;
                    if (!opened) { xml << "    <signals>\n"; opened = true; }
                    const bool dynamic = sign.type == SignType::TrafficSignal ||
                                         sign.type == SignType::PedestrianSignal;
                    xml << "      <signal id=\"signal_" << r << '_' << signalId++
                        << "\" name=\"" << escapeXml(sign.typeString().toStdString())
                        << "\" s=\"" << sign.sPosition << "\" t=\"" << sign.lateralOffset
                        << "\" dynamic=\"" << (dynamic ? "yes" : "no")
                        << "\" orientation=\"" << (sign.side == "left" ? "+" : "-")
                        << "\" zOffset=\"0\" country=\"OpenGeoStudio\" type=\"1000001\" subtype=\""
                        << int(sign.type) << "\" value=\"" << (sign.value.isEmpty() ? "-1" : sign.value.toStdString())
                        << "\" unit=\"km/h\" height=\"" << sign.height << "\" width=\"0.6\"/>\n";
                }
                if (opened) xml << "    </signals>\n";
            }

            xml << "  </road>\n";
        }

        // Connecting roads (incoming → outgoing through each junction)
        for (const auto& cr : connectingRoads) {
            xml << "  <road name=\"junction_" << cr.junctionIdx
                << "_conn_" << cr.id
                << "\" length=\"" << cr.len
                << "\" id=\"" << cr.id
                << "\" junction=\"" << cr.junctionIdx << "\">\n";
            xml << "    <link>\n";
            xml << "      <predecessor elementType=\"road\" id=\""
                << cr.predRoadIdx << "\" contactPoint=\"end\"/>\n";
            xml << "      <successor elementType=\"road\" id=\""
                << cr.succRoadIdx << "\" contactPoint=\"start\"/>\n";
            xml << "    </link>\n";
            xml << "    <planView>\n";
            xml << "      <geometry s=\"0\" x=\"" << cr.from.x
                << "\" y=\"" << cr.from.y
                << "\" hdg=\"" << cr.hdg
                << "\" length=\"" << cr.len << "\">\n";
            xml << "        <line/>\n";
            xml << "      </geometry>\n";
            xml << "    </planView>\n";
            xml << "    <elevationProfile>\n";
            if (cr.hasElevation) {
                xml << "      <elevation s=\"0\" a=\"" << cr.elevStart
                    << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                xml << "      <elevation s=\"" << cr.len << "\" a=\"" << cr.elevEnd
                    << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
            } else {
                xml << "      <elevation s=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/>\n";
            }
            xml << "    </elevationProfile>\n";
            xml << "    <lanes>\n";
            xml << "      <laneSection s=\"0\">\n";
            xml << "        <center>\n";
            xml << "          <lane id=\"0\" type=\"border\" level=\"0\">\n";
            xml << "            <width sOffset=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/>\n";
            xml << "          </lane>\n";
            xml << "        </center>\n";
            xml << "        <right>\n";
            xml << "          <lane id=\"-1\" type=\"driving\" level=\"0\">\n";
            xml << "            <link>\n";
            xml << "              <predecessor id=\"-1\"/>\n";
            xml << "              <successor id=\"-1\"/>\n";
            xml << "            </link>\n";
            xml << "            <width sOffset=\"0\" a=\"" << params.defaultLaneWidth
                << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
            xml << "          </lane>\n";
            xml << "        </right>\n";
            if (cr.hasLeft) {
                xml << "        <left>\n";
                xml << "          <lane id=\"1\" type=\"driving\" level=\"0\">\n";
                xml << "            <link>\n";
                xml << "              <predecessor id=\"1\"/>\n";
                xml << "              <successor id=\"1\"/>\n";
                xml << "            </link>\n";
                xml << "            <width sOffset=\"0\" a=\"" << params.defaultLaneWidth
                    << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                xml << "          </lane>\n";
                xml << "        </left>\n";
            }
            xml << "      </laneSection>\n";
            xml << "    </lanes>\n";
            if (revisionMinor <= 6) {
                xml << "    <roadRunnerProfile>\n";
                if (cr.hasLeft) {
                    xml << "      <left>\n";
                    xml << "        <section type_s=\"0\" laneCount=\"1\" offsetX2=\"0\"/>\n";
                    xml << "      </left>\n";
                }
                xml << "      <right>\n";
                xml << "        <section type_s=\"0\" laneCount=\"1\" offsetX2=\"0\"/>\n";
                xml << "      </right>\n";
                xml << "    </roadRunnerProfile>\n";
            }
            xml << "  </road>\n";
        }

        // Junction elements
        for (const auto& jo : junctionOuts) {
            xml << "  <junction name=\"" << jo.name
                << "\" id=\"" << jo.idx << "\">\n";
            for (const auto& c : jo.connections) {
                // contactPoint is on the connecting road: it starts at the
                // incoming road's endpoint
                xml << "    <connection id=\"" << c.id
                    << "\" incomingRoad=\"" << c.incomingRoadIdx
                    << "\" connectingRoad=\"" << c.connectingRoadId
                    << "\" contactPoint=\"start\">\n";
                for (const auto& ll : c.laneLinks) {
                    xml << "      <laneLink from=\"" << ll.from
                        << "\" to=\"" << ll.to << "\"/>\n";
                }
                xml << "    </connection>\n";
            }
            xml << "  </junction>\n";
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

        appLog().info("[OsmExporter] Exported OpenDRIVE to", path, "—",
                      network.roads.size(), "roads,",
                      connectingRoads.size(), "connecting roads,",
                      junctionOuts.size(), "junctions");
        return true;
    }

    struct SumoParams {
        int geometrySamples = 32;
        double defaultSpeedKmh = 50.0;
        bool includeConnections = true;
    };

    static bool exportToSumo(const QString& path,
                             const RoadNetworkBuilder::Result& network,
                             const std::vector<DetectedJunction>& junctions,
                             const SumoParams& params = {},
                             QString* errorMsg = nullptr)
    {
        std::ostringstream xml;
        xml << std::fixed << std::setprecision(3);
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        xml << "<net version=\"1.16\" junctionCornerDetail=\"5\" limitTurnSpeed=\"5.50\">\n";

        using NodeKey = std::pair<long long, long long>;
        std::map<NodeKey, std::string> nodeIds;
        std::map<std::string, geo::Point2D> nodePositions;
        std::map<std::string, int> nodeDegrees;
        std::unordered_map<std::string, int> laneCounts;
        int nextNodeId = 0;
        auto nodeIdFor = [&](const geo::Point2D& point) {
            const NodeKey key {std::llround(point.x * 100.0), std::llround(point.y * 100.0)};
            auto it = nodeIds.find(key);
            if (it != nodeIds.end()) return it->second;
            const std::string id = "n" + std::to_string(nextNodeId++);
            nodeIds.emplace(key, id);
            nodePositions.emplace(id, point);
            return id;
        };

        struct EdgeInfo {
            const geo::RoadV2* road = nullptr;
            std::string from;
            std::string to;
        };
        std::vector<EdgeInfo> edges;
        for (const auto& road : network.roads) {
            const double length = road.totalLength();
            if (length <= 0 || road.numLaneSections() == 0) continue;
            const auto start = road.geometry().positionAt(0);
            const auto end = road.geometry().positionAt(length);
            EdgeInfo edge {&road, nodeIdFor(start), nodeIdFor(end)};
            nodeDegrees[edge.from]++;
            nodeDegrees[edge.to]++;
            edges.push_back(edge);
        }

        for (const auto& edge : edges) {
            const auto& road = *edge.road;
            const double length = road.totalLength();
            const auto& lanes = road.laneSection(0).lanes();
            std::vector<const geo::Lane*> exportedLanes;
            for (const auto& lane : lanes)
                if (lane.id != 0 && lane.type != geo::LaneType::None)
                    exportedLanes.push_back(&lane);
            if (exportedLanes.empty()) continue;
            laneCounts[road.id] = int(exportedLanes.size());

            xml << "  <edge id=\"" << escapeXml(road.id) << "\" from=\"" << edge.from
                << "\" to=\"" << edge.to << "\" name=\"" << escapeXml(road.name) << "\">\n";
            const auto* metadata = RoadNetworkBuilder::getMetadata(road.id);
            const double speedKmh = metadata && metadata->maxspeed > 0
                ? metadata->maxspeed : params.defaultSpeedKmh;
            std::ostringstream shape;
            shape << std::fixed << std::setprecision(3);
            const int samples = std::max(2, params.geometrySamples);
            for (int sample = 0; sample <= samples; ++sample) {
                if (sample > 0) shape << ' ';
                const auto point = road.geometry().positionAt(length * sample / samples);
                shape << point.x << ',' << point.y;
            }

            for (int index = 0; index < int(exportedLanes.size()); ++index) {
                const auto& lane = *exportedLanes[index];
                xml << "    <lane id=\"" << escapeXml(road.id) << '_' << index
                    << "\" index=\"" << index << "\" speed=\"" << speedKmh / 3.6
                    << "\" length=\"" << length << "\" width=\"" << lane.widthAt(0)
                    << "\" shape=\"" << shape.str() << "\"";
                const std::string type = laneTypeToString(lane.type);
                if (type == "biking") xml << " allow=\"bicycle\"";
                else if (type == "bus" || type == "stop") xml << " allow=\"bus\"";
                else if (type == "sidewalk") xml << " allow=\"pedestrian\"";
                else if (type == "tram") xml << " allow=\"tram\"";
                else if (type == "shoulder") xml << " allow=\"emergency\"";
                else if (type == "restricted" || type == "border") xml << " disallow=\"all\"";
                xml << "/>\n";
            }
            xml << "  </edge>\n";
        }

        for (const auto& [id, point] : nodePositions) {
            const char* type = nodeDegrees[id] > 1 ? "priority" : "dead_end";
            xml << "  <junction id=\"" << id << "\" type=\"" << type
                << "\" x=\"" << point.x << "\" y=\"" << point.y
                << "\" incLanes=\"\" intLanes=\"\" shape=\"" << point.x << ',' << point.y << "\"/>\n";
        }

        if (params.includeConnections) {
            std::unordered_map<std::string, const geo::RoadV2*> roadsById;
            for (const auto& road : network.roads) roadsById[road.id] = &road;
            for (const auto& junction : junctions) {
                std::vector<const geo::RoadV2*> incoming;
                std::vector<const geo::RoadV2*> outgoing;
                for (const auto& roadId : junction.roadIds) {
                    auto it = roadsById.find(roadId.toStdString());
                    if (it == roadsById.end()) continue;
                    const auto* road = it->second;
                    const auto start = road->geometry().positionAt(0);
                    const auto end = road->geometry().positionAt(road->totalLength());
                    const double startDistance = std::hypot(start.x - junction.center.x, start.y - junction.center.y);
                    const double endDistance = std::hypot(end.x - junction.center.x, end.y - junction.center.y);
                    if (endDistance <= startDistance) incoming.push_back(road);
                    if (startDistance <= endDistance) outgoing.push_back(road);
                }
                for (const auto* from : incoming) {
                    for (const auto* to : outgoing) {
                        if (from == to) continue;
                        const int count = std::min(laneCounts[from->id], laneCounts[to->id]);
                        for (int lane = 0; lane < count; ++lane)
                            xml << "  <connection from=\"" << escapeXml(from->id)
                                << "\" to=\"" << escapeXml(to->id)
                                << "\" fromLane=\"" << lane << "\" toLane=\"" << lane
                                << "\" dir=\"s\" state=\"M\"/>\n";
                    }
                }
            }
        }

        xml << "</net>\n";
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QString("Cannot write SUMO file: %1").arg(path);
            return false;
        }
        file.write(QByteArray::fromStdString(xml.str()));
        file.close();
        appLog().info("[OsmExporter] Exported SUMO network to", path, "—", edges.size(), "edges");
        return true;
    }

    // ─── Export to GeoJSON ───
    struct GeoJsonParams {
        int coordinatePrecision = 7;  // decimal places for lat/lon
        bool includeProperties = true;
        bool includeJunctions = true;
        // Optional DEM sampler — appends elevation as a 3rd coordinate
        const DemElevationSampler* elevation = nullptr;
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
                if (params.elevation && params.elevation->valid()) {
                    const double e = params.elevation->sampleLonLat(lon, lat);
                    if (!std::isnan(e))
                        coord.append(QString::number(e, 'f', 2).toDouble());
                }
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

    static bool validateOpenDriveStructure(const QString& path, QStringList* issues = nullptr)
    {
        QStringList localIssues;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            localIssues.append(QString("Cannot open OpenDRIVE file: %1").arg(path));
            if (issues) *issues = localIssues;
            return false;
        }
        QXmlStreamReader xml(&file);
        bool hasRoot = false;
        bool hasHeader = false;
        bool inRoad = false;
        bool roadHasPlanView = false;
        bool roadHasLanes = false;
        QString currentRoad;
        QSet<QString> roadIds;
        QSet<QString> junctionIds;
        std::vector<std::pair<QString, QString>> junctionRoadRefs;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const auto name = xml.name();
                if (name == QLatin1String("OpenDRIVE")) hasRoot = true;
                else if (name == QLatin1String("header")) {
                    hasHeader = true;
                    const int major = xml.attributes().value("revMajor").toInt();
                    const int minor = xml.attributes().value("revMinor").toInt();
                    if (major != 1 || (minor != 6 && minor != 8))
                        localIssues.append(QString("Unsupported OpenDRIVE revision %1.%2").arg(major).arg(minor));
                } else if (name == QLatin1String("road")) {
                    inRoad = true;
                    roadHasPlanView = false;
                    roadHasLanes = false;
                    currentRoad = xml.attributes().value("id").toString();
                    if (currentRoad.isEmpty()) localIssues.append("Road without id");
                    else if (roadIds.contains(currentRoad)) localIssues.append(QString("Duplicate road id %1").arg(currentRoad));
                    else roadIds.insert(currentRoad);
                    if (xml.attributes().value("length").toDouble() <= 0)
                        localIssues.append(QString("Road %1 has non-positive length").arg(currentRoad));
                } else if (inRoad && name == QLatin1String("planView")) roadHasPlanView = true;
                else if (inRoad && name == QLatin1String("lanes")) roadHasLanes = true;
                else if (name == QLatin1String("geometry") &&
                         xml.attributes().value("length").toDouble() <= 0)
                    localIssues.append(QString("Road %1 contains non-positive geometry length").arg(currentRoad));
                else if (name == QLatin1String("junction")) {
                    const QString id = xml.attributes().value("id").toString();
                    if (junctionIds.contains(id)) localIssues.append(QString("Duplicate junction id %1").arg(id));
                    junctionIds.insert(id);
                } else if (name == QLatin1String("connection")) {
                    junctionRoadRefs.emplace_back(xml.attributes().value("incomingRoad").toString(),
                                                  xml.attributes().value("connectingRoad").toString());
                }
            } else if (xml.isEndElement() && xml.name() == QLatin1String("road")) {
                if (!roadHasPlanView) localIssues.append(QString("Road %1 has no planView").arg(currentRoad));
                if (!roadHasLanes) localIssues.append(QString("Road %1 has no lanes").arg(currentRoad));
                inRoad = false;
            }
        }
        if (xml.hasError()) localIssues.append(xml.errorString());
        if (!hasRoot) localIssues.append("Missing OpenDRIVE root element");
        if (!hasHeader) localIssues.append("Missing OpenDRIVE header");
        if (roadIds.isEmpty()) localIssues.append("OpenDRIVE document contains no roads");
        for (const auto& [incoming, connecting] : junctionRoadRefs) {
            if (!roadIds.contains(incoming))
                localIssues.append(QString("Junction references missing incoming road %1").arg(incoming));
            if (!roadIds.contains(connecting))
                localIssues.append(QString("Junction references missing connecting road %1").arg(connecting));
        }
        if (issues) *issues = localIssues;
        return localIssues.empty();
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
            QStringList issues;
            const bool valid = validateOpenDriveStructure(path, &issues);
            if (valid) {
                appLog().info("[OsmExporter] Validation: OpenDRIVE XML OK");
                return true;
            }
            if (errorMsg) *errorMsg = issues.join("; ");
            return false;
        }

        if (errorMsg) *errorMsg = "Unknown export format";
        return false;
    }

private:
    // GeoTerrain sampleElevationProfile port: samples the DEM along the road
    // centerline at ≤10 m spacing. Absolute meters (not normalized) so the
    // roads match the project terrain in tools like Unigine/LaneMaker.
    // Returns (s, elevation) pairs; empty when no sampler/out of bounds.
    template <typename RoadT>
    static std::vector<std::pair<double, double>> buildElevationProfile(
        const RoadT& road, const CoordinateConverter& converter,
        const DemElevationSampler* elev)
    {
        std::vector<std::pair<double, double>> pts;
        if (!elev || !elev->valid()) return pts;
        const double total = road.totalLength();
        if (total <= 0) return pts;

        constexpr double kSampleInterval = 10.0;  // meters (GeoTerrain)
        const int steps = std::max(2, int(std::ceil(total / kSampleInterval)) + 1);
        for (int i = 0; i < steps; ++i) {
            const double s = total * double(i) / double(steps - 1);
            const geo::Point2D p = road.geometry().positionAt(s);
            double lat, lon;
            converter.toGeo(p.x, p.y, lat, lon);
            const double e = elev->sampleLonLat(lon, lat);
            if (std::isnan(e)) continue;  // outside the DEM — skip sample
            pts.emplace_back(s, e);
        }
        return pts;
    }

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
