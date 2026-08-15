// RasterWriter — QGIS-inspired GeoTIFF/PNG raster writer implementation

#include "RasterWriter.hpp"

#include <tiffio.h>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <cmath>
#include <cstring>

// ============================================================
// GeoTIFF tag constants (from GeoTIFF spec, same as libgeotiff uses)
// ============================================================
#ifndef TIFFTAG_GEODOUBLEPARAMS
#define TIFFTAG_GEODOUBLEPARAMS 34736
#endif
#ifndef TIFFTAG_GEOASCIIPARAMS
#define TIFFTAG_GEOASCIIPARAMS 34737
#endif

// ============================================================
// GeoTIFF tag registration
// ============================================================
// libtiff does NOT know about GeoTIFF tags (33550, 33922, 34735, 34736, 34737)
// by default. They are defined in libgeotiff, which we don't link against.
// Without registration, TIFFSetField silently fails and the tags are never
// written, producing a plain TIFF instead of a GeoTIFF.
//
// We register them here using TIFFMergeFieldInfo so libtiff knows the type
// and count for each tag.

static const TIFFFieldInfo kGeoTiffFieldInfo[] = {
    // All GeoTIFF tags use variable count (-1, -1) to match libgeotiff convention.
    // This allows TIFFSetField(tif, tag, count, pointer) for all array tags.
    { 33550, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("ModelPixelScale") },
    { 33922, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("ModelTiepoint") },
    { 34735, -1, -1, TIFF_SHORT, FIELD_CUSTOM, true, true, const_cast<char*>("GeoKeyDirectory") },
    { 34736, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("GeoDoubleParams") },
    { 34737, -1, -1, TIFF_ASCII, FIELD_CUSTOM, true, true, const_cast<char*>("GeoAsciiParams") },
    { 42113, -1, -1, TIFF_ASCII, FIELD_CUSTOM, true, true, const_cast<char*>("GDAL_NODATA") },
};

static void tiffSilentWarning(const char*, const char*, va_list) {}

static void registerGeoTiffTags(TIFF* tif) {
    // Must be called for every TIFFOpen handle — tag registration is per-handle
    TIFFMergeFieldInfo(tif, kGeoTiffFieldInfo,
                       sizeof(kGeoTiffFieldInfo) / sizeof(kGeoTiffFieldInfo[0]));
    // Suppress "Unknown tag" warnings
    TIFFSetWarningHandler(tiffSilentWarning);
}

// GeoKey IDs (from GeoTIFF spec)
static constexpr uint16_t GTMODELTYPE_GEOKEY     = 1024;
static constexpr uint16_t GTRASTERTYPE_GEOKEY    = 1025;
static constexpr uint16_t GTCITATION_GEOKEY      = 1026;
static constexpr uint16_t GEOGRAPHICTYPE_GEOKEY  = 2048;
static constexpr uint16_t GEOGCITATION_GEOKEY    = 2049;
static constexpr uint16_t GEOGGEODETICDATUM_GEOKEY = 2050;
static constexpr uint16_t GEOGPRIMEMERIDIAN_GEOKEY = 2051;
static constexpr uint16_t GEOGANGULARUNITS_GEOKEY  = 2054;
static constexpr uint16_t GEOGSEMIMAJORAXIS_GEOKEY = 2057;
static constexpr uint16_t GEOGINVFLATTENING_GEOKEY = 2059;
static constexpr uint16_t PROJECTEDCSTYPE_GEOKEY = 3072;
static constexpr uint16_t PCSCITATION_GEOKEY     = 3073;
static constexpr uint16_t PROJLINEARUNITS_GEOKEY = 3076;

// GeoKey values
static constexpr uint16_t MODELTYPE_GEOGRAPHIC = 2;
static constexpr uint16_t MODELTYPE_PROJECTED  = 3;
static constexpr uint16_t RASTERTYPE_PIXELAREA = 1;
static constexpr uint16_t ANGULARUNIT_DEGREE   = 9102;
static constexpr uint16_t LINEARUNIT_METER     = 9001;
static constexpr uint16_t PRIMEMERIDIAN_GREENWICH = 8901;

