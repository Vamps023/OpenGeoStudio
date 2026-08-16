#pragma once

// ============================================================
// TerrainAnalyzer — Terrain-derived analysis (slope, aspect, etc.)
// ============================================================
//
// Computes terrain-derived rasters from a DEM:
//   - Slope (degrees or percent)
//   - Aspect (0-360 degrees, or 8-direction classification)
//   - Elevation classification (user-defined ranges)
//   - Curvature (plan and profile)
//   - Roughness (neighborhood-based)
//   - Hillshade
//

#include "TerrainPipelineTypes.hpp"
#include <cmath>
#include <algorithm>

namespace terrain_pipeline {

class TerrainAnalyzer {
public:
    // ============================================================
    // Slope — Compute slope in degrees
    // ============================================================

    static RasterGrid computeSlopeDegrees(const RasterGrid& dem) {
        RasterGrid slope;
        if (!dem.isValid()) return slope;
        slope.width = dem.width;
        slope.height = dem.height;
        slope.data.resize(dem.width * dem.height);
        slope.nodataValue = -1.0f;
        slope.originX = dem.originX;
        slope.originY = dem.originY;
        slope.pixelSizeX = dem.pixelSizeX;
        slope.pixelSizeY = dem.pixelSizeY;
        slope.crs = dem.crs;

        // Pixel size in meters (assume UTM or convert)
        double cellSize = std::abs(dem.pixelSizeX);
        if (dem.crs.isGeographic()) {
            // Approximate meters from degrees
            double lat = (dem.north() + dem.south()) / 2.0;
            cellSize = std::abs(dem.pixelSizeX) * 111320.0 * std::cos(lat * M_PI / 180.0);
        }

        for (int y = 0; y < dem.height; y++) {
            for (int x = 0; x < dem.width; x++) {
                float z = dem.at(x, y);
                if (z == dem.nodataValue || std::isnan(z)) {
                    slope.data[y * dem.width + x] = -1.0f;
                    continue;
                }

                // Get neighbors (use edge replication)
                float zLeft = dem.at(std::max(0, x - 1), y);
                float zRight = dem.at(std::min(dem.width - 1, x + 1), y);
                float zUp = dem.at(x, std::max(0, y - 1));
                float zDown = dem.at(x, std::min(dem.height - 1, y + 1));

                // Skip if any neighbor is nodata
                if (zLeft == dem.nodataValue || zRight == dem.nodataValue ||
                    zUp == dem.nodataValue || zDown == dem.nodataValue) {
                    slope.data[y * dem.width + x] = -1.0f;
                    continue;
                }

                // Horn's formula for slope
                double dzdx = (zRight - zLeft) / (2.0 * cellSize);
                double dzdy = (zDown - zUp) / (2.0 * cellSize);

                double slopeRad = std::atan(std::sqrt(dzdx * dzdx + dzdy * dzdy));
                slope.data[y * dem.width + x] = static_cast<float>(slopeRad * 180.0 / M_PI);
            }
        }
        return slope;
    }

    // ============================================================
    // Aspect — Compute aspect in degrees (0-360)
    // ============================================================

    static RasterGrid computeAspect(const RasterGrid& dem) {
        RasterGrid aspect;
        if (!dem.isValid()) return aspect;
        aspect.width = dem.width;
        aspect.height = dem.height;
        aspect.data.resize(dem.width * dem.height);
        aspect.nodataValue = -1.0f;
        aspect.originX = dem.originX;
        aspect.originY = dem.originY;
        aspect.pixelSizeX = dem.pixelSizeX;
        aspect.pixelSizeY = dem.pixelSizeY;
        aspect.crs = dem.crs;

        double cellSize = std::abs(dem.pixelSizeX);
        if (dem.crs.isGeographic()) {
            double lat = (dem.north() + dem.south()) / 2.0;
            cellSize = std::abs(dem.pixelSizeX) * 111320.0 * std::cos(lat * M_PI / 180.0);
        }

        for (int y = 0; y < dem.height; y++) {
            for (int x = 0; x < dem.width; x++) {
                float z = dem.at(x, y);
                if (z == dem.nodataValue || std::isnan(z)) {
                    aspect.data[y * dem.width + x] = -1.0f;
                    continue;
                }

                float zLeft = dem.at(std::max(0, x - 1), y);
                float zRight = dem.at(std::min(dem.width - 1, x + 1), y);
                float zUp = dem.at(x, std::max(0, y - 1));
                float zDown = dem.at(x, std::min(dem.height - 1, y + 1));

                if (zLeft == dem.nodataValue || zRight == dem.nodataValue ||
                    zUp == dem.nodataValue || zDown == dem.nodataValue) {
                    aspect.data[y * dem.width + x] = -1.0f;
                    continue;
                }

                double dzdx = (zRight - zLeft) / (2.0 * cellSize);
                double dzdy = (zDown - zUp) / (2.0 * cellSize);

                double aspectRad = std::atan2(dzdy, -dzdx);
                if (aspectRad < 0) aspectRad += 2.0 * M_PI;

                aspect.data[y * dem.width + x] = static_cast<float>(aspectRad * 180.0 / M_PI);
            }
        }
        return aspect;
    }

