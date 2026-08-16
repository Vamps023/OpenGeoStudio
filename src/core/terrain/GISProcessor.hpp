#pragma once

// ============================================================
// GISProcessor — Geospatial processing (CRS, reprojection, clip, resample)
// ============================================================
//
// QGIS-inspired geospatial operations without requiring GDAL/PROJ.
// Supports:
//   - CRS detection and validation
//   - Reprojection (WGS84 <-> Web Mercator <-> UTM)
//   - Raster clipping to extent
//   - Resampling (bilinear, nearest-neighbor)
//   - Vector rasterization (polygon, line → raster mask)
//   - Vector reprojection
//

#include "TerrainPipelineTypes.hpp"
#include "../../core/map/CoordinateTransform.hpp"
#include <cmath>
#include <algorithm>

namespace terrain_pipeline {

class GISProcessor {
public:
    // ============================================================
    // CRS Operations
    // ============================================================

    // Detect CRS from EPSG code
    static CrsSpec detectCrs(int epsg) {
        if (epsg == 4326) return CrsSpec::wgs84();
        if (epsg == 3857) return CrsSpec::webMercator();
        if (epsg >= 32601 && epsg <= 32660) {
            int zone = epsg - 32600;
            return CrsSpec::utm(zone, true);
        }
        if (epsg >= 32701 && epsg <= 32760) {
            int zone = epsg - 32700;
            return CrsSpec::utm(zone, false);
        }
        return {epsg, "Unknown"};
    }

    // Validate CRS
    static bool validateCrs(const CrsSpec& crs) {
        return crs.isValid() && crs.epsg > 0;
    }

    // Auto-detect best UTM CRS from bounds
    static CrsSpec autoUtmFromBounds(double minLat, double maxLat,
                                      double minLon, double maxLon) {
        double centerLat = (minLat + maxLat) / 2.0;
        double centerLon = (minLon + maxLon) / 2.0;
        return CrsSpec::autoUtm(centerLat, centerLon);
    }

    // ============================================================
    // Coordinate Transformation
    // ============================================================

    // Transform a single point from source CRS to target CRS
    static std::pair<double, double> transformPoint(
        double x, double y, const CrsSpec& srcCrs, const CrsSpec& dstCrs) {

        if (srcCrs.epsg == dstCrs.epsg) return {x, y};

        // WGS84 <-> Web Mercator
        if (srcCrs.epsg == 4326 && dstCrs.epsg == 3857) {
            auto p = map::CoordinateTransform::lonLatToMercator(x, y);
            return {p.x, p.y};
        }
        if (srcCrs.epsg == 3857 && dstCrs.epsg == 4326) {
            auto p = map::CoordinateTransform::mercatorToLonLat(x, y);
            return {p.x, p.y};
        }

        // WGS84 -> UTM
        if (srcCrs.epsg == 4326 && dstCrs.epsg >= 32601 && dstCrs.epsg <= 32760) {
            int zone = (dstCrs.epsg > 32700) ? (dstCrs.epsg - 32700) : (dstCrs.epsg - 32600);
            bool north = dstCrs.epsg < 32700;
            return lonLatToUtm(x, y, zone, north);
        }

        // UTM -> WGS84
        if ((srcCrs.epsg >= 32601 && srcCrs.epsg <= 32760) && dstCrs.epsg == 4326) {
            int zone = (srcCrs.epsg > 32700) ? (srcCrs.epsg - 32700) : (srcCrs.epsg - 32600);
            bool north = srcCrs.epsg < 32700;
            return utmToLonLat(x, y, zone, north);
        }

        // Fallback: no transform
        return {x, y};
    }

    // Transform a bounding box (returns new bounds)
    struct BoundingBox {
        double minX, minY, maxX, maxY;
    };

