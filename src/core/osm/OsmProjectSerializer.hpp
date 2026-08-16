#pragma once

// ============================================================
// OsmProjectSerializer — Save/reload OSM-derived road networks
// ============================================================
//
// Serializes the complete OSM import result to a project JSON
// file, including:
//   - Original OSM data
//   - Road network (RoadV2 roads)
//   - Junctions
//   - Roundabouts
//   - Road markings
//   - Traffic signs
//   - Coordinate converter settings
//   - Validation results
//
// The serialized format is versioned for forward compatibility.
//

#include "OsmTypes.hpp"
#include "OsmImportPipeline.hpp"
#include "RoadNetworkBuilder.hpp"
#include "JunctionDetector.hpp"
#include "RoundaboutGenerator.hpp"
#include "RoadMarkingGenerator.hpp"
#include "TrafficSignGenerator.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"
#include "../../engine/road/lane_engine.hpp"

#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QString>
#include <QDebug>
#include <vector>

namespace osm {

// ─── OsmProjectData — Complete serializable OSM project ───
struct OsmProjectData {
    static constexpr int FORMAT_VERSION = 1;

    int version = FORMAT_VERSION;
    QString projectName;
    QString sourceFile;
    QString timestamp;

    // Core data
    OsmData osmData;
    CoordinateConverter converter;

    // Generated data
    std::vector<geo::RoadV2> roads;
    std::vector<DetectedJunction> junctions;
    std::vector<RoundaboutGeometry> roundabouts;
    std::vector<RoadMarking> markings;
    std::vector<TrafficSign> signs;

    // Validation
    std::vector<ValidationIssue> validationIssues;

    // Statistics
    int roadCount = 0;
    int junctionCount = 0;
    int roundaboutCount = 0;
    int markingCount = 0;
    int signCount = 0;
    double totalRoadLength = 0.0;

    // ─── Serialize to JSON ───
    QJsonObject toJson() const {
        QJsonObject j;
        j["version"] = version;
        j["projectName"] = projectName;
        j["sourceFile"] = sourceFile;
        j["timestamp"] = timestamp;

        // OSM data
        j["osmData"] = osmData.toJson();

        // Converter
        QJsonObject conv;
        conv["refLat"] = converter.refLat;
        conv["refLon"] = converter.refLon;
        conv["method"] = int(converter.method);
        j["converter"] = conv;

        // Roads
        QJsonArray roadsArr;
        for (const auto& road : roads) {
            roadsArr.append(roadToJson(road));
        }
        j["roads"] = roadsArr;

        // Junctions
        QJsonArray junctionsArr;
        for (const auto& jun : junctions) {
            QJsonObject jo;
            jo["id"] = jun.id;
            jo["osmNodeId"] = qint64(jun.osmNodeId);
            jo["type"] = int(jun.type);
            jo["centerX"] = jun.center.x;
            jo["centerY"] = jun.center.y;
            jo["z"] = jun.z;
            jo["isRoundabout"] = jun.isRoundabout;
            jo["isBridge"] = jun.isBridge;
            jo["isTunnel"] = jun.isTunnel;
            jo["layer"] = jun.layer;

            QJsonArray rids;
            for (const auto& rid : jun.roadIds) rids.append(rid);
            jo["roadIds"] = rids;

            QJsonArray headings;
            for (double h : jun.roadHeadings) headings.append(h);
            jo["roadHeadings"] = headings;

            junctionsArr.append(jo);
        }
        j["junctions"] = junctionsArr;

        // Roundabouts
        QJsonArray rbArr;
        for (const auto& rb : roundabouts) {
            rbArr.append(RoundaboutGenerator::toJson(rb));
        }
        j["roundabouts"] = rbArr;

        // Markings
        j["markings"] = RoadMarkingGenerator::toJsonArray(markings);

        // Signs
        j["signs"] = TrafficSignGenerator::toJsonArray(signs);

        // Validation
        QJsonArray valArr;
        for (const auto& issue : validationIssues) {
            QJsonObject io;
            io["severity"] = issue.severityString();
            io["category"] = issue.category;
            io["roadId"] = issue.roadId;
            io["junctionId"] = issue.junctionId;
            io["message"] = issue.message;
            io["value"] = issue.value;
            valArr.append(io);
        }
        j["validationIssues"] = valArr;

        // Stats
        QJsonObject stats;
        stats["roadCount"] = roadCount;
        stats["junctionCount"] = junctionCount;
        stats["roundaboutCount"] = roundaboutCount;
        stats["markingCount"] = markingCount;
        stats["signCount"] = signCount;
        stats["totalRoadLength"] = totalRoadLength;
        j["stats"] = stats;

        return j;
    }

