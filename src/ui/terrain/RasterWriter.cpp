// RasterWriter — QGIS-inspired GeoTIFF/PNG raster writer implementation

#include "RasterWriter.hpp"

#include <tiffio.h>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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
    // FIELD_CUSTOM is the reserved libtiff bit for custom tags. It lives
    // outside the standard tag bit range so it does not collide with them.
    { 33550, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("ModelPixelScale") },
    { 33922, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("ModelTiepoint") },
    { 34735, -1, -1, TIFF_SHORT,  FIELD_CUSTOM, true, true, const_cast<char*>("GeoKeyDirectory") },
    { 34736, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("GeoDoubleParams") },
    { 34737, -1, -1, TIFF_ASCII,  FIELD_CUSTOM, true, true, const_cast<char*>("GeoAsciiParams") },
    { 42113, -1, -1, TIFF_ASCII,  FIELD_CUSTOM, true, true, const_cast<char*>("GDAL_NODATA") },
};

static void tiffSilentWarning(const char*, const char*, va_list) {}

// ============================================================
// libtiff I/O callbacks that write through a Unicode-safe QFile
// ============================================================
// libtiff's TIFFOpen uses fopen() on Windows, which cannot handle Unicode.
// Using TIFFClientOpen lets libtiff read/write through Qt's QFile directly.

static tmsize_t tiffFileRead(thandle_t handle, void* buf, tmsize_t size) {
    QFile* f = static_cast<QFile*>(handle);
    if (!f || size <= 0) return 0;
    return f->read(static_cast<char*>(buf), static_cast<qint64>(size));
}

static tmsize_t tiffFileWrite(thandle_t handle, void* buf, tmsize_t size) {
    QFile* f = static_cast<QFile*>(handle);
    if (!f || size <= 0) return 0;
    return f->write(static_cast<const char*>(buf), static_cast<qint64>(size));
}

static toff_t tiffFileSeek(thandle_t handle, toff_t off, int whence) {
    QFile* f = static_cast<QFile*>(handle);
    if (!f) return 0;
    qint64 pos = 0;
    switch (whence) {
    case SEEK_SET: pos = static_cast<qint64>(off); break;
    case SEEK_CUR: pos = f->pos() + static_cast<qint64>(off); break;
    case SEEK_END: pos = f->size() + static_cast<qint64>(off); break;
    default: return (toff_t)f->pos();
    }
    if (pos < 0) pos = 0;
    if (!f->seek(pos)) return 0;
    return (toff_t)f->pos();
}

static toff_t tiffFileSize(thandle_t handle) {
    QFile* f = static_cast<QFile*>(handle);
    return f ? (toff_t)f->size() : 0;
}

static int tiffFileClose(thandle_t handle) {
    QFile* f = static_cast<QFile*>(handle);
    if (f) {
        f->close();
        delete f;
    }
    return 0;
}

static TIFF* openTiffForWrite(const QString& path) {
    QFile* f = new QFile(path);
    if (!f->open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        delete f;
        return nullptr;
    }
    TIFF* tif = TIFFClientOpen("qtfile", "w", f,
                               tiffFileRead, tiffFileWrite,
                               tiffFileSeek, tiffFileClose,
                               tiffFileSize, nullptr, nullptr);
    if (!tif) {
        f->close();
        delete f;
        // The Truncate above already created the file on disk — remove the
        // empty leftover so consumers never see a 0-byte GeoTIFF.
        QFile::remove(path);
    }
    return tif;
}

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