// WGS84 constants
static constexpr double WGS84_SEMI_MAJOR = 6378137.0;
static constexpr double WGS84_INV_FLATTENING = 298.257223563;
static constexpr uint16_t DATUM_WGS84 = 6326;

// ============================================================
// Lat/Lon to UTM conversion (WGS84)
// ============================================================
// Converts WGS84 lat/lon to UTM easting/northing in meters.
// Used when writing GeoTIFFs with UTM projection but source bounds
// are in lat/lon degrees.
static void latLonToUtm(double lat, double lon, int& zone, bool& north, double& easting, double& northing) {
    // Compute UTM zone from longitude
    zone = static_cast<int>((lon + 180.0) / 6.0) + 1;
    north = lat >= 0.0;

    // WGS84 ellipsoid parameters
    const double a = 6378137.0;              // semi-major axis
    const double f = 1.0 / 298.257223563;    // flattening
    const double k0 = 0.9996;                // scale factor
    const double e2 = f * (2.0 - f);         // eccentricity squared
    const double e2sq = e2 * e2;
    const double e2cubed = e2sq * e2;

    double latRad = lat * M_PI / 180.0;
    double lonRad = lon * M_PI / 180.0;
    double lonOrigin = (zone - 1) * 6.0 - 180.0 + 3.0;
    double lonOriginRad = lonOrigin * M_PI / 180.0;

    double N = a / sqrt(1.0 - e2 * sin(latRad) * sin(latRad));
    double T = tan(latRad) * tan(latRad);
    double C = e2sq * cos(latRad) * cos(latRad) / (1.0 - e2);
    double A = cos(latRad) * (lonRad - lonOriginRad);

    double M = a * ((1.0 - e2/4.0 - 3.0*e2sq/64.0 - 5.0*e2cubed/256.0) * latRad
              - (3.0*e2/8.0 + 3.0*e2sq/32.0 + 45.0*e2cubed/1024.0) * sin(2.0*latRad)
              + (15.0*e2sq/256.0 + 45.0*e2cubed/1024.0) * sin(4.0*latRad)
              - (35.0*e2cubed/3072.0) * sin(6.0*latRad));

    easting = k0 * N * (A + (1.0 - T + C) * A*A*A / 6.0
              + (5.0 - 18.0*T + T*T + 72.0*C - 58.0*e2sq) * A*A*A*A*A / 120.0) + 500000.0;

    northing = k0 * (M + N * tan(latRad) * (A*A / 2.0
              + (5.0 - T + 9.0*C + 4.0*C*C) * A*A*A*A / 24.0
              + (61.0 - 58.0*T + T*T + 600.0*C - 330.0*e2sq) * A*A*A*A*A*A / 720.0));

    if (!north) northing += 10000000.0;  // Southern hemisphere offset
}

// ============================================================
// GeoTransform calculation (QGIS globalOutputParameters pattern)
// ============================================================
std::array<double, 6> RasterWriter::computeGeoTransform(
    int width, int height,
    const terrain::RasterExtent& extent)
{
    // QGIS pattern:
    //   geoTransform[0] = extent.xMinimum()  (top-left X)
    //   geoTransform[1] = pixelSize          (pixel width)
    //   geoTransform[2] = 0                  (rotation X)
    //   geoTransform[3] = extent.yMaximum()  (top-left Y)
    //   geoTransform[4] = 0                  (rotation Y)
    //   geoTransform[5] = -(extent.height() / nRows)  (pixel height, negative)
    std::array<double, 6> gt;
    gt[0] = extent.west;
    gt[1] = (extent.east - extent.west) / width;
    gt[2] = 0.0;
    gt[3] = extent.north;
    gt[4] = 0.0;
    gt[5] = -(extent.north - extent.south) / height;
    return gt;
}

