// ============================================================
// test_world_workflow.cpp — End-to-end World Authoring workflow tests
// ============================================================
//
// Tests the complete World Authoring System workflow:
//   1. Create World
//   2. Import terrain
//   3. Generate masks
//   4. Create roads
//   5. Generate buildings
//   6. Generate vegetation (PCG)
//   7. Add water
//   8. Add lighting
//   9. Save
//  10. Close (destroy World)
//  11. Reopen (load World)
//  12. Verify everything restored
//  13. Validate world
//

#include "../../core/world/WorldBuilder.hpp"
#include "../../core/world/SplineEditor.hpp"
#include "../../core/world/TerrainWorldBridge.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <iostream>
#include <cmath>

static int g_passed = 0, g_failed = 0;
static QStringList g_failures;

#define VERIFY(cond, name, msg) do { \
    if (cond) { g_passed++; std::cout << "[PASS] " << name << std::endl; } \
    else { g_failed++; g_failures.append(QString("%1: %2").arg(name).arg(msg)); \
           std::cout << "[FAIL] " << name << " — " << QString(msg).toStdString() << std::endl; } \
} while(0)

// ============================================================
// Test cases
// ============================================================

void testCreateWorld() {
    world::WorldBuilder builder;
    builder.createWorld("Houston 4km", 4000.0f);

    VERIFY(builder.world.settings.name == "Houston 4km" &&
           builder.world.settings.terrainSize == 4000.0f,
           "TC-WF-01: Create World", "World not created correctly");
}

void testImportTerrain() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.importTerrain("/path/to/heightmap.png", "/path/to/albedo.png", 100.0f);

    auto terrainActors = builder.world.actorsByType(world::ActorType::Terrain);
    VERIFY(terrainActors.size() == 1 &&
           builder.world.settings.heightmapPath == "/path/to/heightmap.png",
           "TC-WF-02: Import Terrain", "Terrain not imported into World");
}

void testGenerateMasks() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.generateMasks();

    VERIFY(builder.world.maskCount() >= 2,
           "TC-WF-03: Generate Masks", "Masks not generated");
}

void testCreateRoad() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);

    QList<QPair<float, float>> points = {
        {-2000, -1000}, {0, 0}, {2000, 1000}
    };
    auto* road = builder.createRoad("Highway_A", points, 12.0f, 4);

    VERIFY(road != nullptr && road->points.size() == 3 &&
           road->width == 12.0f && road->laneCount == 4,
           "TC-WF-04: Create Road", "Road not created correctly");
}

void testGenerateBuildings() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.generateBuildings(50, 4000.0f, 42);

    VERIFY(builder.world.actorCountByType(world::ActorType::Building) == 50,
           "TC-WF-05: Generate Buildings", "Buildings not generated");
}

void testGenerateBuildingsAlongRoads() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);

    QList<QPair<float, float>> points = {
        {-1000, 0}, {0, 0}, {1000, 0}
    };
    builder.createRoad("Road_A", points, 8.0f, 2);
    builder.generateBuildingsAlongRoads(50.0f, 20.0f);

    VERIFY(builder.world.actorCountByType(world::ActorType::Building) > 0,
           "TC-WF-06: Buildings Along Roads", "No buildings generated along roads");
}

void testCreatePCGGraph() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    auto* graph = builder.createVegetationPCG("Forest_PCG", "/meshes/tree.obj", 0.01f);

    VERIFY(graph != nullptr && graph->nodes.size() >= 5,
           "TC-WF-07: Create PCG Graph", "PCG graph not created with enough nodes");
}

void testGenerateVegetation() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.createVegetationPCG("Forest_PCG", "/meshes/tree.obj", 0.1f, 1000.0f);
    builder.generateVegetation("Forest_PCG", 42);

    VERIFY(builder.world.actorCountByType(world::ActorType::Tree) > 0,
           "TC-WF-08: Generate Vegetation", "No vegetation generated");
}