    // ============================================================
    // Elevation Classification — Classify elevation into ranges
    // ============================================================

    static ByteRaster classifyElevation(const RasterGrid& dem,
                                         const QList<float>& rangeBounds) {
        ByteRaster mask;
        if (!dem.isValid() || rangeBounds.size() < 2) return mask;
        mask.width = dem.width;
        mask.height = dem.height;
        mask.data.resize(dem.width * dem.height, 0);
        mask.nodataValue = 0;
        mask.originX = dem.originX;
        mask.originY = dem.originY;
        mask.pixelSizeX = dem.pixelSizeX;
        mask.pixelSizeY = dem.pixelSizeY;
        mask.crs = dem.crs;

        int numClasses = rangeBounds.size() - 1;
        for (int i = 0; i < dem.width * dem.height; i++) {
            float v = dem.data[i];
            if (v == dem.nodataValue || std::isnan(v)) {
                mask.data[i] = 0;
                continue;
            }
            for (int c = 0; c < numClasses; c++) {
                if (v >= rangeBounds[c] && v < rangeBounds[c + 1]) {
                    // Map to 1-255 range
                    mask.data[i] = static_cast<uint8_t>(
                        1 + (c * 254 / std::max(1, numClasses - 1)));
                    break;
                }
            }
            if (v >= rangeBounds.last())
                mask.data[i] = 255;
        }
        return mask;
    }

    // ============================================================
    // Slope Classification — Classify slope into ranges
    // ============================================================

    static ByteRaster classifySlope(const RasterGrid& slope,
                                     const QList<float>& rangeBounds) {
        ByteRaster mask;
        if (!slope.isValid() || rangeBounds.size() < 2) return mask;
        mask.width = slope.width;
        mask.height = slope.height;
        mask.data.resize(slope.width * slope.height, 0);
        mask.nodataValue = 0;
        mask.originX = slope.originX;
        mask.originY = slope.originY;
        mask.pixelSizeX = slope.pixelSizeX;
        mask.pixelSizeY = slope.pixelSizeY;
        mask.crs = slope.crs;

        int numClasses = rangeBounds.size() - 1;
        for (int i = 0; i < slope.width * slope.height; i++) {
            float v = slope.data[i];
            if (v < 0 || std::isnan(v)) {
                mask.data[i] = 0;
                continue;
            }
            for (int c = 0; c < numClasses; c++) {
                if (v >= rangeBounds[c] && v < rangeBounds[c + 1]) {
                    mask.data[i] = static_cast<uint8_t>(
                        1 + (c * 254 / std::max(1, numClasses - 1)));
                    break;
                }
            }
            if (v >= rangeBounds.last())
                mask.data[i] = 255;
        }
        return mask;
    }

    // ============================================================
    // Aspect Classification — 8-direction classification
    // ============================================================

    static ByteRaster classifyAspect(const RasterGrid& aspect) {
        ByteRaster mask;
        if (!aspect.isValid()) return mask;
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
            if (v < 0 || std::isnan(v)) {
                mask.data[i] = 0;
                continue;
            }
            // 8 directions: N(0), NE(1), E(2), SE(3), S(4), SW(5), W(6), NW(7)
            int dir = static_cast<int>((v + 22.5) / 45.0) % 8;
            mask.data[i] = static_cast<uint8_t>(1 + dir * 31);  // 1, 32, 63, ...
        }
        return mask;
    }

    // ============================================================
    // Curvature — Plan and profile curvature
    // ============================================================

