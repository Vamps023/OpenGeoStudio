#pragma once

// ============================================================
// MaskManager — Mask generation, normalization, and packing
// ============================================================
//
// Manages all mask operations:
//   - Source masks (vegetation, water, road, building, urban)
//   - Terrain-derived masks (slope, aspect, elevation)
//   - Procedural masks (distance-to-road, distance-to-water)
//   - Mask normalization (binary, continuous, class)
//   - Mask packing (RGBA packed mask texture)
//   - Mask metadata generation
//

#include "../TerrainPipelineTypes.hpp"
#include "../TerrainAnalyzer.hpp"
#include "../GISProcessor.hpp"
#include "../providers/TerrainDataProvider.hpp"
#include "../providers/VectorProviders.hpp"
#include <QImage>
#include <QMap>
#include <memory>

namespace terrain_pipeline {

class MaskManager {
public:
    // ============================================================
    // Generate a single mask from a definition
    // ============================================================

    static ByteRaster generateMask(const MaskDefinition& def,
                                    const RasterGrid& dem,
                                    const RasterGrid& landCover,
                                    const QList<RoadProvider::RoadSegment>& roads,
                                    const QList<WaterProvider::WaterBody>& waterBodies,
                                    const QList<BuildingProvider::BuildingFootprint>& buildings) {
        ByteRaster mask;

        switch (def.type) {
        case MaskType::Vegetation:
        case MaskType::Forest:
        case MaskType::Grass:
        case MaskType::Crop:
            // From land-cover classes
            if (landCover.isValid() && !def.classIds.isEmpty()) {
                mask = classesToMask(landCover, def.classIds);
            }
            break;

        case MaskType::Water:
            // From water bodies (rasterized) or land-cover
            if (!waterBodies.isEmpty()) {
                mask = rasterizeWaterBodies(waterBodies, dem);
            } else if (landCover.isValid() && !def.classIds.isEmpty()) {
                mask = classesToMask(landCover, def.classIds.isEmpty() ?
                    QList<int>({80, 90}) : def.classIds);
            }
            break;

        case MaskType::Urban:
            // From land-cover (built-up class 50) or building footprints
            if (landCover.isValid()) {
                mask = classesToMask(landCover, {50});
            } else if (!buildings.isEmpty()) {
                mask = rasterizeBuildings(buildings, dem);
            }
            break;

        case MaskType::Road:
            // From road geometries
            if (!roads.isEmpty()) {
                mask = rasterizeRoads(roads, dem);
            }
            break;

        case MaskType::Building:
            // From building footprints
            if (!buildings.isEmpty()) {
                mask = rasterizeBuildings(buildings, dem);
            }
            break;

        case MaskType::Slope: {
            auto slope = TerrainAnalyzer::computeSlopeDegrees(dem);
            mask = TerrainAnalyzer::classifySlope(slope, def.rangeBounds);
            break;
        }

        case MaskType::Aspect: {
            auto aspect = TerrainAnalyzer::computeAspect(dem);
            mask = TerrainAnalyzer::classifyAspect(aspect);
            break;
        }

        case MaskType::Elevation:
            mask = TerrainAnalyzer::classifyElevation(dem, def.rangeBounds);
            break;

        case MaskType::Curvature: {
            auto curv = TerrainAnalyzer::computeProfileCurvature(dem);
            mask = rasterGridToByteRaster(curv);
            break;
        }

        case MaskType::Roughness: {
            auto rough = TerrainAnalyzer::computeRoughness(dem);
            mask = rasterGridToByteRaster(rough);
            break;
        }

        case MaskType::Hillshade: {
            auto hs = TerrainAnalyzer::computeHillshade(dem);
            mask = rasterGridToByteRaster(hs);
            break;
        }

        case MaskType::DistanceToRoad: {
            if (!roads.isEmpty()) {
                auto roadMask = rasterizeRoads(roads, dem);
                auto dist = TerrainAnalyzer::distanceTransform(roadMask);
                mask = rasterGridToByteRaster(dist);
            }
            break;
        }

        case MaskType::DistanceToWater: {
            if (!waterBodies.isEmpty()) {
                auto waterMask = rasterizeWaterBodies(waterBodies, dem);
                auto dist = TerrainAnalyzer::distanceTransform(waterMask);
                mask = rasterGridToByteRaster(dist);
            }
            break;
        }

        case MaskType::DistanceToBoundary: {
            // Distance from edge of raster
            ByteRaster boundary;
            boundary.width = dem.width;
            boundary.height = dem.height;
            boundary.data.resize(dem.width * dem.height, 0);
            boundary.nodataValue = 0;
            boundary.originX = dem.originX;
            boundary.originY = dem.originY;
            boundary.pixelSizeX = dem.pixelSizeX;
            boundary.pixelSizeY = dem.pixelSizeY;
            boundary.crs = dem.crs;
            for (int x = 0; x < dem.width; x++) {
                boundary.data[x] = 255;
                boundary.data[(dem.height - 1) * dem.width + x] = 255;
            }
            for (int y = 0; y < dem.height; y++) {
                boundary.data[y * dem.width] = 255;
                boundary.data[y * dem.width + dem.width - 1] = 255;
            }
            auto dist = TerrainAnalyzer::distanceTransform(boundary);
            mask = rasterGridToByteRaster(dist);
            break;
        }

        case MaskType::ElevationRange:
            mask = TerrainAnalyzer::classifyElevation(dem, def.rangeBounds);
            break;

        case MaskType::SlopeRange: {
            auto slope = TerrainAnalyzer::computeSlopeDegrees(dem);
            mask = TerrainAnalyzer::classifySlope(slope, def.rangeBounds);
            break;
        }

        case MaskType::AspectRange: {
            auto aspect = TerrainAnalyzer::computeAspect(dem);
            // Filter aspect to specific range
            mask.width = aspect.width;
            mask.height = aspect.height;
            mask.data.resize(aspect.width * aspect.height, 0);
            mask.nodataValue = 0;
            mask.originX = aspect.originX;
            mask.originY = aspect.originY;
            mask.pixelSizeX = aspect.pixelSizeX;
            mask.pixelSizeY = aspect.pixelSizeY;
            mask.crs = aspect.crs;
            for (int i = 0; i < aspect.width * aspect.height; i++) {
                float v = aspect.data[i];
                if (v >= def.minThreshold && v <= def.maxThreshold)
                    mask.data[i] = 255;
            }
            break;
        }

        case MaskType::PolygonMask:
        case MaskType::Custom:
        case MaskType::LandCover:
            // Custom masks require user-provided data
            break;
        }

        return mask;
    }

