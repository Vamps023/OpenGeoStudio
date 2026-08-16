// ============================================================
// test_world_model.cpp — Automated tests for the World model
// ============================================================

#include "../../core/world/WorldTypes.hpp"
#include "../../core/world/World.hpp"
#include "../../core/world/Spline.hpp"
#include "../../core/world/PCGEngine.hpp"
#include "../../core/world/UndoRedo.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
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

void testActorCreation() {
    world::Actor a;
    a.name = "TestActor";
    a.type = world::ActorType::Building;
    VERIFY(!a.id.isEmpty() && a.name == "TestActor" && a.type == world::ActorType::Building,
           "TC-Actor: Actor Creation", "Basic actor properties incorrect");
}

void testActorSerialization() {
    world::Actor a;
    a.name = "TestBuilding";
    a.type = world::ActorType::Building;
    a.transform.posX = 100;
    a.transform.posY = 50;
    a.transform.posZ = 200;
    a.transform.rotY = 45;
    a.transform.scaleX = 10;
    a.metadata["height"] = "30";
    a.assetPath = "/path/to/mesh.obj";

    QJsonObject json = a.toJson();
    world::Actor b = world::Actor::fromJson(json);

    VERIFY(b.id == a.id && b.name == a.name && b.type == a.type &&
           b.transform.posX == 100 && b.transform.posY == 50 &&
           b.transform.posZ == 200 && b.transform.rotY == 45 &&
           b.transform.scaleX == 10 && b.metadata["height"] == "30" &&
           b.assetPath == "/path/to/mesh.obj",
           "TC-Actor: Serialization", "Round-trip serialization failed");
}

void testWorldCreation() {
    world::World w;
    VERIFY(w.actorCount() == 0 && w.layerCount() >= 8,
           "TC-World: World Creation", "World not initialized correctly");
}

void testWorldAddActor() {
    world::World w;
    auto* a = w.addActor(world::ActorType::Building, "Building_001");
    VERIFY(a != nullptr && w.actorCount() == 1 && a->name == "Building_001",
           "TC-World: Add Actor", "Actor not added correctly");
}

void testWorldRemoveActor() {
    world::World w;
    auto* a = w.addActor(world::ActorType::Tree, "Tree_001");
    QString id = a->id;
    w.removeActor(id);
    VERIFY(w.actorCount() == 0, "TC-World: Remove Actor", "Actor not removed");
}

void testWorldHierarchy() {
    world::World w;
    auto* parent = w.addActor(world::ActorType::Group, "Parent");
    QString parentId = parent->id;
    auto* child = w.addActor(world::ActorType::Building, "Child");
    QString childId = child->id;
    w.setParent(childId, parentId);

    auto children = w.children(parentId);
    VERIFY(children.size() == 1 && children[0]->id == childId,
           "TC-World: Hierarchy", "Parent-child relationship failed");

    // Test cycle prevention
    w.setParent(parentId, childId);
    auto* p = w.findActor(parentId);
    VERIFY(p && p->parentId.isEmpty(),
           "TC-World: Cycle Prevention", "Cycle was not prevented");
}

void testWorldSelection() {
    world::World w;
    auto* a1 = w.addActor(world::ActorType::Building, "B1");
    QString id1 = a1->id;
    auto* a2 = w.addActor(world::ActorType::Tree, "T1");
    QString id2 = a2->id;
    w.selectOnly(id1);
    VERIFY(w.selectionCount() == 1 && w.isSelected(id1) && !w.isSelected(id2),
           "TC-World: Selection", "Selection failed");
    w.clearSelection();
    VERIFY(w.selectionCount() == 0, "TC-World: Clear Selection", "Clear failed");
}

void testWorldLayers() {
    world::World w;
    auto* l = w.addLayer("CustomLayer");
    VERIFY(l != nullptr && w.layerCount() > 8,
           "TC-World: Add Layer", "Layer not added");

    w.setLayerVisible(l->id, false);
    VERIFY(!w.isLayerVisible(l->id),
           "TC-World: Layer Visibility", "Visibility toggle failed");

    w.setLayerLocked(l->id, true);
    VERIFY(w.isLayerLocked(l->id),
           "TC-World: Layer Lock", "Lock toggle failed");

    // Can't remove default layer
    VERIFY(!w.removeLayer("default"),
           "TC-World: Default Layer Protection", "Default layer was removed");
}