// ============================================================
// GeoKey directory construction
// ============================================================
RasterWriter::GeoKeySet RasterWriter::buildGeoKeys(
    terrain::GeoCrsMode crsMode, int utmEpsg)
{
    GeoKeySet set;

    // GeoKey directory format:
    // Header: {KeyDirectoryVersion=1, KeyRevision=1, MinorRevision=0, NumberOfKeys}
    // Each key: {KeyID, TIFFTagLocation, Count, Value_Offset}
    //   TIFFTagLocation=0 → Value_Offset is the short value directly
    //   TIFFTagLocation=34736 → Value_Offset is index into doubleParams
    //   TIFFTagLocation=34737 → Value_Offset is byte offset into asciiParams

    if (crsMode == terrain::GeoCrsMode::WGS84) {
        // EPSG:4326 — Geographic WGS84
        // Keys: ModelType, RasterType, GeographicType, GeodeticDatum,
        //        PrimeMeridian, AngularUnits, SemiMajorAxis, InvFlattening
        const int numKeys = 8;
        set.keyDirectory.resize(4 + numKeys * 4);

        // Header
        set.keyDirectory[0] = 1;  // Version
        set.keyDirectory[1] = 1;  // Key revision
        set.keyDirectory[2] = 0;  // Minor revision
        set.keyDirectory[3] = static_cast<uint16_t>(numKeys);

        int idx = 4;
        // Key 1: GTModelTypeGeoKey = Geographic (2)
        set.keyDirectory[idx++] = GTMODELTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;  // Value inline
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = MODELTYPE_GEOGRAPHIC;

        // Key 2: GTRasterTypeGeoKey = PixelIsArea (1)
        set.keyDirectory[idx++] = GTRASTERTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = RASTERTYPE_PIXELAREA;

        // Key 3: GeographicTypeGeoKey = EPSG:4326
        set.keyDirectory[idx++] = GEOGRAPHICTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = 4326;

        // Key 4: GeogGeodeticDatumGeoKey = WGS84 (6326)
        set.keyDirectory[idx++] = GEOGGEODETICDATUM_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = DATUM_WGS84;

        // Key 5: GeogPrimeMeridianGeoKey = Greenwich (8901)
        set.keyDirectory[idx++] = GEOGPRIMEMERIDIAN_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = PRIMEMERIDIAN_GREENWICH;

        // Key 6: GeogAngularUnitsGeoKey = Degree (9102)
        set.keyDirectory[idx++] = GEOGANGULARUNITS_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = ANGULARUNIT_DEGREE;

        // Key 7: GeogSemiMajorAxisGeoKey = 6378137.0 (double param)
        set.doubleParams.push_back(WGS84_SEMI_MAJOR);
        set.keyDirectory[idx++] = GEOGSEMIMAJORAXIS_GEOKEY;
        set.keyDirectory[idx++] = TIFFTAG_GEODOUBLEPARAMS;  // 34736
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = static_cast<uint16_t>(set.doubleParams.size() - 1);

        // Key 8: GeogInvFlatteningGeoKey = 298.257223563 (double param)
        set.doubleParams.push_back(WGS84_INV_FLATTENING);
        set.keyDirectory[idx++] = GEOGINVFLATTENING_GEOKEY;
        set.keyDirectory[idx++] = TIFFTAG_GEODOUBLEPARAMS;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = static_cast<uint16_t>(set.doubleParams.size() - 1);

    } else if (crsMode == terrain::GeoCrsMode::WebMercator) {
        // EPSG:3857 — Web Mercator (Projected)
        const int numKeys = 4;
        set.keyDirectory.resize(4 + numKeys * 4);

        set.keyDirectory[0] = 1;
        set.keyDirectory[1] = 1;
        set.keyDirectory[2] = 0;
        set.keyDirectory[3] = static_cast<uint16_t>(numKeys);

        int idx = 4;
        // GTModelTypeGeoKey = Projected (3)
        set.keyDirectory[idx++] = GTMODELTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = MODELTYPE_PROJECTED;

        // GTRasterTypeGeoKey = PixelIsArea
        set.keyDirectory[idx++] = GTRASTERTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = RASTERTYPE_PIXELAREA;

        // ProjectedCSTypeGeoKey = EPSG:3857
        set.keyDirectory[idx++] = PROJECTEDCSTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = 3857;

        // ProjLinearUnitsGeoKey = Meter (9001)
        set.keyDirectory[idx++] = PROJLINEARUNITS_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = LINEARUNIT_METER;

    } else if (crsMode == terrain::GeoCrsMode::UTM) {
        // UTM zone (e.g., EPSG:32633 for UTM 33N, 32733 for UTM 33S)
        int epsg = utmEpsg;
        if (epsg <= 0) epsg = 32633;  // Default UTM 33N

        const int numKeys = 4;
        set.keyDirectory.resize(4 + numKeys * 4);

        set.keyDirectory[0] = 1;
        set.keyDirectory[1] = 1;
        set.keyDirectory[2] = 0;
        set.keyDirectory[3] = static_cast<uint16_t>(numKeys);

        int idx = 4;
        set.keyDirectory[idx++] = GTMODELTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = MODELTYPE_PROJECTED;

        set.keyDirectory[idx++] = GTRASTERTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = RASTERTYPE_PIXELAREA;

        set.keyDirectory[idx++] = PROJECTEDCSTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = static_cast<uint16_t>(epsg);

        set.keyDirectory[idx++] = PROJLINEARUNITS_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = LINEARUNIT_METER;
    }

    return set;
}

