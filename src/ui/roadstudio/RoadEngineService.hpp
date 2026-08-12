#pragma once

// ============================================================
// RoadEngineService — Direct C++ road engine integration
// ============================================================
//
// Replaces the IPC-based roadEngineClient.ts and road_bridge.cpp.
// Calls the C++ road engine directly — no N-API, no IPC, no Node.js.
//
// Provides: centerline sampling, mesh generation, edge sampling,
// lane boundaries, and OpenDRIVE export.
//

#include "RoadTypes.hpp"
#include "GeoConvert.hpp"
#include "road_engine.hpp"

#include <QObject>
#include <vector>

class RoadEngineService : public QObject {
    Q_OBJECT

public:
    explicit RoadEngineService(QObject* parent = nullptr) : QObject(parent) {}

    // --- Version ---
    QString version() const {
        return QString::fromLatin1(road_engine::versionString());
    }

    // --- Convert our Road type to the engine's Road type ---
    static geo::Road toEngineRoad(const roads::Road& road, double refLat, double refLon);

    // --- Sample centerline ---
    // Returns sampled points in local meters (relative to refLat/refLon)
    std::vector<roads::Point2D> sampleCenterline(
        const roads::Road& road, double refLat, double refLon, int numSamples = 64);

    // --- Sample left/right edges ---
    std::vector<roads::Point2D> sampleLeftEdge(
        const roads::Road& road, double refLat, double refLon, int numSamples = 64);
    std::vector<roads::Point2D> sampleRightEdge(
        const roads::Road& road, double refLat, double refLon, int numSamples = 64);

    // --- Generate lane boundaries ---
    std::vector<std::vector<roads::Point2D>> generateLaneBoundaries(
        const roads::Road& road, double refLat, double refLon, int numSamples = 64);

    // --- Generate mesh ---
    roads::MeshData generateMesh(
        const roads::Road& road, double refLat, double refLon, int numSamples = 64);

    // --- Export to OpenDRIVE ---
    QString exportOpenDrive(const QList<roads::Road>& roads, double refLat, double refLon);

    // --- Generate intersection between two roads ---
    // Returns the intersection polygon, approaches, and fillet corners
    struct IntersectionResult {
        roads::Point2D center;
        QList<roads::Point2D> polygon;
        QList<roads::Point2D> filletArcPoints;
        QList<roads::Point2D> trimPoints;
        QList<roads::Point2D> boundaryIntersections;
        double cornerRadius = 0;
        double intersectionAngle = 0;
        bool valid = false;
    };
    IntersectionResult generateIntersection(
        const roads::Road& road1, const roads::Road& road2,
        double refLat, double refLon);

private:
    // Convert engine Point2D (local meters) to our Point2D
    static roads::Point2D fromEnginePoint(const geo::Point2D& p) {
        return {p.x, p.y};
    }
};