// GeoKey values — GeoTIFF spec 6.3.1.1: Projected=1, Geographic=2, Geocentric=3
static constexpr uint16_t MODELTYPE_GEOGRAPHIC = 2;
static constexpr uint16_t MODELTYPE_PROJECTED  = 1;
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
        const int numKeys = 6;
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

        // GeographicTypeGeoKey = WGS84 (4326). GDAL always pairs the
        // projected CS with its geographic CS; consumers that reproject
        // (QGIS, Unigine, ArcGIS) resolve the datum through this key.
        set.keyDirectory[idx++] = GEOGRAPHICTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = 4326;

        set.keyDirectory[idx++] = GEOGANGULARUNITS_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = ANGULARUNIT_DEGREE;

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

        const int numKeys = 6;
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

        // GeographicTypeGeoKey = WGS84 (4326) — see WebMercator note above.
        // Without it, strict reprojection pipelines cannot resolve the
        // datum of the projected coordinates.
        set.keyDirectory[idx++] = GEOGRAPHICTYPE_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = 4326;

        set.keyDirectory[idx++] = GEOGANGULARUNITS_GEOKEY;
        set.keyDirectory[idx++] = 0;
        set.keyDirectory[idx++] = 1;
        set.keyDirectory[idx++] = ANGULARUNIT_DEGREE;

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

    // GeoAsciiParams (tag 34737) — ASCII string with explicit byte count
    if (!geoKeys.asciiParams.empty()) {
        TIFFSetField(tif, 34737, (uint32_t)geoKeys.asciiParams.size() + 1, geoKeys.asciiParams.c_str());
    }
}

// ============================================================
// Manual GeoTIFF writer — 1:1 port of GeoTerrain's proven
// geotiff-writer.ts (the files Unigine accepts).
//
// Byte layout: little-endian, IFD at offset 8, single strip,
// external tag blobs after the IFD, pixel data last.
// GeoKeys use the GeoTIFF spec model codes Projected=1 /
// Geographic=2 (never 3=Geocentric) and always carry the
// citation + WGS84 ellipsoid parameters.
// ============================================================

// TIFF Deflate (32946) payloads are raw zlib streams. Qt's qCompress emits
// the same zlib stream behind a 4-byte big-endian size prefix — strip it.
// (zlib.lib itself is not linked to every consumer of this file.)

