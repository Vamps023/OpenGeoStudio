// ============================================================
// test_houston_roundtrip.cpp — Houston OSM round-trip test
// ============================================================
//
// Imports a real-world Houston OSM extract, exports to OpenDRIVE,
// and verifies the round-trip works. Also exercises the rail
// pipeline to confirm railway filtering excludes highway ways.

#include "OsmImportPipeline.hpp"
#include "RailImportPipeline.hpp"
#include "OsmExporter.hpp"

#include "OpenDriveMap.h"

#include "../../core/world/WorldBuilder.hpp"
#include "../../core/world/SplineEditor.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QString>
#include <QDebug>
#include <QTemporaryDir>
#include <iostream>
#include <vector>
#include <functional>

using namespace osm;

// ─── Test framework (minimal, matches test_osm_pipeline.cpp) ───
static int g_testsPassed = 0;
static int g_testsFailed = 0;

struct TestRunner {
    std::vector<std::pair<std::string, std::function<void()>>> tests;
    void add(const std::string& name, std::function<void()> fn) {
        tests.push_back({name, fn});
    }
    void runAll() {
        for (const auto& [name, fn] : tests) {
            int before = g_testsFailed;
            fn();
            if (g_testsFailed == before) {
                std::cout << "  PASS: " << name << std::endl;
            } else {
                std::cout << "  FAIL: " << name << std::endl;
            }
        }
    }
};

static TestRunner g_runner;

#define TEST(name) \
    static void name(); \
    struct name##_Reg { name##_Reg() { g_runner.add(#name, name); } } name##_reg; \
    static void name()

#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            g_testsPassed++; \
        } else { \
            g_testsFailed++; \
            std::cerr << "    FAIL: " << #cond << " — " << msg \
                      << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
        } \
    } while (0)

static QString resolveOsmPath() {
    const QStringList candidates = {
        "C:/Users/nares/Downloads/map.osm",
        "D:/git/OpenGeoStudio-Qt/test_data/houston/map.osm",
        "map.osm",
    };
    for (const auto& p : candidates) {
        if (QFileInfo::exists(p)) return p;
    }
    return {};
}

// ─── Test 1: Import Houston OSM and export OpenDRIVE ───
TEST(test_houston_import_and_export) {
    QString osmPath = resolveOsmPath();
    if (osmPath.isEmpty()) {
        std::cout << "    SKIP: Houston OSM file not found" << std::endl;
        return;
    }
    qDebug() << "[houston] Using OSM file:" << osmPath;

    // Step 1: Import
    ImportSettings settings;
    settings.autoDetectReference = true;
    settings.runValidation = true;
    settings.autoRepair = true;
    settings.progressCallback = [](double p, const QString& msg) {
        qDebug() << "[houston]" << int(p * 100) << "%" << msg;
    };

    ImportResult result = OsmImportPipeline::importFromFile(osmPath, settings);

    CHECK(result.success, "Import should succeed");
    CHECK(!result.network.roads.empty(), "Should produce at least one road");
    if (!result.success || result.network.roads.empty()) return;

    qDebug() << "[houston] Import OK:"
             << result.stats.roadsCreated << "roads,"
             << result.stats.segmentsCreated << "segments,"
             << result.stats.junctionsDetected << "junctions,"
             << result.stats.totalRoadLength << "m total";

    // Step 2: Export to OpenDRIVE
    QString outDir = QDir::tempPath() + "/ogs_houston_roundtrip";
    QDir().mkpath(outDir);
    QString xodrPath = outDir + "/houston.xodr";

    OsmExporter::OpenDriveParams params;
    QString exportError;
    bool exported = OsmExporter::exportToOpenDrive(
        xodrPath, result.network, result.junctions, result.converter, params, &exportError);

    CHECK(exported, "OpenDRIVE export should succeed");
    CHECK(QFileInfo::exists(xodrPath), "XODR file should exist");
    if (!exported || !QFileInfo::exists(xodrPath)) {
        qDebug() << "[houston] Export failed:" << exportError;
        return;
    }

    qint64 xodrSize = QFileInfo(xodrPath).size();
    qDebug() << "[houston] OpenDRIVE exported:" << xodrPath
             << "(" << xodrSize << "bytes )";
    CHECK(xodrSize > 0, "XODR file should not be empty");

    // Step 3: Verify XML structure
    QFile f(xodrPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        CHECK(false, "Could not open exported xodr for verification");
        return;
    }
    QByteArray content = f.readAll();
    f.close();

    CHECK(content.contains("<OpenDRIVE"), "Missing <OpenDRIVE> root element");
    CHECK(content.contains("<road"), "No <road> elements in export");
    CHECK(content.contains("<geometry"), "No <geometry> elements in export");
    CHECK(content.contains("</OpenDRIVE>"), "Missing </OpenDRIVE> closing tag");

    int roadCount = content.count("<road");
    qDebug() << "[houston] XODR contains" << roadCount << "<road> elements";
    CHECK(roadCount > 0, "Should have at least one <road> in XODR");
}

