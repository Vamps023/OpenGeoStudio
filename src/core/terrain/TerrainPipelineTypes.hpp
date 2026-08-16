#pragma once

// ============================================================
// TerrainPipelineTypes — Core data types for the terrain pipeline
// ============================================================
//
// Extends the existing terrain::GeoBounds, terrain::Tile types
// with pipeline-specific structures: raster grids, mask definitions,
// processing stages, validation results, metadata.
//

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QStandardPaths>
#include <vector>
#include <array>
#include <cstdint>
#include <cmath>

#include "../../ui/terrain/TerrainTypes.hpp"

namespace terrain_pipeline {

// ============================================================
// CRS — Extended CRS support beyond EPSG:4326/3857
// ============================================================

struct CrsSpec {
    int epsg = 4326;
    QString description;

    static CrsSpec wgs84() { return {4326, "WGS 84 (geographic)"}; }
    static CrsSpec webMercator() { return {3857, "WGS 84 / Pseudo-Mercator"}; }
    static CrsSpec utm(int zone, bool north) {
        return {north ? (32600 + zone) : (32700 + zone),
                QString("UTM Zone %1%2").arg(zone).arg(north ? "N" : "S")};
    }
    static CrsSpec autoUtm(double lat, double lon) {
        int zone = static_cast<int>(std::floor((lon + 180.0) / 6.0)) + 1;
        bool north = lat >= 0.0;
        return utm(zone, north);
    }

    bool isValid() const { return epsg > 0; }
    bool isGeographic() const { return epsg == 4326; }
    QString authId() const { return QString("EPSG:%1").arg(epsg); }

    QJsonObject toJson() const {
        return {{"epsg", epsg}, {"description", description}};
    }
    static CrsSpec fromJson(const QJsonObject& j) {
        CrsSpec c;
        c.epsg = j["epsg"].toInt(4326);
        c.description = j["description"].toString();
        return c;
    }
};

// ============================================================
// RasterGrid — A georeferenced raster grid of float values
// ============================================================

struct RasterGrid {
    int width = 0;
    int height = 0;
    std::vector<float> data;  // row-major, width*height
    float nodataValue = -9999.0f;

    // Georeference
    double originX = 0.0;  // left edge (degrees or meters)
    double originY = 0.0;  // top edge (degrees or meters)
    double pixelSizeX = 1.0;
    double pixelSizeY = 1.0;  // negative (top-down)
    CrsSpec crs;

    bool isValid() const { return width > 0 && height > 0 && (int)data.size() >= width * height; }

    float at(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return nodataValue;
        return data[y * width + x];
    }

    void set(int x, int y, float val) {
        if (x >= 0 && x < width && y >= 0 && y < height)
            data[y * width + x] = val;
    }

    // Bilinear sample at fractional coordinates
    float sampleBilinear(double fx, double fy) const {
        int x0 = static_cast<int>(fx);
        int y0 = static_cast<int>(fy);
        int x1 = std::min(x0 + 1, width - 1);
        int y1 = std::min(y0 + 1, height - 1);
        x0 = std::max(0, std::min(x0, width - 1));
        y0 = std::max(0, std::min(y0, height - 1));
        double dx = fx - x0;
        double dy = fy - y0;
        float v00 = at(x0, y0), v01 = at(x0, y1);
        float v10 = at(x1, y0), v11 = at(x1, y1);
        return static_cast<float>(
            v00 * (1 - dx) * (1 - dy) + v10 * dx * (1 - dy) +
            v01 * (1 - dx) * dy + v11 * dx * dy);
    }

    // World-to-pixel mapping
    void worldToPixel(double worldX, double worldY, double& px, double& py) const {
        px = (worldX - originX) / pixelSizeX;
        py = (worldY - originY) / pixelSizeY;
    }

    void pixelToWorld(double px, double py, double& worldX, double& worldY) const {
        worldX = originX + px * pixelSizeX;
        worldY = originY + py * pixelSizeY;
    }

    // Get bounds
    double west() const { return originX; }
    double east() const { return originX + width * pixelSizeX; }
    double north() const { return originY; }
    double south() const { return originY + height * pixelSizeY; }

