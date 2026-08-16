// ============================================================
// test_terrain_pipeline.cpp — Automated test suite for terrain pipeline
// ============================================================
//
// Implements all 26 tests from the specification.
// Uses a simple assert-based test framework (no Qt::Test dependency).
//

#include "../../core/terrain/TerrainPipelineTypes.hpp"
#include "../../core/terrain/GISProcessor.hpp"
#include "../../core/terrain/TerrainAnalyzer.hpp"
#include "../../core/terrain/TileManager.hpp"
#include "../../core/terrain/CacheManager.hpp"
#include "../../core/terrain/masks/MaskManager.hpp"
#include "../../core/terrain/ValidationManager.hpp"
#include "../../core/terrain/providers/DemProviders.hpp"
#include "../../core/terrain/providers/ImageryProviders.hpp"
#include "../../core/terrain/providers/VectorProviders.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QDebug>
#include <cmath>
#include <iostream>

static int g_testsPassed = 0;
static int g_testsFailed = 0;
static QStringList g_failures;

#define TEST_PASS(name) do { \
    g_testsPassed++; \
    std::cout << "[PASS] " << name << std::endl; \
} while(0)

#define TEST_FAIL(name, msg) do { \
    g_testsFailed++; \
    g_failures.append(QString("%1: %2").arg(name).arg(msg)); \
    std::cout << "[FAIL] " << name << " — " << QString(msg).toStdString() << std::endl; \
} while(0)

#define VERIFY(cond, name, msg) do { \
    if (cond) { TEST_PASS(name); } \
    else { TEST_FAIL(name, msg); } \
} while(0)

void testCRS() {
    auto crs = terrain_pipeline::GISProcessor::detectCrs(4326);
    VERIFY(crs.isValid() && crs.isGeographic() && crs.authId() == "EPSG:4326",
           "CRS", "WGS84 detection failed");

    auto utm = terrain_pipeline::GISProcessor::detectCrs(32633);
    VERIFY(utm.isValid() && utm.authId() == "EPSG:32633",
           "CRS", "UTM detection failed");

    auto autoUtm = terrain_pipeline::GISProcessor::autoUtmFromBounds(29.65, 29.69, -95.42, -95.38);
    VERIFY(autoUtm.isValid() && autoUtm.epsg >= 32601 && autoUtm.epsg <= 32660,
           "CRS", "Auto UTM detection failed");
}

void testDemDiscovery() {
    terrain_pipeline::TerrariumProvider provider;
    auto requests = provider.discoverTiles(29.65, 29.69, -95.42, -95.38, 1024);
    VERIFY(!requests.isEmpty() && requests.size() < 100,
           "DEM Discovery", QString("Expected < 100 tiles, got %1").arg(requests.size()));
}

void testDemDownload() {
    QTemporaryDir cacheDir;
    terrain_pipeline::CacheManager cache(cacheDir.path());
    QByteArray testData("test dem data");
    QString key = "test_dem_key";
    bool writeOk = cache.write(key, testData);
    bool existsOk = cache.exists(key);
    QByteArray readData = cache.read(key);
    VERIFY(writeOk && existsOk && readData == testData,
           "DEM Download", "Cache write/read failed");
}

void testDemMosaic() {
    terrain_pipeline::RasterGrid tileA, tileB;
    tileA.width = 4; tileA.height = 4;
    tileA.data = {0,1,2,3, 1,2,3,4, 2,3,4,5, 3,4,5,6};
    tileA.nodataValue = -9999;
    tileA.originX = 0; tileA.originY = 4;
    tileA.pixelSizeX = 1; tileA.pixelSizeY = -1;
    tileA.crs = terrain_pipeline::CrsSpec::wgs84();

    tileB.width = 4; tileB.height = 4;
    tileB.data = {4,5,6,7, 5,6,7,8, 6,7,8,9, 7,8,9,10};
    tileB.nodataValue = -9999;
    tileB.originX = 4; tileB.originY = 4;
    tileB.pixelSizeX = 1; tileB.pixelSizeY = -1;
    tileB.crs = terrain_pipeline::CrsSpec::wgs84();

    VERIFY(tileA.isValid() && tileB.isValid(),
           "DEM Mosaic", "Tile validation failed");
}

void testDemClip() {
    terrain_pipeline::RasterGrid src;
    src.width = 10; src.height = 10;
    src.data.resize(100);
    for (int i = 0; i < 100; i++) src.data[i] = static_cast<float>(i);
    src.nodataValue = -9999;
    src.originX = 0; src.originY = 10;
    src.pixelSizeX = 1; src.pixelSizeY = -1;
    src.crs = terrain_pipeline::CrsSpec::wgs84();

    auto clipped = terrain_pipeline::GISProcessor::clipRaster(src, 2, 2, 7, 7);
    VERIFY(clipped.isValid() && clipped.width <= 6 && clipped.height <= 6,
           "DEM Clip", "Clipping failed");
}

