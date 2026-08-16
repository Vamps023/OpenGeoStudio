#pragma once

// ============================================================
// TileManager — Tile grid generation and edge matching
// ============================================================

#include "TerrainPipelineTypes.hpp"
#include "GISProcessor.hpp"
#include <cmath>
#include <algorithm>

namespace terrain_pipeline {

class TileManager {
public:
    // ============================================================
    // Generate tile grid for the given area
    // ============================================================

    static QList<TileInfo> generateTileGrid(double minLat, double maxLat,
                                             double minLon, double maxLon,
                                             int rows, int cols,
                                             const CrsSpec& crs,
                                             int pixelsPerTile = 1024) {
        QList<TileInfo> tiles;

        // Transform bounds to target CRS
        GISProcessor::BoundingBox srcBounds{minLon, minLat, maxLon, maxLat};
        auto dstBounds = GISProcessor::transformBounds(srcBounds,
            CrsSpec::wgs84(), crs);

        double tileWidth = (dstBounds.maxX - dstBounds.minX) / cols;
        double tileHeight = (dstBounds.maxY - dstBounds.minY) / rows;

        for (int row = 0; row < rows; row++) {
            for (int col = 0; col < cols; col++) {
                TileInfo tile;
                tile.row = row;
                tile.col = col;
                tile.west = dstBounds.minX + col * tileWidth;
                tile.east = dstBounds.minX + (col + 1) * tileWidth;
                tile.north = dstBounds.maxY - row * tileHeight;
                tile.south = dstBounds.maxY - (row + 1) * tileHeight;
                tile.width = pixelsPerTile;
                tile.height = pixelsPerTile;
                tile.crs = crs;
                tile.resolution = std::abs(tileWidth / pixelsPerTile);
                tiles.append(tile);
            }
        }
        return tiles;
    }

    // ============================================================
    // Clip a raster to a tile
    // ============================================================

    static RasterGrid clipToTile(const RasterGrid& src, const TileInfo& tile) {
        return GISProcessor::clipRaster(src, tile.west, tile.south, tile.east, tile.north);
    }

    // ============================================================
    // Resample a raster to tile dimensions
    // ============================================================

    static RasterGrid resampleToTile(const RasterGrid& src, const TileInfo& tile) {
        auto clipped = clipToTile(src, tile);
        if (!clipped.isValid()) {
            // Tile is outside source — create empty tile
            RasterGrid empty;
            empty.width = tile.width;
            empty.height = tile.height;
            empty.data.resize(tile.width * tile.height, src.nodataValue);
            empty.nodataValue = src.nodataValue;
            empty.originX = tile.west;
            empty.originY = tile.north;
            empty.pixelSizeX = (tile.east - tile.west) / tile.width;
            empty.pixelSizeY = (tile.south - tile.north) / tile.height;
            empty.crs = tile.crs;
            return empty;
        }
        return GISProcessor::resample(clipped, tile.width, tile.height);
    }

    // ============================================================
    // Edge matching — Verify adjacent tiles match
    // ============================================================

    struct EdgeMatchResult {
        bool match = true;
        QString error;
        float maxDifference = 0.0f;
    };

    static EdgeMatchResult verifyEdgeMatch(const RasterGrid& tileA, const RasterGrid& tileB,
                                            bool horizontal) {
        EdgeMatchResult result;

        if (horizontal) {
            // tileA is left, tileB is right — compare last column of A with first column of B
            if (tileA.height != tileB.height) {
                result.match = false;
                result.error = "Height mismatch";
                return result;
            }
            for (int y = 0; y < tileA.height; y++) {
                float a = tileA.at(tileA.width - 1, y);
                float b = tileB.at(0, y);
                if (a != tileA.nodataValue && b != tileB.nodataValue) {
                    float diff = std::abs(a - b);
                    if (diff > result.maxDifference) result.maxDifference = diff;
                }
            }
        } else {
            // tileA is top, tileB is bottom — compare last row of A with first row of B
            if (tileA.width != tileB.width) {
                result.match = false;
                result.error = "Width mismatch";
                return result;
            }
            for (int x = 0; x < tileA.width; x++) {
                float a = tileA.at(x, tileA.height - 1);
                float b = tileB.at(x, 0);
                if (a != tileA.nodataValue && b != tileB.nodataValue) {
                    float diff = std::abs(a - b);
                    if (diff > result.maxDifference) result.maxDifference = diff;
                }
            }
        }

        // Allow small floating-point tolerance
        result.match = result.maxDifference < 0.5f;
        return result;
    }

    // ============================================================
    // Verify tile alignment
    // ============================================================

    struct AlignmentResult {
        bool aligned = true;
        QString error;
    };

    static AlignmentResult verifyAlignment(const QList<TileInfo>& tiles) {
        AlignmentResult result;

        if (tiles.isEmpty()) {
            result.aligned = false;
            result.error = "No tiles";
            return result;
        }

        // Check all tiles have same CRS
        CrsSpec crs = tiles[0].crs;
        for (const auto& tile : tiles) {
            if (tile.crs.epsg != crs.epsg) {
                result.aligned = false;
                result.error = "CRS mismatch";
                return result;
            }
        }

        // Check all tiles have same dimensions
        int w = tiles[0].width, h = tiles[0].height;
        for (const auto& tile : tiles) {
            if (tile.width != w || tile.height != h) {
                result.aligned = false;
                result.error = "Dimension mismatch";
                return result;
            }
        }

        // Check no gaps or overlaps
        for (int i = 0; i < tiles.size(); i++) {
            for (int j = i + 1; j < tiles.size(); j++) {
                const auto& a = tiles[i];
                const auto& b = tiles[j];
                // Adjacent horizontally
                if (a.row == b.row && a.col + 1 == b.col) {
                    if (std::abs(a.east - b.west) > 1e-6) {
                        result.aligned = false;
                        result.error = QString("Gap/overlap between %1 and %2")
                            .arg(a.id()).arg(b.id());
                        return result;
                    }
                }
                // Adjacent vertically
                if (a.col == b.col && a.row + 1 == b.row) {
                    if (std::abs(a.south - b.north) > 1e-6) {
                        result.aligned = false;
                        result.error = QString("Gap/overlap between %1 and %2")
                            .arg(a.id()).arg(b.id());
                        return result;
                    }
                }
            }
        }

        return result;
    }
};

} // namespace terrain_pipeline
