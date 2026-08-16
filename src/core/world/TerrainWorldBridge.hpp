#pragma once

// ============================================================
// TerrainWorldBridge — Connects terrain pipeline to World model
// ============================================================
//
// This bridges the existing terrain pipeline (src/core/terrain/)
// with the new World Authoring System (src/core/world/).
//
// It creates terrain actors, tiles, masks, and materials in the
// World from terrain pipeline outputs.
//

#include "World.hpp"

#include <QString>
#include <QDir>
#include <QFileInfo>
#include "../logger/Logger.hpp"

namespace world {

class TerrainWorldBridge {
public:
    // Import terrain pipeline results into the World
    static void importTerrainIntoWorld(World& world,
                                        const QString& heightmapPath,
                                        const QString& albedoPath,
                                        float terrainSize,
                                        float heightScale,
                                        const QString& crs = "EPSG:4326")
    {
        // Update world settings
        world.settings.terrainSize = terrainSize;
        world.settings.heightScale = heightScale;
        world.settings.heightmapPath = heightmapPath;
        world.settings.albedoPath = albedoPath;

        // Remove existing terrain actors
        auto terrainActors = world.actorsByType(ActorType::Terrain);
        for (auto* a : terrainActors)
            world.removeActor(a->id);

        // Create terrain actor
        Actor* terrainActor = world.addActor(ActorType::Terrain, "Terrain", "terrain");
        terrainActor->transform.posX = 0;
        terrainActor->transform.posY = 0;
        terrainActor->transform.posZ = 0;
        terrainActor->transform.scaleX = 1;
        terrainActor->transform.scaleY = 1;
        terrainActor->transform.scaleZ = 1;
        terrainActor->metadata["heightmapPath"] = heightmapPath;
        terrainActor->metadata["albedoPath"] = albedoPath;
        terrainActor->metadata["terrainSize"] = QString::number(terrainSize);
        terrainActor->metadata["heightScale"] = QString::number(heightScale);
        terrainActor->metadata["crs"] = crs;
        terrainActor->colorR = 0.4f; terrainActor->colorG = 0.5f; terrainActor->colorB = 0.3f;

        appLog().info("[TerrainWorldBridge] Imported terrain into World:", "size=", terrainSize, "heightScale=", heightScale, "heightmap=", heightmapPath);
    }

    // Import terrain masks into the World
    static void importMasksIntoWorld(World& world,
                                      const QString& masksDir)
    {
        QDir dir(masksDir);
        if (!dir.exists()) return;

        QStringList filters;
        filters << "*mask*.png" << "*mask*.tif";
        QStringList files = dir.entryList(filters, QDir::Files);

        for (const auto& file : files) {
            QString path = masksDir + "/" + file;
            QString baseName = QFileInfo(file).baseName().toLower();

            MaskType type = MaskType::Custom;
            if (baseName.contains("vegetation") || baseName.contains("forest"))
                type = MaskType::Vegetation;
            else if (baseName.contains("water"))
                type = MaskType::Water;
            else if (baseName.contains("road"))
                type = MaskType::Road;
            else if (baseName.contains("building") || baseName.contains("urban"))
                type = MaskType::Building;
            else if (baseName.contains("slope"))
                type = MaskType::Slope;
            else if (baseName.contains("elevation") || baseName.contains("height"))
                type = MaskType::Elevation;

            TerrainMask* mask = world.addMask(baseName, type);
            mask->sourcePath = path;
            mask->width = 512;
            mask->height = 512;
        }

        appLog().info("[TerrainWorldBridge] Imported", files.size(), "masks from", masksDir);
    }

    // Create terrain tiles for large worlds
    static void createTerrainTiles(World& world,
                                    int rows, int cols,
                                    float tileSize)
    {
        world.terrainTiles.clear();
        float totalSize = tileSize * std::max(rows, cols);
        float halfTotal = totalSize * 0.5f;

        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                TerrainTile* tile = world.addTerrainTile(r, c);
                tile->west = -halfTotal + c * tileSize;
                tile->east = tile->west + tileSize;
                tile->north = halfTotal - r * tileSize;
                tile->south = tile->north - tileSize;
                tile->width = 256;
                tile->height = 256;
            }
        }