namespace {

constexpr uint16_t kTiffTypeAscii = 2;
constexpr uint16_t kTiffTypeShort = 3;
constexpr uint16_t kTiffTypeLong = 4;
constexpr uint16_t kTiffTypeDouble = 12;

size_t manualTypeSize(uint16_t t) {
    switch (t) {
    case 1: case 2: return 1;
    case 3: return 2;
    case 4: case 11: return 4;
    case 5: case 12: return 8;
    default: return 1;
    }
}

struct ManualEntry {
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    std::vector<uint64_t> values;   // SHORT/LONG values
    QByteArray ascii;               // ASCII payload (count included)
    std::vector<double> doubles;    // DOUBLE payload
};

ManualEntry shortEntry(uint16_t tag, std::vector<uint64_t> v) {
    ManualEntry e; e.tag = tag; e.type = kTiffTypeShort;
    e.count = static_cast<uint32_t>(v.size()); e.values = std::move(v); return e;
}
ManualEntry longEntry(uint16_t tag, uint64_t v) {
    ManualEntry e; e.tag = tag; e.type = kTiffTypeLong; e.count = 1; e.values = {v}; return e;
}
ManualEntry doubleEntry(uint16_t tag, std::vector<double> v) {
    ManualEntry e; e.tag = tag; e.type = kTiffTypeDouble;
    e.count = static_cast<uint32_t>(v.size()); e.doubles = std::move(v); return e;
}
ManualEntry asciiEntry(uint16_t tag, QByteArray bytes, uint32_t count) {
    ManualEntry e; e.tag = tag; e.type = kTiffTypeAscii; e.count = count;
    e.ascii = std::move(bytes); return e;
}

void putU16(QByteArray& b, qint64 off, uint16_t v) { memcpy(b.data() + off, &v, 2); }
void putU32(QByteArray& b, qint64 off, uint32_t v) { memcpy(b.data() + off, &v, 4); }
void putF64(QByteArray& b, qint64 off, double v) { memcpy(b.data() + off, &v, 8); }

QByteArray deflateZlib(const QByteArray& raw, bool& ok) {
    const QByteArray compressed = qCompress(raw, -1);  // -1 = zlib default level
    if (compressed.size() > 4) {
        ok = true;
        return compressed.mid(4);  // drop the size prefix → raw zlib stream
    }
    ok = false;
    return raw;
}

bool writeGeoTiffManual(const QString& path,
                        const QByteArray& pixelStrip,
                        int width, int height,
                        int bitsPerSample, int samplesPerPixel,
                        int sampleFormat, int photometric,
                        const terrain::RasterExtent& extent,
                        bool rasterIsPoint,
                        double nodataValue,
                        bool deflate)
{
    // ── CRS branch (mirrors geotiff-writer.ts) ──
    int epsg = 4326;
    bool isProjected = false;
    QString citation = QStringLiteral("WGS 84");
    if (extent.crsMode == terrain::GeoCrsMode::UTM) {
        epsg = extent.utmEpsg > 0 ? extent.utmEpsg : 32633;
        isProjected = true;
        const int zone = (epsg >= 32701) ? (epsg - 32700) : (epsg - 32600);
        citation = QStringLiteral("WGS 84 / UTM zone %1%2")
                       .arg(zone).arg(epsg < 32701 ? QStringLiteral("N") : QStringLiteral("S"));
    } else if (extent.crsMode == terrain::GeoCrsMode::WebMercator) {
        epsg = 3857;
        isProjected = true;
        citation = QStringLiteral("WGS 84 / Pseudo-Mercator");
    }

    // PixelIsPoint: samples sit at pixel corners → divisor (size - 1)
    const double divW = rasterIsPoint ? (width - 1) : width;
    const double divH = rasterIsPoint ? (height - 1) : height;
    const double pixelW = (extent.east - extent.west) / (divW > 0 ? divW : 1);
    const double pixelH = (extent.south - extent.north) / (divH > 0 ? divH : 1);  // negative, north-up
    if (std::abs(pixelW) < 1e-10 || std::abs(pixelH) < 1e-10) return false;

    std::vector<ManualEntry> entries;
    entries.push_back(longEntry(256, static_cast<uint64_t>(width)));
    entries.push_back(longEntry(257, static_cast<uint64_t>(height)));
    entries.push_back(shortEntry(258, std::vector<uint64_t>(
        samplesPerPixel, static_cast<uint64_t>(bitsPerSample))));
    entries.push_back(shortEntry(259, std::vector<uint64_t>{deflate ? 32946u : 1u}));
    entries.push_back(shortEntry(262, std::vector<uint64_t>{
        static_cast<uint64_t>(photometric)}));
    entries.push_back(longEntry(273, 0));                                   // StripOffsets (patched)
    entries.push_back(shortEntry(277, std::vector<uint64_t>{
        static_cast<uint64_t>(samplesPerPixel)}));
    entries.push_back(longEntry(278, static_cast<uint64_t>(height)));        // single strip
    entries.push_back(longEntry(279, static_cast<uint32_t>(pixelStrip.size())));  // patched if deflated
    entries.push_back(shortEntry(284, std::vector<uint64_t>{1}));
    entries.push_back(shortEntry(339, std::vector<uint64_t>(
        samplesPerPixel, static_cast<uint64_t>(sampleFormat))));

    entries.push_back(doubleEntry(33550, {std::abs(pixelW), std::abs(pixelH), 0.0}));
    entries.push_back(doubleEntry(33922, {0.0, 0.0, 0.0, extent.west, extent.north, 0.0}));

    // ── GeoKey directory ──
    const uint16_t rasterTypeCode = rasterIsPoint ? 2 : 1;
    std::vector<uint64_t> kd;
    auto key = [&kd](uint16_t id, uint16_t loc, uint16_t cnt, uint16_t val) {
        kd.push_back(id); kd.push_back(loc); kd.push_back(cnt); kd.push_back(val);
    };
    if (isProjected) {
        key(1024, 0, 1, 1);   // GTModelTypeGeoKey = Projected (1)
        key(1025, 0, 1, rasterTypeCode);
        key(3072, 0, 1, static_cast<uint16_t>(epsg));  // ProjectedCSTypeGeoKey
        key(3073, 34737, uint16_t(citation.size() + 1), 0);  // PCSCitation
        key(2057, 34736, 1, 0);  // GeogSemiMajorAxis → doubleParams[0]
        key(2059, 34736, 1, 1);  // GeogInvFlattening → doubleParams[1]
    } else {
        key(1024, 0, 1, 2);   // GTModelTypeGeoKey = Geographic (2)
        key(1025, 0, 1, rasterTypeCode);
        key(2048, 0, 1, static_cast<uint16_t>(epsg));  // GeographicTypeGeoKey
        key(2049, 34737, uint16_t(citation.size() + 1), 0);  // GeogCitation
        key(2054, 0, 1, 9102);  // GeogAngularUnits = Degree
        key(2057, 34736, 1, 0);
        key(2059, 34736, 1, 1);
    }
    // Header: version 1, revision 1.0, key count
    const uint32_t numKeys = uint32_t(kd.size() / 4);
    std::vector<uint64_t> geoKeys = {1, 1, 0, numKeys};
    geoKeys.insert(geoKeys.end(), kd.begin(), kd.end());
    entries.push_back(shortEntry(34735, std::move(geoKeys)));
    entries.push_back(doubleEntry(34736, {6378137.0, 298.257223563}));

    QByteArray asciiRaw = citation.toUtf8();
    asciiRaw.append('\0');
    if (asciiRaw.size() % 2 != 0) asciiRaw.append('\0');
    entries.push_back(asciiEntry(34737, asciiRaw, uint32_t(asciiRaw.size())));

    if (!std::isnan(nodataValue)) {
        const QByteArray nd = QByteArray::number(nodataValue, 'f', 6).append('\0');
        entries.push_back(asciiEntry(42113, nd, uint32_t(nd.size())));
    }

    std::sort(entries.begin(), entries.end(),
              [](const ManualEntry& a, const ManualEntry& b) { return a.tag < b.tag; });

    // ── Compression ──
    QByteArray stripData = pixelStrip;
    if (deflate) {
        bool ok = false;
        stripData = deflateZlib(pixelStrip, ok);
        if (!ok) {
            for (auto& e : entries)
                if (e.tag == 259) e.values = {1};
        } else {
            for (auto& e : entries)
                if (e.tag == 279) e.values = {uint64_t(stripData.size())};
        }
    }

    // ── Layout: header(8) + IFD + blobs + pixel data ──
    const qint64 ifdSize = 2 + qint64(entries.size()) * 12 + 4;
    qint64 cursor = 8 + ifdSize;

    struct Blob { int entryIndex; qint64 offset; QByteArray bytes; };
    std::vector<Blob> blobs;
    for (int i = 0; i < int(entries.size()); ++i) {
        const ManualEntry& e = entries[i];
        size_t sz = 0;
        QByteArray bytes;
        if (e.type == kTiffTypeAscii) {
            sz = size_t(e.ascii.size());
            bytes = e.ascii;
        } else if (e.type == kTiffTypeDouble) {
            sz = e.doubles.size() * 8;
            bytes.resize(int(sz));
            for (int j = 0; j < int(e.doubles.size()); ++j)
                putF64(bytes, j * 8, e.doubles[j]);
        } else {
            sz = e.values.size() * manualTypeSize(e.type);
            bytes.resize(int(sz));
            for (int j = 0; j < int(e.values.size()); ++j) {
                if (e.type == kTiffTypeShort)
                    putU16(bytes, j * 2, uint16_t(e.values[j]));
                else
                    putU32(bytes, j * 4, uint32_t(e.values[j]));
            }
        }
        if (sz > 4) {
            if (cursor % 2 != 0) cursor++;
            blobs.push_back({i, cursor, bytes});
            cursor += qint64(sz);
        }
    }
    const qint64 stripOffset = cursor;
    // Patch StripOffsets
    for (auto& e : entries)
        if (e.tag == 273) e.values = {uint64_t(stripOffset)};

    QByteArray file(int(stripOffset + stripData.size()), Qt::Uninitialized);
    file.fill(0);
    putU16(file, 0, 0x4949);          // "II"
    putU16(file, 2, 42);
    putU32(file, 4, 8);               // IFD at offset 8

    qint64 pos = 8;
    putU16(file, pos, uint16_t(entries.size()));
    pos += 2;
    for (int i = 0; i < int(entries.size()); ++i) {
        const ManualEntry& e = entries[i];
        putU16(file, pos, e.tag);
        putU16(file, pos + 2, e.type);
        putU32(file, pos + 4, e.count);
        // Value / offset field
        size_t sz = 0;
        if (e.type == kTiffTypeAscii) sz = size_t(e.ascii.size());
        else if (e.type == kTiffTypeDouble) sz = e.doubles.size() * 8;
        else sz = e.values.size() * manualTypeSize(e.type);

        if (sz <= 4) {
            QByteArray inlineBytes(4, Qt::Uninitialized);
            inlineBytes.fill(0);
            if (e.type == kTiffTypeAscii) {
                memcpy(inlineBytes.data(), e.ascii.constData(),
                       std::min<size_t>(e.ascii.size(), 4));
            } else if (e.type == kTiffTypeDouble) {
                // 1 double never fits inline; only 0-length would
            } else {
                for (int j = 0; j < int(e.values.size()); ++j) {
                    if (e.type == kTiffTypeShort && j < 2)
                        putU16(inlineBytes, j * 2, uint16_t(e.values[j]));
                    else if (e.type == kTiffTypeLong && j == 0)
                        putU32(inlineBytes, 0, uint32_t(e.values[0]));
                }
            }
            memcpy(file.data() + pos + 8, inlineBytes.constData(), 4);
        } else {
            // find blob offset
            qint64 off = 0;
            for (const auto& b : blobs)
                if (b.entryIndex == i) { off = b.offset; break; }
            putU32(file, pos + 8, uint32_t(off));
        }
        pos += 12;
    }
    putU32(file, pos, 0);             // next IFD = none
    for (const auto& b : blobs)
        memcpy(file.data() + b.offset, b.bytes.constData(), b.bytes.size());
    memcpy(file.data() + stripOffset, stripData.constData(), stripData.size());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    if (f.write(file) != file.size()) {
        f.close();
        QFile::remove(path);
        return false;
    }
    f.close();
    return true;
}

} // namespace

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
    int bitsPerSample = 8;
    int sampleFormat = 1;  // 1 = unsigned int

    switch (dataType) {
    case terrain::RasterDataType::Byte:
        bitsPerSample = 8;
        sampleFormat = 1;
        break;
    case terrain::RasterDataType::UInt16:
        bitsPerSample = 16;
        sampleFormat = 1;
        break;
    case terrain::RasterDataType::Int16:
        bitsPerSample = 16;
        sampleFormat = 2;  // signed int
        break;
    case terrain::RasterDataType::Float32:
        bitsPerSample = 32;
        sampleFormat = 3;  // IEEE float
        break;
    }

    const int bytesPerSample = bitsPerSample / 8;
    QByteArray strip(int(qint64(width) * height * bytesPerSample), Qt::Uninitialized);
    memcpy(strip.data(), data, size_t(strip.size()));

    // DEM bands use PixelIsPoint like GeoTerrain's heightmap exports
    // (samples at pixel corners; scale divisor size-1)
    return writeGeoTiffManual(path, strip, width, height,
                              bitsPerSample, 1, sampleFormat,
                              1 /* black-is-zero */, extent,
                              true /* rasterIsPoint */, nodataValue,
                              compression == terrain::Compression::Deflate ||
                              compression == terrain::Compression::LZW);
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
    const int width = image.width();
    const int height = image.height();

    // Imagery uses PixelIsArea (like GeoTerrain's albedo exports)
    QImage rgbImg = image.convertToFormat(QImage::Format_RGB888);
    QByteArray strip(int(qint64(width) * height * 3), Qt::Uninitialized);
    for (int y = 0; y < height; ++y)
        memcpy(strip.data() + qint64(y) * width * 3, rgbImg.scanLine(y),
               size_t(width) * 3);

    return writeGeoTiffManual(path, strip, width, height,
                              8, 3, 1 /* unsigned */,
                              2 /* RGB */, extent,
                              false /* rasterIsArea */,
                              std::numeric_limits<double>::quiet_NaN(),
                              compression == terrain::Compression::Deflate ||
                              compression == terrain::Compression::LZW);
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