// ============================================================
// Compression tag mapping
// ============================================================
int RasterWriter::compressionTag(terrain::Compression c) {
    switch (c) {
    case terrain::Compression::Deflate: return COMPRESSION_DEFLATE;
    case terrain::Compression::LZW:     return COMPRESSION_LZW;
    case terrain::Compression::None:
    default:                            return COMPRESSION_NONE;
    }
}

// ============================================================
// Set GeoTIFF tags on an open TIFF handle
// ============================================================
void RasterWriter::setGeoTags(void* tifPtr, int width, int height,
                               const terrain::RasterExtent& extent,
                               const GeoKeySet& geoKeys)
{
    TIFF* tif = static_cast<TIFF*>(tifPtr);

    // ModelPixelScale (tag 33550): [ScaleX, ScaleY, ScaleZ]
    double scaleX = (extent.east - extent.west) / width;
    double scaleY = (extent.north - extent.south) / height;
    double pixelScale[3] = {scaleX, scaleY, 0.0};
    TIFFSetField(tif, 33550, 3, pixelScale);

    // ModelTiepoint (tag 33922): [I, J, K, X, Y, Z]
    // Maps raster (0,0) to world (west, north)
    double tiepoint[6] = {0.0, 0.0, 0.0, extent.west, extent.north, 0.0};
    TIFFSetField(tif, 33922, 6, tiepoint);

    // GeoKeyDirectory (tag 34735) — variable count SHORT array
    if (!geoKeys.keyDirectory.empty()) {
        TIFFSetField(tif, 34735,
                     static_cast<uint32_t>(geoKeys.keyDirectory.size()),
                     geoKeys.keyDirectory.data());
    }

    // GeoDoubleParams (tag 34736) — variable count DOUBLE array
    if (!geoKeys.doubleParams.empty()) {
        TIFFSetField(tif, 34736,
                     static_cast<uint32_t>(geoKeys.doubleParams.size()),
                     geoKeys.doubleParams.data());
    }

    // GeoAsciiParams (tag 34737) — ASCII string
    if (!geoKeys.asciiParams.empty()) {
        TIFFSetField(tif, 34737, geoKeys.asciiParams.c_str());
    }
}

