#pragma once

// ============================================================
// DemDecoder — Decode elevation from tile formats
// ============================================================
//
// QGIS doesn't have built-in terrarium/terrain-rgb decoding in core
// (handled by GDAL raster providers), but we need to decode these
// formats ourselves since we download raw PNG tiles.
//
// Supported formats:
//   - Terrarium (AWS/Mapzen): RGB → elevation in meters
//     Formula: height = (R * 256 + G + B / 256) - 32768
//   - Mapbox Terrain-RGB: RGB → elevation in meters
//     Formula: height = -10000 + ((R * 256 * 256 + G * 256 + B) * 0.1)
//   - AAIGrid (ASCII ArcGrid): text → elevation in meters
//   - GeoTIFF: loaded via QImage/libtiff → raw elevation values
//

#include <QImage>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <tiffio.h>
#include <cstring>
#include <vector>
#include <cmath>
#include <limits>
#include <cstdint>

namespace terrain {

struct DemTile {
    int width = 0;
    int height = 0;
    std::vector<float> elevations;  // Row-major, meters
    float nodataValue = -9999.0f;
    bool valid = false;
};

class DemDecoder {
public:
    // ============================================================
    // Terrarium format (AWS/Mapzen elevation tiles)
    // ============================================================
    // Each pixel: (R * 256 + G + B / 256) - 32768 = elevation in meters
    static DemTile decodeTerrarium(const QImage& img) {
        DemTile tile;
        tile.width = img.width();
        tile.height = img.height();
        tile.nodataValue = -32768.0f;
        tile.elevations.resize(tile.width * tile.height);

        QImage rgb = img.convertToFormat(QImage::Format_RGB888);
        for (int y = 0; y < tile.height; ++y) {
            const uint8_t* row = rgb.scanLine(y);
            for (int x = 0; x < tile.width; ++x) {
                int idx = (x * 3);
                float r = row[idx];
                float g = row[idx + 1];
                float b = row[idx + 2];
                // Terrarium formula
                float elev = (r * 256.0f + g + b / 256.0f) - 32768.0f;
                tile.elevations[y * tile.width + x] = elev;
            }
        }
        tile.valid = true;
        return tile;
    }

    // Decode Terrarium from raw PNG bytes
    static DemTile decodeTerrarium(const QByteArray& data) {
        QImage img;
        if (!img.loadFromData(data)) return DemTile{};
        return decodeTerrarium(img);
    }

    // ============================================================
    // Mapbox Terrain-RGB format
    // ============================================================
    // Each pixel: -10000 + ((R * 256 * 256 + G * 256 + B) * 0.1) = elevation in meters
    static DemTile decodeMapboxTerrainRgb(const QImage& img) {
        DemTile tile;
        tile.width = img.width();
        tile.height = img.height();
        tile.nodataValue = -9999.0f;
        tile.elevations.resize(tile.width * tile.height);

        QImage rgb = img.convertToFormat(QImage::Format_RGB888);
        for (int y = 0; y < tile.height; ++y) {
            const uint8_t* row = rgb.scanLine(y);
            for (int x = 0; x < tile.width; ++x) {
                int idx = (x * 3);
                float r = row[idx];
                float g = row[idx + 1];
                float b = row[idx + 2];
                // Mapbox Terrain-RGB formula
                float elev = -10000.0f + ((r * 256.0f * 256.0f + g * 256.0f + b) * 0.1f);
                tile.elevations[y * tile.width + x] = elev;
            }
        }
        tile.valid = true;
        return tile;
    }

    static DemTile decodeMapboxTerrainRgb(const QByteArray& data) {
        QImage img;
        if (!img.loadFromData(data)) return DemTile{};
        return decodeMapboxTerrainRgb(img);
    }