        appLog().info("[TerrainWorldBridge] Created", (rows * cols), "terrain tiles");
    }

    // Generate terrain material from masks
    static TerrainMaterial* createTerrainMaterial(World& world,
                                                   const QString& name)
    {
        TerrainMaterial mat;
        mat.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        mat.name = name;
        world.materials.append(mat);
        return &world.materials.last();
    }

    // Add a material layer based on slope/height
    static void addMaterialLayer(TerrainMaterial& material,
                                 const QString& name,
                                 float slopeMin, float slopeMax,
                                 float heightMin, float heightMax,
                                 const QString& maskId = QString())
    {
        TerrainMaterialLayer layer;
        layer.name = name;
        layer.slopeMin = slopeMin;
        layer.slopeMax = slopeMax;
        layer.heightMin = heightMin;
        layer.heightMax = heightMax;
        layer.maskId = maskId;
        material.layers.append(layer);
    }

    // Sample terrain height at world coordinates
    // This is a simplified version — the real implementation would
    // use the heightmap data from the terrain pipeline
    static float sampleHeight(const World& world, float x, float z)
    {
        // For now, return 0 — the actual sampling is done by OgreWidget
        // which has the loaded heightmap image
        Q_UNUSED(world); Q_UNUSED(x); Q_UNUSED(z);
        return 0;
    }

    // Calculate slope at a point (simplified)
    static float sampleSlope(const World& world, float x, float z)
    {
        float h = sampleHeight(world, x, z);
        float hx = sampleHeight(world, x + 1, z);
        float hz = sampleHeight(world, x, z + 1);
        float dx = hx - h;
        float dz = hz - h;
        return std::atan2(std::sqrt(dx * dx + dz * dz), 1.0f) * 180.0f / 3.14159265358979f;
    }

    // Generate a height mask from terrain
    static TerrainMask generateHeightMask(const World& world,
                                           float minHeight, float maxHeight,
                                           int width = 512, int height = 512)
    {
        TerrainMask mask;
        mask.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        mask.name = "HeightMask";
        mask.type = MaskType::Height;
        mask.width = width;
        mask.height = height;
        mask.data.resize(width * height, 0);

        float terrainSize = world.settings.terrainSize;
        float halfSize = terrainSize * 0.5f;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float worldX = -halfSize + (float(x) / width) * terrainSize;
                float worldZ = -halfSize + (float(y) / height) * terrainSize;
                float h = sampleHeight(world, worldX, worldZ);
                float normalized = (h - minHeight) / (maxHeight - minHeight);
                normalized = std::max(0.0f, std::min(1.0f, normalized));
                mask.data[y * width + x] = uint8_t(normalized * 255);
            }
        }

        return mask;
    }

    // Generate a slope mask from terrain
    static TerrainMask generateSlopeMask(const World& world,
                                          float maxSlope = 45.0f,
                                          int width = 512, int height = 512)
    {
        TerrainMask mask;
        mask.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        mask.name = "SlopeMask";
        mask.type = MaskType::Slope;
        mask.width = width;
        mask.height = height;
        mask.data.resize(width * height, 0);

        float terrainSize = world.settings.terrainSize;
        float halfSize = terrainSize * 0.5f;

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float worldX = -halfSize + (float(x) / width) * terrainSize;
                float worldZ = -halfSize + (float(y) / height) * terrainSize;
                float slope = sampleSlope(world, worldX, worldZ);
                float normalized = std::max(0.0f, std::min(1.0f, slope / maxSlope));
                mask.data[y * width + x] = uint8_t((1.0f - normalized) * 255);
            }
        }

        return mask;
    }
};

} // namespace world