    // Statistics
    struct Stats {
        float min = 0, max = 0, mean = 0;
        int validCount = 0, nodataCount = 0;
        bool valid = false;
    };

    Stats computeStats() const {
        Stats s;
        bool first = true;
        double sum = 0;
        for (int i = 0; i < width * height; i++) {
            float v = data[i];
            if (v == nodataValue || std::isnan(v)) {
                s.nodataCount++;
                continue;
            }
            if (first) { s.min = v; s.max = v; first = false; }
            if (v < s.min) s.min = v;
            if (v > s.max) s.max = v;
            sum += v;
            s.validCount++;
        }
        if (s.validCount > 0) {
            s.mean = static_cast<float>(sum / s.validCount);
            s.valid = true;
        }
        return s;
    }
};

// ============================================================
// ByteRaster — 8-bit raster for masks
// ============================================================

struct ByteRaster {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;
    uint8_t nodataValue = 0;

    double originX = 0.0, originY = 0.0;
    double pixelSizeX = 1.0, pixelSizeY = 1.0;
    CrsSpec crs;

    bool isValid() const { return width > 0 && height > 0 && (int)data.size() >= width * height; }

    uint8_t at(int x, int y) const {
        if (x < 0 || x >= width || y < 0 || y >= height) return nodataValue;
        return data[y * width + x];
    }

    void set(int x, int y, uint8_t val) {
        if (x >= 0 && x < width && y >= 0 && y < height)
            data[y * width + x] = val;
    }
};

// ============================================================
// MaskType — Classification of mask categories
// ============================================================

enum class MaskCategory {
    Source,         // From external data (vegetation, water, road, building)
    TerrainDerived, // Computed from DEM (slope, aspect, elevation)
    Procedural,     // Computed from distance/geometry (distance-to-road, etc.)
};

enum class MaskType {
    // Source masks
    Vegetation,
    Forest,
    Grass,
    Crop,
    Water,
    Urban,
    Road,
    Building,
    LandCover,
    // Terrain-derived masks
    Slope,
    Aspect,
    Elevation,
    Curvature,
    Roughness,
    Hillshade,
    // Procedural masks
    DistanceToRoad,
    DistanceToWater,
    DistanceToBoundary,
    ElevationRange,
    SlopeRange,
    AspectRange,
    PolygonMask,
    Custom,
};

// ============================================================
// MaskDefinition — Configuration for a single mask
// ============================================================

struct MaskDefinition {
    QString name;           // "Vegetation", "Water", etc.
    QString id;             // unique identifier
    MaskType type = MaskType::Custom;
    MaskCategory category = MaskCategory::Source;
    bool enabled = false;

    // For source masks: provider name
    QString providerName;

    // For terrain-derived: thresholds
    float minThreshold = 0.0f;
    float maxThreshold = 1.0f;

    // For land-cover class mapping
    QList<int> classIds;    // e.g., [10, 20] for forest

    // For slope/aspect/elevation ranges
    QList<float> rangeBounds;  // e.g., [0, 5, 15, 30, 45, 90] for slope

    // Normalization
    enum class NormalizeMode { Binary, Continuous, Class };
    NormalizeMode normalizeMode = NormalizeMode::Binary;

    // Metadata
    QString source;
    QString sourceVersion;

    QJsonObject toJson() const {
        QJsonObject j;
        j["name"] = name;
        j["id"] = id;
        j["type"] = static_cast<int>(type);
        j["category"] = static_cast<int>(category);
        j["enabled"] = enabled;
        j["providerName"] = providerName;
        j["minThreshold"] = static_cast<double>(minThreshold);
        j["maxThreshold"] = static_cast<double>(maxThreshold);
        j["normalizeMode"] = static_cast<int>(normalizeMode);
        j["source"] = source;
        j["sourceVersion"] = sourceVersion;

        QJsonArray classArr;
        for (int c : classIds) classArr.append(c);
        j["classIds"] = classArr;

        QJsonArray rangeArr;
        for (float r : rangeBounds) rangeArr.append(static_cast<double>(r));
        j["rangeBounds"] = rangeArr;

        return j;
    }