    // ─── Deserialize from JSON ───
    static OsmProjectData fromJson(const QJsonObject& j) {
        OsmProjectData p;
        p.version = j["version"].toInt(FORMAT_VERSION);
        p.projectName = j["projectName"].toString();
        p.sourceFile = j["sourceFile"].toString();
        p.timestamp = j["timestamp"].toString();

        if (j.contains("osmData")) {
            p.osmData = OsmData::fromJson(j["osmData"].toObject());
        }

        if (j.contains("converter")) {
            QJsonObject c = j["converter"].toObject();
            p.converter.refLat = c["refLat"].toDouble();
            p.converter.refLon = c["refLon"].toDouble();
            p.converter.method = CoordinateConverter::Method(c["method"].toInt(0));
            p.converter.setReference(p.converter.refLat, p.converter.refLon,
                                      p.converter.method);
        }

        // Roads
        if (j.contains("roads")) {
            for (const auto& v : j["roads"].toArray()) {
                p.roads.push_back(roadFromJson(v.toObject()));
            }
        }

        // Junctions
        if (j.contains("junctions")) {
            for (const auto& v : j["junctions"].toArray()) {
                QJsonObject jo = v.toObject();
                DetectedJunction jun;
                jun.id = jo["id"].toString();
                jun.osmNodeId = jo["osmNodeId"].toVariant().toLongLong();
                jun.type = JunctionType(jo["type"].toInt());
                jun.center.x = jo["centerX"].toDouble();
                jun.center.y = jo["centerY"].toDouble();
                jun.z = jo["z"].toDouble();
                jun.isRoundabout = jo["isRoundabout"].toBool();
                jun.isBridge = jo["isBridge"].toBool();
                jun.isTunnel = jo["isTunnel"].toBool();
                jun.layer = jo["layer"].toInt();

                for (const auto& rid : jo["roadIds"].toArray()) {
                    jun.roadIds.push_back(rid.toString());
                }
                for (const auto& h : jo["roadHeadings"].toArray()) {
                    jun.roadHeadings.push_back(h.toDouble());
                }
                p.junctions.push_back(jun);
            }
        }

        // Roundabouts
        if (j.contains("roundabouts")) {
            for (const auto& v : j["roundabouts"].toArray()) {
                p.roundabouts.push_back(RoundaboutGenerator::fromJson(v.toObject()));
            }
        }

        // Markings
        if (j.contains("markings")) {
            p.markings = RoadMarkingGenerator::fromJsonArray(j["markings"].toArray());
        }

        // Signs
        if (j.contains("signs")) {
            p.signs = TrafficSignGenerator::fromJsonArray(j["signs"].toArray());
        }

        // Validation
        if (j.contains("validationIssues")) {
            for (const auto& v : j["validationIssues"].toArray()) {
                QJsonObject io = v.toObject();
                ValidationIssue issue;
                QString sev = io["severity"].toString();
                issue.severity = sev == "ERROR" ? Severity::Error :
                                sev == "WARNING" ? Severity::Warning : Severity::Info;
                issue.category = io["category"].toString();
                issue.roadId = io["roadId"].toString();
                issue.junctionId = io["junctionId"].toString();
                issue.message = io["message"].toString();
                issue.value = io["value"].toDouble();
                p.validationIssues.push_back(issue);
            }
        }

        // Stats
        if (j.contains("stats")) {
            QJsonObject s = j["stats"].toObject();
            p.roadCount = s["roadCount"].toInt();
            p.junctionCount = s["junctionCount"].toInt();
            p.roundaboutCount = s["roundaboutCount"].toInt();
            p.markingCount = s["markingCount"].toInt();
            p.signCount = s["signCount"].toInt();
            p.totalRoadLength = s["totalRoadLength"].toDouble();
        }

        return p;
    }

    // ─── Save to file ───
    static bool saveToFile(const QString& path, const OsmProjectData& data,
                            QString* errorMsg = nullptr)
    {
        QJsonObject json = data.toJson();
        QJsonDocument doc(json);

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QString("Cannot open file for writing: %1").arg(path);
            return false;
        }

        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();

        qDebug() << "[OsmProjectSerializer] Saved project to" << path
                 << "—" << data.roadCount << "roads,"
                 << data.junctionCount << "junctions";
        return true;
    }

    // ─── Load from file ───
    static OsmProjectData loadFromFile(const QString& path, bool* ok = nullptr,
                                        QString* errorMsg = nullptr)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (ok) *ok = false;
            if (errorMsg) *errorMsg = QString("Cannot open file: %1").arg(path);
            return OsmProjectData();
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();

        if (parseError.error != QJsonParseError::NoError) {
            if (ok) *ok = false;
            if (errorMsg) *errorMsg = QString("JSON parse error: %1").arg(parseError.errorString());
            return OsmProjectData();
        }

        if (ok) *ok = true;
        OsmProjectData data = fromJson(doc.object());

