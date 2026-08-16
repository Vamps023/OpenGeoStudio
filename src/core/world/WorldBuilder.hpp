#pragma once

// ============================================================
// WorldBuilder — High-level World authoring API
// ============================================================
//
// Provides a unified API for the complete world authoring workflow:
//   1. Create World
//   2. Import terrain
//   3. Generate masks
//   4. Create roads
//   5. Generate buildings
//   6. Generate vegetation (PCG)
//   7. Add water
//   8. Add lighting
//   9. Save / Load
//

#include "World.hpp"
#include "Spline.hpp"
#include "SplineEditor.hpp"
#include "PCGEngine.hpp"
#include "TerrainWorldBridge.hpp"
#include <QString>
#include <QDir>
#include <QFileInfo>
#include "../logger/Logger.hpp"
#include <functional>

namespace world {

class WorldBuilder {
public:
    World world;

    // Height sampling function (set by OgreWidget)
    std::function<float(float, float)> sampleHeight;
    std::function<float(float, float)> sampleSlope;

    // ============================================================
    // Step 1: Create World
    // ============================================================

    void createWorld(const QString& name, float terrainSize = 4000.0f) {
        world = World();  // Reset
        world.settings.name = name;
        world.settings.terrainSize = terrainSize;
        appLog().info("[WorldBuilder] Created world:", name, "size:", terrainSize);
    }

    // ============================================================
    // Step 2: Import terrain
    // ============================================================

    void importTerrain(const QString& heightmapPath, const QString& albedoPath,
                       float heightScale = 100.0f)
    {
        TerrainWorldBridge::importTerrainIntoWorld(world,
            heightmapPath, albedoPath,
            world.settings.terrainSize, heightScale);
    }

    // ============================================================
    // Step 3: Generate masks
    // ============================================================

    void generateMasks() {
        // Height mask
        TerrainMask heightMask = TerrainWorldBridge::generateHeightMask(
            world, 0, world.settings.heightScale);
        world.masks.append(heightMask);

        // Slope mask
        TerrainMask slopeMask = TerrainWorldBridge::generateSlopeMask(
            world, 45.0f);
        world.masks.append(slopeMask);

        appLog().info("[WorldBuilder] Generated", world.maskCount(), "masks");
    }

    // ============================================================
    // Step 4: Create roads
    // ============================================================

    Spline* createRoad(const QString& name,
                       const QList<QPair<float, float>>& points,
                       float width = 8.0f, int laneCount = 2)
    {
        Spline road = SplineEditor::createRoad(name, points, width, laneCount);
        world.splines.append(road);

        // Create road actor
        Actor roadActor = SplineEditor::splineToActor(world.splines.last());
        world.addActor(roadActor);

        appLog().info("[WorldBuilder] Created road:", name, "with", points.size(), "points");
        return &world.splines.last();
    }

    // ============================================================
    // Step 5: Generate buildings
    // ============================================================

    void generateBuildingsAlongRoads(float spacing = 50.0f, float offset = 20.0f) {
        for (const auto& spline : world.splines) {
            if (spline.type != SplineType::Road) continue;

            auto buildings = SplineEditor::generateBuildingsAlongRoad(
                spline, spacing, offset);
            for (const auto& b : buildings)
                world.addActor(b);
        }

        appLog().info("[WorldBuilder] Generated buildings:", world.actorCountByType(ActorType::Building));
    }

    void generateBuildings(int count, float areaSize = 4000.0f, int seed = 42) {
        uint32_t rng = seed;
        auto rand01 = [&rng]() {
            rng = rng * 1103515245 + 12345;
            return (rng >> 16) / 65535.0f;
        };

        for (int i = 0; i < count; i++) {
            Actor b;
            b.name = QString("Building_%1").arg(i);
            b.type = ActorType::Building;
            b.layerId = "buildings";
            b.transform.posX = (rand01() - 0.5f) * areaSize;
            b.transform.posZ = (rand01() - 0.5f) * areaSize;
            if (sampleHeight)
                b.transform.posY = sampleHeight(b.transform.posX, b.transform.posZ);
            b.transform.scaleX = 8 + rand01() * 12;
            b.transform.scaleY = 10 + rand01() * 20;
            b.transform.scaleZ = 8 + rand01() * 12;
            b.transform.rotY = rand01() * 360;
            b.colorR = 0.5f + rand01() * 0.3f;
            b.colorG = 0.5f + rand01() * 0.3f;
            b.colorB = 0.5f + rand01() * 0.3f;
            b.seed = int(rng);
            world.addActor(b);
        }

        appLog().info("[WorldBuilder] Generated", count, "buildings");
    }

    // ============================================================
    // Step 6: Generate vegetation (PCG)
    // ============================================================

