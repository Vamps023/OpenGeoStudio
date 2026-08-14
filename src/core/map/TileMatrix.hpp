#pragma once

// ============================================================
// TileMatrix — Standard XYZ tile scheme (inspired by QgsTileMatrix)
// Based on QGIS qgstiles.cpp Web Mercator tile math
// ============================================================

#include "MapRectangle.hpp"
#include "CoordinateTransform.hpp"
#include <cmath>
#include <algorithm>
#include <vector>

namespace map {

// Tile identifier (column, row, zoom level)
struct TileXYZ {
    int x = 0;  // column
    int y = 0;  // row
    int z = 0;  // zoom level

    TileXYZ() = default;
    TileXYZ(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}

    bool operator==(const TileXYZ& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    std::string toString() const {
        return "X=" + std::to_string(x) + " Y=" + std::to_string(y) + " Z=" + std::to_string(z);
    }
};

// Range of tiles [startCol..endCol, startRow..endRow]
struct TileRange {
    int startCol = 0, endCol = 0;
    int startRow = 0, endRow = 0;

    TileRange() = default;
    TileRange(int sc, int ec, int sr, int er)
        : startCol(sc), endCol(ec), startRow(sr), endRow(er) {}

    bool isValid() const { return startCol <= endCol && startRow <= endRow; }
    int count() const { return (endCol - startCol + 1) * (endRow - startRow + 1); }
};

// Tile matrix for a single zoom level
class TileMatrix {
public:
    TileMatrix() = default;

    // Create standard Web Mercator tile matrix for given zoom level
    static TileMatrix fromWebMercator(int zoomLevel) {
        TileMatrix m;
        m.m_zoomLevel = zoomLevel;
        m.mMatrixWidth = 1 << zoomLevel;
        m.mMatrixHeight = 1 << zoomLevel;
        // Web Mercator extent: -ORIGIN_SHIFT to +ORIGIN_SHIFT
        m.mExtent = MapRectangle(-ORIGIN_SHIFT, -ORIGIN_SHIFT, ORIGIN_SHIFT, ORIGIN_SHIFT);
        m.mTileXSpan = (2.0 * ORIGIN_SHIFT) / m.mMatrixWidth;
        m.mTileYSpan = (2.0 * ORIGIN_SHIFT) / m.mMatrixHeight;
        return m;
    }

    int zoomLevel() const { return m_zoomLevel; }
    int matrixWidth() const { return mMatrixWidth; }
    int matrixHeight() const { return mMatrixHeight; }
    const MapRectangle& extent() const { return mExtent; }

    // Geographic extent of a single tile
    MapRectangle tileExtent(const TileXYZ& id) const {
        double xMin = mExtent.xMin + mTileXSpan * id.x;
        double xMax = xMin + mTileXSpan;
        double yMax = mExtent.yMax - mTileYSpan * id.y;
        double yMin = yMax - mTileYSpan;
        return MapRectangle(xMin, yMin, xMax, yMax);
    }

    // Center of a tile
    MapPoint tileCenter(const TileXYZ& id) const {
        auto ext = tileExtent(id);
        return MapPoint(ext.centerX(), ext.centerY());
    }

    // Which tiles are needed to cover the given extent?
    TileRange tileRangeFromExtent(const MapRectangle& rect) const {
        double tileX1 = (rect.xMin - mExtent.xMin) / mTileXSpan;
        double tileX2 = (rect.xMax - mExtent.xMin) / mTileXSpan;
        double tileY1 = (mExtent.yMax - rect.yMax) / mTileYSpan;
        double tileY2 = (mExtent.yMax - rect.yMin) / mTileYSpan;

        int startCol = std::max(0, static_cast<int>(std::floor(tileX1)));
        int endCol   = std::min(mMatrixWidth - 1, static_cast<int>(std::floor(tileX2)));
        int startRow = std::max(0, static_cast<int>(std::floor(tileY1)));
        int endRow   = std::min(mMatrixHeight - 1, static_cast<int>(std::floor(tileY2)));

        return TileRange(startCol, endCol, startRow, endRow);
    }

    // Convert map point to tile fractional coordinates
    MapPoint mapToTileCoordinates(const MapPoint& mapPoint) const {
        double fx = (mapPoint.x - mExtent.xMin) / mTileXSpan;
        double fy = (mExtent.yMax - mapPoint.y) / mTileYSpan;
        return MapPoint(fx, fy);
    }

private:
    int m_zoomLevel = 0;
    int mMatrixWidth = 1;
    int mMatrixHeight = 1;
    MapRectangle mExtent;
    double mTileXSpan = 0;
    double mTileYSpan = 0;
};

// Set of tile matrices for zoom levels 0..maxZoom
class TileMatrixSet {
public:
    static constexpr int MIN_ZOOM = 0;
    static constexpr int MAX_ZOOM = 22;

    TileMatrixSet() {
        for (int z = MIN_ZOOM; z <= MAX_ZOOM; ++z)
            m_matrices.push_back(TileMatrix::fromWebMercator(z));
    }

    const TileMatrix& matrix(int zoom) const {
        zoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, zoom));
        return m_matrices[zoom];
    }

    // Convert map scale (meters per pixel) to zoom level
    int scaleToZoomLevel(double metersPerPixel, int minZoom = 2, int maxZoom = 19) const {
        // At zoom z, meters per pixel = 2*ORIGIN_SHIFT / (256 * 2^z)
        // => 2^z = 2*ORIGIN_SHIFT / (256 * mpp)
        double z = std::log2((2.0 * ORIGIN_SHIFT) / (256.0 * metersPerPixel));
        int zoom = static_cast<int>(std::round(z));
        return std::max(minZoom, std::min(maxZoom, zoom));
    }

    // Convert lat/lon extent to appropriate zoom level
    int zoomLevelForExtent(const MapRectangle& lonLatExtent, int viewportWidth, int viewportHeight,
                           int minZoom = 2, int maxZoom = 19) const {
        // Transform to Web Mercator
        auto mercExtent = CoordinateTransform(CRSId::EPSG_4326, CRSId::EPSG_3857).transform(lonLatExtent);
        if (mercExtent.isEmpty()) return minZoom;

        double mppX = mercExtent.width() / viewportWidth;
        double mppY = mercExtent.height() / viewportHeight;
        double mpp = std::max(mppX, mppY);
        return scaleToZoomLevel(mpp, minZoom, maxZoom);
    }

private:
    std::vector<TileMatrix> m_matrices;
};

} // namespace map