        qDebug() << "[OsmProjectSerializer] Loaded project from" << path
                 << "—" << data.roadCount << "roads,"
                 << data.junctionCount << "junctions";
        return data;
    }

    // ─── Build from import result ───
    static OsmProjectData fromImportResult(const ImportResult& result,
                                            const std::vector<RoundaboutGeometry>& roundabouts,
                                            const std::vector<RoadMarking>& markings,
                                            const std::vector<TrafficSign>& signs,
                                            const QString& projectName = "OSM Import")
    {
        OsmProjectData data;
        data.projectName = projectName;
        data.sourceFile = result.osmData.sourceFile;
        data.osmData = result.osmData;
        data.converter = result.converter;
        data.roads = result.network.roads;
        data.junctions = result.junctions;
        data.roundabouts = roundabouts;
        data.markings = markings;
        data.signs = signs;
        data.validationIssues = result.validationIssues;
        data.roadCount = int(data.roads.size());
        data.junctionCount = int(data.junctions.size());
        data.roundaboutCount = int(data.roundabouts.size());
        data.markingCount = int(data.markings.size());
        data.signCount = int(data.signs.size());
        data.totalRoadLength = result.stats.totalRoadLength;
        return data;
    }

private:
    // ─── RoadV2 serialization ───
    static QJsonObject roadToJson(const geo::RoadV2& road) {
        QJsonObject j;
        j["id"] = QString::fromStdString(road.id);
        j["name"] = QString::fromStdString(road.name);
        j["color"] = QString::fromStdString(road.color);
        j["profileName"] = QString::fromStdString(road.profileName);
        j["width"] = road.width;
        j["laneCount"] = road.laneCount;
        j["startIntersectionId"] = QString::fromStdString(road.startIntersectionId);
        j["endIntersectionId"] = QString::fromStdString(road.endIntersectionId);

        // Segments — serialize as point pairs (LineSegment only for now)
        QJsonArray segs;
        for (int i = 0; i < road.numSegments(); i++) {
            const auto& seg = road.segment(i);
            QJsonObject s;
            s["type"] = "line";  // only line segments from OSM import
            s["startX"] = seg.startPoint().x;
            s["startY"] = seg.startPoint().y;
            s["endX"] = seg.endPoint().x;
            s["endY"] = seg.endPoint().y;
            segs.append(s);
        }
        j["segments"] = segs;

        // Lane sections
        QJsonArray laneSections;
        for (int i = 0; i < road.numLaneSections(); i++) {
            const auto& ls = road.laneSection(i);
            QJsonObject lso;
            lso["startS"] = ls.startS;

            QJsonArray lanes;
            for (const auto& lane : ls.lanes()) {
                QJsonObject lo;
                lo["id"] = lane.id;
                lo["type"] = int(lane.type);
                lo["width"] = lane.widthAt(0.0);  // constant width
                lanes.append(lo);
            }
            lso["lanes"] = lanes;
            laneSections.append(lso);
        }
        j["laneSections"] = laneSections;

        return j;
    }

    static geo::RoadV2 roadFromJson(const QJsonObject& j) {
        geo::RoadV2 road;
        road.id = j["id"].toString().toStdString();
        road.name = j["name"].toString().toStdString();
        road.color = j["color"].toString().toStdString();
        road.profileName = j["profileName"].toString().toStdString();
        road.width = j["width"].toDouble(8.0);
        road.laneCount = j["laneCount"].toInt(2);
        road.startIntersectionId = j["startIntersectionId"].toString().toStdString();
        road.endIntersectionId = j["endIntersectionId"].toString().toStdString();

        // Segments
        if (j.contains("segments")) {
            for (const auto& v : j["segments"].toArray()) {
                QJsonObject s = v.toObject();
                QString type = s["type"].toString();
                if (type == "line") {
                    geo::Point2D start(s["startX"].toDouble(), s["startY"].toDouble());
                    geo::Point2D end(s["endX"].toDouble(), s["endY"].toDouble());
                    road.addSegment<geo::LineSegment>(start, end);
                }
            }
        }

        // Lane sections
        if (j.contains("laneSections")) {
            for (const auto& v : j["laneSections"].toArray()) {
                QJsonObject lso = v.toObject();
                geo::LaneSection ls;
                ls.startS = lso["startS"].toDouble();

                for (const auto& lv : lso["lanes"].toArray()) {
                    QJsonObject lo = lv.toObject();
                    geo::Lane lane;
                    lane.id = lo["id"].toInt();
                    lane.type = geo::LaneType(lo["type"].toInt());
                    lane.width = geo::Polynomial3(lo["width"].toDouble(3.5));
                    ls.addLane(lane);
                }
                road.addLaneSection(ls);
            }
        }

        return road;
    }
};

} // namespace osm