void testWorldSaveLoad() {
    world::World w;
    w.settings.name = "Test World";
    w.settings.terrainSize = 4000;
    auto* a = w.addActor(world::ActorType::Building, "Building_001");
    a->transform.posX = 100;
    a->transform.posZ = 200;
    w.addSpline(world::SplineType::Road, "Road_001");
    w.addPCGGraph("Forest_PCG");

    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/test.world";

    VERIFY(w.saveToFile(path), "TC-World: Save", "Save failed");

    world::World loaded = world::World::loadFromFile(path);
    VERIFY(loaded.settings.name == "Test World" &&
           loaded.actorCount() == 1 &&
           loaded.splineCount() == 1 &&
           loaded.pcgGraphCount() == 1,
           "TC-World: Load", "Load failed");
}

void testWorldValidation() {
    world::World w;
    auto* a = w.addActor(world::ActorType::Building, "B1");
    a->parentId = "nonexistent";  // broken parent
    a->layerId = "nonexistent";   // broken layer

    auto errors = w.validate();
    bool hasBrokenParent = false, hasBrokenLayer = false;
    for (const auto& e : errors) {
        if (e.message.contains("Broken parent")) hasBrokenParent = true;
        if (e.message.contains("Invalid layer")) hasBrokenLayer = true;
    }
    VERIFY(hasBrokenParent && hasBrokenLayer,
           "TC-World: Validation", "Validation didn't detect broken references");
}

void testSplineCreation() {
    world::Spline s;
    s.name = "Road_001";
    s.type = world::SplineType::Road;
    s.width = 8.0f;
    s.laneCount = 2;

    world::SplineControlPoint p1, p2, p3, p4;
    p1.x = 0; p1.z = 0;
    p2.x = 100; p2.z = 0;
    p3.x = 200; p3.z = 50;
    p4.x = 300; p4.z = 100;
    s.points = {p1, p2, p3, p4};

    QJsonObject json = s.toJson();
    world::Spline loaded = world::Spline::fromJson(json);

    VERIFY(loaded.name == "Road_001" && loaded.type == world::SplineType::Road &&
           loaded.width == 8.0f && loaded.laneCount == 2 &&
           loaded.points.size() == 4,
           "TC-Spline: Creation & Serialization", "Spline round-trip failed");
}

void testSplineEvaluation() {
    world::Spline s;
    world::SplineControlPoint p1, p2, p3, p4;
    p1.x = 0; p1.z = 0;
    p2.x = 100; p2.z = 0;
    p3.x = 200; p3.z = 50;
    p4.x = 300; p4.z = 100;
    s.points = {p1, p2, p3, p4};

    auto samples = world::SplineEvaluator::sample(s, 10.0f);
    VERIFY(samples.size() > 4,
           "TC-Spline: Evaluation", QString("Expected > 4 samples, got %1").arg((int)samples.size()));

    float len = world::SplineEvaluator::length(s);
    VERIFY(len > 300, "TC-Spline: Length", QString("Length %1 too short").arg(len));
}

void testSplineRoadMesh() {
    world::Spline s;
    s.width = 8.0f;
    world::SplineControlPoint p1, p2, p3;
    p1.x = 0; p1.z = 0;
    p2.x = 100; p2.z = 0;
    p3.x = 200; p2.z = 50;
    s.points = {p1, p2, p3};

    auto verts = world::SplineEvaluator::generateRoadMesh(s, 0.5f);
    auto indices = world::SplineEvaluator::generateRoadIndices(int(verts.size()));

    VERIFY(!verts.empty() && !indices.empty() && indices.size() >= 6,
           "TC-Spline: Road Mesh", "Road mesh generation failed");
}

void testPCGGraphCreation() {
    world::PCGGraph g;
    g.name = "Forest_PCG";
    g.seed = 42;

    world::PCGNode scatterNode;
    scatterNode.name = "Scatter";
    scatterNode.type = world::PCGNodeType::ScatterPoints;
    scatterNode.properties["areaSize"] = "1000";
    scatterNode.properties["density"] = "0.1";
    g.nodes.append(scatterNode);

    world::PCGNode outputNode;
    outputNode.name = "Output";
    outputNode.type = world::PCGNodeType::InstanceOutput;
    outputNode.inputNodeIds.append(scatterNode.id);
    g.nodes.append(outputNode);

    QJsonObject json = g.toJson();
    world::PCGGraph loaded = world::PCGGraph::fromJson(json);

    VERIFY(loaded.name == "Forest_PCG" && loaded.seed == 42 &&
           loaded.nodes.size() == 2,
           "TC-PCG: Graph Creation", "Graph round-trip failed");
}

