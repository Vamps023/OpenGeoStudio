#pragma once

// ============================================================
// TerrainManager — Main terrain pipeline orchestrator
// ============================================================
//
// Orchestrates the full terrain data pipeline:
//   1. Area Selection
//   2. CRS Resolution
//   3. Source Discovery
//   4. Download
//   5. Source Validation
//   6. DEM Processing
//   7. Imagery Processing
//   8. Land-Cover Processing
//   9. Vector Processing
//  10. Terrain-Derived Analysis
//  11. Mask Generation
//  12. Mask Alignment
//  13. Tile Generation
//  14. Export
//  15. Validation
//  16. Project Save
//

#include "TerrainPipelineTypes.hpp"
#include "GISProcessor.hpp"
#include "TerrainAnalyzer.hpp"
#include "TileManager.hpp"
#include "CacheManager.hpp"
#include "DownloadManager.hpp"
#include "masks/MaskManager.hpp"
#include "providers/TerrainDataProvider.hpp"
#include "providers/DemProviders.hpp"
#include "providers/ImageryProviders.hpp"
#include "providers/VectorProviders.hpp"
#include "../../ui/terrain/RasterWriter.hpp"
#include "../../ui/terrain/DemDecoder.hpp"

#include <QObject>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <memory>
#include <functional>

namespace terrain_pipeline {

class TerrainManager : public QObject {
    Q_OBJECT

public:
    explicit TerrainManager(QObject* parent = nullptr)
        : QObject(parent) {
        // Default cache directory
        QString cacheDir = QStandardPaths::writableLocation(
            QStandardPaths::CacheLocation) + "/terrain";
        m_cache = std::make_unique<CacheManager>(cacheDir);
        m_downloader = std::make_unique<DownloadManager>(m_cache.get(), this);

        connect(m_downloader.get(), &DownloadManager::downloadProgress,
                this, &TerrainManager::onDownloadProgress);
        connect(m_downloader.get(), &DownloadManager::stageMessage,
                this, &TerrainManager::onStageMessage);
    }

    // ============================================================
    // Run the full pipeline
    // ============================================================

    void runPipeline(const PipelineConfig& config) {
        m_config = config;
        m_results.clear();
        m_tiles.clear();
        m_demTiles.clear();
        m_albedoTiles.clear();
        m_maskTiles.clear();

        emit progress(0, "Starting terrain pipeline...");

        // Stage 1: Area Selection
        emit progress(5, "Stage 1: Area Selection");
        if (config.minLat >= config.maxLat || config.minLon >= config.maxLon) {
            addResult("AreaSelection", StageStatus::Failed,
                      "Invalid bounding box");
            emit finished(false, "Invalid area");
            return;
        }
        addResult("AreaSelection", StageStatus::Success,
                  QString("Area: %1,%2 to %3,%4")
                      .arg(config.minLat).arg(config.minLon)
                      .arg(config.maxLat).arg(config.maxLon));

        // Stage 2: CRS Resolution
        emit progress(10, "Stage 2: CRS Resolution");
        CrsSpec targetCrs = config.targetCrs;
        if (targetCrs.epsg == 0 || targetCrs.epsg == 4326) {
            targetCrs = GISProcessor::autoUtmFromBounds(
                config.minLat, config.maxLat, config.minLon, config.maxLon);
        }
        if (!GISProcessor::validateCrs(targetCrs)) {
            addResult("CRSResolution", StageStatus::Failed, "Invalid CRS");
            emit finished(false, "CRS validation failed");
            return;
        }
        m_config.targetCrs = targetCrs;
        addResult("CRSResolution", StageStatus::Success,
                  QString("Target CRS: %1").arg(targetCrs.authId()));

        // Generate tile grid
        m_tiles = TileManager::generateTileGrid(
            config.minLat, config.maxLat, config.minLon, config.maxLon,
            config.tileRows, config.tileCols, targetCrs, config.tileSize);
        addResult("TileGridGeneration", StageStatus::Success,
                  QString("Generated %1 tiles (%2x%3)")
                      .arg(m_tiles.size()).arg(config.tileRows).arg(config.tileCols));

        // Stage 3-5: Download and process DEM
        if (config.enableDEM) {
            emit progress(15, "Stage 3: DEM Discovery & Download");
            processDEM();
        }

        // Stage 6-7: Download and process imagery
        if (config.enableImagery) {
            emit progress(40, "Stage 6: Imagery Discovery & Download");
            processImagery();
        }

        // Stage 8: Land-cover
        if (config.enableLandCover) {
            emit progress(55, "Stage 8: Land-Cover Processing");
            processLandCover();
        }

        // Stage 9: Vector data (roads, water, buildings)
        if (config.enableRoads || config.enableWater || config.enableBuildings) {
            emit progress(60, "Stage 9: Vector Processing");
            processVectorData();
        }

        // Stage 10-11: Terrain-derived analysis and mask generation
        emit progress(70, "Stage 10-11: Mask Generation");
        processMasks();

        // Stage 12-13: Tile generation and export
        emit progress(85, "Stage 12-13: Tile Generation & Export");
        exportTiles();

        // Stage 14: Validation
        emit progress(95, "Stage 14: Validation");
        validatePipeline();

        // Stage 15: Save project state
        emit progress(100, "Pipeline complete");
        emit finished(true, "Pipeline completed successfully");
    }

