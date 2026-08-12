#pragma once

// ═══════════════════════════════════════════════════════════
// OpenDRIVE I/O — Read and write .xodr files
// ═══════════════════════════════════════════════════════════
//
// OpenDRIVE is the de facto standard for describing road networks
// in driving simulators (SCANeR, Vires VTD, CARLA, etc.).
//
// This is a basic implementation that supports:
// - Road elements with geometry (lines, arcs, clothoids)
// - Lanes (left and right)
// - Lane sections
// - Junctions (basic)
// - Elevation profiles
//
// Reference: OpenDRIVE 1.6 specification
// http://www.opendrive.org/docs/OpenDRIVEFormatSpecRev16.pdf

#include "geometry.hpp"
#include "road.hpp"
#include "clothoid.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <cmath>

namespace geo {

// ─── OpenDRIVE geometry types ──────────────────────────────
enum class OdGeometryType {
    Line,
    Arc,
    Clothoid,
    ParamPoly3
};

struct OdGeometry {
    OdGeometryType type;
    double s;       // start position along road
    double x;       // start position x
    double y;       // start position y
    double hdg;     // heading (radians)
    double length;  // length of geometry element

    // Arc-specific
    double curvature = 0;

    // Clothoid-specific
    double curvStart = 0;
    double curvEnd = 0;

    // ParamPoly3-specific (cubic polynomial)
    double aU = 0, bU = 0, cU = 0, dU = 0;
    double aV = 0, bV = 0, cV = 0, dV = 0;
};

struct OdLane {
    int id;
    double width;
    std::string type;  // "driving", "sidewalk", "shoulder", etc.
    std::string material;
    bool level = false;
};

struct OdLaneSection {
    double s;
    std::vector<OdLane> leftLanes;
    std::vector<OdLane> rightLanes;
};

struct OdElevationProfile {
    double s;
    double a, b, c, d;  // cubic polynomial: z = a + b*ds + c*ds² + d*ds³
};

struct OdRoad {
    std::string id;
    std::string name;
    double length;
    std::string junction;  // "-1" if not in a junction
    std::vector<OdGeometry> geometries;
    std::vector<OdLaneSection> laneSections;
    std::vector<OdElevationProfile> elevationProfiles;
    std::string predecessorId;
    std::string successorId;
};

struct OdJunction {
    std::string id;
    std::string name;
    std::vector<struct OdConnection> connections;
};

struct OdConnection {
    std::string id;
    std::string incomingRoad;
    std::string connectingRoad;
    int contactPoint;  // 0 = start, 1 = end
};

struct OpenDriveDocument {
    double headerRevMajor;
    double headerRevMinor;
    std::string headerName;
    std::string headerDate;
    std::string headerNorth;
    std::string headerSouth;
    std::string headerEast;
    std::string headerWest;
    std::vector<OdRoad> roads;
    std::vector<OdJunction> junctions;
};

// ─── XML escaping ──────────────────────────────────────────
inline std::string xmlEscape(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '&': result += "&amp;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&apos;"; break;
            default: result += c;
        }
    }
    return result;
}

// ─── Format a double with fixed precision ──────────────────
inline std::string fmt(double val, int precision = 6) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(precision);
    oss << val;
    return oss.str();
}