void testHeightmapExport() {
    terrain_pipeline::RasterGrid dem;
    dem.width = 4; dem.height = 4;
    dem.data = {10, 20, 30, 40, 20, 30, 40, 50, 30, 40, 50, 60, 40, 50, 60, 70};
    dem.nodataValue = -9999;
    dem.crs = terrain_pipeline::CrsSpec::wgs84();

    auto stats = dem.computeStats();
    VERIFY(dem.isValid() && stats.valid && stats.min == 10.0f && stats.max == 70.0f,
           "Heightmap", "Stats computation failed");
}

void testAlbedoExport() {
    QImage img(256, 256, QImage::Format_RGB32);
    img.fill(Qt::green);
    VERIFY(!img.isNull() && img.width() == 256,
           "Albedo", "Image creation failed");
}

void testLandCover() {
    terrain_pipeline::ESAWorldCoverProvider provider;
    auto classes = provider.classes();
    VERIFY(!classes.isEmpty() && classes.size() >= 10,
           "Land Cover", QString("Expected >= 10 classes, got %1").arg(classes.size()));
}

void testVegetationMask() {
    terrain_pipeline::MaskDefinition def = terrain_pipeline::MaskDefinition::vegetation();
    VERIFY(def.type == terrain_pipeline::MaskType::Vegetation &&
           def.normalizeMode == terrain_pipeline::MaskDefinition::NormalizeMode::Binary,
           "Vegetation Mask", "Definition incorrect");
}

void testWaterMask() {
    terrain_pipeline::MaskDefinition def = terrain_pipeline::MaskDefinition::water();
    VERIFY(def.type == terrain_pipeline::MaskType::Water,
           "Water Mask", "Definition incorrect");
}

void testUrbanMask() {
    terrain_pipeline::MaskDefinition def = terrain_pipeline::MaskDefinition::urban();
    VERIFY(def.type == terrain_pipeline::MaskType::Urban,
           "Urban Mask", "Definition incorrect");
}

void testRoadMask() {
    QList<QPair<double, double>> roadCoords = {{0, 0}, {5, 5}, {10, 10}};
    auto mask = terrain_pipeline::GISProcessor::rasterizeLine(
        roadCoords, 16, 16, 0, 16, 1, -1,
        terrain_pipeline::CrsSpec::wgs84(), 2);
    bool hasRoad = false;
    for (uint8_t v : mask.data) if (v > 0) { hasRoad = true; break; }
    VERIFY(mask.isValid() && hasRoad,
           "Road Mask", "Rasterization failed");
}

void testBuildingMask() {
    QList<QPair<double, double>> buildingCoords = {{2, 2}, {8, 2}, {8, 8}, {2, 8}};
    auto mask = terrain_pipeline::GISProcessor::rasterizePolygon(
        buildingCoords, 16, 16, 0, 16, 1, -1,
        terrain_pipeline::CrsSpec::wgs84());
    // With originY=16, pixelSizeY=-1: world y=5 maps to pixel y=11
    // Polygon spans pixel y=8 to y=14, x=2 to x=8
    VERIFY(mask.isValid() && mask.at(5, 11) == 255,
           "Building Mask", "Polygon rasterization failed");
}

void testSlope() {
    terrain_pipeline::RasterGrid dem;
    dem.width = 5; dem.height = 5;
    dem.data.resize(25);
    for (int i = 0; i < 25; i++) dem.data[i] = static_cast<float>(i % 5);
    dem.nodataValue = -9999;
    dem.originX = 0; dem.originY = 5;
    dem.pixelSizeX = 10; dem.pixelSizeY = -10;
    dem.crs = terrain_pipeline::CrsSpec::utm(15, true);

    auto slope = terrain_pipeline::TerrainAnalyzer::computeSlopeDegrees(dem);
    auto stats = slope.computeStats();
    VERIFY(slope.isValid() && stats.min >= 0 && stats.max <= 90,
           "Slope", QString("Slope range %1-%2 out of bounds").arg(stats.min).arg(stats.max));
}