    static BoundingBox transformBounds(const BoundingBox& src,
                                        const CrsSpec& srcCrs,
                                        const CrsSpec& dstCrs) {
        // Transform all 4 corners and take envelope
        auto [x1, y1] = transformPoint(src.minX, src.minY, srcCrs, dstCrs);
        auto [x2, y2] = transformPoint(src.maxX, src.minY, srcCrs, dstCrs);
        auto [x3, y3] = transformPoint(src.minX, src.maxY, srcCrs, dstCrs);
        auto [x4, y4] = transformPoint(src.maxX, src.maxY, srcCrs, dstCrs);

        double minX = std::min({x1, x2, x3, x4});
        double maxX = std::max({x1, x2, x3, x4});
        double minY = std::min({y1, y2, y3, y4});
        double maxY = std::max({y1, y2, y3, y4});

        return {minX, minY, maxX, maxY};
    }

    // ============================================================
    // Raster Reprojection
    // ============================================================

    static RasterGrid reprojectRaster(const RasterGrid& src,
                                       const CrsSpec& dstCrs,
                                       int dstWidth, int dstHeight,
                                       double dstOriginX, double dstOriginY,
                                       double dstPixelSizeX, double dstPixelSizeY) {
        RasterGrid dst;
        dst.width = dstWidth;
        dst.height = dstHeight;
        dst.data.resize(dstWidth * dstHeight);
        dst.nodataValue = src.nodataValue;
        dst.originX = dstOriginX;
        dst.originY = dstOriginY;
        dst.pixelSizeX = dstPixelSizeX;
        dst.pixelSizeY = dstPixelSizeY;
        dst.crs = dstCrs;

        for (int y = 0; y < dstHeight; y++) {
            for (int x = 0; x < dstWidth; x++) {
                // Destination pixel -> world coordinates (dst CRS)
                double worldX = dstOriginX + x * dstPixelSizeX;
                double worldY = dstOriginY + y * dstPixelSizeY;

                // Transform to source CRS
                auto [srcX, srcY] = transformPoint(worldX, worldY, dstCrs, src.crs);

                // Source pixel coordinates
                double srcPx = (srcX - src.originX) / src.pixelSizeX;
                double srcPy = (srcY - src.originY) / src.pixelSizeY;

                if (srcPx >= 0 && srcPx < src.width - 1 &&
                    srcPy >= 0 && srcPy < src.height - 1) {
                    dst.data[y * dstWidth + x] = src.sampleBilinear(srcPx, srcPy);
                } else {
                    dst.data[y * dstWidth + x] = src.nodataValue;
                }
            }
        }
        return dst;
    }

    // ============================================================
    // Raster Clipping
    // ============================================================

    static RasterGrid clipRaster(const RasterGrid& src,
                                  double minX, double minY,
                                  double maxX, double maxY) {
        // Convert world bounds to pixel coordinates
        double pxMin = (minX - src.originX) / src.pixelSizeX;
        double pyMin = (minY - src.originY) / src.pixelSizeY;
        double pxMax = (maxX - src.originX) / src.pixelSizeX;
        double pyMax = (maxY - src.originY) / src.pixelSizeY;

        // Handle negative pixel sizes (inverted y-axis)
        if (pxMin > pxMax) std::swap(pxMin, pxMax);
        if (pyMin > pyMax) std::swap(pyMin, pyMax);

        int x0 = std::max(0, static_cast<int>(std::floor(pxMin)));
        int y0 = std::max(0, static_cast<int>(std::floor(pyMin)));
        int x1 = std::min(src.width, static_cast<int>(std::ceil(pxMax)));
        int y1 = std::min(src.height, static_cast<int>(std::ceil(pyMax)));

        if (x1 <= x0 || y1 <= y0) return RasterGrid();

        RasterGrid dst;
        dst.width = x1 - x0;
        dst.height = y1 - y0;
        dst.data.resize(dst.width * dst.height);
        dst.nodataValue = src.nodataValue;
        dst.originX = src.originX + x0 * src.pixelSizeX;
        dst.originY = src.originY + y0 * src.pixelSizeY;
        dst.pixelSizeX = src.pixelSizeX;
        dst.pixelSizeY = src.pixelSizeY;
        dst.crs = src.crs;

        for (int y = 0; y < dst.height; y++) {
            for (int x = 0; x < dst.width; x++) {
                dst.data[y * dst.width + x] = src.at(x0 + x, y0 + y);
            }
        }
        return dst;
    }