// ============================================================
// Internal: write 1-band GeoTIFF
// ============================================================
bool RasterWriter::writeGeoTiffBand(
    const QString& path,
    const void* data,
    int width, int height,
    terrain::RasterDataType dataType,
    const terrain::RasterExtent& extent,
    double nodataValue,
    terrain::Compression compression)
{
    TIFF* tif = TIFFOpen(path.toUtf8().constData(), "w");
    if (!tif) return false;

    // Register GeoTIFF tags so TIFFSetField doesn't silently fail
    registerGeoTiffTags(tif);

    // Determine bits per sample and sample format
    uint16_t bitsPerSample = 8;
    uint16_t sampleFormat = SAMPLEFORMAT_UINT;

    switch (dataType) {
    case terrain::RasterDataType::Byte:
        bitsPerSample = 8;
        sampleFormat = SAMPLEFORMAT_UINT;
        break;
    case terrain::RasterDataType::UInt16:
        bitsPerSample = 16;
        sampleFormat = SAMPLEFORMAT_UINT;
        break;
    case terrain::RasterDataType::Int16:
        bitsPerSample = 16;
        sampleFormat = SAMPLEFORMAT_INT;
        break;
    case terrain::RasterDataType::Float32:
        bitsPerSample = 32;
        sampleFormat = SAMPLEFORMAT_IEEEFP;
        break;
    }

    // Basic TIFF tags
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, bitsPerSample);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, sampleFormat);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    // Use libtiff's default strip size (typically ~8KB) instead of 1 row/strip
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));

    // Compression (QGIS/GDAL creation option pattern)
    int comp = compressionTag(compression);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, comp);
    if (comp != COMPRESSION_NONE) {
        TIFFSetField(tif, TIFFTAG_PREDICTOR, PREDICTOR_NONE);
    }

    // Nodata value (QGIS pattern: GDALSetRasterNoDataValue)
    TIFFSetField(tif, TIFFTAG_GDAL_NODATA, nodataValue);

    // GeoTIFF tags
    GeoKeySet geoKeys = buildGeoKeys(extent.crsMode, extent.utmEpsg);
    setGeoTags(tif, width, height, extent, geoKeys);

    // Write scanlines
    int bytesPerSample = bitsPerSample / 8;
    const uint8_t* src = static_cast<const uint8_t*>(data);
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + (y * width * bytesPerSample);
        if (TIFFWriteScanline(tif, const_cast<uint8_t*>(row), y, 0) < 0) {
            TIFFClose(tif);
            return false;
        }
    }

    TIFFClose(tif);
    return true;
}

// ============================================================
// Public: Float32 GeoTIFF
// ============================================================
bool RasterWriter::writeFloat32GeoTiff(
    const QString& path,
    const std::vector<float>& elevations,
    int width, int height,
    const terrain::RasterExtent& extent,
    float nodataValue,
    terrain::Compression compression)
{
    if (static_cast<int>(elevations.size()) < width * height) return false;
    return writeGeoTiffBand(path, elevations.data(), width, height,
                            terrain::RasterDataType::Float32,
                            extent, nodataValue, compression);
}

// ============================================================
// Public: Int16 GeoTIFF
// ============================================================
bool RasterWriter::writeInt16GeoTiff(
    const QString& path,
    const std::vector<int16_t>& values,
    int width, int height,
    const terrain::RasterExtent& extent,
    int16_t nodataValue,
    terrain::Compression compression)
{
    if (static_cast<int>(values.size()) < width * height) return false;
    return writeGeoTiffBand(path, values.data(), width, height,
                            terrain::RasterDataType::Int16,
                            extent, static_cast<double>(nodataValue), compression);
}

// ============================================================
// Public: UInt16 GeoTIFF
// ============================================================
bool RasterWriter::writeUInt16GeoTiff(
    const QString& path,
    const std::vector<uint16_t>& values,
    int width, int height,
    const terrain::RasterExtent& extent,
    uint16_t nodataValue,
    terrain::Compression compression)
{
    if (static_cast<int>(values.size()) < width * height) return false;
    return writeGeoTiffBand(path, values.data(), width, height,
                            terrain::RasterDataType::UInt16,
                            extent, static_cast<double>(nodataValue), compression);
}