void testAddWater() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.addLake("Lake_A", 500, 500, 200, 200, 50);

    VERIFY(builder.world.waterCount() == 1,
           "TC-WF-09: Add Water", "Water body not added");
}

void testAddLighting() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.addSunLight(45.0f, 60.0f, 3.0f);
    builder.addSkyLight(1.0f);

    int lightCount = builder.world.actorCountByType(world::ActorType::SunLight) +
                     builder.world.actorCountByType(world::ActorType::SkyLight);
    VERIFY(lightCount == 2,
           "TC-WF-10: Add Lighting", "Lights not added");
}

void testSaveWorld() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.generateBuildings(10, 4000.0f, 42);

    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/test.world";
    VERIFY(builder.save(path) && QFile::exists(path),
           "TC-WF-11: Save World", "World save failed");
}

void testLoadWorld() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.generateBuildings(10, 4000.0f, 42);
    builder.addLake("Lake_A", 100, 100, 50, 50, 30);

    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/test.world";
    builder.save(path);

    world::WorldBuilder loader;
    bool loaded = loader.load(path);

    VERIFY(loaded && loader.world.actorCount() == builder.world.actorCount() &&
           loader.world.waterCount() == 1,
           "TC-WF-12: Load World", "World load failed");
}

void testSaveLoadCloseReopen() {
    // Full save/close/reopen cycle
    world::WorldBuilder builder;
    builder.createWorld("Houston 4km", 4000.0f);
    builder.importTerrain("/data/heightmap.png", "/data/albedo.png", 100.0f);
    builder.generateMasks();

    QList<QPair<float, float>> roadPoints = {
        {-2000, -1000}, {0, 0}, {2000, 1000}
    };
    builder.createRoad("Highway_A", roadPoints, 12.0f, 4);
    builder.generateBuildingsAlongRoads(50.0f, 20.0f);
    builder.createVegetationPCG("Forest_PCG", "/meshes/tree.obj", 0.01f);
    builder.generateVegetation("Forest_PCG", 42);
    builder.addLake("Lake_Houston", 500, 500, 300, 300, 50);
    builder.addSunLight(45.0f, 60.0f, 3.0f);

    int originalActorCount = builder.world.actorCount();
    int originalBuildingCount = builder.world.actorCountByType(world::ActorType::Building);
    int originalTreeCount = builder.world.actorCountByType(world::ActorType::Tree);
    int originalSplineCount = builder.world.splineCount();
    int originalPcgCount = builder.world.pcgGraphCount();
    int originalWaterCount = builder.world.waterCount();
    int originalMaskCount = builder.world.maskCount();

    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/houston.world";
    VERIFY(builder.save(path), "TC-WF-13: Save Full World", "Save failed");

    // Simulate close — destroy the builder
    {
        world::WorldBuilder closedBuilder;
        Q_UNUSED(closedBuilder);
    }

    // Reopen
    world::WorldBuilder reopened;
    bool loaded = reopened.load(path);

    bool allCorrect = loaded &&
        reopened.world.actorCount() == originalActorCount &&
        reopened.world.actorCountByType(world::ActorType::Building) == originalBuildingCount &&
        reopened.world.actorCountByType(world::ActorType::Tree) == originalTreeCount &&
        reopened.world.splineCount() == originalSplineCount &&
        reopened.world.pcgGraphCount() == originalPcgCount &&
        reopened.world.waterCount() == originalWaterCount &&
        reopened.world.maskCount() == originalMaskCount &&
        reopened.world.settings.name == "Houston 4km";

    VERIFY(allCorrect,
           "TC-WF-13: Save/Close/Reopen",
           QString("World restoration failed. Loaded=%1 Actors=%2/%3 Buildings=%4/%5 Trees=%6/%7")
               .arg(loaded)
               .arg(reopened.world.actorCount()).arg(originalActorCount)
               .arg(reopened.world.actorCountByType(world::ActorType::Building)).arg(originalBuildingCount)
               .arg(reopened.world.actorCountByType(world::ActorType::Tree)).arg(originalTreeCount));
}