    // ============================================================
    // Raster Resampling
    // ============================================================

    enum class ResampleMethod { NearestNeighbor, Bilinear, Cubic };

    static RasterGrid resample(const RasterGrid& src,
                                int targetWidth, int targetHeight,
                                ResampleMethod method = ResampleMethod::Bilinear) {
        RasterGrid dst;
        if (!src.isValid() || targetWidth <= 0 || targetHeight <= 0) return dst;

        dst.width = targetWidth;
        dst.height = targetHeight;
        dst.data.resize(targetWidth * targetHeight);
        dst.nodataValue = src.nodataValue;
        dst.originX = src.originX;
        dst.originY = src.originY;
        dst.pixelSizeX = (src.width * src.pixelSizeX) / targetWidth;
        dst.pixelSizeY = (src.height * src.pixelSizeY) / targetHeight;
        dst.crs = src.crs;

        for (int y = 0; y < targetHeight; y++) {
            for (int x = 0; x < targetWidth; x++) {
                double srcX = static_cast<double>(x) * src.width / targetWidth;
                double srcY = static_cast<double>(y) * src.height / targetHeight;

                if (method == ResampleMethod::NearestNeighbor) {
                    int sx = std::min(static_cast<int>(srcX), src.width - 1);
                    int sy = std::min(static_cast<int>(srcY), src.height - 1);
                    dst.data[y * targetWidth + x] = src.at(sx, sy);
                } else {
                    dst.data[y * targetWidth + x] = src.sampleBilinear(srcX, srcY);
                }
            }
        }
        return dst;
    }

    // ============================================================
    // Vector Rasterization
    // ============================================================

    // Rasterize a polygon (list of lon,lat pairs) into a byte mask
    static ByteRaster rasterizePolygon(
        const QList<QPair<double, double>>& polygon,
        int width, int height,
        double originX, double originY,
        double pixelSizeX, double pixelSizeY,
        const CrsSpec& crs) {

        ByteRaster mask;
        mask.width = width;
        mask.height = height;
        mask.data.resize(width * height, 0);
        mask.nodataValue = 0;
        mask.originX = originX;
        mask.originY = originY;
        mask.pixelSizeX = pixelSizeX;
        mask.pixelSizeY = pixelSizeY;
        mask.crs = crs;

        if (polygon.size() < 3) return mask;

        // Convert polygon to pixel coordinates
        std::vector<std::pair<double, double>> pts;
        for (const auto& p : polygon) {
            double px = (p.first - originX) / pixelSizeX;
            double py = (p.second - originY) / pixelSizeY;
            pts.push_back({px, py});
        }

        // Scanline polygon fill
        for (int y = 0; y < height; y++) {
            double scanY = y + 0.5;
            std::vector<double> intersections;

            for (size_t i = 0; i < pts.size(); i++) {
                size_t j = (i + 1) % pts.size();
                double y0 = pts[i].second, y1 = pts[j].second;
                double x0 = pts[i].first, x1 = pts[j].first;

                if ((y0 <= scanY && scanY < y1) || (y1 <= scanY && scanY < y0)) {
                    double t = (scanY - y0) / (y1 - y0);
                    intersections.push_back(x0 + t * (x1 - x0));
                }
            }

            std::sort(intersections.begin(), intersections.end());

            for (size_t i = 0; i + 1 < intersections.size(); i += 2) {
                int xStart = std::max(0, static_cast<int>(std::ceil(intersections[i])));
                int xEnd = std::min(width - 1, static_cast<int>(std::floor(intersections[i + 1])));
                for (int x = xStart; x <= xEnd; x++) {
                    mask.data[y * width + x] = 255;
                }
            }
        }
        return mask;
    }

