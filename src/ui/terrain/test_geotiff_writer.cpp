// test_geotiff_writer.cpp — Verify GeoTIFF tags are written correctly
//
// Creates a small test GeoTIFF with known coordinates and checks
// that the GeoTIFF tags (33550, 33922, 34735) are present.

#include <QCoreApplication>
#include <QString>
#include <QDir>
#include <iostream>
#include <tiffio.h>
#include <cmath>

#include "RasterWriter.hpp"

// Register GeoTIFF tags for reading (same as in RasterWriter.cpp)
static const TIFFFieldInfo kReadFieldInfo[] = {
    { 33550, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("ModelPixelScale") },
    { 33922, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("ModelTiepoint") },
    { 34735, -1, -1, TIFF_SHORT, FIELD_CUSTOM, true, true, const_cast<char*>("GeoKeyDirectory") },
    { 34736, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("GeoDoubleParams") },
    { 34737, -1, -1, TIFF_ASCII, FIELD_CUSTOM, true, true, const_cast<char*>("GeoAsciiParams") },
};

// Dump all geo tags of an open TIFF exactly as a reader (GDAL/Unigine) sees them
static void dumpGeoTags(TIFF* tif) {
    // passcount custom tags need (count*, data**) varargs
    uint16_t count = 0;
    double* doubles = nullptr;
    uint16_t* shorts = nullptr;

    if (TIFFGetField(tif, 33550, &count, &doubles) && doubles && count >= 3) {
        printf("  ModelPixelScale (33550): count=%u ScaleX=%.10f ScaleY=%.10f\n",
               count, doubles[0], doubles[1]);
    } else {
        printf("  ModelPixelScale (33550): MISSING!\n");
    }

    if (TIFFGetField(tif, 33922, &count, &doubles) && doubles && count >= 6) {
        printf("  ModelTiepoint (33922): count=%u raster(%.1f,%.1f) -> world(%.10f, %.10f)\n",
               count, doubles[0], doubles[1], doubles[3], doubles[4]);
    } else {
        printf("  ModelTiepoint (33922): MISSING!\n");
    }

    if (TIFFGetField(tif, 34735, &count, &shorts) && shorts && count >= 4) {
        const int numKeys = shorts[3];
        printf("  GeoKeyDirectory (34735): count=%u shorts, %d keys, header v%d.%d.%d\n",
               count, numKeys, shorts[0], shorts[1], shorts[2]);
        for (int i = 4; i + 3 < static_cast<int>(count) && (i - 4) / 4 < numKeys; i += 4) {
            printf("    key %u: location=%u count=%u value=%u\n",
                   shorts[i], shorts[i + 1], shorts[i + 2], shorts[i + 3]);
        }
    } else {
        printf("  GeoKeyDirectory (34735): MISSING!\n");
    }

    if (TIFFGetField(tif, 34736, &count, &doubles) && doubles && count > 0) {
        printf("  GeoDoubleParams (34736): count=%u", count);
        for (int i = 0; i < count && i < 4; ++i) printf(" [%.6f]", doubles[i]);
        printf("\n");
    }

    char* ascii = nullptr;
    if (TIFFGetField(tif, 34737, &count, &ascii) && ascii) {
        printf("  GeoAsciiParams (34737): count=%u '%s'\n", count, ascii);
    }
    fflush(stdout);
}

static void tiffSilentWarning(const char*, const char*, va_list) {}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    std::cerr << "=== GeoTIFF Writer Test ===" << std::endl;

    // Create a small 4x4 test raster
    int w = 4, h = 4;
    std::vector<float> elevations(w * h, 100.0f);
    for (int i = 0; i < w * h; i++) elevations[i] = 100.0f + i;

    // Test 1: WGS84
    {
        terrain::RasterExtent ext;
        ext.west = -0.5;
        ext.east = 0.5;
        ext.north = 51.5;
        ext.south = 51.0;
        ext.crsMode = terrain::GeoCrsMode::WGS84;

        QString path = QDir::tempPath() + "/test_geotiff_wgs84.tif";
        bool ok = RasterWriter::writeFloat32GeoTiff(path, elevations, w, h, ext, -9999.0f,
                                                      terrain::Compression::None);
        std::cerr << "WGS84 write: " << (ok ? "OK" : "FAILED") << std::endl;

        // Read back and check for GeoTIFF tags
        TIFF* tif = TIFFOpen(path.toUtf8().constData(), "r");
        if (tif) {
            TIFFMergeFieldInfo(tif, kReadFieldInfo, sizeof(kReadFieldInfo) / sizeof(kReadFieldInfo[0]));
            TIFFSetWarningHandler(tiffSilentWarning);
            dumpGeoTags(tif);
            TIFFClose(tif);
        }
        // Keep file for inspection
        printf("  File saved at: %s\n", path.toUtf8().constData()); fflush(stdout);
    }

    // Test 2: UTM
    {
        terrain::RasterExtent ext;
        ext.west = 500000;    // UTM easting
        ext.east = 600000;
        ext.north = 5700000;  // UTM northing
        ext.south = 5600000;
        ext.crsMode = terrain::GeoCrsMode::UTM;
        ext.utmEpsg = 32630;

        QString path = QDir::tempPath() + "/test_geotiff_utm.tif";
        bool ok = RasterWriter::writeFloat32GeoTiff(path, elevations, w, h, ext, -9999.0f,
                                                      terrain::Compression::None);
        std::cerr << "\nUTM write: " << (ok ? "OK" : "FAILED") << std::endl;

        TIFF* tif = TIFFOpen(path.toUtf8().constData(), "r");
        if (tif) {
            TIFFMergeFieldInfo(tif, kReadFieldInfo, sizeof(kReadFieldInfo) / sizeof(kReadFieldInfo[0]));
            TIFFSetWarningHandler(tiffSilentWarning);
            dumpGeoTags(tif);
            TIFFClose(tif);
        }
        QFile::remove(path);
    }

    std::cerr << "\n=== Done ===" << std::endl;
    return 0;
}