    // ============================================================
    // Get pipeline results
    // ============================================================

    const QList<StageResult>& results() const { return m_results; }
    const QList<TileInfo>& tiles() const { return m_tiles; }
    const PipelineConfig& config() const { return m_config; }

    // Get the full pipeline state as JSON (for project save)
    QJsonObject toJson() const {
        QJsonObject j;
        j["config"] = m_config.toJson();

        QJsonArray resultsArr;
        for (const auto& r : m_results) resultsArr.append(r.toJson());
        j["results"] = resultsArr;

        QJsonArray tilesArr;
        for (const auto& t : m_tiles) tilesArr.append(t.toJson());
        j["tiles"] = tilesArr;

        j["cacheDir"] = m_cache->cacheDir();
        return j;
    }

    void fromJson(const QJsonObject& j) {
        m_config = PipelineConfig::fromJson(j["config"].toObject());

        m_results.clear();
        QJsonArray resultsArr = j["results"].toArray();
        for (const auto& v : resultsArr) {
            StageResult r;
            auto obj = v.toObject();
            r.stageName = obj["stage"].toString();
            r.status = static_cast<StageStatus>(obj["status"].toInt());
            r.message = obj["message"].toString();
            m_results.append(r);
        }

        m_tiles.clear();
        QJsonArray tilesArr = j["tiles"].toArray();
        for (const auto& v : tilesArr) {
            auto obj = v.toObject();
            TileInfo t;
            t.row = obj["row"].toInt();
            t.col = obj["col"].toInt();
            t.west = obj["west"].toDouble();
            t.east = obj["east"].toDouble();
            t.north = obj["north"].toDouble();
            t.south = obj["south"].toDouble();
            t.width = obj["width"].toInt();
            t.height = obj["height"].toInt();
            t.crs = CrsSpec::fromJson(obj["crs"].toObject());
            t.resolution = obj["resolution"].toDouble();
            m_tiles.append(t);
        }
    }

signals:
    void progress(int percent, const QString& stage);
    void finished(bool success, const QString& message);
    void stageResult(const StageResult& result);

private:
    PipelineConfig m_config;
    QList<StageResult> m_results;
    QList<TileInfo> m_tiles;
    std::unique_ptr<CacheManager> m_cache;
    std::unique_ptr<DownloadManager> m_downloader;

    // Processed data
    RasterGrid m_fullDEM;
    RasterGrid m_fullLandCover;
    QImage m_fullImagery;
    QList<RoadProvider::RoadSegment> m_roads;
    QList<WaterProvider::WaterBody> m_waterBodies;
    QList<BuildingProvider::BuildingFootprint> m_buildings;

    // Per-tile data
    QMap<QString, RasterGrid> m_demTiles;
    QMap<QString, QImage> m_albedoTiles;
    QMap<QString, QMap<QString, ByteRaster>> m_maskTiles;  // tileId -> maskName -> mask

    void addResult(const QString& stage, StageStatus status, const QString& msg) {
        StageResult r;
        r.stageName = stage;
        r.status = status;
        r.message = msg;
        m_results.append(r);
        emit stageResult(r);
    }

    // ============================================================
    // DEM Processing
    // ============================================================