void testWorldValidation() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);
    builder.generateBuildings(10, 4000.0f, 42);

    auto errors = builder.validate();
    bool noCriticalErrors = true;
    for (const auto& e : errors) {
        if (e.severity == "critical" || e.severity == "high")
            noCriticalErrors = false;
    }

    VERIFY(noCriticalErrors,
           "TC-WF-14: World Validation", "World has critical/high validation errors");
}

void testSplineEvaluation() {
    world::Spline spline;
    spline.type = world::SplineType::Road;
    spline.width = 8.0f;

    world::SplineEditor::addPoint(spline, 0, 0, 0);
    world::SplineEditor::addPoint(spline, 100, 0, 0);
    world::SplineEditor::addPoint(spline, 200, 0, 50);
    world::SplineEditor::addPoint(spline, 300, 0, 100);

    auto samples = world::SplineEvaluator::sample(spline, 10.0f);
    float len = world::SplineEvaluator::length(spline);

    VERIFY(samples.size() > 4 && len > 300,
           "TC-WF-15: Spline Evaluation", "Spline evaluation failed");
}

void testRoadMeshGeneration() {
    world::Spline spline;
    spline.type = world::SplineType::Road;
    spline.width = 8.0f;

    world::SplineEditor::addPoint(spline, 0, 0, 0);
    world::SplineEditor::addPoint(spline, 100, 0, 0);
    world::SplineEditor::addPoint(spline, 200, 0, 50);

    auto meshData = world::SplineEditor::generateRoadMeshData(spline, 0.5f);

    VERIFY(!meshData.vertices.empty() && !meshData.indices.empty() &&
           meshData.vertices.size() == meshData.normals.size() &&
           meshData.vertices.size() / 3 * 2 == meshData.uvs.size(),
           "TC-WF-16: Road Mesh Generation", "Road mesh data invalid");
}

void testPCGDeterminism() {
    world::WorldBuilder builder1;
    builder1.createWorld("Test1", 4000.0f);
    builder1.createVegetationPCG("Forest", "/tree.obj", 0.1f, 1000.0f);
    builder1.generateVegetation("Forest", 12345);
    int count1 = builder1.world.actorCountByType(world::ActorType::Tree);

    world::WorldBuilder builder2;
    builder2.createWorld("Test2", 4000.0f);
    builder2.createVegetationPCG("Forest", "/tree.obj", 0.1f, 1000.0f);
    builder2.generateVegetation("Forest", 12345);
    int count2 = builder2.world.actorCountByType(world::ActorType::Tree);

    VERIFY(count1 == count2 && count1 > 0,
           "TC-WF-17: PCG Determinism",
           QString("Same seed produced different counts: %1 vs %2").arg(count1).arg(count2));
}

void testLayerManagement() {
    world::WorldBuilder builder;
    builder.createWorld("Test World", 4000.0f);

    // Check standard layers exist
    bool hasTerrain = false, hasRoads = false, hasBuildings = false, hasVegetation = false;
    for (const auto& l : builder.world.layers) {
        if (l.name == "Terrain") hasTerrain = true;
        if (l.name == "Roads") hasRoads = true;
        if (l.name == "Buildings") hasBuildings = true;
        if (l.name == "Vegetation") hasVegetation = true;
    }

    VERIFY(hasTerrain && hasRoads && hasBuildings && hasVegetation,
           "TC-WF-18: Layer Management", "Standard layers not created");
}