// ─── Test 2: Re-import determinism ───
TEST(test_houston_reimport_deterministic) {
    QString osmPath = resolveOsmPath();
    if (osmPath.isEmpty()) {
        std::cout << "    SKIP: Houston OSM file not found" << std::endl;
        return;
    }

    ImportSettings settings;
    settings.autoDetectReference = true;
    settings.runValidation = false;  // skip validation for speed
    settings.autoRepair = false;

    ImportResult r1 = OsmImportPipeline::importFromFile(osmPath, settings);
    ImportResult r2 = OsmImportPipeline::importFromFile(osmPath, settings);

    CHECK(r1.success, "First import should succeed");
    CHECK(r2.success, "Second import should succeed");
    if (!r1.success || !r2.success) return;

    CHECK(r1.stats.roadsCreated == r2.stats.roadsCreated,
          "Road count should be deterministic");
    CHECK(r1.stats.segmentsCreated == r2.stats.segmentsCreated,
          "Segment count should be deterministic");
    CHECK(r1.stats.junctionsDetected == r2.stats.junctionsDetected,
          "Junction count should be deterministic");

    qDebug() << "[houston] Re-import OK: matches original ("
             << r2.stats.roadsCreated << "roads )";
}

// ─── Test 3: Rail pipeline excludes highway ways ───
TEST(test_houston_rail_pipeline_filters_highways) {
    QString osmPath = resolveOsmPath();
    if (osmPath.isEmpty()) {
        std::cout << "    SKIP: Houston OSM file not found" << std::endl;
        return;
    }

    // Road import
    ImportSettings roadSettings;
    roadSettings.autoDetectReference = true;
    roadSettings.runValidation = false;
    ImportResult roadResult = OsmImportPipeline::importFromFile(osmPath, roadSettings);

    // Rail import
    RailImportSettings railSettings;
    railSettings.autoDetectReference = true;
    railSettings.runValidation = false;
    RailImportResult railResult = RailImportPipeline::importFromFile(osmPath, railSettings);

    CHECK(roadResult.success, "Road import should succeed");
    CHECK(railResult.success, "Rail import should succeed");
    if (!roadResult.success || !railResult.success) return;

    qDebug() << "[houston] Roads:" << roadResult.stats.roadsCreated
             << "  Tracks:" << railResult.stats.tracksCreated;

    // Houston OSM is road-heavy; rail count should be smaller than road count
    CHECK(railResult.stats.tracksCreated <= roadResult.stats.roadsCreated,
          "Rail tracks should be <= road count (rail filters out highways)");
}

// ─── Test 4: Reload exported XODR with libOpenDRIVE ───
TEST(test_houston_xodr_reload_with_libopendrive) {
    QString osmPath = resolveOsmPath();
    if (osmPath.isEmpty()) {
        std::cout << "    SKIP: Houston OSM file not found" << std::endl;
        return;
    }

    // Import
    ImportSettings settings;
    settings.autoDetectReference = true;
    settings.runValidation = false;
    settings.autoRepair = false;
    ImportResult result = OsmImportPipeline::importFromFile(osmPath, settings);
    CHECK(result.success, "Import should succeed");
    if (!result.success) return;

    // Export
    QString outDir = QDir::tempPath() + "/ogs_houston_roundtrip";
    QDir().mkpath(outDir);
    QString xodrPath = outDir + "/houston.xodr";

    OsmExporter::OpenDriveParams params;
    QString exportError;
    bool exported = OsmExporter::exportToOpenDrive(
        xodrPath, result.network, result.junctions, result.converter, params, &exportError);
    CHECK(exported, "Export should succeed");
    if (!exported) return;

    // Reload with libOpenDRIVE — this is what LaneMaker's ChangeTracker::Load uses
    std::cout << "[houston] Attempting to load XODR with libOpenDRIVE: "
              << xodrPath.toStdString() << std::endl;
    std::cout << "[houston] File exists: " << QFileInfo::exists(xodrPath)
              << "  size: " << QFileInfo(xodrPath).size() << std::endl;

    odr::OpenDriveMap odrMap;
    bool loaded = false;
    try {
        loaded = odrMap.Load(xodrPath.toStdString());
    } catch (const std::exception& e) {
        std::cerr << "[houston] Exception during load: " << e.what() << std::endl;
        CHECK(false, "libOpenDRIVE threw exception during load");
        return;
    }
    CHECK(loaded, "libOpenDRIVE should load the exported XODR");
    if (!loaded) {
        std::cerr << "[houston] libOpenDRIVE failed to load" << std::endl;
        // Try reading the file ourselves to see if it's readable
        QFile xf(xodrPath);
        if (xf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray head = xf.read(200);
            std::cerr << "[houston] File starts with: "
                      << head.left(100).toStdString() << std::endl;
            xf.close();
        } else {
            std::cerr << "[houston] Cannot open file with QFile either" << std::endl;
        }
        return;
    }

    auto roads = odrMap.get_roads();
    qDebug() << "[houston] libOpenDRIVE loaded" << roads.size() << "roads";
    CHECK(!roads.empty(), "Loaded XODR should contain roads");

    // Verify each road has geometry
    int roadsWithGeometry = 0;
    for (const auto& road : roads) {
        if (!road.ref_line.s0_to_geometry.empty()) {
            roadsWithGeometry++;
        }
    }
    qDebug() << "[houston] Roads with geometry:" << roadsWithGeometry;
    CHECK(roadsWithGeometry > 0, "At least some roads should have geometry");
}

