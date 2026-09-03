#pragma once

// ============================================================
// OsmImportPipeline — Complete OSM → Road Studio pipeline
// ============================================================
//
// Single entry point for the complete OSM import workflow:
//
//   OSM file → Parse → Project coordinates → Build network →
//   Detect junctions → Validate → Return result
//
// The result contains everything Road Studio needs to display
// and edit the imported road network.
//

#include "OsmTypes.hpp"
#include "OsmXmlParser.hpp"
#include "CoordinateConverter.hpp"
#include "RoadClassifier.hpp"
#include "RoadNetworkBuilder.hpp"
#include "JunctionDetector.hpp"
#include "RoadValidator.hpp"

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include "../logger/Logger.hpp"
#include <functional>
#include <memory>
#include <queue>
#include <limits>

namespace osm {

struct OsmPreprocessResult {
    int constructionWaysNormalized = 0;
    int disconnectedWaysRemoved = 0;
    int endpointGapsSnapped = 0;
    QStringList log;
};

class OsmPreprocessor {
public:
    struct Params {
        bool normalizeConstruction = true;
        bool removeDisconnectedComponents = false;
        bool snapEndpointGaps = true;
        double snapDistance = 5.0;
    };

    static OsmPreprocessResult process(OsmData& data, const CoordinateConverter& converter,
                                       const Params& params = {})
    {
        OsmPreprocessResult result;
        if (params.normalizeConstruction) normalizeConstruction(data, result);
        if (params.removeDisconnectedComponents) removeDisconnectedComponents(data, result);
        if (params.snapEndpointGaps && params.snapDistance > 0)
            snapEndpointGaps(data, converter, params.snapDistance, result);
        return result;
    }

private:
    static bool isDrivable(const Way& way)
    {
        if (!way.visible || way.isArea() || !way.isHighway()) return false;
        return RoadClassifier::isDrivable(RoadClassifier::classifyAndGet(way.highwayType()).cls);
    }

    static void setTag(Way& way, const QString& key, const QString& value)
    {
        for (auto& tag : way.tags) {
            if (tag.key == key) {
                tag.value = value;
                return;
            }
        }
        way.tags.append({key, value});
    }

    static QString constructionFallback(const Way& way)
    {
        const int lanes = way.lanes();
        if (lanes >= 4) return "primary";
        if (lanes >= 3) return "secondary";
        if (lanes >= 2) return way.name().isEmpty() ? "residential" : "tertiary";
        return way.name().isEmpty() ? "service" : "residential";
    }

    static void normalizeConstruction(OsmData& data, OsmPreprocessResult& result)
    {
        static const std::unordered_set<std::string> supported {
            "motorway", "trunk", "primary", "secondary", "tertiary", "residential",
            "unclassified", "living_street", "service"
        };
        for (auto& [id, way] : data.ways) {
            if (way.highwayType() != "construction") continue;
            QString type = way.tag("construction");
            if (supported.find(type.toStdString()) == supported.end())
                type = constructionFallback(way);
            setTag(way, "highway", type);
            result.constructionWaysNormalized++;
            result.log.append(QString("Normalized construction way %1 to %2").arg(id).arg(type));
        }
    }

    static void removeDisconnectedComponents(OsmData& data, OsmPreprocessResult& result)
    {
        std::unordered_map<qint64, std::vector<qint64>> nodeWays;
        std::unordered_set<qint64> drivableWays;
        for (const auto& [id, way] : data.ways) {
            if (!isDrivable(way)) continue;
            drivableWays.insert(id);
            for (qint64 node : way.nodeRefs) nodeWays[node].push_back(id);
        }
        if (drivableWays.empty()) return;

        std::vector<std::unordered_set<qint64>> components;
        while (!drivableWays.empty()) {
            std::unordered_set<qint64> component;
            std::queue<qint64> pending;
            pending.push(*drivableWays.begin());
            while (!pending.empty()) {
                const qint64 id = pending.front();
                pending.pop();
                if (!component.insert(id).second) continue;
                const auto it = data.ways.find(id);
                if (it == data.ways.end()) continue;
                for (qint64 node : it->second.nodeRefs)
                    for (qint64 neighbour : nodeWays[node])
                        if (!component.contains(neighbour)) pending.push(neighbour);
            }
            for (qint64 id : component) drivableWays.erase(id);
            components.push_back(std::move(component));
        }
        const auto largest = std::max_element(components.begin(), components.end(),
            [](const auto& a, const auto& b) { return a.size() < b.size(); });
        for (auto it = components.begin(); it != components.end(); ++it) {
            if (it == largest) continue;
            for (qint64 id : *it) {
                data.ways[id].visible = false;
                result.disconnectedWaysRemoved++;
            }
        }
        if (result.disconnectedWaysRemoved > 0)
            result.log.append(QString("Removed %1 ways outside the largest road component")
                              .arg(result.disconnectedWaysRemoved));
    }