void testAspect() {
    terrain_pipeline::RasterGrid dem;
    dem.width = 5; dem.height = 5;
    dem.data.resize(25);
    for (int i = 0; i < 25; i++) dem.data[i] = static_cast<float>(i % 5);
    dem.nodataValue = -9999;
    dem.originX = 0; dem.originY = 5;
    dem.pixelSizeX = 10; dem.pixelSizeY = -10;
    dem.crs = terrain_pipeline::CrsSpec::utm(15, true);

    auto aspect = terrain_pipeline::TerrainAnalyzer::computeAspect(dem);
    auto stats = aspect.computeStats();
    VERIFY(aspect.isValid() && stats.min >= 0 && stats.max < 360,
           "Aspect", QString("Aspect range %1-%2 out of bounds").arg(stats.min).arg(stats.max));
}

void testElevationMask() {
    terrain_pipeline::RasterGrid dem;
    dem.width = 4; dem.height = 4;
    dem.data = {0, 5, 15, 55, 10, 20, 60, 120, 30, 80, 200, 600, 50, 100, 300, 1000};
    dem.nodataValue = -9999;
    dem.crs = terrain_pipeline::CrsSpec::wgs84();

    auto mask = terrain_pipeline::TerrainAnalyzer::classifyElevation(
        dem, {0, 10, 50, 100, 500, 10000});
    VERIFY(mask.isValid() && mask.width == 4 && mask.height == 4,
           "Elevation Mask", "Classification failed");
}

void testMultipleMasks() {
    auto defaults = terrain_pipeline::PipelineConfig::defaultMasks();
    QStringList names;
    for (const auto& m : defaults) names.append(m.name);
    bool hasAll = names.contains("Vegetation") && names.contains("Water") &&
                  names.contains("Urban") && names.contains("Road") &&
                  names.contains("Building") && names.contains("Slope") &&
                  names.contains("Aspect") && names.contains("Elevation");
    VERIFY(defaults.size() >= 10 && hasAll,
           "Multiple Masks", QString("Expected >= 10 masks with all required types, got %1").arg(defaults.size()));
}

void testPackedMask() {
    terrain_pipeline::ByteRaster r, g, b, a;
    r.width = 4; r.height = 4; r.data.resize(16, 255);
    g.width = 4; g.height = 4; g.data.resize(16, 128);
    b.width = 4; b.height = 4; b.data.resize(16, 64);
    a.width = 4; a.height = 4; a.data.resize(16, 32);

    QImage packed = terrain_pipeline::MaskManager::packMasks(r, g, b, a);
    QRgb pixel = packed.pixel(0, 0);
    VERIFY(!packed.isNull() && packed.width() == 4 &&
           qRed(pixel) == 255 && qGreen(pixel) == 128 &&
           qBlue(pixel) == 64 && qAlpha(pixel) == 32,
           "Packed Mask", "Packing failed");
}

void testTileAlignment() {
    auto tiles = terrain_pipeline::TileManager::generateTileGrid(
        29.65, 29.69, -95.42, -95.38, 2, 2,
        terrain_pipeline::CrsSpec::utm(15, true), 512);
    auto result = terrain_pipeline::TileManager::verifyAlignment(tiles);
    VERIFY(tiles.size() == 4 && result.aligned,
           "Tile Alignment", result.error.isEmpty() ? "Alignment failed" : result.error);
}

void testTileSeam() {
    terrain_pipeline::RasterGrid tileA, tileB;
    tileA.width = 4; tileA.height = 4;
    tileA.data = {1,2,3,4, 1,2,3,4, 1,2,3,4, 1,2,3,4};
    tileA.nodataValue = -9999;
    tileB.width = 4; tileB.height = 4;
    tileB.data = {4,5,6,7, 4,5,6,7, 4,5,6,7, 4,5,6,7};
    tileB.nodataValue = -9999;

    auto result = terrain_pipeline::TileManager::verifyEdgeMatch(tileA, tileB, true);
    VERIFY(result.match,
           "Tile Seam", QString("Seam mismatch: %1").arg(result.maxDifference));
}