    PCGGraph* createVegetationPCG(const QString& name,
                                   const QString& meshPath = "",
                                   float density = 0.01f,
                                   float areaSize = 4000.0f)
    {
        PCGGraph* graph = world.addPCGGraph(name);
        graph->seed = 42;
        graph->targetMeshPath = meshPath;

        // Scatter node
        PCGNode scatter;
        scatter.name = "Scatter Trees";
        scatter.type = PCGNodeType::ScatterPoints;
        scatter.properties["areaSize"] = QString::number(areaSize);
        scatter.properties["density"] = QString::number(density);
        scatter.properties["scaleMin"] = "0.8";
        scatter.properties["scaleMax"] = "1.5";
        graph->nodes.append(scatter);

        // Height filter
        PCGNode heightFilter;
        heightFilter.name = "Height Filter";
        heightFilter.type = PCGNodeType::HeightFilter;
        heightFilter.properties["minHeight"] = "0";
        heightFilter.properties["maxHeight"] = QString::number(world.settings.heightScale * 0.8);
        heightFilter.inputNodeIds.append(scatter.id);
        graph->nodes.append(heightFilter);

        // Slope filter
        PCGNode slopeFilter;
        slopeFilter.name = "Slope Filter";
        slopeFilter.type = PCGNodeType::SlopeFilter;
        slopeFilter.properties["maxSlope"] = "30";
        slopeFilter.inputNodeIds.append(heightFilter.id);
        graph->nodes.append(slopeFilter);

        // Random rotation
        PCGNode rot;
        rot.name = "Random Rotation";
        rot.type = PCGNodeType::RandomRotation;
        rot.properties["yAxisOnly"] = "true";
        rot.inputNodeIds.append(slopeFilter.id);
        graph->nodes.append(rot);

        // Random scale
        PCGNode scale;
        scale.name = "Random Scale";
        scale.type = PCGNodeType::RandomScale;
        scale.properties["scaleMin"] = "0.8";
        scale.properties["scaleMax"] = "1.5";
        scale.properties["uniform"] = "true";
        scale.inputNodeIds.append(rot.id);
        graph->nodes.append(scale);

        // Output
        PCGNode output;
        output.name = "Output";
        output.type = PCGNodeType::InstanceOutput;
        output.inputNodeIds.append(scale.id);
        graph->nodes.append(output);

        appLog().info("[WorldBuilder] Created PCG graph:", name, "with", graph->nodes.size(), "nodes");
        return graph;
    }

    void generateVegetation(const QString& graphName, int seed = 42) {
        PCGGraph* graph = world.findPCGGraph(graphName);
        if (!graph) {
            appLog().info("[WorldBuilder] PCG graph not found:", graphName);
            return;
        }

        PCGContext ctx(seed);
        ctx.world = &world;
        ctx.sampleHeight = sampleHeight;
        ctx.sampleSlope = sampleSlope;

        auto points = PCGEngine::evaluate(*graph, ctx);
        graph->cachedPoints = points;

        // Create actors from generated points
        for (const auto& p : points) {
            Actor tree;
            tree.name = "Tree";
            tree.type = ActorType::Tree;
            tree.layerId = "vegetation";
            tree.transform.posX = p.x;
            tree.transform.posY = p.y;
            tree.transform.posZ = p.z;
            tree.transform.rotY = p.rotY;
            tree.transform.scaleX = p.scaleX;
            tree.transform.scaleY = p.scaleY;
            tree.transform.scaleZ = p.scaleZ;
            tree.colorR = 0.2f; tree.colorG = 0.5f; tree.colorB = 0.2f;
            tree.pcgGraphId = graph->id;
            tree.seed = seed;
            world.addActor(tree);
        }

        appLog().info("[WorldBuilder] Generated", points.size(), "vegetation actors");
    }

    // ============================================================
    // Step 7: Add water
    // ============================================================

    WaterBody* addLake(const QString& name, float x, float z,
                       float sizeX, float sizeZ, float level)
    {
        WaterBody* w = world.addWater(name, WaterType::Lake);
        w->x = x; w->z = z;
        w->sizeX = sizeX; w->sizeZ = sizeZ;
        w->level = level;
        return w;
    }

    WaterBody* addRiver(const QString& name, const QString& splineId, float level)
    {
        WaterBody* w = world.addWater(name, WaterType::River);
        w->splineId = splineId;
        w->level = level;
        return w;
    }

    // ============================================================
    // Step 8: Add lighting
    // ============================================================

    void addSunLight(float yaw = 45.0f, float pitch = 60.0f, float intensity = 3.0f) {
        world.settings.sunYaw = yaw;
        world.settings.sunPitch = pitch;
        world.settings.sunIntensity = intensity;

        Actor* sun = world.addActor(ActorType::SunLight, "Sun", "lighting");
        sun->metadata["yaw"] = QString::number(yaw);
        sun->metadata["pitch"] = QString::number(pitch);
        sun->metadata["intensity"] = QString::number(intensity);
        sun->colorR = 1.0f; sun->colorG = 0.95f; sun->colorB = 0.8f;
    }

    void addSkyLight(float intensity = 1.0f) {
        Actor* sky = world.addActor(ActorType::SkyLight, "Sky Light", "lighting");
        sky->metadata["intensity"] = QString::number(intensity);
        sky->colorR = 0.4f; sky->colorG = 0.6f; sky->colorB = 0.9f;
    }

    // ============================================================
    // Step 9: Save / Load
    // ============================================================

    bool save(const QString& path) const {
        return world.saveToFile(path);
    }

    bool load(const QString& path) {
        world = World::loadFromFile(path);
        return world.actorCount() > 0 || world.layerCount() > 0;
    }

    // ============================================================
    // Validation
    // ============================================================

    QList<World::ValidationError> validate() const {
        return world.validate();
    }

    // ============================================================
    // Statistics
    // ============================================================

    QString statsString() const {
        return QString("World: %1\n"
                        "  Actors: %2 (Buildings: %3, Trees: %4)\n"
                        "  Layers: %5\n"
                        "  Splines: %6\n"
                        "  PCG Graphs: %7\n"
                        "  Terrain Tiles: %8\n"
                        "  Masks: %9\n"
                        "  Water Bodies: %10\n"
                        "  Materials: %11")
            .arg(world.settings.name)
            .arg(world.actorCount())
            .arg(world.actorCountByType(ActorType::Building))
            .arg(world.actorCountByType(ActorType::Tree))
            .arg(world.layerCount())
            .arg(world.splineCount())
            .arg(world.pcgGraphCount())
            .arg(world.tileCount())
            .arg(world.maskCount())
            .arg(world.waterCount())
            .arg(world.materials.size());
    }
};

} // namespace world