    static void snapEndpointGaps(OsmData& data, const CoordinateConverter& converter,
                                 double snapDistance, OsmPreprocessResult& result)
    {
        struct ProjectedNode { qint64 id; double x; double y; };
        std::unordered_map<qint64, std::vector<qint64>> nodeWays;
        std::vector<ProjectedNode> candidates;
        for (const auto& [wayId, way] : data.ways) {
            if (!isDrivable(way)) continue;
            for (qint64 nodeId : way.nodeRefs) nodeWays[nodeId].push_back(wayId);
        }
        candidates.reserve(nodeWays.size());
        for (const auto& [nodeId, ways] : nodeWays) {
            const Node* node = data.getNode(nodeId);
            if (!node) continue;
            double x, y;
            converter.toLocal(node->lat, node->lon, x, y);
            candidates.push_back({nodeId, x, y});
        }

        for (auto& [wayId, way] : data.ways) {
            if (!isDrivable(way) || way.nodeRefs.size() < 2) continue;
            for (bool atStart : {true, false}) {
                const qsizetype endpoint = atStart ? 0 : way.nodeRefs.size() - 1;
                const qint64 sourceId = way.nodeRefs[endpoint];
                if (nodeWays[sourceId].size() != 1) continue;
                const Node* source = data.getNode(sourceId);
                if (!source) continue;
                double sourceX, sourceY;
                converter.toLocal(source->lat, source->lon, sourceX, sourceY);
                qint64 bestId = 0;
                double bestDistance = snapDistance;
                for (const auto& candidate : candidates) {
                    if (candidate.id == sourceId) continue;
                    bool belongsToOtherWay = false;
                    for (qint64 candidateWay : nodeWays[candidate.id])
                        if (candidateWay != wayId) { belongsToOtherWay = true; break; }
                    if (!belongsToOtherWay) continue;
                    const double distance = std::hypot(candidate.x - sourceX, candidate.y - sourceY);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestId = candidate.id;
                    }
                }
                if (bestId == 0) continue;
                way.nodeRefs[endpoint] = bestId;
                nodeWays[sourceId].erase(std::remove(nodeWays[sourceId].begin(), nodeWays[sourceId].end(), wayId),
                                         nodeWays[sourceId].end());
                nodeWays[bestId].push_back(wayId);
                result.endpointGapsSnapped++;
                result.log.append(QString("Snapped endpoint of way %1 by %2 m")
                                  .arg(wayId).arg(bestDistance, 0, 'f', 2));
            }
        }
    }
};

// ─── ImportSettings ───
struct ImportSettings {
    // Coordinate projection method
    CoordinateConverter::Method projectionMethod = CoordinateConverter::Method::Equirectangular;

    // Reference origin (if autoDetectReference is false)
    double refLat = 0.0;
    double refLon = 0.0;
    bool autoDetectReference = true;  // use bounds center

    // Geometry simplification
    double simplifyTolerance = 0.5;       // meters
    double minSegmentLength = 0.5;        // meters
    bool preserveJunctionNodes = true;
    bool fitCurves = false;

    // Import filters
    bool importFootways = false;
    bool importCycleways = true;
    bool importServiceRoads = true;
    bool importTracks = false;

    bool normalizeConstruction = true;
    bool removeDisconnectedComponents = false;
    bool snapEndpointGaps = true;
    double endpointSnapDistance = 5.0;

    // Validation
    bool runValidation = true;
    bool autoRepair = true;

    // Progress callback (0.0 to 1.0)
    std::function<void(double, const QString&)> progressCallback;
};

// ─── ImportResult ───
struct ImportResult {
    bool success = false;
    QString errorMessage;

    // Raw OSM data
    OsmData osmData;

    // Coordinate converter used
    CoordinateConverter converter;

    // Generated road network
    RoadNetworkBuilder::Result network;

    // Detected junctions
    std::vector<DetectedJunction> junctions;

    // Validation issues
    std::vector<ValidationIssue> validationIssues;

    // Repair results
    RoadValidator::RepairResult repairResult;
    OsmPreprocessResult preprocessResult;