    // Rasterize a line (list of lon,lat pairs) into a byte mask with width
    static ByteRaster rasterizeLine(
        const QList<QPair<double, double>>& line,
        int width, int height,
        double originX, double originY,
        double pixelSizeX, double pixelSizeY,
        const CrsSpec& crs,
        int lineWidthPx = 3) {

        ByteRaster mask;
        mask.width = width;
        mask.height = height;
        mask.data.resize(width * height, 0);
        mask.nodataValue = 0;
        mask.originX = originX;
        mask.originY = originY;
        mask.pixelSizeX = pixelSizeX;
        mask.pixelSizeY = pixelSizeY;
        mask.crs = crs;

        if (line.size() < 2) return mask;

        // Convert line to pixel coordinates and draw thick lines
        int halfWidth = lineWidthPx / 2;
        for (int i = 0; i + 1 < line.size(); i++) {
            double x0 = (line[i].first - originX) / pixelSizeX;
            double y0 = (line[i].second - originY) / pixelSizeY;
            double x1 = (line[i + 1].first - originX) / pixelSizeX;
            double y1 = (line[i + 1].second - originY) / pixelSizeY;

            // Bresenham line with thickness
            double dx = x1 - x0, dy = y1 - y0;
            double len = std::sqrt(dx * dx + dy * dy);
            if (len < 1) continue;
            int steps = static_cast<int>(std::ceil(len));
            for (int s = 0; s <= steps; s++) {
                double t = static_cast<double>(s) / steps;
                int px = static_cast<int>(x0 + t * dx);
                int py = static_cast<int>(y0 + t * dy);
                // Draw thick point
                for (int dy2 = -halfWidth; dy2 <= halfWidth; dy2++) {
                    for (int dx2 = -halfWidth; dx2 <= halfWidth; dx2++) {
                        if (dx2 * dx2 + dy2 * dy2 <= halfWidth * halfWidth) {
                            int fx = px + dx2, fy = py + dy2;
                            if (fx >= 0 && fx < width && fy >= 0 && fy < height)
                                mask.data[fy * width + fx] = 255;
                        }
                    }
                }
            }
        }
        return mask;
    }

    // ============================================================
    // NoData Processing
    // ============================================================

    struct NoDataReport {
        int totalPixels = 0;
        int nodataPixels = 0;
        double percentage = 0;
        QString affectedArea;
        QString processingAction;
    };

    static NoDataReport analyzeNoData(const RasterGrid& grid) {
        NoDataReport report;
        report.totalPixels = grid.width * grid.height;
        for (int i = 0; i < report.totalPixels; i++) {
            if (grid.data[i] == grid.nodataValue || std::isnan(grid.data[i]))
                report.nodataPixels++;
        }
        report.percentage = report.totalPixels > 0
            ? (100.0 * report.nodataPixels / report.totalPixels) : 0;
        return report;
    }

    // Fill NoData with nearest valid pixel (simple interpolation)
    static void fillNoData(RasterGrid& grid) {
        if (!grid.isValid()) return;
        // Simple fill: replace NoData with mean of valid neighbors
        for (int y = 0; y < grid.height; y++) {
            for (int x = 0; x < grid.width; x++) {
                if (grid.at(x, y) == grid.nodataValue || std::isnan(grid.at(x, y))) {
                    float sum = 0;
                    int count = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            if (dx == 0 && dy == 0) continue;
                            float v = grid.at(x + dx, y + dy);
                            if (v != grid.nodataValue && !std::isnan(v)) {
                                sum += v;
                                count++;
                            }
                        }
                    }
                    if (count > 0) grid.set(x, y, sum / count);
                }
            }
        }
    }