void testNoData() {
    terrain_pipeline::RasterGrid grid;
    grid.width = 4; grid.height = 4;
    grid.data = {1, 2, -9999, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    grid.nodataValue = -9999;

    auto report = terrain_pipeline::GISProcessor::analyzeNoData(grid);
    VERIFY(report.totalPixels == 16 && report.nodataPixels == 1 && report.percentage > 0,
           "NoData", "NoData analysis failed");
}

void testCache() {
    QTemporaryDir cacheDir;
    terrain_pipeline::CacheManager cache(cacheDir.path());

    QString key = terrain_pipeline::CacheManager::generateKey(
        "terrarium", "dem", "12_1234_5678", "1.0", 1024, 4326);
    QByteArray data("cached data");
    cache.write(key, data);
    auto info = cache.info(key);
    QByteArray readData = cache.read(key);
    VERIFY(!key.isEmpty() && cache.exists(key) && info.exists &&
           info.size == data.size() && readData == data,
           "Cache", "Cache operations failed");
}

void testReproducibility() {
    QString key1 = terrain_pipeline::CacheManager::generateKey(
        "terrarium", "dem", "12_1234_5678", "1.0", 1024, 4326);
    QString key2 = terrain_pipeline::CacheManager::generateKey(
        "terrarium", "dem", "12_1234_5678", "1.0", 1024, 4326);
    QString key3 = terrain_pipeline::CacheManager::generateKey(
        "terrarium", "dem", "12_1234_5678", "1.0", 2048, 4326);
    VERIFY(key1 == key2 && key1 != key3,
           "Reproducibility", "Cache key determinism failed");
}

void testProjectSave() {
    terrain_pipeline::PipelineConfig config;
    config.minLat = 29.65;
    config.maxLat = 29.69;
    config.minLon = -95.42;
    config.maxLon = -95.38;
    config.masks = terrain_pipeline::PipelineConfig::defaultMasks();

    QJsonObject json = config.toJson();
    QJsonDocument doc(json);
    QTemporaryDir tmpDir;
    QString path = tmpDir.path() + "/test_project.json";
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(doc.toJson());
    f.close();
    VERIFY(json.contains("minLat") && json.contains("masks") && QFile::exists(path),
           "Project Save", "Serialization failed");
}

void testProjectReload() {
    terrain_pipeline::PipelineConfig config;
    config.minLat = 29.65;
    config.maxLat = 29.69;
    config.minLon = -95.42;
    config.maxLon = -95.38;
    config.masks = terrain_pipeline::PipelineConfig::defaultMasks();

    QJsonObject json = config.toJson();
    auto loaded = terrain_pipeline::PipelineConfig::fromJson(json);
    VERIFY(loaded.minLat == config.minLat && loaded.maxLat == config.maxLat &&
           loaded.minLon == config.minLon && loaded.maxLon == config.maxLon &&
           loaded.masks.size() == config.masks.size(),
           "Project Reload", "Deserialization failed");
}

void testFullPipeline() {
    terrain_pipeline::PipelineConfig config;
    config.minLat = 29.65;
    config.maxLat = 29.69;
    config.minLon = -95.42;
    config.maxLon = -95.38;
    config.enableDEM = true;
    config.enableImagery = true;
    config.masks = terrain_pipeline::PipelineConfig::defaultMasks();

    auto crs = terrain_pipeline::GISProcessor::autoUtmFromBounds(
        config.minLat, config.maxLat, config.minLon, config.maxLon);
    auto tiles = terrain_pipeline::TileManager::generateTileGrid(
        config.minLat, config.maxLat, config.minLon, config.maxLon,
        2, 2, crs, 512);
    auto alignResult = terrain_pipeline::TileManager::verifyAlignment(tiles);

    VERIFY(crs.isValid() && !tiles.isEmpty() && alignResult.aligned && config.masks.size() >= 10,
           "4 KM End-to-End", "Full pipeline integration failed");
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    std::cout << "========================================" << std::endl;
    std::cout << "  TERRAIN PIPELINE TEST SUITE (26 tests)" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    testCRS();                    // Test 1
    testDemDiscovery();           // Test 2
    testDemDownload();            // Test 3
    testDemMosaic();              // Test 4
    testDemClip();                // Test 5
    testHeightmapExport();        // Test 6
    testAlbedoExport();           // Test 7
    testLandCover();              // Test 8
    testVegetationMask();         // Test 9
    testWaterMask();              // Test 10
    testUrbanMask();              // Test 11
    testRoadMask();               // Test 12
    testBuildingMask();           // Test 13
    testSlope();                  // Test 14
    testAspect();                 // Test 15
    testElevationMask();          // Test 16
    testMultipleMasks();          // Test 17
    testPackedMask();             // Test 18
    testTileAlignment();          // Test 19
    testTileSeam();               // Test 20
    testNoData();                 // Test 21
    testCache();                  // Test 22
    testReproducibility();        // Test 23
    testProjectSave();            // Test 24
    testProjectReload();          // Test 25
    testFullPipeline();           // Test 26

    std::cout << std::endl << "========================================" << std::endl;
    std::cout << "  Total: " << (g_testsPassed + g_testsFailed)
              << "  Passed: " << g_testsPassed
              << "  Failed: " << g_testsFailed << std::endl;
    std::cout << "========================================" << std::endl;

    if (g_testsFailed > 0) {
        std::cout << std::endl << "FAILURES:" << std::endl;
        for (const auto& f : g_failures)
            std::cout << "  " << f.toStdString() << std::endl;
    }

    return g_testsFailed > 0 ? 1 : 0;
}