    // Statistics
    struct Stats {
        int osmNodes = 0;
        int osmWays = 0;
        int osmRelations = 0;
        int roadsCreated = 0;
        int segmentsCreated = 0;
        int junctionsDetected = 0;
        int endPointsDetected = 0;
        int validationErrors = 0;
        int validationWarnings = 0;
        int repairsApplied = 0;
        int constructionWaysNormalized = 0;
        int disconnectedWaysRemoved = 0;
        int endpointGapsSnapped = 0;
        double totalRoadLength = 0.0;  // meters
    } stats;

    // ─── Serialization (for project save/reload) ───
    QJsonObject toJson() const {
        QJsonObject j;

        j["success"] = success;
        j["errorMessage"] = errorMessage;

        // OSM data
        j["osmData"] = osmData.toJson();

        // Converter settings
        QJsonObject conv;
        conv["refLat"] = converter.refLat;
        conv["refLon"] = converter.refLon;
        conv["method"] = int(converter.method);
        j["converter"] = conv;

        // Stats
        QJsonObject s;
        s["osmNodes"] = stats.osmNodes;
        s["osmWays"] = stats.osmWays;
        s["osmRelations"] = stats.osmRelations;
        s["roadsCreated"] = stats.roadsCreated;
        s["segmentsCreated"] = stats.segmentsCreated;
        s["junctionsDetected"] = stats.junctionsDetected;
        s["endPointsDetected"] = stats.endPointsDetected;
        s["validationErrors"] = stats.validationErrors;
        s["validationWarnings"] = stats.validationWarnings;
        s["repairsApplied"] = stats.repairsApplied;
        s["constructionWaysNormalized"] = stats.constructionWaysNormalized;
        s["disconnectedWaysRemoved"] = stats.disconnectedWaysRemoved;
        s["endpointGapsSnapped"] = stats.endpointGapsSnapped;
        s["totalRoadLength"] = stats.totalRoadLength;
        j["stats"] = s;

        // Validation issues
        QJsonArray issuesArr;
        for (const auto& issue : validationIssues) {
            QJsonObject i;
            i["severity"] = issue.severityString();
            i["category"] = issue.category;
            i["roadId"] = issue.roadId;
            i["junctionId"] = issue.junctionId;
            i["message"] = issue.message;
            i["value"] = issue.value;
            issuesArr.append(i);
        }
        j["validationIssues"] = issuesArr;

        return j;
    }

    static ImportResult fromJson(const QJsonObject& j) {
        ImportResult r;
        r.success = j["success"].toBool();
        r.errorMessage = j["errorMessage"].toString();

        if (j.contains("osmData")) {
            r.osmData = OsmData::fromJson(j["osmData"].toObject());
        }

        if (j.contains("converter")) {
            QJsonObject c = j["converter"].toObject();
            r.converter.refLat = c["refLat"].toDouble();
            r.converter.refLon = c["refLon"].toDouble();
            r.converter.method = CoordinateConverter::Method(c["method"].toInt(0));
            r.converter.setReference(r.converter.refLat, r.converter.refLon, r.converter.method);
        }

        if (j.contains("stats")) {
            QJsonObject s = j["stats"].toObject();
            r.stats.osmNodes = s["osmNodes"].toInt();
            r.stats.osmWays = s["osmWays"].toInt();
            r.stats.osmRelations = s["osmRelations"].toInt();
            r.stats.roadsCreated = s["roadsCreated"].toInt();
            r.stats.segmentsCreated = s["segmentsCreated"].toInt();
            r.stats.junctionsDetected = s["junctionsDetected"].toInt();
            r.stats.endPointsDetected = s["endPointsDetected"].toInt();
            r.stats.validationErrors = s["validationErrors"].toInt();
            r.stats.validationWarnings = s["validationWarnings"].toInt();
            r.stats.repairsApplied = s["repairsApplied"].toInt();
            r.stats.constructionWaysNormalized = s["constructionWaysNormalized"].toInt();
            r.stats.disconnectedWaysRemoved = s["disconnectedWaysRemoved"].toInt();
            r.stats.endpointGapsSnapped = s["endpointGapsSnapped"].toInt();
            r.stats.totalRoadLength = s["totalRoadLength"].toDouble();
        }

        if (j.contains("validationIssues")) {
            for (const auto& v : j["validationIssues"].toArray()) {
                QJsonObject i = v.toObject();
                ValidationIssue issue;
                QString sev = i["severity"].toString();
                issue.severity = sev == "ERROR" ? Severity::Error :
                                sev == "WARNING" ? Severity::Warning : Severity::Info;
                issue.category = i["category"].toString();
                issue.roadId = i["roadId"].toString();
                issue.junctionId = i["junctionId"].toString();
                issue.message = i["message"].toString();
                issue.value = i["value"].toDouble();
                r.validationIssues.push_back(issue);
            }
        }

        return r;
    }
};