// ─── Convert Road to OpenDRIVE ─────────────────────────────
// Converts a road with control points to OpenDRIVE geometry.
// Each segment between control points becomes a geometry element:
// - Linear segments → <line>
// - Bezier segments → approximated as <clothoid> or <paramPoly3>
inline OdRoad roadToOpenDrive(const Road& road, double refLat, double refLon) {
    OdRoad odRoad;
    odRoad.id = road.id;
    odRoad.name = road.name;
    odRoad.junction = "-1";

    // Sample the road centerline
    auto samples = road.sampleCenterline(64);
    if (samples.size() < 2) {
        odRoad.length = 0;
        return odRoad;
    }

    // Compute total length
    double totalLen = polylineLength(samples);
    odRoad.length = totalLen;

    // Convert each segment to geometry
    double s = 0;
    for (size_t i = 0; i < samples.size() - 1; i++) {
        const Point2D& p0 = samples[i];
        const Point2D& p1 = samples[i + 1];

        double segLen = p0.distanceTo(p1);
        double hdg = std::atan2(p1.y - p0.y, p1.x - p0.x);

        OdGeometry geom;
        geom.type = OdGeometryType::Line;
        geom.s = s;
        geom.x = p0.x;
        geom.y = p0.y;
        geom.hdg = hdg;
        geom.length = segLen;

        odRoad.geometries.push_back(geom);
        s += segLen;
    }

    // Lane section (simplified: one lane on each side)
    OdLaneSection laneSection;
    laneSection.s = 0;

    double laneWidth = road.width / road.laneCount;

    // Right lanes (positive IDs in OpenDRIVE)
    for (int i = 1; i <= road.laneCount / 2; i++) {
        OdLane lane;
        lane.id = i;
        lane.width = laneWidth;
        lane.type = "driving";
        laneSection.rightLanes.push_back(lane);
    }

    // Left lanes (negative IDs)
    for (int i = 1; i <= road.laneCount / 2; i++) {
        OdLane lane;
        lane.id = -i;
        lane.width = laneWidth;
        lane.type = "driving";
        laneSection.leftLanes.push_back(lane);
    }

    odRoad.laneSections.push_back(laneSection);

    // Elevation profile (simplified: constant z)
    if (!road.points.empty()) {
        OdElevationProfile elev;
        elev.s = 0;
        elev.a = road.points[0].z;
        elev.b = 0;
        elev.c = 0;
        elev.d = 0;
        odRoad.elevationProfiles.push_back(elev);
    }

    return odRoad;
}