    static RasterGrid computeProfileCurvature(const RasterGrid& dem) {
        RasterGrid curv;
        if (!dem.isValid()) return curv;
        curv.width = dem.width;
        curv.height = dem.height;
        curv.data.resize(dem.width * dem.height, 0);
        curv.nodataValue = -9999.0f;
        curv.originX = dem.originX;
        curv.originY = dem.originY;
        curv.pixelSizeX = dem.pixelSizeX;
        curv.pixelSizeY = dem.pixelSizeY;
        curv.crs = dem.crs;

        double cellSize = std::abs(dem.pixelSizeX);
        if (dem.crs.isGeographic()) {
            double lat = (dem.north() + dem.south()) / 2.0;
            cellSize = std::abs(dem.pixelSizeX) * 111320.0 * std::cos(lat * M_PI / 180.0);
        }

        for (int y = 1; y < dem.height - 1; y++) {
            for (int x = 1; x < dem.width - 1; x++) {
                float z = dem.at(x, y);
                if (z == dem.nodataValue || std::isnan(z)) {
                    curv.data[y * dem.width + x] = -9999.0f;
                    continue;
                }
                // Zevenbergen-Thorne formula for profile curvature
                float D = ((dem.at(x + 1, y) + dem.at(x - 1, y)) / 2.0 - z) / (cellSize * cellSize);
                float E = ((dem.at(x, y + 1) + dem.at(x, y - 1)) / 2.0 - z) / (cellSize * cellSize);
                curv.data[y * dem.width + x] = static_cast<float>(-(D + E));
            }
        }
        return curv;
    }

    // ============================================================
    // Roughness — Neighborhood-based roughness (stddev of elevation)
    // ============================================================

    static RasterGrid computeRoughness(const RasterGrid& dem, int windowSize = 3) {
        RasterGrid rough;
        if (!dem.isValid()) return rough;
        rough.width = dem.width;
        rough.height = dem.height;
        rough.data.resize(dem.width * dem.height, 0);
        rough.nodataValue = -1.0f;
        rough.originX = dem.originX;
        rough.originY = dem.originY;
        rough.pixelSizeX = dem.pixelSizeX;
        rough.pixelSizeY = dem.pixelSizeY;
        rough.crs = dem.crs;

        int halfWindow = windowSize / 2;
        for (int y = 0; y < dem.height; y++) {
            for (int x = 0; x < dem.width; x++) {
                if (dem.at(x, y) == dem.nodataValue) {
                    rough.data[y * dem.width + x] = -1.0f;
                    continue;
                }

                // Compute standard deviation in window
                double sum = 0, sumSq = 0;
                int count = 0;
                for (int dy = -halfWindow; dy <= halfWindow; dy++) {
                    for (int dx = -halfWindow; dx <= halfWindow; dx++) {
                        float v = dem.at(x + dx, y + dy);
                        if (v != dem.nodataValue && !std::isnan(v)) {
                            sum += v;
                            sumSq += v * v;
                            count++;
                        }
                    }
                }
                if (count > 1) {
                    double mean = sum / count;
                    double variance = (sumSq - sum * mean) / (count - 1);
                    rough.data[y * dem.width + x] = static_cast<float>(std::sqrt(std::max(0.0, variance)));
                }
            }
        }
        return rough;
    }

    // ============================================================
    // Hillshade — Compute hillshade for visualization
    // ============================================================