    static MaskDefinition fromJson(const QJsonObject& j) {
        MaskDefinition m;
        m.name = j["name"].toString();
        m.id = j["id"].toString();
        m.type = static_cast<MaskType>(j["type"].toInt(0));
        m.category = static_cast<MaskCategory>(j["category"].toInt(0));
        m.enabled = j["enabled"].toBool(false);
        m.providerName = j["providerName"].toString();
        m.minThreshold = static_cast<float>(j["minThreshold"].toDouble(0));
        m.maxThreshold = static_cast<float>(j["maxThreshold"].toDouble(1));
        m.normalizeMode = static_cast<NormalizeMode>(j["normalizeMode"].toInt(0));
        m.source = j["source"].toString();
        m.sourceVersion = j["sourceVersion"].toString();

        QJsonArray classArr = j["classIds"].toArray();
        for (const auto& v : classArr) m.classIds.append(v.toInt());

        QJsonArray rangeArr = j["rangeBounds"].toArray();
        for (const auto& v : rangeArr) m.rangeBounds.append(static_cast<float>(v.toDouble()));

        return m;
    }

    static MaskDefinition vegetation() {
        MaskDefinition m;
        m.name = "Vegetation"; m.id = "vegetation";
        m.type = MaskType::Vegetation; m.category = MaskCategory::Source;
        m.normalizeMode = NormalizeMode::Binary;
        return m;
    }
    static MaskDefinition water() {
        MaskDefinition m;
        m.name = "Water"; m.id = "water";
        m.type = MaskType::Water; m.category = MaskCategory::Source;
        m.normalizeMode = NormalizeMode::Binary;
        return m;
    }
    static MaskDefinition urban() {
        MaskDefinition m;
        m.name = "Urban"; m.id = "urban";
        m.type = MaskType::Urban; m.category = MaskCategory::Source;
        m.normalizeMode = NormalizeMode::Binary;
        return m;
    }
    static MaskDefinition road() {
        MaskDefinition m;
        m.name = "Road"; m.id = "road";
        m.type = MaskType::Road; m.category = MaskCategory::Source;
        m.normalizeMode = NormalizeMode::Binary;
        return m;
    }
    static MaskDefinition building() {
        MaskDefinition m;
        m.name = "Building"; m.id = "building";
        m.type = MaskType::Building; m.category = MaskCategory::Source;
        m.normalizeMode = NormalizeMode::Binary;
        return m;
    }
    static MaskDefinition slope() {
        MaskDefinition m;
        m.name = "Slope"; m.id = "slope";
        m.type = MaskType::Slope; m.category = MaskCategory::TerrainDerived;
        m.normalizeMode = NormalizeMode::Continuous;
        m.rangeBounds = {0, 5, 15, 30, 45, 90};
        return m;
    }
    static MaskDefinition aspect() {
        MaskDefinition m;
        m.name = "Aspect"; m.id = "aspect";
        m.type = MaskType::Aspect; m.category = MaskCategory::TerrainDerived;
        m.normalizeMode = NormalizeMode::Continuous;
        return m;
    }
    static MaskDefinition elevation() {
        MaskDefinition m;
        m.name = "Elevation"; m.id = "elevation";
        m.type = MaskType::Elevation; m.category = MaskCategory::TerrainDerived;
        m.normalizeMode = NormalizeMode::Continuous;
        m.rangeBounds = {0, 10, 50, 100, 500, 10000};
        return m;
    }
    static MaskDefinition forest() {
        MaskDefinition m;
        m.name = "Forest"; m.id = "forest";
        m.type = MaskType::Forest; m.category = MaskCategory::Source;
        m.normalizeMode = NormalizeMode::Binary;
        m.classIds = {10, 20};
        return m;
    }
    static MaskDefinition grass() {
        MaskDefinition m;
        m.name = "Grass"; m.id = "grass";
        m.type = MaskType::Grass; m.category = MaskCategory::Source;
        m.normalizeMode = NormalizeMode::Binary;
        m.classIds = {30};
        return m;
    }
    static MaskDefinition crop() {
        MaskDefinition m;
        m.name = "Crop"; m.id = "crop";
        m.type = MaskType::Crop; m.category = MaskCategory::Source;
        m.normalizeMode = NormalizeMode::Binary;
        m.classIds = {40};
        return m;
    }
};

// ============================================================
// PipelineStage — Processing stage status
// ============================================================

enum class StageStatus { Success, Warning, Failed, Skipped };

struct StageResult {
    QString stageName;
    StageStatus status = StageStatus::Skipped;
    QString message;
    QString errorDetail;
    int itemsProcessed = 0;
    int itemsTotal = 0;