    // ============================================================
    // Normalize a mask
    // ============================================================

    static ByteRaster normalizeMask(const ByteRaster& mask,
                                     MaskDefinition::NormalizeMode mode) {
        ByteRaster result = mask;
        if (!result.isValid()) return result;

        switch (mode) {
        case MaskDefinition::NormalizeMode::Binary:
            for (auto& v : result.data) v = (v > 0) ? 255 : 0;
            break;

        case MaskDefinition::NormalizeMode::Continuous: {
            // Normalize to 0-255
            uint8_t minV = 255, maxV = 0;
            for (uint8_t v : result.data) {
                if (v < minV) minV = v;
                if (v > maxV) maxV = v;
            }
            if (maxV > minV) {
                for (auto& v : result.data)
                    v = static_cast<uint8_t>((v - minV) * 255 / (maxV - minV));
            }
            break;
        }

        case MaskDefinition::NormalizeMode::Class:
            // Keep as-is (class values)
            break;
        }
        return result;
    }

    // ============================================================
    // Pack multiple masks into a single RGBA image
    // ============================================================

    static QImage packMasks(const ByteRaster& r, const ByteRaster& g,
                             const ByteRaster& b, const ByteRaster& a) {
        int w = r.isValid() ? r.width : (g.isValid() ? g.width : (b.isValid() ? b.width : a.width));
        int h = r.isValid() ? r.height : (g.isValid() ? g.height : (b.isValid() ? b.height : a.height));
        if (w <= 0 || h <= 0) return QImage();

        QImage img(w, h, QImage::Format_RGBA8888);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t rv = r.isValid() ? r.at(x, y) : 0;
                uint8_t gv = g.isValid() ? g.at(x, y) : 0;
                uint8_t bv = b.isValid() ? b.at(x, y) : 0;
                uint8_t av = a.isValid() ? a.at(x, y) : 255;
                img.setPixel(x, y, qRgba(rv, gv, bv, av));
            }
        }
        return img;
    }

    // ============================================================
    // Convert ByteRaster to QImage (grayscale)
    // ============================================================

    static QImage toImage(const ByteRaster& mask) {
        if (!mask.isValid()) return QImage();
        QImage img(mask.width, mask.height, QImage::Format_Grayscale8);
        for (int y = 0; y < mask.height; y++) {
            uint8_t* line = img.scanLine(y);
            for (int x = 0; x < mask.width; x++) {
                line[x] = mask.at(x, y);
            }
        }
        return img;
    }

    // ============================================================
    // Generate metadata for a mask
    // ============================================================

    static DatasetMetadata generateMetadata(const MaskDefinition& def,
                                             const ByteRaster& mask,
                                             const QString& tileId) {
        DatasetMetadata meta;
        meta.dataset = def.id + "_mask";
        meta.source = def.providerName.isEmpty() ? "derived" : def.providerName;
        meta.sourceVersion = def.sourceVersion;
        meta.crs = mask.crs;
        meta.resolution = std::abs(mask.pixelSizeX);
        meta.bounds[0] = mask.originX;
        meta.bounds[1] = mask.originY + mask.height * mask.pixelSizeY;
        meta.bounds[2] = mask.originX + mask.width * mask.pixelSizeX;
        meta.bounds[3] = mask.originY;
        meta.tileId = tileId;
        meta.generatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        meta.nodata = static_cast<float>(mask.nodataValue);
        return meta;
    }

