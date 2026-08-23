#pragma once

// ============================================================
// RasterWriter — QGIS-inspired GeoTIFF/PNG raster writer
// ============================================================
//
// Adopts QGIS patterns from QgsRasterFileWriter and QgsGdalProvider:
//   - Proper GeoKey directory construction (EPSG:4326, 3857, UTM)
//   - GeoTransform calculation from extent (QGIS globalOutputParameters)
//   - Compression options (Deflate, LZW, None) like GDAL creation options
//   - Nodata value support per band
//   - Float32 / Int16 / UInt16 / Byte data types
//   - World file generation for PNG (QGS layout exporter pattern)
//   - RGB (3-band) and grayscale (1-band) output
//
// Uses libtiff directly (no GDAL dependency) but follows the same
// tag conventions that GDAL/libgeotiff would set.
//

#include <QString>
#include <QImage>
#include <vector>
#include <array>
#include <cstdint>
#include <string>

namespace terrain {

// Data types matching GDAL's GDALDataType / QGIS's Qgis::DataType
enum class RasterDataType {
    Byte,       // 8-bit unsigned
    UInt16,     // 16-bit unsigned
    Int16,      // 16-bit signed
    Float32,    // 32-bit float (full precision elevation)
};

// Compression options matching GDAL creation options
enum class Compression {
    None,
    Deflate,    // COMPRESS=DEFLATE
    LZW,        // COMPRESS=LZW
};

// CRS modes for GeoTIFF embedding
enum class GeoCrsMode {
    WGS84,          // EPSG:4326 (geographic lat/lon)
    WebMercator,    // EPSG:3857 (projected meters)
    UTM,            // EPSG:326xx / 327xx (projected meters)
};

// Raster extent + CRS specification for writing
struct RasterExtent {
    double west = 0;
    double east = 0;
    double north = 0;
    double south = 0;
    GeoCrsMode crsMode = GeoCrsMode::WGS84;
    int utmZone = 33;       // For UTM mode (positive = north, negative = south)
    int utmEpsg = 0;        // Override (e.g., 25832 for ETRS89 UTM 32N)
};

} // namespace terrain

class RasterWriter {
public:
    // ============================================================
    // GeoTIFF — Heightmap (1-band grayscale)
    // ============================================================

    // Write Float32 GeoTIFF (full precision elevation — QGIS preferred for DEM)
    static bool writeFloat32GeoTiff(
        const QString& path,
        const std::vector<float>& elevations,  // row-major, width*height
        int width, int height,
        const terrain::RasterExtent& extent,
        float nodataValue = -9999.0f,
        terrain::Compression compression = terrain::Compression::Deflate);

    // Write Int16 GeoTIFF (signed, for raw DEM values in meters)
    static bool writeInt16GeoTiff(
        const QString& path,
        const std::vector<int16_t>& values,
        int width, int height,
        const terrain::RasterExtent& extent,
        int16_t nodataValue = -9999,
        terrain::Compression compression = terrain::Compression::Deflate);

    // Write UInt16 GeoTIFF (normalized 0-65535)
    static bool writeUInt16GeoTiff(
        const QString& path,
        const std::vector<uint16_t>& values,
        int width, int height,
        const terrain::RasterExtent& extent,
        uint16_t nodataValue = 0,
        terrain::Compression compression = terrain::Compression::Deflate);

    // Write Byte GeoTIFF (8-bit grayscale)
    static bool writeByteGeoTiff(
        const QString& path,
        const std::vector<uint8_t>& values,
        int width, int height,
        const terrain::RasterExtent& extent,
        uint8_t nodataValue = 0,
        terrain::Compression compression = terrain::Compression::None);

    // ============================================================
    // GeoTIFF — RGB Imagery (3-band)
    // ============================================================

    // Write RGB GeoTIFF from QImage (8-bit per channel)
    static bool writeRgbGeoTiff(
        const QString& path,
        const QImage& image,
        const terrain::RasterExtent& extent,
        terrain::Compression compression = terrain::Compression::Deflate);

    // ============================================================
    // PNG + World File (QGIS layout exporter pattern)
    // ============================================================

    // Write PNG image with accompanying world file (.pgw/.tfw)
    static bool writePngWithWorldFile(
        const QString& path,
        const QImage& image,
        const terrain::RasterExtent& extent);

    // Write world file only (for existing images)
    static bool writeWorldFile(
        const QString& imagePath,
        int width, int height,
        const terrain::RasterExtent& extent);

    // ============================================================
    // GeoTransform calculation (QGIS globalOutputParameters pattern)
    // ============================================================

    // Returns 6-element GeoTransform: [originX, pixelW, 0, originY, 0, pixelH]
    // pixelH is negative (top-down), matching GDAL convention
    static std::array<double, 6> computeGeoTransform(
        int width, int height,
        const terrain::RasterExtent& extent);

    static terrain::RasterExtent alignedSubExtent(
        const terrain::RasterExtent& fullSource,
        const terrain::RasterExtent& subSource,
        const terrain::RasterExtent& fullTarget);

    // ============================================================
    // CRS / GeoKey helpers
    // ============================================================

    // Build GeoKey directory for the given CRS mode
    // Returns the key directory array + optional double/ascii params
    struct GeoKeySet {
        std::vector<uint16_t> keyDirectory;
        std::vector<double> doubleParams;
        std::string asciiParams;
    };

    static GeoKeySet buildGeoKeys(terrain::GeoCrsMode crsMode, int utmEpsg = 0);

private:
    // Internal: write 1-band GeoTIFF with arbitrary data type
    static bool writeGeoTiffBand(
        const QString& path,
        const void* data,
        int width, int height,
        terrain::RasterDataType dataType,
        const terrain::RasterExtent& extent,
        double nodataValue,
        terrain::Compression compression);

    // Set GeoTIFF tags (ModelPixelScale, ModelTiepoint, GeoKeyDirectory)
    static void setGeoTags(void* tif, int width, int height,
                           const terrain::RasterExtent& extent,
                           const GeoKeySet& geoKeys);

    // Convert compression enum to libtiff constant
    static int compressionTag(terrain::Compression c);
};

