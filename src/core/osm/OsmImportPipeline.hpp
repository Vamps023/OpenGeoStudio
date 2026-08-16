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
#include <QDebug>
#include <functional>
#include <memory>

namespace osm {

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