void testPCGGraphCycles() {
    world::PCGGraph g;
    world::PCGNode n1, n2;
    n1.id = "n1"; n2.id = "n2";
    n1.inputNodeIds.append("n2");  // n1 depends on n2
    n2.inputNodeIds.append("n1");  // n2 depends on n1 — cycle!
    g.nodes = {n1, n2};

    VERIFY(g.hasCycles(), "TC-PCG: Cycle Detection", "Cycle not detected");

    world::PCGGraph g2;
    world::PCGNode n3, n4;
    n3.id = "n3"; n4.id = "n4";
    n4.inputNodeIds.append("n3");  // n4 depends on n3 — no cycle
    g2.nodes = {n3, n4};
    VERIFY(!g2.hasCycles(), "TC-PCG: No False Cycles", "False cycle detected");
}

void testPCGEvaluation() {
    world::PCGGraph g;
    g.seed = 42;

    world::PCGNode scatterNode;
    scatterNode.name = "Scatter";
    scatterNode.type = world::PCGNodeType::ScatterPoints;
    scatterNode.properties["areaSize"] = "100";
    scatterNode.properties["density"] = "1.0";
    scatterNode.properties["scaleMin"] = "0.5";
    scatterNode.properties["scaleMax"] = "2.0";
    g.nodes.append(scatterNode);

    world::PCGNode outputNode;
    outputNode.name = "Output";
    outputNode.type = world::PCGNodeType::InstanceOutput;
    outputNode.inputNodeIds.append(scatterNode.id);
    g.nodes.append(outputNode);

    world::PCGContext ctx(42);
    auto points = world::PCGEngine::evaluate(g, ctx);

    VERIFY(!points.isEmpty(),
           "TC-PCG: Evaluation", QString("No points generated, got %1").arg(points.size()));
}

void testPCGDeterminism() {
    world::PCGGraph g;
    g.seed = 12345;

    world::PCGNode scatterNode;
    scatterNode.type = world::PCGNodeType::ScatterPoints;
    scatterNode.properties["areaSize"] = "100";
    scatterNode.properties["density"] = "1.0";
    g.nodes.append(scatterNode);

    world::PCGContext ctx1(12345);
    world::PCGContext ctx2(12345);
    auto points1 = world::PCGEngine::evaluate(g, ctx1);
    auto points2 = world::PCGEngine::evaluate(g, ctx2);

    bool same = points1.size() == points2.size();
    if (same) {
        for (int i = 0; i < points1.size() && same; i++) {
            if (std::abs(points1[i].x - points2[i].x) > 0.001f ||
                std::abs(points1[i].z - points2[i].z) > 0.001f)
                same = false;
        }
    }
    VERIFY(same, "TC-PCG: Determinism", "Same seed produced different results");
}

void testPCGFilters() {
    world::PCGGraph g;
    g.seed = 42;

    world::PCGNode scatter;
    scatter.type = world::PCGNodeType::ScatterPoints;
    scatter.properties["areaSize"] = "100";
    scatter.properties["density"] = "1.0";
    g.nodes.append(scatter);

    world::PCGNode heightFilter;
    heightFilter.type = world::PCGNodeType::HeightFilter;
    heightFilter.properties["minHeight"] = "-1000";
    heightFilter.properties["maxHeight"] = "1000";
    heightFilter.inputNodeIds.append(scatter.id);
    g.nodes.append(heightFilter);

    world::PCGNode output;
    output.type = world::PCGNodeType::InstanceOutput;
    output.inputNodeIds.append(heightFilter.id);
    g.nodes.append(output);

    world::PCGContext ctx(42);
    auto points = world::PCGEngine::evaluate(g, ctx);
    VERIFY(!points.isEmpty(),
           "TC-PCG: Filter Chain", "Filter chain evaluation failed");
}

void testUndoRedo() {
    world::World w;
    QUndoStack undoStack;

    // Add actor
    world::Actor a;
    a.name = "TestBuilding";
    a.type = world::ActorType::Building;
    auto* addCmd = new world::AddActorCommand(&w, a);
    undoStack.push(addCmd);
    VERIFY(w.actorCount() == 1, "TC-Undo: Add", "Actor not added");

    // Undo
    undoStack.undo();
    VERIFY(w.actorCount() == 0, "TC-Undo: Undo Add", "Undo failed");

    // Redo
    undoStack.redo();
    VERIFY(w.actorCount() == 1, "TC-Undo: Redo Add", "Redo failed");

    // Transform
    QString actorId = w.actors[0].id;
    world::Transform oldT = w.actors[0].transform;
    world::Transform newT;
    newT.posX = 100;
    newT.posZ = 200;
    auto* transCmd = new world::TransformActorCommand(&w, actorId, oldT, newT);
    undoStack.push(transCmd);
    VERIFY(w.actors[0].transform.posX == 100,
           "TC-Undo: Transform", "Transform not applied");

    undoStack.undo();
    VERIFY(w.actors[0].transform.posX == 0,
           "TC-Undo: Undo Transform", "Transform undo failed");

    undoStack.redo();
    VERIFY(w.actors[0].transform.posX == 100,
           "TC-Undo: Redo Transform", "Transform redo failed");
}