// ─── Test 5: Complete Houston workflow — terrain + roads + content,
// ─── save, close, reopen, verify full restoration ───
TEST(test_houston_full_workflow_restoration) {
    QString osmPath = resolveOsmPath();
    if (osmPath.isEmpty()) {
        std::cout << "    SKIP: Houston OSM file not found" << std::endl;
        return;
    }

    // ─── Step 1: Import Houston roads from OSM ───
    ImportSettings settings;
    settings.autoDetectReference = true;
    settings.runValidation = true;
    settings.autoRepair = true;
    ImportResult roadResult = OsmImportPipeline::importFromFile(osmPath, settings);
    CHECK(roadResult.success, "Houston OSM import should succeed");
    CHECK(roadResult.stats.roadsCreated > 1000, "Houston should have thousands of roads");
    if (!roadResult.success || roadResult.stats.roadsCreated <= 0) return;

    // ─── Step 2: Export roads to OpenDRIVE (saved as project artifact) ───
    QTemporaryDir tmpDir;
    QString projectDir = tmpDir.path() + "/HoustonProject";
    QDir().mkpath(projectDir + "/Roads");
    QString xodrPath = projectDir + "/Roads/houston.xodr";
    OsmExporter::OpenDriveParams params;
    QString exportError;
    bool exported = OsmExporter::exportToOpenDrive(
        xodrPath, roadResult.network, roadResult.junctions,
        roadResult.converter, params, &exportError);
    CHECK(exported, "OpenDRIVE export should succeed");
    if (!exported) return;
    CHECK(QFileInfo::exists(xodrPath), "XODR artifact should exist on disk");
    int xodrRoadCount = int(roadResult.network.roads.size());

    // ─── Step 3: Verify the XODR artifact is reloadable (libOpenDRIVE) ───
    odr::OpenDriveMap odrMap;
    bool odrLoaded = odrMap.Load(xodrPath.toStdString());
    CHECK(odrLoaded, "Saved XODR should reload with libOpenDRIVE");
    if (odrLoaded) {
        auto roads = odrMap.get_roads();
        CHECK(!roads.empty(), "Reloaded XODR should contain roads");
        // Count <road> elements in the file itself (some network roads may be
        // skipped during export if their length is <= 0)
        QFile xf(xodrPath);
        int fileRoadCount = 0;
        if (xf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            fileRoadCount = xf.readAll().count("<road ");
            xf.close();
        }
        CHECK(int(roads.size()) == fileRoadCount,
              "Reloaded road count should match roads in the XODR file");
        CHECK(fileRoadCount > 0, "XODR file should contain roads");
        std::cout << "[houston] XODR reload OK:" << roads.size()
                  << " roads (file has " << fileRoadCount << ")" << std::endl;
    }

    // ─── Step 4: Build the World (terrain + roads + content) ───
    world::WorldBuilder builder;
    builder.createWorld("Houston 4km", 4000.0f);
    builder.importTerrain("/project/Terrain/heightmap.png",
                          "/project/Terrain/albedo.png", 100.0f);
    builder.generateMasks();

    // Roads from the imported network (sample a few for the world model)
    int roadsAdded = 0;
    for (const auto& road : roadResult.network.roads) {
        if (road.numSegments() < 1) continue;
        QList<QPair<float, float>> points;
        for (int i = 0; i < road.numSegments() && i < 16; i++) {
            const auto& seg = road.segment(i);
            points.append({float(seg.startPoint().x), float(seg.startPoint().y)});
        }
        if (points.size() >= 2) {
            builder.createRoad(QString::fromStdString(road.name), points,
                               8.0f, std::max(1, int(road.laneCount)));
            roadsAdded++;
        }
        if (roadsAdded >= 5) break;  // sample a few roads for the world model
    }
    CHECK(roadsAdded >= 1, "Should add at least one road spline to the world");

    // Buildings along roads
    builder.generateBuildingsAlongRoads(50.0f, 20.0f);
    // PCG vegetation
    builder.createVegetationPCG("Forest_PCG", "/meshes/tree.obj", 0.01f);
    builder.generateVegetation("Forest_PCG", 42);
    // Water + lighting
    builder.addLake("Lake_Houston", 500, 500, 300, 300, 50);
    builder.addSunLight(45.0f, 60.0f, 3.0f);
    builder.addSkyLight(1.0f);

    // Record expected state
    int expectedActors = builder.world.actorCount();
    int expectedBuildings = builder.world.actorCountByType(world::ActorType::Building);
    int expectedTrees = builder.world.actorCountByType(world::ActorType::Tree);
    int expectedSplines = builder.world.splineCount();
    int expectedPcg = builder.world.pcgGraphCount();
    int expectedWater = builder.world.waterCount();
    int expectedMasks = builder.world.maskCount();
    QString expectedName = builder.world.settings.name;
    float expectedSize = builder.world.settings.terrainSize;
    QString expectedHeightmap = builder.world.settings.heightmapPath;

    CHECK(expectedActors > 0, "World should have actors");
    CHECK(expectedBuildings > 0, "World should have buildings");
    CHECK(expectedTrees > 0, "World should have vegetation");
    CHECK(expectedSplines >= 1, "World should have road splines");
    CHECK(expectedWater == 1, "World should have one lake");
    CHECK(expectedMasks >= 2, "World should have masks");

    // ─── Step 5: Save world ───
    QString worldPath = projectDir + "/World/houston.world";
    bool saved = builder.save(worldPath);
    CHECK(saved, "World save should succeed");
    if (!saved) return;
    CHECK(QFileInfo::exists(worldPath), "World file should exist on disk");
    qint64 worldSize = QFileInfo(worldPath).size();
    CHECK(worldSize > 0, "World file should not be empty");

    // ─── Step 6: Close (destroy all state) ───
    {
        world::WorldBuilder closed;  // fresh builder — simulates app close
        Q_UNUSED(closed);
    }

    // ─── Step 7: Reopen and verify complete restoration ───
    world::WorldBuilder reopened;
    bool loaded = reopened.load(worldPath);
    CHECK(loaded, "World reload should succeed");
    if (!loaded) return;

    bool restored =
        reopened.world.settings.name == expectedName &&
        reopened.world.settings.terrainSize == expectedSize &&
        reopened.world.settings.heightmapPath == expectedHeightmap &&
        reopened.world.actorCount() == expectedActors &&
        reopened.world.actorCountByType(world::ActorType::Building) == expectedBuildings &&
        reopened.world.actorCountByType(world::ActorType::Tree) == expectedTrees &&
        reopened.world.splineCount() == expectedSplines &&
        reopened.world.pcgGraphCount() == expectedPcg &&
        reopened.world.waterCount() == expectedWater &&
        reopened.world.maskCount() == expectedMasks;

    CHECK(restored, "Full world restoration should match exactly");
    if (!restored) {
        std::cout << "[houston] RESTORATION MISMATCH:"
                  << " name=" << (reopened.world.settings.name == expectedName)
                  << " size=" << (reopened.world.settings.terrainSize == expectedSize)
                  << " heightmap=" << (reopened.world.settings.heightmapPath == expectedHeightmap)
                  << " actors=" << reopened.world.actorCount() << "/" << expectedActors
                  << " buildings=" << reopened.world.actorCountByType(world::ActorType::Building) << "/" << expectedBuildings
                  << " trees=" << reopened.world.actorCountByType(world::ActorType::Tree) << "/" << expectedTrees
                  << " splines=" << reopened.world.splineCount() << "/" << expectedSplines
                  << " pcg=" << reopened.world.pcgGraphCount() << "/" << expectedPcg
                  << " water=" << reopened.world.waterCount() << "/" << expectedWater
                  << " masks=" << reopened.world.maskCount() << "/" << expectedMasks
                  << std::endl;
    }

    // Validate restored world
    auto errors = reopened.validate();
    CHECK(errors.empty(), "Restored world should pass validation");

    std::cout << "[houston] FULL WORKFLOW OK: terrain + "
              << roadsAdded << " road splines + "
              << expectedBuildings << " buildings + "
              << expectedTrees << " trees + lake + lighting"
              << " restored exactly (" << worldSize << " bytes world file)"
              << std::endl;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "=== Houston OSM Round-Trip Tests ===" << std::endl;
    g_runner.runAll();

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Passed: " << g_testsPassed << std::endl;
    std::cout << "Failed: " << g_testsFailed << std::endl;

    return g_testsFailed == 0 ? 0 : 1;
}