// ─── Serialize OpenDRIVE document to XML ───────────────────
inline std::string serializeOpenDrive(const OpenDriveDocument& doc) {
    std::ostringstream xml;

    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    xml << "<OpenDRIVE>\n";

    // Header
    xml << "  <header revMajor=\"" << doc.headerRevMajor
        << "\" revMinor=\"" << doc.headerRevMinor
        << "\" name=\"" << xmlEscape(doc.headerName)
        << "\" version=\"1.6"
        << "\" date=\"" << xmlEscape(doc.headerDate)
        << "\" north=\"" << xmlEscape(doc.headerNorth)
        << "\" south=\"" << xmlEscape(doc.headerSouth)
        << "\" east=\"" << xmlEscape(doc.headerEast)
        << "\" west=\"" << xmlEscape(doc.headerWest)
        << "\"/>\n";

    // Roads
    for (const auto& road : doc.roads) {
        xml << "  <road name=\"" << xmlEscape(road.name)
            << "\" length=\"" << fmt(road.length)
            << "\" id=\"" << xmlEscape(road.id)
            << "\" junction=\"" << xmlEscape(road.junction)
            << "\">\n";

        // Link (predecessor/successor)
        if (!road.predecessorId.empty() || !road.successorId.empty()) {
            xml << "    <link>\n";
            if (!road.predecessorId.empty()) {
                xml << "      <predecessor elementId=\"" << xmlEscape(road.predecessorId) << "\"/>\n";
            }
            if (!road.successorId.empty()) {
                xml << "      <successor elementId=\"" << xmlEscape(road.successorId) << "\"/>\n";
            }
            xml << "    </link>\n";
        }

        // Type
        xml << "    <type s=\"0\" type=\"town\"/>\n";

        // Plan view (geometry)
        xml << "    <planView>\n";
        for (const auto& geom : road.geometries) {
            xml << "      <geometry s=\"" << fmt(geom.s)
                << "\" x=\"" << fmt(geom.x)
                << "\" y=\"" << fmt(geom.y)
                << "\" hdg=\"" << fmt(geom.hdg)
                << "\" length=\"" << fmt(geom.length)
                << "\">\n";

            switch (geom.type) {
                case OdGeometryType::Line:
                    xml << "        <line/>\n";
                    break;
                case OdGeometryType::Arc:
                    xml << "        <arc curvature=\"" << fmt(geom.curvature) << "\"/>\n";
                    break;
                case OdGeometryType::Clothoid:
                    xml << "        <spiral curvStart=\"" << fmt(geom.curvStart)
                        << "\" curvEnd=\"" << fmt(geom.curvEnd) << "\"/>\n";
                    break;
                case OdGeometryType::ParamPoly3:
                    xml << "        <paramPoly3 aU=\"" << fmt(geom.aU)
                        << "\" bU=\"" << fmt(geom.bU)
                        << "\" cU=\"" << fmt(geom.cU)
                        << "\" dU=\"" << fmt(geom.dU)
                        << "\" aV=\"" << fmt(geom.aV)
                        << "\" bV=\"" << fmt(geom.bV)
                        << "\" cV=\"" << fmt(geom.cV)
                        << "\" dV=\"" << fmt(geom.dV)
                        << "\" pRange=\"local\"/>\n";
                    break;
            }

            xml << "      </geometry>\n";
        }
        xml << "    </planView>\n";

        // Elevation profile
        if (!road.elevationProfiles.empty()) {
            xml << "    <elevationProfile>\n";
            for (const auto& elev : road.elevationProfiles) {
                xml << "      <elevation s=\"" << fmt(elev.s)
                    << "\" a=\"" << fmt(elev.a)
                    << "\" b=\"" << fmt(elev.b)
                    << "\" c=\"" << fmt(elev.c)
                    << "\" d=\"" << fmt(elev.d) << "\"/>\n";
            }
            xml << "    </elevationProfile>\n";
        }

        // Lanes
        if (!road.laneSections.empty()) {
            xml << "    <lanes>\n";
            for (const auto& ls : road.laneSections) {
                xml << "      <laneSection s=\"" << fmt(ls.s) << "\">\n";

                // Left lanes
                if (!ls.leftLanes.empty()) {
                    xml << "        <left>\n";
                    for (const auto& lane : ls.leftLanes) {
                        xml << "          <lane id=\"" << lane.id
                            << "\" type=\"" << lane.type
                            << "\" level=\"" << (lane.level ? "true" : "false") << "\">\n";
                        xml << "            <width sOffset=\"0\" a=\"" << fmt(lane.width)
                            << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                        xml << "          </lane>\n";
                    }
                    xml << "        </left>\n";
                }

                // Center lane
                xml << "        <center>\n";
                xml << "          <lane id=\"0\" type=\"border\" level=\"true\">\n";
                xml << "            <width sOffset=\"0\" a=\"0\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                xml << "          </lane>\n";
                xml << "        </center>\n";

                // Right lanes
                if (!ls.rightLanes.empty()) {
                    xml << "        <right>\n";
                    for (const auto& lane : ls.rightLanes) {
                        xml << "          <lane id=\"" << lane.id
                            << "\" type=\"" << lane.type
                            << "\" level=\"" << (lane.level ? "true" : "false") << "\">\n";
                        xml << "            <width sOffset=\"0\" a=\"" << fmt(lane.width)
                            << "\" b=\"0\" c=\"0\" d=\"0\"/>\n";
                        xml << "          </lane>\n";
                    }
                    xml << "        </right>\n";
                }

                xml << "      </laneSection>\n";
            }
            xml << "    </lanes>\n";
        }

        xml << "  </road>\n";
    }

    // Junctions
    for (const auto& junction : doc.junctions) {
        xml << "  <junction name=\"" << xmlEscape(junction.name)
            << "\" id=\"" << xmlEscape(junction.id) << "\">\n";
        for (const auto& conn : junction.connections) {
            xml << "    <connection id=\"" << xmlEscape(conn.id)
                << "\" incomingRoad=\"" << xmlEscape(conn.incomingRoad)
                << "\" connectingRoad=\"" << xmlEscape(conn.connectingRoad)
                << "\" contactPoint=\"" << (conn.contactPoint == 0 ? "start" : "end") << "\"/>\n";
        }
        xml << "  </junction>\n";
    }

    xml << "</OpenDRIVE>\n";
    return xml.str();
}

// ─── Export roads to OpenDRIVE XML ─────────────────────────
inline std::string exportOpenDrive(
    const std::vector<Road>& roads,
    double refLat,
    double refLon
) {
    OpenDriveDocument doc;
    doc.headerRevMajor = 1;
    doc.headerRevMinor = 6;
    doc.headerName = "OpenGeoStudio Export";
    doc.headerDate = "2026-08-07";

    for (const auto& road : roads) {
        doc.roads.push_back(roadToOpenDrive(road, refLat, refLon));
    }

    return serializeOpenDrive(doc);
}

} // namespace geo