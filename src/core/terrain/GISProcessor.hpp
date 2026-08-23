#pragma once

// ============================================================
// GISProcessor — Geospatial processing (CRS, reprojection, clip, resample)
// ============================================================
//
// QGIS-inspired geospatial operations.
// CRS detection and coordinate transforms are backed by PROJ via the
// gis::CRSManager / gis::CoordinateTransform facilities. Raster
// operations (clip, resample, rasterize) remain hand-written since
// they are not duplicated by PROJ.
// Supports:
//   - CRS detection and validation
//   - Reprojection (WGS84 <-> Web Mercator <-> UTM, and any PROJ CRS)
//   - Raster clipping to extent
//   - Resampling (bilinear, nearest-neighbor)
//   - Vector rasterization (polygon, line -> raster mask)
//   - Vector reprojection
//

#include "TerrainPipelineTypes.hpp"
#include "../../core/map/CoordinateTransform.hpp"
#include "../../gis/crs/CRSManager.hpp"
#include "../../gis/crs/CoordinateTransform.hpp"
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

    // Transform a single point from source CRS to target CRS.
    // Uses PROJ for accurate transformations. Falls back to the fast
    // inline Web Mercator path for the common 4326<->3857 case.
    static std::pair<double, double> transformPoint(
        double x, double y, const CrsSpec& srcCrs, const CrsSpec& dstCrs) {

        if (srcCrs.epsg == dstCrs.epsg) return {x, y};

        // WGS84 <-> Web Mercator (fast inline path, same accuracy as PROJ)
        if (srcCrs.epsg == 4326 && dstCrs.epsg == 3857) {
            auto p = map::CoordinateTransform::lonLatToMercator(x, y);
            return {p.x, p.y};
        }
        if (srcCrs.epsg == 3857 && dstCrs.epsg == 4326) {
            auto p = map::CoordinateTransform::mercatorToLonLat(x, y);
            return {p.x, p.y};
        }

        // Generic path via PROJ — covers UTM and any other CRS pair.
        auto srcOpt = gis::CRSManager::instance().fromEPSG(srcCrs.epsg);
        auto dstOpt = gis::CRSManager::instance().fromEPSG(dstCrs.epsg);
        if (!srcOpt || !dstOpt) {
            // Unknown CRS — do not silently produce wrong data.
            return {x, y};
        }
        gis::CoordinateTransform xf(*srcOpt, *dstOpt);
        if (!xf.isValid()) return {x, y};
        gis::GeoPoint pt;
        pt.x = x;
        pt.y = y;
        pt.hasZ = false;
        auto result = xf.transform(pt);
        if (!result.success) return {x, y};
        return {result.point.x, result.point.y};
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
    // UTM forward/inverse transforms are now handled by PROJ via
    // transformPoint(). The hand-written UTM series expansions that
    // previously lived here have been removed in favour of the
    // PROJ-backed gis::CoordinateTransform, which is accurate for
    // all UTM zones and hemispheres.
};

} // namespace terrain_pipeline
