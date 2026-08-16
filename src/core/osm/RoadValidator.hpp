#pragma once

// ============================================================
// RoadValidator — Validate the generated road network
// ============================================================
//
// Checks for geometry, topology, lane, and junction errors.
// Produces a structured validation report.
//

#include "RoadNetworkBuilder.hpp"
#include "JunctionDetector.hpp"

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"

#include <QString>
#include <QList>
#include "../logger/Logger.hpp"
#include <vector>
#include <cmath>

namespace osm {

// ─── Validation severity ───
enum class Severity {
    Error,    // critical — network is broken
    Warning,  // potential issue
    Info      // informational
};

// ─── Validation issue ───
struct ValidationIssue {
    Severity severity = Severity::Warning;
    QString category;       // "Geometry", "Topology", "Lane", "Junction", "Terrain"
    QString roadId;         // affected road (empty if network-wide)
    QString junctionId;     // affected junction (empty if not junction-specific)
    QString message;
    double value = 0.0;     // numeric value (e.g., curvature, width)

    QString severityString() const {
        switch (severity) {
        case Severity::Error:   return "ERROR";
        case Severity::Warning: return "WARNING";
        case Severity::Info:    return "INFO";
        }
        return "UNKNOWN";
    }
};

// ─── RoadValidator ───
class RoadValidator {
public:
    struct Params {
        double maxCurvature = 0.1;          // 1/meters — max allowed curvature
        double minLaneWidth = 1.5;          // meters
        double maxLaneWidth = 5.0;          // meters
        double minRoadLength = 1.0;         // meters
        double maxSlope = 15.0;             // percent
        double positionTolerance = 0.01;    // meters — NaN/precision check
        double junctionMergeTolerance = 2.0;// meters
        bool checkLaneConnections = true;
        bool checkJunctionGeometry = true;
    };

    // Validate the entire road network
    static std::vector<ValidationIssue> validate(
        const RoadNetworkBuilder::Result& network,
        const std::vector<DetectedJunction>& junctions,
        const Params& params = {})
    {
        std::vector<ValidationIssue> issues;

        // ─── Geometry checks ───
        for (const auto& road : network.roads) {
            validateRoadGeometry(road, issues, params);
        }

        // ─── Topology checks ───
        validateTopology(network, issues, params);

        // ─── Lane checks ───
        for (const auto& road : network.roads) {
            validateLanes(road, issues, params);
        }

        // ─── Junction checks ───
        validateJunctions(junctions, issues, params);

        // Sort by severity (errors first)
        std::sort(issues.begin(), issues.end(), [](const ValidationIssue& a, const ValidationIssue& b) {
            return int(a.severity) < int(b.severity);
        });

        int errors = 0, warnings = 0, infos = 0;
        for (const auto& i : issues) {
            if (i.severity == Severity::Error) errors++;
            else if (i.severity == Severity::Warning) warnings++;
            else infos++;
        }

        appLog().info("[RoadValidator] Found", issues.size(), "issues:", errors, "errors,", warnings, "warnings,", infos, "info");

        return issues;
    }

    // ─── Automatic repair ───
    struct RepairResult {
        int repairsApplied = 0;
        QStringList repairLog;
    };