    static RasterGrid computeHillshade(const RasterGrid& dem,
                                        double azimuthDeg = 315.0,
                                        double altitudeDeg = 45.0) {
        RasterGrid hs;
        if (!dem.isValid()) return hs;
        hs.width = dem.width;
        hs.height = dem.height;
        hs.data.resize(dem.width * dem.height, 0);
        hs.nodataValue = -1.0f;
        hs.originX = dem.originX;
        hs.originY = dem.originY;
        hs.pixelSizeX = dem.pixelSizeX;
        hs.pixelSizeY = dem.pixelSizeY;
        hs.crs = dem.crs;

        double cellSize = std::abs(dem.pixelSizeX);
        if (dem.crs.isGeographic()) {
            double lat = (dem.north() + dem.south()) / 2.0;
            cellSize = std::abs(dem.pixelSizeX) * 111320.0 * std::cos(lat * M_PI / 180.0);
        }

        double azimuthRad = (360.0 - azimuthDeg + 90.0) * M_PI / 180.0;
        double zenithRad = (90.0 - altitudeDeg) * M_PI / 180.0;

        for (int y = 0; y < dem.height; y++) {
            for (int x = 0; x < dem.width; x++) {
                float z = dem.at(x, y);
                if (z == dem.nodataValue || std::isnan(z)) {
                    hs.data[y * dem.width + x] = -1.0f;
                    continue;
                }

                float zLeft = dem.at(std::max(0, x - 1), y);
                float zRight = dem.at(std::min(dem.width - 1, x + 1), y);
                float zUp = dem.at(x, std::max(0, y - 1));
                float zDown = dem.at(x, std::min(dem.height - 1, y + 1));

                if (zLeft == dem.nodataValue || zRight == dem.nodataValue ||
                    zUp == dem.nodataValue || zDown == dem.nodataValue) {
                    hs.data[y * dem.width + x] = -1.0f;
                    continue;
                }

                double dzdx = (zRight - zLeft) / (2.0 * cellSize);
                double dzdy = (zDown - zUp) / (2.0 * cellSize);

                double slope = std::atan(std::sqrt(dzdx * dzdx + dzdy * dzdy));
                double aspect = std::atan2(dzdy, -dzdx);
                if (aspect < 0) aspect += 2.0 * M_PI;

                double shade = (std::cos(zenithRad) * std::cos(slope) +
                    std::sin(zenithRad) * std::sin(slope) * std::cos(azimuthRad - aspect));

                hs.data[y * dem.width + x] = static_cast<float>(std::max(0.0, std::min(255.0, shade * 255.0)));
            }
        }
        return hs;
    }

    // ============================================================
    // Distance Transform — Distance from mask pixels
    // ============================================================

    static RasterGrid distanceTransform(const ByteRaster& mask) {
        RasterGrid dist;
        if (!mask.isValid()) return dist;
        dist.width = mask.width;
        dist.height = mask.height;
        dist.data.resize(mask.width * mask.height, 0);
        dist.nodataValue = -1.0f;
        dist.originX = mask.originX;
        dist.originY = mask.originY;
        dist.pixelSizeX = mask.pixelSizeX;
        dist.pixelSizeY = mask.pixelSizeY;
        dist.crs = mask.crs;

        // Two-pass distance transform (Chamfer 3-4)
        // Forward pass
        for (int y = 0; y < mask.height; y++) {
            for (int x = 0; x < mask.width; x++) {
                if (mask.at(x, y) > 0) {
                    dist.data[y * mask.width + x] = 0;
                } else {
                    float d = 1e30f;
                    if (x > 0) d = std::min(d, dist.data[y * mask.width + (x - 1)] + 3);
                    if (y > 0) d = std::min(d, dist.data[(y - 1) * mask.width + x] + 3);
                    if (x > 0 && y > 0) d = std::min(d, dist.data[(y - 1) * mask.width + (x - 1)] + 4);
                    if (x < mask.width - 1 && y > 0) d = std::min(d, dist.data[(y - 1) * mask.width + (x + 1)] + 4);
                    dist.data[y * mask.width + x] = d;
                }
            }
        }
        // Backward pass
        for (int y = mask.height - 1; y >= 0; y--) {
            for (int x = mask.width - 1; x >= 0; x--) {
                if (mask.at(x, y) > 0) continue;
                float d = dist.data[y * mask.width + x];
                if (x < mask.width - 1) d = std::min(d, dist.data[y * mask.width + (x + 1)] + 3);
                if (y < mask.height - 1) d = std::min(d, dist.data[(y + 1) * mask.width + x] + 3);
                if (x < mask.width - 1 && y < mask.height - 1) d = std::min(d, dist.data[(y + 1) * mask.width + (x + 1)] + 4);
                if (x > 0 && y < mask.height - 1) d = std::min(d, dist.data[(y + 1) * mask.width + (x - 1)] + 4);
                dist.data[y * mask.width + x] = d;
            }
        }
        // Normalize to 0-1
        float maxDist = 0;
        for (float v : dist.data) if (v < 1e30f && v > maxDist) maxDist = v;
        if (maxDist > 0) {
            for (float& v : dist.data) {
                if (v < 1e30f) v = v / maxDist;
                else v = -1.0f;
            }
        }
        return dist;
    }
};

} // namespace terrain_pipeline
