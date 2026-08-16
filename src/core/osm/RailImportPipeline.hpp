#pragma once

// ============================================================
// RailImportPipeline — OSM → Train Studio pipeline
// ============================================================
//
// Mirrors OsmImportPipeline but filters for railway data
// instead of highway data. Reuses the same OSM parsing,
// coordinate conversion, and network building infrastructure.
//
// Pipeline:
//   OSM file → Parse → Project coordinates → Build rail network →
//   Validate → Return result
//
// No junctions, roundabouts, markings, or traffic signs are
// generated (those are road-specific concepts).
//

#include "OsmTypes.hpp"
#include "OsmXmlParser.hpp"
#include "CoordinateConverter.hpp"
#include "RoadNetworkBuilder.hpp"
#include "RoadValidator.hpp"

#include <QString>
#include <QJsonObject>
#include <QDebug>
#include <functional>

namespace osm {

// ─── RailImportSettings ───
struct RailImportSettings {
    CoordinateConverter::Method projectionMethod = CoordinateConverter::Method::Equirectangular;
    double refLat = 0.0;
    double refLon = 0.0;
    bool autoDetectReference = true;

    double simplifyTolerance = 0.5;
    double minSegmentLength = 0.5;
    bool preserveJunctionNodes = true;

    bool runValidation = true;
    bool autoRepair = true;

    std::function<void(double, const QString&)> progressCallback;
};

// ─── RailImportResult ───
struct RailImportResult {
    bool success = false;
    QString errorMessage;

    OsmData osmData;
    CoordinateConverter converter;
    RoadNetworkBuilder::Result network;

    std::vector<ValidationIssue> validationIssues;
    RoadValidator::RepairResult repairResult;

    struct Stats {
        int osmNodes = 0;
        int osmWays = 0;
        int osmRelations = 0;
        int tracksCreated = 0;
        int segmentsCreated = 0;
        int switchesDetected = 0;
        int endPointsDetected = 0;
        int validationErrors = 0;
        int validationWarnings = 0;
        int repairsApplied = 0;
        double totalTrackLength = 0.0;
    } stats;

    QJsonObject toJson() const {
        QJsonObject j;
        j["success"] = success;
        j["errorMessage"] = errorMessage;
        j["osmData"] = osmData.toJson();

        QJsonObject conv;
        conv["refLat"] = converter.refLat;
        conv["refLon"] = converter.refLon;
        conv["method"] = int(converter.method);
        j["converter"] = conv;

        QJsonObject s;
        s["osmNodes"] = stats.osmNodes;
        s["osmWays"] = stats.osmWays;
        s["osmRelations"] = stats.osmRelations;
        s["tracksCreated"] = stats.tracksCreated;
        s["segmentsCreated"] = stats.segmentsCreated;
        s["switchesDetected"] = stats.switchesDetected;
        s["endPointsDetected"] = stats.endPointsDetected;
        s["validationErrors"] = stats.validationErrors;
        s["validationWarnings"] = stats.validationWarnings;
        s["repairsApplied"] = stats.repairsApplied;
        s["totalTrackLength"] = stats.totalTrackLength;
        j["stats"] = s;
        return j;
    }

    static RailImportResult fromJson(const QJsonObject& j) {
        RailImportResult r;
        r.success = j["success"].toBool();
        r.errorMessage = j["errorMessage"].toString();

        if (j.contains("osmData"))
            r.osmData = OsmData::fromJson(j["osmData"].toObject());

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
            r.stats.tracksCreated = s["tracksCreated"].toInt();
            r.stats.segmentsCreated = s["segmentsCreated"].toInt();
            r.stats.switchesDetected = s["switchesDetected"].toInt();
            r.stats.endPointsDetected = s["endPointsDetected"].toInt();
            r.stats.validationErrors = s["validationErrors"].toInt();
            r.stats.validationWarnings = s["validationWarnings"].toInt();
            r.stats.repairsApplied = s["repairsApplied"].toInt();
            r.stats.totalTrackLength = s["totalTrackLength"].toDouble();
        }
        return r;
    }
};

// ─── RailImportPipeline ───
class RailImportPipeline {
public:
    static RailImportResult importFromFile(const QString& osmFilePath,
                                            const RailImportSettings& settings = {})
    {
        RailImportResult result;
        auto report = [&](double progress, const QString& msg) {
            if (settings.progressCallback) settings.progressCallback(progress, msg);
        };

        // Step 1: Parse OSM file
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

        // Step 2: Set up coordinate converter
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

        // Step 3: Build rail network (Railway mode)
        report(0.4, "Building rail network...");
        RoadNetworkBuilder::Params buildParams;
        buildParams.mode = RoadNetworkBuilder::Mode::Railway;
        buildParams.simplifyTolerance = settings.simplifyTolerance;
        buildParams.minSegmentLength = settings.minSegmentLength;
        buildParams.preserveJunctionNodes = settings.preserveJunctionNodes;

        result.network = RoadNetworkBuilder::build(result.osmData, result.converter, buildParams);
        result.stats.tracksCreated = result.network.roadsCreated;
        result.stats.segmentsCreated = result.network.segmentsCreated;
        result.stats.switchesDetected = result.network.junctionsDetected;
        result.stats.endPointsDetected = result.network.endPointsDetected;

        for (const auto& track : result.network.roads)
            result.stats.totalTrackLength += track.totalLength();

        report(0.6, QString("Created %1 tracks (%2m total)")
            .arg(result.stats.tracksCreated)
            .arg(result.stats.totalTrackLength, 0, 'f', 0));

        // Step 4: Validate
        if (settings.runValidation) {
            report(0.85, "Validating rail network...");
            RoadValidator::Params valParams;
            result.validationIssues = RoadValidator::validate(
                result.network, {}, valParams);

            for (const auto& issue : result.validationIssues) {
                if (issue.severity == Severity::Error) result.stats.validationErrors++;
                else if (issue.severity == Severity::Warning) result.stats.validationWarnings++;
            }

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