private:
    // ============================================================
    // Helper: Convert land-cover classes to binary mask
    // ============================================================

    static ByteRaster classesToMask(const RasterGrid& classes, const QList<int>& targetIds) {
        ByteRaster mask;
        mask.width = classes.width;
        mask.height = classes.height;
        mask.data.resize(classes.width * classes.height, 0);
        mask.nodataValue = 0;
        mask.originX = classes.originX;
        mask.originY = classes.originY;
        mask.pixelSizeX = classes.pixelSizeX;
        mask.pixelSizeY = classes.pixelSizeY;
        mask.crs = classes.crs;

        for (int i = 0; i < classes.width * classes.height; i++) {
            int classVal = static_cast<int>(classes.data[i]);
            for (int targetId : targetIds) {
                if (classVal == targetId) {
                    mask.data[i] = 255;
                    break;
                }
            }
        }
        return mask;
    }

    // ============================================================
    // Helper: Rasterize road geometries
    // ============================================================

    static ByteRaster rasterizeRoads(const QList<RoadProvider::RoadSegment>& roads,
                                      const RasterGrid& dem) {
        ByteRaster mask;
        mask.width = dem.width;
        mask.height = dem.height;
        mask.data.resize(dem.width * dem.height, 0);
        mask.nodataValue = 0;
        mask.originX = dem.originX;
        mask.originY = dem.originY;
        mask.pixelSizeX = dem.pixelSizeX;
        mask.pixelSizeY = dem.pixelSizeY;
        mask.crs = dem.crs;

        for (const auto& road : roads) {
            // Transform road coordinates to DEM CRS if needed
            QList<QPair<double, double>> coords;
            for (const auto& c : road.coordinates) {
                // c.first = lon, c.second = lat
                if (dem.crs.epsg == 4326) {
                    coords.append(c);
                } else {
                    auto [x, y] = GISProcessor::transformPoint(
                        c.first, c.second, CrsSpec::wgs84(), dem.crs);
                    coords.append(qMakePair(x, y));
                }
            }
            auto roadMask = GISProcessor::rasterizeLine(
                coords, mask.width, mask.height,
                mask.originX, mask.originY,
                mask.pixelSizeX, mask.pixelSizeY,
                mask.crs, 3);
            // Merge
            for (int i = 0; i < mask.width * mask.height; i++) {
                if (roadMask.data[i] > 0) mask.data[i] = 255;
            }
        }
        return mask;
    }

    // ============================================================
    // Helper: Rasterize water bodies
    // ============================================================

    static ByteRaster rasterizeWaterBodies(const QList<WaterProvider::WaterBody>& bodies,
                                            const RasterGrid& dem) {
        ByteRaster mask;
        mask.width = dem.width;
        mask.height = dem.height;
        mask.data.resize(dem.width * dem.height, 0);
        mask.nodataValue = 0;
        mask.originX = dem.originX;
        mask.originY = dem.originY;
        mask.pixelSizeX = dem.pixelSizeX;
        mask.pixelSizeY = dem.pixelSizeY;
        mask.crs = dem.crs;

        for (const auto& body : bodies) {
            if (body.coordinates.size() < 3) continue;
            QList<QPair<double, double>> coords;
            for (const auto& c : body.coordinates) {
                if (dem.crs.epsg == 4326) {
                    coords.append(c);
                } else {
                    auto [x, y] = GISProcessor::transformPoint(
                        c.first, c.second, CrsSpec::wgs84(), dem.crs);
                    coords.append(qMakePair(x, y));
                }
            }
            auto waterMask = GISProcessor::rasterizePolygon(
                coords, mask.width, mask.height,
                mask.originX, mask.originY,
                mask.pixelSizeX, mask.pixelSizeY, mask.crs);
            for (int i = 0; i < mask.width * mask.height; i++) {
                if (waterMask.data[i] > 0) mask.data[i] = 255;
            }
        }
        return mask;
    }

    // ============================================================
    // Helper: Rasterize building footprints
    // ============================================================

    static ByteRaster rasterizeBuildings(const QList<BuildingProvider::BuildingFootprint>& buildings,
                                          const RasterGrid& dem) {
        ByteRaster mask;
        mask.width = dem.width;
        mask.height = dem.height;
        mask.data.resize(dem.width * dem.height, 0);
        mask.nodataValue = 0;
        mask.originX = dem.originX;
        mask.originY = dem.originY;
        mask.pixelSizeX = dem.pixelSizeX;
        mask.pixelSizeY = dem.pixelSizeY;
        mask.crs = dem.crs;

        for (const auto& bldg : buildings) {
            if (bldg.coordinates.size() < 3) continue;
            QList<QPair<double, double>> coords;
            for (const auto& c : bldg.coordinates) {
                if (dem.crs.epsg == 4326) {
                    coords.append(c);
                } else {
                    auto [x, y] = GISProcessor::transformPoint(
                        c.first, c.second, CrsSpec::wgs84(), dem.crs);
                    coords.append(qMakePair(x, y));
                }
            }
            auto bldgMask = GISProcessor::rasterizePolygon(
                coords, mask.width, mask.height,
                mask.originX, mask.originY,
                mask.pixelSizeX, mask.pixelSizeY, mask.crs);
            for (int i = 0; i < mask.width * mask.height; i++) {
                if (bldgMask.data[i] > 0) mask.data[i] = 255;
            }
        }
        return mask;
    }

    // ============================================================
    // Helper: Convert RasterGrid to ByteRaster (normalize 0-255)
    // ============================================================

    static ByteRaster rasterGridToByteRaster(const RasterGrid& grid) {
        ByteRaster mask;
        if (!grid.isValid()) return mask;
        mask.width = grid.width;
        mask.height = grid.height;
        mask.data.resize(grid.width * grid.height, 0);
        mask.nodataValue = 0;
        mask.originX = grid.originX;
        mask.originY = grid.originY;
        mask.pixelSizeX = grid.pixelSizeX;
        mask.pixelSizeY = grid.pixelSizeY;
        mask.crs = grid.crs;

        float minV = 1e30f, maxV = -1e30f;
        for (float v : grid.data) {
            if (v == grid.nodataValue || std::isnan(v)) continue;
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
        if (maxV <= minV) return mask;

        for (int i = 0; i < grid.width * grid.height; i++) {
            float v = grid.data[i];
            if (v == grid.nodataValue || std::isnan(v)) {
                mask.data[i] = 0;
            } else {
                mask.data[i] = static_cast<uint8_t>(
                    std::max(0.0f, std::min(255.0f,
                        (v - minV) * 255.0f / (maxV - minV))));
            }
        }
        return mask;
    }
};

} // namespace terrain_pipeline