void testEndToEndWorld() {
    // Create a complete world with terrain, roads, buildings, vegetation, PCG
    world::World w;
    w.settings.name = "4km Test World";
    w.settings.terrainSize = 4000;
    w.settings.heightScale = 100;

    // Add terrain tiles
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 2; c++)
            w.addTerrainTile(r, c);

    // Add terrain actor
    w.addActor(world::ActorType::Terrain, "Terrain", "terrain");

    // Add roads
    auto* road = w.addSpline(world::SplineType::Road, "Highway_A");
    world::SplineControlPoint p1, p2, p3;
    p1.x = -2000; p1.z = -1000;
    p2.x = 0; p2.z = 0;
    p3.x = 2000; p3.z = 1000;
    road->points = {p1, p2, p3};
    road->width = 12;
    road->laneCount = 4;

    // Add buildings
    for (int i = 0; i < 10; i++) {
        auto* b = w.addActor(world::ActorType::Building, "Building_" + QString::number(i), "buildings");
        b->transform.posX = (i - 5) * 50;
        b->transform.posZ = (i % 3) * 100 - 100;
        b->transform.scaleX = 20;
        b->transform.scaleY = 30;
        b->transform.scaleZ = 20;
    }

    // Add vegetation
    for (int i = 0; i < 20; i++) {
        auto* t = w.addActor(world::ActorType::Tree, "Tree_" + QString::number(i), "vegetation");
        t->transform.posX = (i - 10) * 80;
        t->transform.posZ = (i % 5) * 60 - 120;
    }

    // Add PCG graph
    auto* pcg = w.addPCGGraph("Forest_PCG");
    world::PCGNode scatter;
    scatter.type = world::PCGNodeType::ScatterPoints;
    scatter.properties["areaSize"] = "4000";
    scatter.properties["density"] = "0.01";
    pcg->nodes.append(scatter);

    world::PCGNode output;
    output.type = world::PCGNodeType::InstanceOutput;
    output.inputNodeIds.append(scatter.id);
    pcg->nodes.append(output);

    // Add water
    w.addWater("Lake_001", world::WaterType::Lake);

    // Add lighting
    w.addActor(world::ActorType::SunLight, "Sun", "lighting");

    // Add masks
    w.addMask("Vegetation", world::MaskType::Vegetation);
    w.addMask("Water", world::MaskType::Water);
    w.addMask("Road", world::MaskType::Road);

    // Save
    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/e2e.world";
    VERIFY(w.saveToFile(path), "TC-E2E: Save", "Save failed");

    // Load
    world::World loaded = world::World::loadFromFile(path);

    // Verify everything restored
    bool allCorrect = true;
    QString errMsg;

    if (loaded.settings.name != "4km Test World") { allCorrect = false; errMsg = "Settings"; }
    if (loaded.tileCount() != 4) { allCorrect = false; errMsg = "Tiles"; }
    if (loaded.splineCount() != 1) { allCorrect = false; errMsg = "Splines"; }
    if (loaded.actorCountByType(world::ActorType::Building) != 10) { allCorrect = false; errMsg = "Buildings"; }
    if (loaded.actorCountByType(world::ActorType::Tree) != 20) { allCorrect = false; errMsg = "Trees"; }
    if (loaded.pcgGraphCount() != 1) { allCorrect = false; errMsg = "PCG"; }
    if (loaded.waterCount() != 1) { allCorrect = false; errMsg = "Water"; }
    if (loaded.maskCount() != 3) { allCorrect = false; errMsg = "Masks"; }

    VERIFY(allCorrect,
           "TC-E2E: Full World Reload",
           QString("World reload mismatch: %1").arg(errMsg));
}

// ============================================================
// Main
// ============================================================

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "========================================" << std::endl;
    std::cout << "  3D STUDIO WORLD MODEL TEST SUITE" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    testActorCreation();
    testActorSerialization();
    testWorldCreation();
    testWorldAddActor();
    testWorldRemoveActor();
    testWorldHierarchy();
    testWorldSelection();
    testWorldLayers();
    testWorldSaveLoad();
    testWorldValidation();
    testSplineCreation();
    testSplineEvaluation();
    testSplineRoadMesh();
    testPCGGraphCreation();
    testPCGGraphCycles();
    testPCGEvaluation();
    testPCGDeterminism();
    testPCGFilters();
    testUndoRedo();
    testEndToEndWorld();

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