// ============================================================
// Public: Byte GeoTIFF
// ============================================================
bool RasterWriter::writeByteGeoTiff(
    const QString& path,
    const std::vector<uint8_t>& values,
    int width, int height,
    const terrain::RasterExtent& extent,
    uint8_t nodataValue,
    terrain::Compression compression)
{
    if (static_cast<int>(values.size()) < width * height) return false;
    return writeGeoTiffBand(path, values.data(), width, height,
                            terrain::RasterDataType::Byte,
                            extent, static_cast<double>(nodataValue), compression);
}

// ============================================================
// Public: RGB GeoTIFF (3-band, 8-bit per channel)
// ============================================================
bool RasterWriter::writeRgbGeoTiff(
    const QString& path,
    const QImage& image,
    const terrain::RasterExtent& extent,
    terrain::Compression compression)
{
    TIFF* tif = TIFFOpen(path.toUtf8().constData(), "w");
    if (!tif) return false;

    // Register GeoTIFF tags so TIFFSetField doesn't silently fail
    registerGeoTiffTags(tif);

    int width = image.width();
    int height = image.height();

    // Basic tags
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);  // RGB
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tif, 0));

    // Compression
    int comp = compressionTag(compression);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, comp);

    // GeoTIFF tags
    GeoKeySet geoKeys = buildGeoKeys(extent.crsMode, extent.utmEpsg);
    setGeoTags(tif, width, height, extent, geoKeys);

    // Write scanlines (convert to RGB888)
    QImage rgbImg = image.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = rgbImg.scanLine(y);
        if (TIFFWriteScanline(tif, const_cast<unsigned char*>(row), y, 0) < 0) {
            TIFFClose(tif);
            return false;
        }
    }

    TIFFClose(tif);
    return true;
}

// ============================================================
// World file generation (QGIS layout exporter pattern)
// ============================================================
bool RasterWriter::writeWorldFile(
    const QString& imagePath,
    int width, int height,
    const terrain::RasterExtent& extent)
{
    // World file naming: image.png → image.pgw, image.tif → image.tfw
    // QGIS pattern: {basename}.{first}{last}w
    QString worldPath = imagePath;
    QFileInfo fi(imagePath);
    QString suffix = fi.suffix().toLower();
    QString worldSuffix;

    if (suffix == "png") worldSuffix = "pgw";
    else if (suffix == "tif" || suffix == "tiff") worldSuffix = "tfw";
    else if (suffix == "jpg" || suffix == "jpeg") worldSuffix = "jgw";
    else worldSuffix = suffix.left(1) + suffix.right(1) + "w";

    worldPath = fi.absolutePath() + "/" + fi.completeBaseName() + "." + worldSuffix;

    // GeoTransform (QGIS pattern)
    auto gt = computeGeoTransform(width, height, extent);

    // World file format (6 lines):
    //   Line 1: pixel X size (a)
    //   Line 2: rotation about Y axis (d) — usually 0
    //   Line 3: rotation about X axis (b) — usually 0
    //   Line 4: pixel Y size (e) — negative for top-down
    //   Line 5: X coordinate of center of upper-left pixel (c)
    //   Line 6: Y coordinate of center of upper-left pixel (f)
    QFile file(worldPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;

    QTextStream out(&file);
    out.setRealNumberNotation(QTextStream::FixedNotation);
    out.setRealNumberPrecision(12);
    out << gt[1] << "\r\n";   // Pixel X size
    out << gt[4] << "\r\n";   // Rotation Y (0)
    out << gt[2] << "\r\n";   // Rotation X (0)
    out << gt[5] << "\r\n";   // Pixel Y size (negative)
    // Center of upper-left pixel = origin + half pixel
    out << (gt[0] + gt[1] / 2.0) << "\r\n";  // Origin X (center)
    out << (gt[3] + gt[5] / 2.0) << "\r\n";  // Origin Y (center)
    file.close();

    return true;
}

// ============================================================
// PNG + World file
// ============================================================
bool RasterWriter::writePngWithWorldFile(
    const QString& path,
    const QImage& image,
    const terrain::RasterExtent& extent)
{
    // Save PNG
    if (!image.save(path, "PNG")) return false;

    // Write world file
    return writeWorldFile(path, image.width(), image.height(), extent);
}