void testCompleteWorkflow() {
    // Complete end-to-end workflow
    world::WorldBuilder builder;

    // 1. Create World
    builder.createWorld("Houston Complete", 4000.0f);

    // 2. Import terrain
    builder.importTerrain("/data/houston_heightmap.png", "/data/houston_albedo.png", 100.0f);

    // 3. Generate masks
    builder.generateMasks();

    // 4. Create roads
    QList<QPair<float, float>> road1 = {{-2000, -1000}, {0, 0}, {2000, 1000}};
    builder.createRoad("I-45", road1, 12.0f, 4);

    QList<QPair<float, float>> road2 = {{-1000, -2000}, {0, 0}, {1000, 2000}};
    builder.createRoad("I-10", road2, 12.0f, 4);

    // 5. Generate buildings
    builder.generateBuildingsAlongRoads(50.0f, 20.0f);
    builder.generateBuildings(100, 4000.0f, 42);

    // 6. Generate vegetation
    builder.createVegetationPCG("Forest_PCG", "/meshes/tree.obj", 0.01f);
    builder.generateVegetation("Forest_PCG", 42);

    builder.createVegetationPCG("Grass_PCG", "/meshes/grass.obj", 0.05f);
    builder.generateVegetation("Grass_PCG", 100);

    // 7. Add water
    builder.addLake("Lake_Houston", 500, 500, 300, 300, 50);

    // 8. Add lighting
    builder.addSunLight(45.0f, 60.0f, 3.0f);
    builder.addSkyLight(1.0f);

    // Verify world is complete
    bool complete = builder.world.actorCount() > 100 &&
                    builder.world.actorCountByType(world::ActorType::Building) > 0 &&
                    builder.world.actorCountByType(world::ActorType::Tree) > 0 &&
                    builder.world.splineCount() == 2 &&
                    builder.world.pcgGraphCount() == 2 &&
                    builder.world.waterCount() == 1 &&
                    builder.world.maskCount() >= 2;

    VERIFY(complete,
           "TC-WF-19: Complete Workflow",
           QString("World incomplete. Actors=%1 Buildings=%2 Trees=%3 Splines=%4 PCG=%5 Water=%6 Masks=%7")
               .arg(builder.world.actorCount())
               .arg(builder.world.actorCountByType(world::ActorType::Building))
               .arg(builder.world.actorCountByType(world::ActorType::Tree))
               .arg(builder.world.splineCount())
               .arg(builder.world.pcgGraphCount())
               .arg(builder.world.waterCount())
               .arg(builder.world.maskCount()));

    // Save
    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/houston_complete.world";
    VERIFY(builder.save(path), "TC-WF-20: Save Complete World", "Save failed");

    // Load and verify
    world::WorldBuilder loader;
    bool loaded = loader.load(path);

    VERIFY(loaded && loader.world.actorCount() == builder.world.actorCount(),
           "TC-WF-20: Load Complete World",
           QString("Load failed or actor count mismatch: %1 vs %2")
               .arg(loader.world.actorCount()).arg(builder.world.actorCount()));
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "========================================" << std::endl;
    std::cout << "  WORLD AUTHORING WORKFLOW TEST SUITE" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    testCreateWorld();
    testImportTerrain();
    testGenerateMasks();
    testCreateRoad();
    testGenerateBuildings();
    testGenerateBuildingsAlongRoads();
    testCreatePCGGraph();
    testGenerateVegetation();
    testAddWater();
    testAddLighting();
    testSaveWorld();
    testLoadWorld();
    testSaveLoadCloseReopen();
    testWorldValidation();
    testSplineEvaluation();
    testRoadMeshGeneration();
    testPCGDeterminism();
    testLayerManagement();
    testCompleteWorkflow();

    std::cout << std::endl << "========================================" << std::endl;
    std::cout << "  Total: " << (g_passed + g_failed)
              << "  Passed: " << g_passed
              << "  Failed: " << g_failed << std::endl;
    std::cout << "========================================" << std::endl;

    if (g_failed > 0) {
        std::cout << std::endl << "FAILURES:" << std::endl;
        for (const auto& f : g_failures)
            std::cout << "  " << f.toStdString() << std::endl;
    }

    return g_failed > 0 ? 1 : 0;
}