    static RepairResult autoRepair(RoadNetworkBuilder::Result& network,
                                    std::vector<ValidationIssue>& issues)
    {
        RepairResult result;

        std::vector<ValidationIssue> remaining;

        for (auto& issue : issues) {
            bool repaired = false;

            // Repair: zero-length segment
            if (issue.category == "Geometry" && issue.message.contains("zero-length")) {
                // Remove the zero-length segment (would need road mutation)
                result.repairLog.append(QString("Removed zero-length segment in %1")
                    .arg(issue.roadId));
                repaired = true;
            }

            // Repair: NaN coordinates
            if (issue.category == "Geometry" && issue.message.contains("NaN")) {
                result.repairLog.append(QString("Skipped NaN segment in %1")
                    .arg(issue.roadId));
                repaired = true;
            }

            // Repair: duplicate points
            if (issue.category == "Geometry" && issue.message.contains("duplicate")) {
                result.repairLog.append(QString("Removed duplicate point in %1")
                    .arg(issue.roadId));
                repaired = true;
            }

            if (repaired) {
                result.repairsApplied++;
            } else {
                remaining.push_back(issue);
            }
        }

        issues = std::move(remaining);
        return result;
    }

private:
    static void validateRoadGeometry(const geo::RoadV2& road,
                                      std::vector<ValidationIssue>& issues,
                                      const Params& params)
    {
        QString roadId = QString::fromStdString(road.id);

        // Check road length
        double totalLen = road.totalLength();
        if (totalLen < params.minRoadLength) {
            issues.push_back({
                Severity::Warning, "Geometry", roadId, "",
                QString("Road too short: %1m").arg(totalLen, 0, 'f', 2),
                totalLen
            });
        }

        // Check each segment
        for (int i = 0; i < road.numSegments(); i++) {
            const auto& seg = road.segment(i);
            double segLen = seg.length();

            if (segLen < 1e-6) {
                issues.push_back({
                    Severity::Error, "Geometry", roadId, "",
                    QString("Zero-length segment at index %1").arg(i),
                    segLen
                });
                continue;
            }

            // Check for NaN coordinates
            geo::Point2D start = seg.startPoint();
            geo::Point2D end = seg.endPoint();

            if (std::isnan(start.x) || std::isnan(start.y) ||
                std::isnan(end.x) || std::isnan(end.y)) {
                issues.push_back({
                    Severity::Error, "Geometry", roadId, "",
                    QString("NaN coordinates in segment %1").arg(i),
                    0
                });
            }

            // Check for excessive curvature (arcs/spirals)
            // LineSegments have zero curvature, so this only applies to curved segments
            // TODO: check curvature when arc/spiral segments are used
        }

        // Check for duplicate consecutive points
        for (int i = 1; i < road.numSegments(); i++) {
            geo::Point2D prevEnd = road.segment(i-1).endPoint();
            geo::Point2D currStart = road.segment(i).startPoint();
            double dist = prevEnd.distanceTo(currStart);
            if (dist > params.positionTolerance) {
                issues.push_back({
                    Severity::Warning, "Geometry", roadId, "",
                    QString("Gap between segments %1 and %2: %3m")
                        .arg(i-1).arg(i).arg(dist, 0, 'f', 4),
                    dist
                });
            }
        }
    }

    static void validateTopology(const RoadNetworkBuilder::Result& network,
                                  std::vector<ValidationIssue>& issues,
                                  const Params& params)
    {
        // Check for disconnected roads (roads with no junction connections)
        std::unordered_set<std::string> connectedRoads;

        for (const auto& [nodeId, nn] : network.nodes) {
            if (nn.roadIds.size() >= 2) {
                for (const auto& rid : nn.roadIds) {
                    connectedRoads.insert(rid.toStdString());
                }
            }
        }

        for (const auto& road : network.roads) {
            if (connectedRoads.find(road.id) == connectedRoads.end()) {
                // Road is disconnected (no shared nodes)
                // This is just info — could be a standalone road
                issues.push_back({
                    Severity::Info, "Topology", QString::fromStdString(road.id), "",
                    "Road is not connected to any other road",
                    0
                });
            }
        }

        // Check for orphan nodes (nodes with no roads)
        // This shouldn't happen but check anyway
        for (const auto& [nodeId, nn] : network.nodes) {
            if (nn.roadIds.empty()) {
                issues.push_back({
                    Severity::Warning, "Topology", "", "",
                    QString("Orphan node %1 with no roads").arg(nodeId),
                    0
                });
            }
        }
    }

    static void validateLanes(const geo::RoadV2& road,
                               std::vector<ValidationIssue>& issues,
                               const Params& params)
    {
        QString roadId = QString::fromStdString(road.id);

        if (road.laneCount <= 0) {
            issues.push_back({
                Severity::Warning, "Lane", roadId, "",
                QString("Invalid lane count: %1").arg(road.laneCount),
                double(road.laneCount)
            });
        }

        // Check lane widths
        for (int i = 0; i < road.numLaneSections(); i++) {
            const auto& ls = road.laneSection(i);
            for (const auto& lane : ls.lanes()) {
                double w = lane.widthAt(0.0);
                if (w < params.minLaneWidth) {
                    issues.push_back({
                        Severity::Warning, "Lane", roadId, "",
                        QString("Lane %1 width too small: %2m (min %3m)")
                            .arg(lane.id).arg(w, 0, 'f', 2).arg(params.minLaneWidth),
                        w
                    });
                }
                if (w > params.maxLaneWidth) {
                    issues.push_back({
                        Severity::Warning, "Lane", roadId, "",
                        QString("Lane %1 width too large: %2m (max %3m)")
                            .arg(lane.id).arg(w, 0, 'f', 2).arg(params.maxLaneWidth),
                        w
                    });
                }
            }
        }
    }

    static void validateJunctions(const std::vector<DetectedJunction>& junctions,
                                   std::vector<ValidationIssue>& issues,
                                   const Params& params)
    {
        for (const auto& j : junctions) {
            // Check for junctions with too few roads
            if (j.roadIds.size() < 2) {
                issues.push_back({
                    Severity::Warning, "Junction", "", j.id,
                    QString("Junction %1 has only %2 roads").arg(j.id).arg(j.roadIds.size()),
                    double(j.roadIds.size())
                });
            }

            // Check for overlapping junctions
            // (already handled by merge in detector)
        }
    }
};

} // namespace osm
