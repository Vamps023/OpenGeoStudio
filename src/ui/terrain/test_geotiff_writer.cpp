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
    { 33550, 3, 3, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("ModelPixelScale") },
    { 33922, 6, 6, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("ModelTiepoint") },
    { 34735, -1, -1, TIFF_SHORT, FIELD_CUSTOM, true, true, const_cast<char*>("GeoKeyDirectory") },
    { 34736, -1, -1, TIFF_DOUBLE, FIELD_CUSTOM, true, true, const_cast<char*>("GeoDoubleParams") },
    { 34737, -1, -1, TIFF_ASCII, FIELD_CUSTOM, true, true, const_cast<char*>("GeoAsciiParams") },
};

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
        TIFFMergeFieldInfo(tif, kReadFieldInfo, sizeof(kReadFieldInfo) / sizeof(kReadFieldInfo[0]));
        TIFFSetWarningHandler(tiffSilentWarning);
        if (tif) {
            // Check tag 33550 (ModelPixelScale) — custom tag returns pointer
            printf("  Checking ModelPixelScale (33550)...\n"); fflush(stdout);
            double* scalePtr = nullptr;
            int hasScale = TIFFGetField(tif, 33550, &scalePtr);
            printf("  ModelPixelScale (33550): %s\n", hasScale ? "PRESENT" : "MISSING"); fflush(stdout);
            if (hasScale && scalePtr) {
                printf("    ScaleX=%.10f ScaleY=%.10f\n", scalePtr[0], scalePtr[1]); fflush(stdout);
            }

            // Check tag 33922 (ModelTiepoint) — custom tag returns pointer
            printf("  Checking ModelTiepoint (33922)...\n"); fflush(stdout);
            double* tiepointPtr = nullptr;
            int hasTiepoint = TIFFGetField(tif, 33922, &tiepointPtr);
            printf("  ModelTiepoint (33922): %s\n", hasTiepoint ? "PRESENT" : "MISSING"); fflush(stdout);
            if (hasTiepoint && tiepointPtr) {
                printf("    X=%.10f Y=%.10f\n", tiepointPtr[3], tiepointPtr[4]); fflush(stdout);
            }

            // Check tag 34735 (GeoKeyDirectory)
            uint16_t* geokeys = nullptr;
            uint16_t keycount = 0;
            bool hasKeys = TIFFGetField(tif, 34735, &keycount, &geokeys);
            std::cerr << "  GeoKeyDirectory (34735): " << (hasKeys ? "PRESENT" : "MISSING") << std::endl;
            if (hasKeys && geokeys) {
                int numKeys = geokeys[3];
                std::cerr << "    Number of keys: " << numKeys << std::endl;
            }

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
            double* scalePtr = nullptr;
            int hasScale = TIFFGetField(tif, 33550, &scalePtr);
            printf("  ModelPixelScale (33550): %s\n", hasScale ? "PRESENT" : "MISSING"); fflush(stdout);
            if (hasScale && scalePtr) {
                printf("    ScaleX=%.4f ScaleY=%.4f\n", scalePtr[0], scalePtr[1]); fflush(stdout);
            }

            double* tiepointPtr = nullptr;
            int hasTiepoint = TIFFGetField(tif, 33922, &tiepointPtr);
            printf("  ModelTiepoint (33922): %s\n", hasTiepoint ? "PRESENT" : "MISSING"); fflush(stdout);
            if (hasTiepoint && tiepointPtr) {
                printf("    X=%.4f Y=%.4f\n", tiepointPtr[3], tiepointPtr[4]); fflush(stdout);
            }

            uint16_t* geokeys = nullptr;
            uint16_t keycount = 0;
            bool hasKeys = TIFFGetField(tif, 34735, &keycount, &geokeys);
            std::cerr << "  GeoKeyDirectory (34735): " << (hasKeys ? "PRESENT" : "MISSING") << std::endl;
            if (hasKeys && geokeys) {
                int numKeys = geokeys[3];
                std::cerr << "    Number of keys: " << numKeys << std::endl;
                // Print the ProjectedCSTypeGeoKey (should be 32630)
                for (int i = 4; i < 4 + numKeys * 4; i += 4) {
                    if (geokeys[i] == 3072) {  // PROJECTEDCSTYPE_GEOKEY
                        std::cerr << "    ProjectedCSTypeGeoKey = " << geokeys[i+3] << std::endl;
                    }
                }
            }

            TIFFClose(tif);
        }
        QFile::remove(path);
    }

    std::cerr << "\n=== Done ===" << std::endl;
    return 0;
}