private:
    // ============================================================
    // UTM Transformation (WGS84 <-> UTM)
    // ============================================================

    static std::pair<double, double> lonLatToUtm(double lon, double lat, int zone, bool north) {
        // WGS84 ellipsoid parameters
        constexpr double a = 6378137.0;
        constexpr double f = 1.0 / 298.257223563;
        constexpr double k0 = 0.9996;
        double e = std::sqrt(1 - (1 - f) * (1 - f));
        double e2 = e * e / (1 - e * e);

        double latRad = lat * M_PI / 180.0;
        double lonRad = lon * M_PI / 180.0;
        double lonOrigin = (zone - 1) * 6 - 180 + 3;
        double lonOriginRad = lonOrigin * M_PI / 180.0;

        double N = a / std::sqrt(1 - e2 * std::sin(latRad) * std::sin(latRad));
        double T = std::tan(latRad) * std::tan(latRad);
        double C = e2 * std::cos(latRad) * std::cos(latRad);
        double A = std::cos(latRad) * (lonRad - lonOriginRad);
        double M = a * ((1 - e / 4 - 3 * e * e / 64 - 5 * e * e * e / 256) * latRad
            - (3 * e / 8 + 3 * e * e / 32 + 45 * e * e * e / 1024) * std::sin(2 * latRad)
            + (15 * e * e / 256 + 45 * e * e * e / 1024) * std::sin(4 * latRad)
            - (35 * e * e * e / 3072) * std::sin(6 * latRad));

        double easting = k0 * N * (A + (1 - T + C) * A * A * A / 6
            + (5 - 18 * T + T * T + 72 * C - 58 * e2) * A * A * A * A * A / 120) + 500000.0;

        double northing = k0 * (M + N * std::tan(latRad) * (A * A / 2
            + (5 - T + 9 * C + 4 * C * C) * A * A * A * A / 24
            + (61 - 58 * T + T * T + 600 * C - 330 * e2) * A * A * A * A * A * A / 720));

        if (!north) northing += 10000000.0;

        return {easting, northing};
    }

    static std::pair<double, double> utmToLonLat(double easting, double northing, int zone, bool north) {
        constexpr double a = 6378137.0;
        constexpr double f = 1.0 / 298.257223563;
        constexpr double k0 = 0.9996;
        double e = std::sqrt(1 - (1 - f) * (1 - f));
        double e2 = e * e / (1 - e * e);
        double e1 = (1 - std::sqrt(1 - e * e)) / (1 + std::sqrt(1 - e * e));

        if (!north) northing -= 10000000.0;

        double x = easting - 500000.0;
        double y = northing;

        double M = y / k0;
        double mu = M / (a * (1 - e / 4 - 3 * e * e / 64 - 5 * e * e * e / 256));

        double phi1 = mu + (3 * e1 / 2 - 27 * e1 * e1 * e1 / 32) * std::sin(2 * mu)
            + (21 * e1 * e1 / 16 - 55 * e1 * e1 * e1 * e1 / 32) * std::sin(4 * mu)
            + (151 * e1 * e1 * e1 / 96) * std::sin(6 * mu)
            + (1097 * e1 * e1 * e1 * e1 / 512) * std::sin(8 * mu);

        double N1 = a / std::sqrt(1 - e2 * std::sin(phi1) * std::sin(phi1));
        double T1 = std::tan(phi1) * std::tan(phi1);
        double C1 = e2 * std::cos(phi1) * std::cos(phi1);
        double R1 = a * (1 - e * e) / std::pow(1 - e2 * std::sin(phi1) * std::sin(phi1), 1.5);
        double D = x / (N1 * k0);

        double lonOrigin = (zone - 1) * 6 - 180 + 3;

        double lat = phi1 - (N1 * std::tan(phi1) / R1) * (D * D / 2
            - (5 + 3 * T1 + 10 * C1 - 4 * C1 * C1 - 9 * e2) * D * D * D * D / 24
            + (61 + 90 * T1 + 298 * C1 + 45 * T1 * T1 - 252 * e2 - 3 * C1 * C1) * D * D * D * D * D * D / 720);

        double lon = lonOrigin * M_PI / 180.0 + (D - (1 + 2 * T1 + C1) * D * D * D / 6
            + (5 - 2 * C1 + 28 * T1 - 3 * C1 * C1 + 8 * e2 + 24 * T1 * T1) * D * D * D * D * D / 120) / std::cos(phi1);

        return {lon * 180.0 / M_PI, lat * 180.0 / M_PI};
    }
};

} // namespace terrain_pipeline