    // ============================================================
    // AAIGrid (ASCII ArcGrid) — OpenTopography format
    // ============================================================
    // Header: ncols, nrows, xllcorner, yllcorner, cellsize, NODATA_value
    // Body: space-separated elevation values
    static DemTile decodeAaiGrid(const QByteArray& data) {
        DemTile tile;
        QString text = QString::fromLatin1(data);
        QStringList lines = text.split('\n', Qt::SkipEmptyParts);

        if (lines.size() < 7) return tile;

        int ncols = 0, nrows = 0;
        double xllcorner = 0, yllcorner = 0, cellsize = 1;
        double nodata = -9999;
        int headerLines = 0;

        static const QRegularExpression ws("\\s+");

        for (int i = 0; i < lines.size() && headerLines < 6; ++i) {
            QString line = lines[i].trimmed().toLower();
            auto parts = line.split(ws, Qt::SkipEmptyParts);
            if (parts.size() < 2) continue;

            if (line.startsWith("ncols")) {
                ncols = parts[1].toInt();
                headerLines++;
            } else if (line.startsWith("nrows")) {
                nrows = parts[1].toInt();
                headerLines++;
            } else if (line.startsWith("xllcorner") || line.startsWith("xll")) {
                xllcorner = parts[1].toDouble();
                headerLines++;
            } else if (line.startsWith("yllcorner") || line.startsWith("yll")) {
                yllcorner = parts[1].toDouble();
                headerLines++;
            } else if (line.startsWith("cellsize")) {
                cellsize = parts[1].toDouble();
                headerLines++;
            } else if (line.startsWith("nodata_value") || line.startsWith("nodata")) {
                nodata = parts[1].toDouble();
                headerLines++;
            }
        }

        if (ncols <= 0 || nrows <= 0 || headerLines < 5) return tile;

        tile.width = ncols;
        tile.height = nrows;
        tile.nodataValue = static_cast<float>(nodata);
        tile.elevations.resize(ncols * nrows);

        // Read elevation values
        int elevIdx = 0;
        for (int i = headerLines; i < lines.size() && elevIdx < ncols * nrows; ++i) {
            auto vals = lines[i].split(ws, Qt::SkipEmptyParts);
            for (const auto& v : vals) {
                if (elevIdx >= ncols * nrows) break;
                bool ok;
                double e = v.toDouble(&ok);
                if (ok) {
                    tile.elevations[elevIdx] = static_cast<float>(e);
                } else {
                    tile.elevations[elevIdx] = tile.nodataValue;
                }
                elevIdx++;
            }
        }

        tile.valid = (elevIdx >= ncols * nrows);
        return tile;
    }

    // ============================================================
    // GeoTIFF DEM (Copernicus, SRTM, GPXZ, etc.)
    // ============================================================
    // Uses libtiff to read GeoTIFF directly — supports Float32,
    // Int16, UInt16, and Byte data types with proper nodata handling.
    // Handles both stripped (scanline) and tiled (COG) TIFF layouts.
    // Also handles BigTIFF (used by GPXZ Cloud Optimized GeoTIFFs).
    // Uses TIFFClientOpen for memory-based I/O (no temp file needed).
    // QImage cannot read Float32 GeoTIFFs, so libtiff is required.

    // Memory reader context for TIFFClientOpen
    struct MemTiffContext {
        const uint8_t* data;
        tmsize_t size;
        tmsize_t pos;
    };

    static tmsize_t tiffReadProc(void* ctx, void* buf, tmsize_t size) {
        auto* mc = static_cast<MemTiffContext*>(ctx);
        tmsize_t remaining = mc->size - mc->pos;
        tmsize_t toRead = std::min(size, remaining);
        if (toRead <= 0) return 0;
        std::memcpy(buf, mc->data + mc->pos, static_cast<size_t>(toRead));
        mc->pos += toRead;
        return toRead;
    }

    static tmsize_t tiffWriteProc(void*, void*, tmsize_t) {
        return 0;  // Read-only
    }

    static toff_t tiffSeekProc(void* ctx, toff_t off, int whence) {
        auto* mc = static_cast<MemTiffContext*>(ctx);
        switch (whence) {
        case SEEK_SET: mc->pos = static_cast<tmsize_t>(off); break;
        case SEEK_CUR: mc->pos += static_cast<tmsize_t>(off); break;
        case SEEK_END: mc->pos = mc->size + static_cast<tmsize_t>(off); break;
        }
        if (mc->pos < 0) mc->pos = 0;
        if (mc->pos > mc->size) mc->pos = mc->size;
        return static_cast<toff_t>(mc->pos);
    }

    static toff_t tiffSizeProc(void* ctx) {
        auto* mc = static_cast<MemTiffContext*>(ctx);
        return static_cast<toff_t>(mc->size);
    }

    static int tiffCloseProc(void*) {
        return 0;  // Nothing to close — memory is managed externally
    }