// ─── OsmImportPipeline ───
class OsmImportPipeline {
public:
    // Run the complete import pipeline from a file
    static ImportResult importFromFile(const QString& osmFilePath,
                                        const ImportSettings& settings = {})
    {
        ImportResult result;
        auto report = [&](double progress, const QString& msg) {
            if (settings.progressCallback) settings.progressCallback(progress, msg);
        };

        // ─── Step 1: Parse OSM file ───
        report(0.0, "Parsing OSM file...");
        QString parseError;
        if (!OsmXmlParser::parseFile(osmFilePath, result.osmData, &parseError)) {
            result.errorMessage = parseError;
            return result;
        }
        result.stats.osmNodes = result.osmData.nodeCount();
        result.stats.osmWays = result.osmData.wayCount();
        result.stats.osmRelations = result.osmData.relationCount();
        report(0.2, QString("Parsed %1 nodes, %2 ways").arg(result.stats.osmNodes).arg(result.stats.osmWays));

        // ─── Step 2: Set up coordinate converter ───
        report(0.3, "Setting up coordinate system...");
        if (settings.autoDetectReference && result.osmData.hasBounds) {
            result.converter.setReferenceFromBounds(
                result.osmData.minLat, result.osmData.minLon,
                result.osmData.maxLat, result.osmData.maxLon,
                settings.projectionMethod);
        } else {
            result.converter.setReference(settings.refLat, settings.refLon,
                                           settings.projectionMethod);
        }

        report(0.35, "Repairing OSM topology...");
        OsmPreprocessor::Params preprocessParams;
        preprocessParams.normalizeConstruction = settings.normalizeConstruction;
        preprocessParams.removeDisconnectedComponents = settings.removeDisconnectedComponents;
        preprocessParams.snapEndpointGaps = settings.snapEndpointGaps;
        preprocessParams.snapDistance = settings.endpointSnapDistance;
        result.preprocessResult = OsmPreprocessor::process(
            result.osmData, result.converter, preprocessParams);
        result.stats.constructionWaysNormalized = result.preprocessResult.constructionWaysNormalized;
        result.stats.disconnectedWaysRemoved = result.preprocessResult.disconnectedWaysRemoved;
        result.stats.endpointGapsSnapped = result.preprocessResult.endpointGapsSnapped;

        // ─── Step 3: Build road network ───
        report(0.4, "Building road network...");
        RoadNetworkBuilder::Params buildParams;
        buildParams.simplifyTolerance = settings.simplifyTolerance;
        buildParams.minSegmentLength = settings.minSegmentLength;
        buildParams.preserveJunctionNodes = settings.preserveJunctionNodes;
        buildParams.fitCurves = settings.fitCurves;

        result.network = RoadNetworkBuilder::build(result.osmData, result.converter, buildParams);
        result.stats.roadsCreated = result.network.roadsCreated;
        result.stats.segmentsCreated = result.network.segmentsCreated;
        result.stats.junctionsDetected = result.network.junctionsDetected;
        result.stats.endPointsDetected = result.network.endPointsDetected;

        // Compute total road length
        for (const auto& road : result.network.roads) {
            result.stats.totalRoadLength += road.totalLength();
        }

        report(0.6, QString("Created %1 roads (%2m total)").arg(result.stats.roadsCreated).arg(result.stats.totalRoadLength, 0, 'f', 0));

        // ─── Step 4: Detect junctions ───
        report(0.7, "Detecting junctions...");
        JunctionDetector::Params junctionParams;
        result.junctions = JunctionDetector::detect(result.network, result.osmData, junctionParams);
        result.stats.junctionsDetected = int(result.junctions.size());

        report(0.8, QString("Detected %1 junctions").arg(result.stats.junctionsDetected));

        // ─── Step 5: Validate ───
        if (settings.runValidation) {
            report(0.85, "Validating road network...");
            RoadValidator::Params valParams;
            result.validationIssues = RoadValidator::validate(
                result.network, result.junctions, valParams);

            for (const auto& issue : result.validationIssues) {
                if (issue.severity == Severity::Error) result.stats.validationErrors++;
                else if (issue.severity == Severity::Warning) result.stats.validationWarnings++;
            }

            // ─── Step 6: Auto-repair ───
            if (settings.autoRepair && !result.validationIssues.empty()) {
                report(0.9, "Auto-repairing...");
                result.repairResult = RoadValidator::autoRepair(
                    result.network, result.validationIssues);
                result.stats.repairsApplied = result.repairResult.repairsApplied;
            }
        }

        report(1.0, "Import complete");
        result.success = true;
        return result;
    }
};

} // namespace osm