    void processDEM() {
        std::unique_ptr<DemProvider> provider = createDemProvider(m_config);
        if (!provider || !provider->isAvailable()) {
            addResult("DEMDownload", StageStatus::Failed, "DEM provider not available");
            return;
        }

        // Discover required tiles
        auto requests = provider->discoverTiles(
            m_config.minLat, m_config.maxLat,
            m_config.minLon, m_config.maxLon,
            m_config.heightmapResolution);

        addResult("DEMDiscovery", StageStatus::Success,
                  QString("Discovered %1 DEM tiles").arg(requests.size()));

        if (requests.isEmpty()) {
            addResult("DEMDownload", StageStatus::Failed, "No DEM tiles to download");
            return;
        }

        // Download all tiles
        auto results = m_downloader->downloadAll(requests);

        int successCount = 0;
        QList<RasterGrid> demTiles;
        for (int i = 0; i < results.size(); i++) {
            if (results[i].success) {
                RasterGrid grid = provider->decodeElevation(results[i].data);
                if (grid.isValid()) {
                    demTiles.append(grid);
                    successCount++;
                }
            }
        }

        addResult("DEMDownload", StageStatus::Success,
                  QString("Downloaded %1/%2 DEM tiles").arg(successCount).arg(requests.size()));

        if (demTiles.isEmpty()) {
            addResult("DEMMosaic", StageStatus::Failed, "No valid DEM tiles");
            return;
        }

        // Merge tiles into a single raster
        m_fullDEM = mergeDemTiles(demTiles, m_config);
        if (!m_fullDEM.isValid()) {
            addResult("DEMMosaic", StageStatus::Failed, "Failed to merge DEM tiles");
            return;
        }

        // Reproject to target CRS
        auto stats = m_fullDEM.computeStats();
        addResult("DEMMosaic", StageStatus::Success,
                  QString("Merged DEM: %1x%2, elev %3-%4m")
                      .arg(m_fullDEM.width).arg(m_fullDEM.height)
                      .arg(stats.min).arg(stats.max));

        // Clip to requested area
        GISProcessor::BoundingBox srcBounds{
            m_config.minLon, m_config.minLat, m_config.maxLon, m_config.maxLat};
        auto dstBounds = GISProcessor::transformBounds(srcBounds,
            CrsSpec::wgs84(), m_config.targetCrs);

        auto clipped = GISProcessor::clipRaster(m_fullDEM,
            dstBounds.minX, dstBounds.minY, dstBounds.maxX, dstBounds.maxY);
        if (clipped.isValid()) m_fullDEM = clipped;

        // Resample to target resolution
        m_fullDEM = GISProcessor::resample(m_fullDEM,
            m_config.heightmapResolution, m_config.heightmapResolution);

        // Fill NoData
        GISProcessor::fillNoData(m_fullDEM);

        auto finalStats = m_fullDEM.computeStats();
        addResult("DEMProcessing", StageStatus::Success,
                  QString("Final DEM: %1x%2, elev %3-%4m, CRS %5")
                      .arg(m_fullDEM.width).arg(m_fullDEM.height)
                      .arg(finalStats.min).arg(finalStats.max)
                      .arg(m_fullDEM.crs.authId()));
    }

    // ============================================================
    // Imagery Processing
    // ============================================================