    static DemTile decodeGeoTiff(const QByteArray& data) {
        DemTile tile;

        if (data.isEmpty()) return tile;
        if (data.size() < 16) return tile;  // Minimum TIFF header size

        // Use TIFFClientOpen for memory-based I/O — avoids all temp file
        // and path encoding issues on Windows.
        MemTiffContext ctx;
        ctx.data = reinterpret_cast<const uint8_t*>(data.constData());
        ctx.size = static_cast<tmsize_t>(data.size());
        ctx.pos = 0;

        TIFF* tif = TIFFClientOpen("mem-tiff", "r", &ctx,
                                   tiffReadProc, tiffWriteProc,
                                   tiffSeekProc, tiffCloseProc,
                                   tiffSizeProc, nullptr, nullptr);
        if (!tif) return tile;

        // Guard to ensure TIFFClose is called on all exit paths
        struct TiffGuard {
            TIFF* tif;
            ~TiffGuard() { if (tif) TIFFClose(tif); }
        } guard{tif};

        uint32_t width = 0, height = 0;
        if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width)) {
            return tile;
        }
        if (!TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height)) {
            return tile;
        }

        if (width == 0 || height == 0 || width > 10000 || height > 10000) {
            return tile;  // Sanity check — DEM tiles shouldn't be huge
        }

        uint16_t bitsPerSample = 8, samplesPerPixel = 1, sampleFormat = SAMPLEFORMAT_UINT;
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
        TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleFormat);

        // Only handle 1-sample-per-pixel DEM data
        if (samplesPerPixel != 1) {
            return tile;
        }

        tile.width = static_cast<int>(width);
        tile.height = static_cast<int>(height);
        tile.nodataValue = -9999.0f;

        // Safely resize — check for overflow
        size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (pixelCount > 100'000'000) {  // 100M pixels max — sanity
            return tile;
        }
        try {
            tile.elevations.resize(pixelCount);
        } catch (const std::bad_alloc&) {
            return tile;
        }

        // Read nodata value if present
        char* nodataStr = nullptr;
        if (TIFFGetField(tif, TIFFTAG_GDAL_NODATA, &nodataStr) && nodataStr) {
            tile.nodataValue = static_cast<float>(QString(nodataStr).toDouble());
        }

        // Determine bytes per sample
        int bytesPerSample = bitsPerSample / 8;
        if (bitsPerSample % 8 != 0 || bytesPerSample < 1 || bytesPerSample > 8) {
            return tile;
        }

        // Determine the data type for reading
        // GPXZ returns Float32 COG (tiled, BigTIFF, DEFLATE), Copernicus returns Float32 stripped,
        // SRTM returns Int16 stripped.
        bool isTiled = TIFFIsTiled(tif) != 0;

        // Helper lambda to read a single pixel value as float
        auto readPixelAsFloat = [&](const uint8_t* buf, size_t bufSize, uint32_t idx) -> float {
            size_t byteOffset = idx * static_cast<size_t>(bytesPerSample);
            if (byteOffset + static_cast<size_t>(bytesPerSample) > bufSize) {
                return tile.nodataValue;  // Out of bounds — return nodata
            }
            switch (sampleFormat) {
            case SAMPLEFORMAT_IEEEFP:
                if (bitsPerSample == 32)
                    return reinterpret_cast<const float*>(buf)[idx];
                if (bitsPerSample == 64)
                    return static_cast<float>(reinterpret_cast<const double*>(buf)[idx]);
                break;
            case SAMPLEFORMAT_INT:
                if (bitsPerSample == 16)
                    return static_cast<float>(reinterpret_cast<const int16_t*>(buf)[idx]);
                if (bitsPerSample == 32)
                    return static_cast<float>(reinterpret_cast<const int32_t*>(buf)[idx]);
                break;
            case SAMPLEFORMAT_UINT:
                if (bitsPerSample == 8)
                    return static_cast<float>(buf[idx]);
                if (bitsPerSample == 16)
                    return static_cast<float>(reinterpret_cast<const uint16_t*>(buf)[idx]);
                if (bitsPerSample == 32)
                    return static_cast<float>(reinterpret_cast<const uint32_t*>(buf)[idx]);
                break;
            }
            return tile.nodataValue;
        };

        if (isTiled) {
            // Tiled TIFF (Cloud Optimized GeoTIFF — GPXZ, some Copernicus)
            uint32_t tileWidth = 0, tileHeight = 0;
            if (!TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tileWidth) || tileWidth == 0) {
                return tile;
            }
            if (!TIFFGetField(tif, TIFFTAG_TILELENGTH, &tileHeight) || tileHeight == 0) {
                return tile;
            }

            tsize_t tileBufSize = TIFFTileSize(tif);
            if (tileBufSize <= 0) {
                return tile;
            }

            std::vector<uint8_t> tileBuf;
            try {
                tileBuf.resize(static_cast<size_t>(tileBufSize));
            } catch (const std::bad_alloc&) {
                return tile;
            }

            for (uint32_t ty = 0; ty < height; ty += tileHeight) {
                for (uint32_t tx = 0; tx < width; tx += tileWidth) {
                    uint32_t tileRow = ty / tileHeight;
                    uint32_t tileCol = tx / tileWidth;
                    tmsize_t bytesRead = TIFFReadTile(tif, tileBuf.data(), tileCol, tileRow, 0, 0);
                    if (bytesRead < 0) {
                        // Read failed — fill this tile's area with nodata
                        uint32_t rowsInTile = std::min(tileHeight, height - ty);
                        uint32_t colsInTile = std::min(tileWidth, width - tx);
                        for (uint32_t py = 0; py < rowsInTile; ++py) {
                            for (uint32_t px = 0; px < colsInTile; ++px) {
                                uint32_t outIdx = (ty + py) * width + (tx + px);
                                tile.elevations[outIdx] = tile.nodataValue;
                            }
                        }
                        continue;
                    }

                    // Copy pixels from tile to output
                    uint32_t rowsInTile = std::min(tileHeight, height - ty);
                    uint32_t colsInTile = std::min(tileWidth, width - tx);
                    size_t bufSize = static_cast<size_t>(bytesRead);
                    for (uint32_t py = 0; py < rowsInTile; ++py) {
                        for (uint32_t px = 0; px < colsInTile; ++px) {
                            uint32_t tileIdx = py * tileWidth + px;
                            uint32_t outIdx = (ty + py) * width + (tx + px);
                            tile.elevations[outIdx] = readPixelAsFloat(tileBuf.data(), bufSize, tileIdx);
                        }
                    }
                }
            }
            tile.valid = true;
        } else {
            // Stripped TIFF (scanline-based — SRTM, some Copernicus)
            tsize_t scanlineSize = TIFFScanlineSize(tif);
            if (scanlineSize <= 0) {
                return tile;
            }

            std::vector<uint8_t> scanlineBuf;
            try {
                scanlineBuf.resize(static_cast<size_t>(scanlineSize));
            } catch (const std::bad_alloc&) {
                return tile;
            }

            for (uint32_t y = 0; y < height; ++y) {
                if (TIFFReadScanline(tif, scanlineBuf.data(), y, 0) < 0) {
                    // Fill remaining rows with nodata
                    for (uint32_t yy = y; yy < height; ++yy) {
                        for (uint32_t x = 0; x < width; ++x) {
                            tile.elevations[yy * width + x] = tile.nodataValue;
                        }
                    }
                    break;
                }
                size_t bufSize = static_cast<size_t>(scanlineSize);
                for (uint32_t x = 0; x < width; ++x) {
                    tile.elevations[y * width + x] = readPixelAsFloat(scanlineBuf.data(), bufSize, x);
                }
            }
            tile.valid = true;
        }

        return tile;
    }

    // ============================================================
    // Auto-detect format from data
    // ============================================================
    static DemTile decodeAuto(const QByteArray& data, const QString& sourceName) {
        // Check if it's AAIGrid (text format)
        if (data.size() > 0 && data[0] == 'n') {
            // Starts with "ncols" — likely AAIGrid
            DemTile tile = decodeAaiGrid(data);
            if (tile.valid) return tile;
        }

        // Check if it's a PNG (magic bytes)
        if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' &&
            data[2] == 'N' && data[3] == 'G') {
            // It's a PNG — check source to determine encoding
            if (sourceName.contains("terrarium", Qt::CaseInsensitive)) {
                return decodeTerrarium(data);
            } else if (sourceName.contains("mapbox", Qt::CaseInsensitive) &&
                       sourceName.contains("terrain", Qt::CaseInsensitive)) {
                return decodeMapboxTerrainRgb(data);
            }
            // Try terrarium as default for PNG elevation tiles
            return decodeTerrarium(data);
        }

        // Try GeoTIFF
        if (data.size() >= 4 && data[0] == 'I' && data[1] == 'I') {
            // TIFF magic bytes (little-endian)
            return decodeGeoTiff(data);
        }
        if (data.size() >= 4 && data[0] == 'M' && data[1] == 'M') {
            // TIFF magic bytes (big-endian)
            return decodeGeoTiff(data);
        }

        // Fallback: try AAIGrid
        DemTile tile = decodeAaiGrid(data);
        if (tile.valid) return tile;

        return DemTile{};
    }

    // ============================================================
    // Resample DEM tile to target resolution
    // ============================================================
    // QGIS uses nearest-neighbor or bilinear; we use bilinear for smoothness
    static DemTile resample(const DemTile& src, int targetWidth, int targetHeight) {
        DemTile dst;
        if (!src.valid || src.width <= 0 || src.height <= 0 ||
            targetWidth <= 0 || targetHeight <= 0 ||
            src.elevations.size() < static_cast<size_t>(src.width * src.height)) {
            return dst;
        }
        dst.width = targetWidth;
        dst.height = targetHeight;
        dst.nodataValue = src.nodataValue;
        dst.elevations.resize(targetWidth * targetHeight);

        for (int y = 0; y < targetHeight; ++y) {
            for (int x = 0; x < targetWidth; ++x) {
                // Bilinear interpolation
                double srcX = static_cast<double>(x) * src.width / targetWidth;
                double srcY = static_cast<double>(y) * src.height / targetHeight;

                int x0 = static_cast<int>(srcX);
                int y0 = static_cast<int>(srcY);
                int x1 = std::min(x0 + 1, src.width - 1);
                int y1 = std::min(y0 + 1, src.height - 1);

                double fx = srcX - x0;
                double fy = srcY - y0;

                float v00 = src.elevations[y0 * src.width + x0];
                float v01 = src.elevations[y0 * src.width + x1];
                float v10 = src.elevations[y1 * src.width + x0];
                float v11 = src.elevations[y1 * src.width + x1];

                // Skip nodata
                if (v00 == src.nodataValue || v01 == src.nodataValue ||
                    v10 == src.nodataValue || v11 == src.nodataValue) {
                    dst.elevations[y * targetWidth + x] = src.nodataValue;
                } else {
                    double v = (1 - fx) * (1 - fy) * v00 +
                               fx * (1 - fy) * v01 +
                               (1 - fx) * fy * v10 +
                               fx * fy * v11;
                    dst.elevations[y * targetWidth + x] = static_cast<float>(v);
                }
            }
        }
        dst.valid = true;
        return dst;
    }

    // ============================================================
    // Convert DEM to QImage (for visualization/preview)
    // ============================================================
    static QImage toImage(const DemTile& tile, bool normalize = true) {
        QImage img(tile.width, tile.height, QImage::Format_Grayscale16);

        if (normalize) {
            // Find min/max (excluding nodata)
            float zMin = std::numeric_limits<float>::max();
            float zMax = std::numeric_limits<float>::lowest();
            for (float e : tile.elevations) {
                if (e != tile.nodataValue) {
                    zMin = std::min(zMin, e);
                    zMax = std::max(zMax, e);
                }
            }
            if (zMax <= zMin) { zMin = 0; zMax = 1; }
            float zRange = zMax - zMin;

            for (int y = 0; y < tile.height; ++y) {
                for (int x = 0; x < tile.width; ++x) {
                    float e = tile.elevations[y * tile.width + x];
                    if (e == tile.nodataValue) e = zMin;
                    uint16_t val = static_cast<uint16_t>(((e - zMin) / zRange) * 65535.0f);
                    img.setPixel(x, y, qRgba64(val, val, val, 65535));
                }
            }
        } else {
            // Direct elevation → 16-bit (clamped to 0-65535)
            for (int y = 0; y < tile.height; ++y) {
                for (int x = 0; x < tile.width; ++x) {
                    float e = tile.elevations[y * tile.width + x];
                    uint16_t val = static_cast<uint16_t>(std::max(0.0f, std::min(65535.0f, e)));
                    img.setPixel(x, y, qRgba64(val, val, val, 65535));
                }
            }
        }
        return img;
    }
};

} // namespace terrain