    QJsonObject toJson() const {
        return {
            {"stage", stageName},
            {"status", static_cast<int>(status)},
            {"message", message},
            {"errorDetail", errorDetail},
            {"itemsProcessed", itemsProcessed},
            {"itemsTotal", itemsTotal}
        };
    }
};

// ============================================================
// PipelineConfig — Full pipeline configuration
// ============================================================

struct PipelineConfig {
    // Area
    double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;

    // CRS
    terrain::CrsSource inputCrs = terrain::CrsSource::EPSG_4326;
    CrsSpec targetCrs;

    // Resolution
    int heightmapResolution = 1024;
    int albedoResolution = 1024;
    int tileSize = 1024;  // pixels per tile

    // Datasets to acquire
    bool enableDEM = true;
    bool enableImagery = true;
    bool enableLandCover = false;
    bool enableWater = false;
    bool enableRoads = false;
    bool enableBuildings = false;

    // DEM source
    terrain::DemSource demSource = terrain::DemSource::AWS_Terrarium;
    terrain::ImagerySource imagerySource = terrain::ImagerySource::ArcGIS_World_Imagery;

    // API keys
    QString openTopoApiKey;
    QString gpxzApiKey;
    QString mapboxToken;

    // Masks
    QList<MaskDefinition> masks;

    // Tile settings
    int tileRows = 2, tileCols = 2;

    // Export settings
    QString exportDir;
    terrain::HeightmapFormat heightmapFormat = terrain::HeightmapFormat::GeoTIFF_Float32;
    terrain::AlbedoFormat albedoFormat = terrain::AlbedoFormat::PNG;

    // Packed mask config (RGBA channel assignments)
    QString packedMaskR = "vegetation";
    QString packedMaskG = "water";
    QString packedMaskB = "urban";
    QString packedMaskA = "road";
    bool exportPackedMask = false;

    QJsonObject toJson() const {
        QJsonObject j;
        j["minLat"] = minLat; j["maxLat"] = maxLat;
        j["minLon"] = minLon; j["maxLon"] = maxLon;
        j["targetCrs"] = targetCrs.toJson();
        j["heightmapResolution"] = heightmapResolution;
        j["albedoResolution"] = albedoResolution;
        j["tileSize"] = tileSize;
        j["enableDEM"] = enableDEM;
        j["enableImagery"] = enableImagery;
        j["enableLandCover"] = enableLandCover;
        j["enableWater"] = enableWater;
        j["enableRoads"] = enableRoads;
        j["enableBuildings"] = enableBuildings;
        j["demSource"] = static_cast<int>(demSource);
        j["imagerySource"] = static_cast<int>(imagerySource);
        j["openTopoApiKey"] = openTopoApiKey;
        j["gpxzApiKey"] = gpxzApiKey;
        j["mapboxToken"] = mapboxToken;
        j["tileRows"] = tileRows;
        j["tileCols"] = tileCols;
        j["exportDir"] = exportDir;
        j["heightmapFormat"] = static_cast<int>(heightmapFormat);
        j["albedoFormat"] = static_cast<int>(albedoFormat);
        j["exportPackedMask"] = exportPackedMask;
        j["packedMaskR"] = packedMaskR;
        j["packedMaskG"] = packedMaskG;
        j["packedMaskB"] = packedMaskB;
        j["packedMaskA"] = packedMaskA;

        QJsonArray maskArr;
        for (const auto& m : masks) maskArr.append(m.toJson());
        j["masks"] = maskArr;

        return j;
    }