    void processImagery() {
        std::unique_ptr<ImageryProvider> provider = createImageryProvider(m_config);
        if (!provider || !provider->isAvailable()) {
            addResult("ImageryDownload", StageStatus::Failed,
                      "Imagery provider not available");
            return;
        }

        auto requests = provider->discoverTiles(
            m_config.minLat, m_config.maxLat,
            m_config.minLon, m_config.maxLon,
            m_config.albedoResolution);

        addResult("ImageryDiscovery", StageStatus::Success,
                  QString("Discovered %1 imagery tiles").arg(requests.size()));

        if (requests.isEmpty()) {
            addResult("ImageryDownload", StageStatus::Failed, "No imagery tiles");
            return;
        }

        auto results = m_downloader->downloadAll(requests);

        int successCount = 0;
        QList<QImage> images;
        for (const auto& r : results) {
            if (r.success && !r.data.isEmpty()) {
                QImage img;
                img.loadFromData(r.data);
                if (!img.isNull()) {
                    images.append(img);
                    successCount++;
                }
            }
        }

        addResult("ImageryDownload", StageStatus::Success,
                  QString("Downloaded %1/%2 imagery tiles")
                      .arg(successCount).arg(requests.size()));

        if (images.isEmpty()) {
            addResult("ImageryProcessing", StageStatus::Failed, "No valid imagery");
            return;
        }

        // Mosaic images (simplified: use first image scaled to target)
        m_fullImagery = QImage(m_config.albedoResolution, m_config.albedoResolution,
                               QImage::Format_RGB32);
        m_fullImagery.fill(Qt::black);

        QPainter painter(&m_fullImagery);
        if (images.size() == 1) {
            painter.drawImage(m_fullImagery.rect(), images[0]);
        } else {
            // Simple grid mosaic
            int cols = static_cast<int>(std::ceil(std::sqrt(images.size())));
            int rows = static_cast<int>(std::ceil(static_cast<double>(images.size()) / cols));
            int tileW = m_config.albedoResolution / cols;
            int tileH = m_config.albedoResolution / rows;
            for (int i = 0; i < images.size() && i < rows * cols; i++) {
                int r = i / cols, c = i % cols;
                QRect dest(c * tileW, r * tileH, tileW, tileH);
                painter.drawImage(dest, images[i].scaled(tileW, tileH,
                    Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            }
        }
        painter.end();

        addResult("ImageryProcessing", StageStatus::Success,
                  QString("Mosaicked imagery: %1x%2")
                      .arg(m_fullImagery.width()).arg(m_fullImagery.height()));
    }

    // ============================================================
    // Land-Cover Processing
    // ============================================================

    void processLandCover() {
        ESAWorldCoverProvider provider;
        auto requests = provider.discoverTiles(
            m_config.minLat, m_config.maxLat,
            m_config.minLon, m_config.maxLon, 1024);

        addResult("LandCoverDiscovery", StageStatus::Success,
                  QString("Discovered %1 land-cover tiles").arg(requests.size()));

        if (requests.isEmpty()) {
            addResult("LandCoverDownload", StageStatus::Skipped, "No land-cover tiles");
            return;
        }

        auto results = m_downloader->downloadAll(requests);

        // Decode land-cover data
        for (const auto& r : results) {
            if (r.success && !r.data.isEmpty()) {
                terrain::DemTile dt = terrain::DemDecoder::decodeGeoTiff(r.data);
                if (dt.valid) {
                    RasterGrid grid;
                    grid.width = dt.width;
                    grid.height = dt.height;
                    grid.data = std::move(dt.elevations);
                    grid.nodataValue = dt.nodataValue;
                    // Merge into full land-cover
                    // Simplified: use last valid tile
                    m_fullLandCover = grid;
                }
            }
        }

        if (m_fullLandCover.isValid()) {
            addResult("LandCoverProcessing", StageStatus::Success,
                      QString("Land-cover: %1x%2")
                          .arg(m_fullLandCover.width).arg(m_fullLandCover.height));
        } else {
            addResult("LandCoverProcessing", StageStatus::Warning,
                      "No valid land-cover data");
        }
    }

    // ============================================================
    // Vector Data Processing
    // ============================================================

    void processVectorData() {
        if (m_config.enableRoads) {
            OSMRoadProvider provider;
            auto requests = provider.discoverTiles(
                m_config.minLat, m_config.maxLat,
                m_config.minLon, m_config.maxLon, 0);
            if (!requests.isEmpty()) {
                auto results = m_downloader->downloadAll(requests);
                for (const auto& r : results) {
                    if (r.success && !r.data.isEmpty()) {
                        auto roads = provider.parseRoads(r.data);
                        m_roads.append(roads);
                    }
                }
            }
            addResult("RoadDownload", StageStatus::Success,
                      QString("Downloaded %1 road segments").arg(m_roads.size()));
        }

        if (m_config.enableWater) {
            OSMWaterProvider provider;
            auto requests = provider.discoverTiles(
                m_config.minLat, m_config.maxLat,
                m_config.minLon, m_config.maxLon, 0);
            if (!requests.isEmpty()) {
                auto results = m_downloader->downloadAll(requests);
                for (const auto& r : results) {
                    if (r.success && !r.data.isEmpty()) {
                        auto water = provider.parseWater(r.data);
                        m_waterBodies.append(water);
                    }
                }
            }
            addResult("WaterDownload", StageStatus::Success,
                      QString("Downloaded %1 water bodies").arg(m_waterBodies.size()));
        }

        if (m_config.enableBuildings) {
            OSMBuildingProvider provider;
            auto requests = provider.discoverTiles(
                m_config.minLat, m_config.maxLat,
                m_config.minLon, m_config.maxLon, 0);
            if (!requests.isEmpty()) {
                auto results = m_downloader->downloadAll(requests);
                for (const auto& r : results) {
                    if (r.success && !r.data.isEmpty()) {
                        auto buildings = provider.parseBuildings(r.data);
                        m_buildings.append(buildings);
                    }
                }
            }
            addResult("BuildingDownload", StageStatus::Success,
                      QString("Downloaded %1 building footprints").arg(m_buildings.size()));
        }
    }

    // ============================================================
    // Mask Generation
    // ============================================================

    void processMasks() {
        if (!m_fullDEM.isValid()) {
            addResult("MaskGeneration", StageStatus::Skipped, "No DEM for mask generation");
            return;
        }

        int maskCount = 0;
        for (const auto& maskDef : m_config.masks) {
            if (!maskDef.enabled) continue;

            ByteRaster mask = MaskManager::generateMask(
                maskDef, m_fullDEM, m_fullLandCover,
                m_roads, m_waterBodies, m_buildings);

            if (mask.isValid()) {
                mask = MaskManager::normalizeMask(mask, maskDef.normalizeMode);
                m_maskTiles["full"][maskDef.id] = mask;
                maskCount++;
            }
        }

        addResult("MaskGeneration", StageStatus::Success,
                  QString("Generated %1 masks").arg(maskCount));

        // Generate packed mask if enabled
        if (m_config.exportPackedMask) {
            generatePackedMask();
        }
    }

    void generatePackedMask() {
        auto getMask = [&](const QString& name) -> ByteRaster {
            auto it = m_maskTiles["full"].find(name);
            return (it != m_maskTiles["full"].end()) ? it.value() : ByteRaster();
        };

        auto r = getMask(m_config.packedMaskR);
        auto g = getMask(m_config.packedMaskG);
        auto b = getMask(m_config.packedMaskB);
        auto a = getMask(m_config.packedMaskA);

        QImage packed = MaskManager::packMasks(r, g, b, a);
        if (!packed.isNull()) {
            QString path = m_config.exportDir + "/Masks/packed_mask.png";
            QDir().mkpath(QFileInfo(path).absolutePath());
            packed.save(path);
            addResult("PackedMask", StageStatus::Success,
                      QString("Packed mask saved: %1").arg(path));
        }
    }

    // ============================================================
    // Tile Export
    // ============================================================

    void exportTiles() {
        if (m_config.exportDir.isEmpty()) {
            addResult("TileExport", StageStatus::Skipped, "No export directory");
            return;
        }

        QDir().mkpath(m_config.exportDir);
        QDir().mkpath(m_config.exportDir + "/Height");
        QDir().mkpath(m_config.exportDir + "/Albedo");
        QDir().mkpath(m_config.exportDir + "/Masks");

        for (const auto& tile : m_tiles) {
            QString tileId = tile.id();

            // Export heightmap
            if (m_fullDEM.isValid()) {
                auto tileDEM = TileManager::resampleToTile(m_fullDEM, tile);
                QString path = m_config.exportDir + "/Height/tile_" + tileId + "_heightmap.tif";
                exportDemTile(tileDEM, tile, path);
                m_demTiles[tileId] = tileDEM;
            }

            // Export albedo
            if (!m_fullImagery.isNull()) {
                QImage tileImg = m_fullImagery.scaled(tile.width, tile.height,
                    Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                QString path = m_config.exportDir + "/Albedo/tile_" + tileId + "_albedo.png";
                QDir().mkpath(QFileInfo(path).absolutePath());
                tileImg.save(path);
                m_albedoTiles[tileId] = tileImg;
            }

            // Export masks
            for (const auto& maskDef : m_config.masks) {
                if (!maskDef.enabled) continue;
                auto fullMaskIt = m_maskTiles["full"].find(maskDef.id);
                if (fullMaskIt == m_maskTiles["full"].end()) continue;

                // Clip mask to tile
                ByteRaster tileMask = clipMaskToTile(fullMaskIt.value(), tile);
                QString maskDir = m_config.exportDir + "/Masks/" + maskDef.name;
                QDir().mkpath(maskDir);
                QString path = maskDir + "/tile_" + tileId + ".png";
                MaskManager::toImage(tileMask).save(path);

                // Save metadata
                DatasetMetadata meta = MaskManager::generateMetadata(
                    maskDef, tileMask, tileId);
                QString metaPath = maskDir + "/tile_" + tileId + "_meta.json";
                QFile mf(metaPath);
                if (mf.open(QIODevice::WriteOnly)) {
                    QJsonDocument doc(meta.toJson());
                    mf.write(doc.toJson());
                    mf.close();
                }

                m_maskTiles[tileId][maskDef.id] = tileMask;
            }
        }

        // Write tile manifest
        writeManifest();

        addResult("TileExport", StageStatus::Success,
                  QString("Exported %1 tiles").arg(m_tiles.size()));
    }

    void exportDemTile(const RasterGrid& dem, const TileInfo& tile, const QString& path) {
        terrain::RasterExtent extent;
        extent.west = tile.west;
        extent.east = tile.east;
        extent.north = tile.north;
        extent.south = tile.south;

        if (tile.crs.epsg == 4326) {
            extent.crsMode = terrain::GeoCrsMode::WGS84;
        } else if (tile.crs.epsg == 3857) {
            extent.crsMode = terrain::GeoCrsMode::WebMercator;
        } else {
            extent.crsMode = terrain::GeoCrsMode::UTM;
            extent.utmEpsg = tile.crs.epsg;
        }

        switch (m_config.heightmapFormat) {
        case terrain::HeightmapFormat::GeoTIFF_Float32:
            RasterWriter::writeFloat32GeoTiff(path, dem.data, dem.width, dem.height,
                                               extent, dem.nodataValue);
            break;
        case terrain::HeightmapFormat::GeoTIFF_Int16: {
            std::vector<int16_t> intData(dem.data.size());
            for (size_t i = 0; i < dem.data.size(); i++)
                intData[i] = static_cast<int16_t>(dem.data[i]);
            RasterWriter::writeInt16GeoTiff(path, intData, dem.width, dem.height,
                                             extent, -9999);
            break;
        }
        case terrain::HeightmapFormat::GeoTIFF_UInt16: {
            // Normalize to 0-65535
            auto stats = dem.computeStats();
            std::vector<uint16_t> uintData(dem.data.size());
            float range = stats.max - stats.min;
            if (range > 0) {
                for (size_t i = 0; i < dem.data.size(); i++)
                    uintData[i] = static_cast<uint16_t>(
                        (dem.data[i] - stats.min) * 65535.0f / range);
            }
            RasterWriter::writeUInt16GeoTiff(path, uintData, dem.width, dem.height,
                                              extent, 0);
            break;
        }
        case terrain::HeightmapFormat::PNG16: {
            // Write 16-bit PNG
            auto stats = dem.computeStats();
            QImage img(dem.width, dem.height, QImage::Format_Grayscale16);
            float range = stats.max - stats.min;
            if (range > 0) {
                for (int y = 0; y < dem.height; y++) {
                    uint16_t* line = reinterpret_cast<uint16_t*>(img.scanLine(y));
                    for (int x = 0; x < dem.width; x++) {
                        line[x] = static_cast<uint16_t>(
                            (dem.at(x, y) - stats.min) * 65535.0f / range);
                    }
                }
            }
            img.save(path);
            break;
        }
        default:
            // Fallback to Float32
            RasterWriter::writeFloat32GeoTiff(path, dem.data, dem.width, dem.height,
                                               extent, dem.nodataValue);
            break;
        }
    }

    ByteRaster clipMaskToTile(const ByteRaster& mask, const TileInfo& tile) {
        ByteRaster result;
        if (!mask.isValid()) return result;

        // Calculate pixel range for this tile
        double pxMin = (tile.west - mask.originX) / mask.pixelSizeX;
        double pyMin = (tile.north - mask.originY) / mask.pixelSizeY;
        double pxMax = (tile.east - mask.originX) / mask.pixelSizeX;
        double pyMax = (tile.south - mask.originY) / mask.pixelSizeY;

        int x0 = std::max(0, static_cast<int>(std::floor(pxMin)));
        int y0 = std::max(0, static_cast<int>(std::floor(pyMin)));
        int x1 = std::min(mask.width, static_cast<int>(std::ceil(pxMax)));
        int y1 = std::min(mask.height, static_cast<int>(std::ceil(pyMax)));

        if (x1 <= x0 || y1 <= y0) {
            // Tile outside mask — create empty tile
            result.width = tile.width;
            result.height = tile.height;
            result.data.resize(tile.width * tile.height, 0);
            result.nodataValue = 0;
            result.originX = tile.west;
            result.originY = tile.north;
            result.pixelSizeX = (tile.east - tile.west) / tile.width;
            result.pixelSizeY = (tile.south - tile.north) / tile.height;
            result.crs = tile.crs;
            return result;
        }

        // Clip and resample
        result.width = tile.width;
        result.height = tile.height;
        result.data.resize(tile.width * tile.height, 0);
        result.nodataValue = 0;
        result.originX = tile.west;
        result.originY = tile.north;
        result.pixelSizeX = (tile.east - tile.west) / tile.width;
        result.pixelSizeY = (tile.south - tile.north) / tile.height;
        result.crs = tile.crs;

        for (int y = 0; y < tile.height; y++) {
            for (int x = 0; x < tile.width; x++) {
                double srcX = (x / static_cast<double>(tile.width)) * (x1 - x0) + x0;
                double srcY = (y / static_cast<double>(tile.height)) * (y1 - y0) + y0;
                int sx = std::min(static_cast<int>(srcX), mask.width - 1);
                int sy = std::min(static_cast<int>(srcY), mask.height - 1);
                result.data[y * tile.width + x] = mask.at(sx, sy);
            }
        }
        return result;
    }

    void writeManifest() {
        QJsonObject manifest;
        manifest["pipelineVersion"] = "1.0";
        manifest["generatedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

        QJsonObject configObj = m_config.toJson();
        manifest["config"] = configObj;

        QJsonArray tilesArr;
        for (const auto& t : m_tiles) tilesArr.append(t.toJson());
        manifest["tiles"] = tilesArr;

        QJsonArray resultsArr;
        for (const auto& r : m_results) resultsArr.append(r.toJson());
        manifest["stages"] = resultsArr;

        QString path = m_config.exportDir + "/manifest.json";
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            QJsonDocument doc(manifest);
            f.write(doc.toJson(QJsonDocument::Indented));
            f.close();
        }
    }

    // ============================================================
    // Validation
    // ============================================================

    void validatePipeline() {
        // Validate tile alignment
        auto alignResult = TileManager::verifyAlignment(m_tiles);
        if (alignResult.aligned) {
            addResult("TileAlignment", StageStatus::Success, "All tiles aligned");
        } else {
            addResult("TileAlignment", StageStatus::Failed, alignResult.error);
        }

        // Validate DEM
        if (m_fullDEM.isValid()) {
            auto stats = m_fullDEM.computeStats();
            if (stats.valid && stats.min > -500 && stats.max < 9000) {
                addResult("DEMValidation", StageStatus::Success,
                          QString("DEM valid: %1-%2m").arg(stats.min).arg(stats.max));
            } else {
                addResult("DEMValidation", StageStatus::Warning,
                          "DEM values out of expected range");
            }
        }

        // Validate NoData
        if (m_fullDEM.isValid()) {
            auto nodataReport = GISProcessor::analyzeNoData(m_fullDEM);
            if (nodataReport.nodataPixels == 0) {
                addResult("NoDataValidation", StageStatus::Success, "No NoData pixels");
            } else {
                addResult("NoDataValidation", StageStatus::Warning,
                          QString("%1 NoData pixels (%2%)")
                              .arg(nodataReport.nodataPixels)
                              .arg(nodataReport.percentage, 0, 'f', 2));
            }
        }

        // Validate masks
        int validMasks = 0;
        for (const auto& maskDef : m_config.masks) {
            if (maskDef.enabled && m_maskTiles["full"].contains(maskDef.id))
                validMasks++;
        }
        addResult("MaskValidation", StageStatus::Success,
                  QString("%1 masks generated").arg(validMasks));
    }

    // ============================================================
    // Provider Factory
    // ============================================================

    std::unique_ptr<DemProvider> createDemProvider(const PipelineConfig& config) {
        switch (config.demSource) {
        case terrain::DemSource::AWS_Terrarium:
        case terrain::DemSource::Mapzen_Terrarium:
            return std::make_unique<TerrariumProvider>();
        case terrain::DemSource::Mapbox_TerrainRGB:
            return std::make_unique<MapboxTerrainProvider>(config.mapboxToken);
        case terrain::DemSource::NASA_EarthData_Copernicus:
            return std::make_unique<CopernicusProvider>();
        case terrain::DemSource::OpenTopo_Copernicus_GLO30:
            return std::make_unique<OpenTopographyProvider>(config.openTopoApiKey, "COP30");
        case terrain::DemSource::OpenTopo_NASADEM:
            return std::make_unique<OpenTopographyProvider>(config.openTopoApiKey, "NASADEM");
        case terrain::DemSource::OpenTopo_SRTM_GL1:
            return std::make_unique<OpenTopographyProvider>(config.openTopoApiKey, "SRTMGL1");
        case terrain::DemSource::OpenTopo_SRTM_GL3:
            return std::make_unique<OpenTopographyProvider>(config.openTopoApiKey, "SRTMGL3");
        case terrain::DemSource::OpenTopo_ALOS_AW3D30:
            return std::make_unique<OpenTopographyProvider>(config.openTopoApiKey, "AW3D30");
        case terrain::DemSource::OpenTopo_USGS_3DEP:
            return std::make_unique<OpenTopographyProvider>(config.openTopoApiKey, "USGS10m");
        case terrain::DemSource::GPXZ_LiDAR:
            return std::make_unique<GPXZProvider>(config.gpxzApiKey);
        case terrain::DemSource::GLAD_SRTM:
            return std::make_unique<GLADSrtmProvider>();
        case terrain::DemSource::Local_File:
            return std::make_unique<LocalDemProvider>(
                config.exportDir);  // Would need localDemFilePath
        default:
            return std::make_unique<TerrariumProvider>();
        }
    }

    std::unique_ptr<ImageryProvider> createImageryProvider(const PipelineConfig& config) {
        switch (config.imagerySource) {
        case terrain::ImagerySource::Google_Satellite:
            return std::make_unique<GoogleSatelliteProvider>();
        case terrain::ImagerySource::ArcGIS_World_Imagery:
            return std::make_unique<ArcGisImageryProvider>();
        case terrain::ImagerySource::Mapbox_Satellite:
            return std::make_unique<MapboxImageryProvider>(config.mapboxToken);
        case terrain::ImagerySource::Local_File:
            return std::make_unique<LocalImageryProvider>(config.exportDir);
        default:
            return std::make_unique<ArcGisImageryProvider>();
        }
    }

    // ============================================================
    // DEM Tile Merging
    // ============================================================

    RasterGrid mergeDemTiles(const QList<RasterGrid>& tiles, const PipelineConfig& config) {
        if (tiles.isEmpty()) return RasterGrid();
        if (tiles.size() == 1) return tiles[0];

        // Find the bounding box of all tiles
        double minX = 1e30, minY = 1e30, maxX = -1e30, maxY = -1e30;
        for (const auto& t : tiles) {
            minX = std::min(minX, t.originX);
            minY = std::min(minY, t.originY + t.height * t.pixelSizeY);
            maxX = std::max(maxX, t.originX + t.width * t.pixelSizeX);
            maxY = std::max(maxY, t.originY);
        }

        // Use the pixel size from the first tile
        double pixelSize = std::abs(tiles[0].pixelSizeX);
        int totalWidth = static_cast<int>((maxX - minX) / pixelSize);
        int totalHeight = static_cast<int>((maxY - minY) / pixelSize);

        if (totalWidth <= 0 || totalHeight <= 0 ||
            totalWidth > 50000 || totalHeight > 50000) {
            return RasterGrid();
        }

        RasterGrid merged;
        merged.width = totalWidth;
        merged.height = totalHeight;
        merged.data.resize(totalWidth * totalHeight, tiles[0].nodataValue);
        merged.nodataValue = tiles[0].nodataValue;
        merged.originX = minX;
        merged.originY = maxY;
        merged.pixelSizeX = pixelSize;
        merged.pixelSizeY = -pixelSize;
        merged.crs = tiles[0].crs;

        // Copy each tile into the merged grid
        for (const auto& t : tiles) {
            int offsetX = static_cast<int>((t.originX - minX) / pixelSize);
            int offsetY = static_cast<int>((maxY - t.originY) / pixelSize);

            for (int y = 0; y < t.height; y++) {
                for (int x = 0; x < t.width; x++) {
                    int dstX = offsetX + x;
                    int dstY = offsetY + y;
                    if (dstX >= 0 && dstX < totalWidth && dstY >= 0 && dstY < totalHeight) {
                        float v = t.at(x, y);
                        if (v != t.nodataValue && !std::isnan(v))
                            merged.data[dstY * totalWidth + dstX] = v;
                    }
                }
            }
        }
        return merged;
    }

private slots:
    void onDownloadProgress(int completed, int total) {
        emit progress(15 + (completed * 25 / std::max(1, total)),
                      QString("Downloading %1/%2...").arg(completed).arg(total));
    }

    void onStageMessage(const QString& msg) {
        // Forward to progress signal
        emit progress(0, msg);
    }
};

} // namespace terrain_pipeline