    static PipelineConfig fromJson(const QJsonObject& j) {
        PipelineConfig c;
        c.minLat = j["minLat"].toDouble(0);
        c.maxLat = j["maxLat"].toDouble(0);
        c.minLon = j["minLon"].toDouble(0);
        c.maxLon = j["maxLon"].toDouble(0);
        c.targetCrs = CrsSpec::fromJson(j["targetCrs"].toObject());
        c.heightmapResolution = j["heightmapResolution"].toInt(1024);
        c.albedoResolution = j["albedoResolution"].toInt(1024);
        c.tileSize = j["tileSize"].toInt(1024);
        c.enableDEM = j["enableDEM"].toBool(true);
        c.enableImagery = j["enableImagery"].toBool(true);
        c.enableLandCover = j["enableLandCover"].toBool(false);
        c.enableWater = j["enableWater"].toBool(false);
        c.enableRoads = j["enableRoads"].toBool(false);
        c.enableBuildings = j["enableBuildings"].toBool(false);
        c.demSource = static_cast<terrain::DemSource>(j["demSource"].toInt(0));
        c.imagerySource = static_cast<terrain::ImagerySource>(j["imagerySource"].toInt(1));
        c.openTopoApiKey = j["openTopoApiKey"].toString();
        c.gpxzApiKey = j["gpxzApiKey"].toString();
        c.mapboxToken = j["mapboxToken"].toString();
        c.tileRows = j["tileRows"].toInt(2);
        c.tileCols = j["tileCols"].toInt(2);
        c.exportDir = j["exportDir"].toString();
        c.heightmapFormat = static_cast<terrain::HeightmapFormat>(j["heightmapFormat"].toInt(5));
        c.albedoFormat = static_cast<terrain::AlbedoFormat>(j["albedoFormat"].toInt(0));
        c.exportPackedMask = j["exportPackedMask"].toBool(false);
        c.packedMaskR = j["packedMaskR"].toString("vegetation");
        c.packedMaskG = j["packedMaskG"].toString("water");
        c.packedMaskB = j["packedMaskB"].toString("urban");
        c.packedMaskA = j["packedMaskA"].toString("road");

        QJsonArray maskArr = j["masks"].toArray();
        for (const auto& v : maskArr)
            c.masks.append(MaskDefinition::fromJson(v.toObject()));

        return c;
    }

    // Default masks for a full pipeline run
    static QList<MaskDefinition> defaultMasks() {
        return {
            MaskDefinition::vegetation(),
            MaskDefinition::forest(),
            MaskDefinition::grass(),
            MaskDefinition::water(),
            MaskDefinition::urban(),
            MaskDefinition::road(),
            MaskDefinition::building(),
            MaskDefinition::slope(),
            MaskDefinition::aspect(),
            MaskDefinition::elevation(),
        };
    }
};

// ============================================================
// TileInfo — Metadata for a generated tile
// ============================================================

struct TileInfo {
    int row = 0, col = 0;
    double west = 0, east = 0, north = 0, south = 0;
    int width = 0, height = 0;
    CrsSpec crs;
    double resolution = 0;  // meters per pixel

    QString id() const { return QString("%1_%2").arg(row).arg(col); }

    QJsonObject toJson() const {
        return {
            {"row", row}, {"col", col},
            {"west", west}, {"east", east},
            {"north", north}, {"south", south},
            {"width", width}, {"height", height},
            {"crs", crs.toJson()},
            {"resolution", resolution}
        };
    }
};

// ============================================================
// DatasetMetadata — Metadata for a generated dataset
// ============================================================

struct DatasetMetadata {
    QString dataset;        // "vegetation_mask", "heightmap", etc.
    QString source;         // provider name
    QString sourceVersion;
    CrsSpec crs;
    double resolution = 0;
    double bounds[4] = {0, 0, 0, 0};  // west, south, east, north
    QString tileId;
    QString generatedAt;
    QString processingVersion = "1.0";
    float nodata = 0;

    QJsonObject toJson() const {
        QJsonObject j;
        j["dataset"] = dataset;
        j["source"] = source;
        j["sourceVersion"] = sourceVersion;
        j["crs"] = crs.toJson();
        j["resolution"] = resolution;
        j["tileId"] = tileId;
        j["generatedAt"] = generatedAt;
        j["processingVersion"] = processingVersion;
        j["nodata"] = static_cast<double>(nodata);
        QJsonArray b;
        b.append(bounds[0]); b.append(bounds[1]);
        b.append(bounds[2]); b.append(bounds[3]);
        j["bounds"] = b;
        return j;
    }
};

} // namespace terrain_pipeline
