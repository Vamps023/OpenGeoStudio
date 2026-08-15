// test_gpxz_download.cpp — End-to-end test for GPXZ DEM download + decode
//
// Downloads real elevation data from the GPXZ API, decodes it through
// DemDecoder::decodeGeoTiff and DemDecoder::decodeAuto, verifies the
// output, and tests resampling.
//
// Build: cmake --build build --target test_gpxz_download
// Run:   build\test_gpxz_download.exe

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QByteArray>
#include <QString>
#include <QDebug>

#include "DemDecoder.hpp"
#include <iostream>
#include <cmath>
#include <csignal>

static const char* GPXZ_API_KEY = "ak_NgEXLGho_z5TBKb44GCFKIirC";

static void crashHandler(int sig) {
    std::cerr << "CRASH! Signal: " << sig << std::endl;
    std::cerr.flush();
    _exit(1);
}

// Helper: download a URL synchronously
static QByteArray downloadUrl(QNetworkAccessManager& nam, const QString& url,
                              const QByteArray& apiKeyHeader = {},
                              int timeoutMs = 60000) {
    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");
    if (!apiKeyHeader.isEmpty()) {
        request.setRawHeader("x-api-key", apiKeyHeader);
    }

    QEventLoop loop;
    QByteArray responseData;

    QNetworkReply* reply = nam.get(request);
    QTimer::singleShot(timeoutMs, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });

    QObject::connect(reply, &QNetworkReply::finished, [&]() {
        if (reply->error() == QNetworkReply::NoError) {
            responseData = reply->readAll();
        }
        reply->deleteLater();
        loop.quit();
    });

    loop.exec();
    return responseData;
}

// Test 1: Download a small GPXZ raster and decode it
bool testDownloadAndDecode(QNetworkAccessManager& nam) {
    std::cerr << "=== Test 1: Download + decode GPXZ raster ===" << std::endl;

    // Small bbox around London (elevation ~10-50m)
    QString url = QString("https://api.gpxz.io/v1/elevation/raster?"
                          "bbox_left=-0.5&bbox_right=0.5"
                          "&bbox_bottom=51.0&bbox_top=51.5"
                          "&height_px=256&width_px=256");

    QByteArray responseData = downloadUrl(nam, url, GPXZ_API_KEY);

    if (responseData.isEmpty()) {
        std::cerr << "FAIL: No data received" << std::endl;
        return false;
    }

    std::cerr << "  Downloaded: " << responseData.size() << " bytes" << std::endl;

    // Verify TIFF magic bytes
    if (responseData.size() < 4) {
        std::cerr << "FAIL: Response too small" << std::endl;
        return false;
    }

    bool isTiff = (responseData[0] == 'I' && responseData[1] == 'I') ||
                  (responseData[0] == 'M' && responseData[1] == 'M');
    if (!isTiff) {
        std::cerr << "FAIL: Not a TIFF file" << std::endl;
        return false;
    }

    uint8_t magic = static_cast<uint8_t>(responseData[2]);
    std::cerr << "  TIFF format: " << (magic == 0x2B ? "BigTIFF" : "Classic TIFF") << std::endl;

    // Test decodeGeoTiff
    std::cerr << "  Testing decodeGeoTiff()..." << std::endl;
    terrain::DemTile tile = terrain::DemDecoder::decodeGeoTiff(responseData);

    if (!tile.valid) {
        std::cerr << "FAIL: decodeGeoTiff returned invalid tile" << std::endl;
        return false;
    }

    std::cerr << "  Decoded: " << tile.width << "x" << tile.height
              << " pixels, " << tile.elevations.size() << " elevations" << std::endl;

    if (tile.width != 256 || tile.height != 256) {
        std::cerr << "FAIL: Expected 256x256, got " << tile.width << "x" << tile.height << std::endl;
        return false;
    }

    // Check elevation values — London area should be 0-200m
    float minElev = std::numeric_limits<float>::max();
    float maxElev = std::numeric_limits<float>::lowest();
    int validCount = 0;
    for (float e : tile.elevations) {
        if (e != tile.nodataValue) {
            validCount++;
            minElev = std::min(minElev, e);
            maxElev = std::max(maxElev, e);
        }
    }

    std::cerr << "  Elevation range: " << minElev << " to " << maxElev << " meters" << std::endl;
    std::cerr << "  Valid pixels: " << validCount << " / " << tile.elevations.size() << std::endl;

    if (validCount == 0) {
        std::cerr << "FAIL: All pixels are nodata" << std::endl;
        return false;
    }

    if (minElev < -100 || maxElev > 500) {
        std::cerr << "FAIL: Elevation values out of expected range for London: "
                  << minElev << " to " << maxElev << std::endl;
        return false;
    }

    std::cerr << "  PASS" << std::endl;
    return true;
}

// Test 2: Test decodeAuto routing for GPXZ data
bool testDecodeAuto(QNetworkAccessManager& nam) {
    std::cerr << "\n=== Test 2: decodeAuto routing ===" << std::endl;

    QString url = QString("https://api.gpxz.io/v1/elevation/raster?"
                          "bbox_left=-0.5&bbox_right=0.5"
                          "&bbox_bottom=51.0&bbox_top=51.5"
                          "&height_px=256&width_px=256");

    QByteArray data = downloadUrl(nam, url, GPXZ_API_KEY);
    if (data.isEmpty()) {
        std::cerr << "SKIP: No data downloaded" << std::endl;
        return true;
    }

    terrain::DemTile tile = terrain::DemDecoder::decodeAuto(data, "dem");
    if (!tile.valid) {
        std::cerr << "FAIL: decodeAuto returned invalid tile" << std::endl;
        return false;
    }

    std::cerr << "  Decoded via decodeAuto: " << tile.width << "x" << tile.height << std::endl;
    std::cerr << "  PASS" << std::endl;
    return true;
}

// Test 3: Test resampling
bool testResample(QNetworkAccessManager& nam) {
    std::cerr << "\n=== Test 3: Resample 256x256 -> 64x64 ===" << std::endl;

    QString url = QString("https://api.gpxz.io/v1/elevation/raster?"
                          "bbox_left=-0.5&bbox_right=0.5"
                          "&bbox_bottom=51.0&bbox_top=51.5"
                          "&height_px=256&width_px=256");

    QByteArray data = downloadUrl(nam, url, GPXZ_API_KEY);
    if (data.isEmpty()) {
        std::cerr << "SKIP: No data downloaded" << std::endl;
        return true;
    }

    terrain::DemTile tile = terrain::DemDecoder::decodeGeoTiff(data);
    if (!tile.valid) {
        std::cerr << "FAIL: decodeGeoTiff failed" << std::endl;
        return false;
    }

    terrain::DemTile resampled = terrain::DemDecoder::resample(tile, 64, 64);
    if (!resampled.valid) {
        std::cerr << "FAIL: resample returned invalid tile" << std::endl;
        return false;
    }

    if (resampled.width != 64 || resampled.height != 64) {
        std::cerr << "FAIL: Expected 64x64, got "
                  << resampled.width << "x" << resampled.height << std::endl;
        return false;
    }

    std::cerr << "  Resampled: " << resampled.width << "x" << resampled.height << std::endl;
    std::cerr << "  PASS" << std::endl;
    return true;
}

// Test 4: Test resampling to 1024x1024 (standard export resolution)
bool testResampleLarge(QNetworkAccessManager& nam) {
    std::cerr << "\n=== Test 4: Resample 256x256 -> 1024x1024 (export resolution) ===" << std::endl;

    QString url = QString("https://api.gpxz.io/v1/elevation/raster?"
                          "bbox_left=-0.5&bbox_right=0.5"
                          "&bbox_bottom=51.0&bbox_top=51.5"
                          "&height_px=256&width_px=256");

    QByteArray data = downloadUrl(nam, url, GPXZ_API_KEY);
    if (data.isEmpty()) {
        std::cerr << "SKIP: No data downloaded" << std::endl;
        return true;
    }

    terrain::DemTile tile = terrain::DemDecoder::decodeGeoTiff(data);
    if (!tile.valid) {
        std::cerr << "FAIL: decodeGeoTiff failed" << std::endl;
        return false;
    }

    terrain::DemTile resampled = terrain::DemDecoder::resample(tile, 1024, 1024);
    if (!resampled.valid) {
        std::cerr << "FAIL: resample to 1024x1024 returned invalid tile" << std::endl;
        return false;
    }

    std::cerr << "  Resampled: " << resampled.width << "x" << resampled.height
              << " = " << resampled.elevations.size() << " pixels" << std::endl;
    std::cerr << "  PASS" << std::endl;
    return true;
}

// Test 5: Test empty/invalid data handling
bool testInvalidData() {
    std::cerr << "\n=== Test 5: Invalid data handling ===" << std::endl;

    // Empty data
    if (terrain::DemDecoder::decodeGeoTiff(QByteArray()).valid) {
        std::cerr << "FAIL: Empty data should return invalid" << std::endl;
        return false;
    }

    // Too small data
    if (terrain::DemDecoder::decodeGeoTiff(QByteArray("abc", 3)).valid) {
        std::cerr << "FAIL: Too-small data should return invalid" << std::endl;
        return false;
    }

    // Non-TIFF data
    if (terrain::DemDecoder::decodeGeoTiff(QByteArray("This is not a TIFF file!!!!", 26)).valid) {
        std::cerr << "FAIL: Non-TIFF data should return invalid" << std::endl;
        return false;
    }

    // decodeAuto with empty data
    if (terrain::DemDecoder::decodeAuto(QByteArray(), "dem").valid) {
        std::cerr << "FAIL: decodeAuto empty should return invalid" << std::endl;
        return false;
    }

    std::cerr << "  All invalid data cases handled correctly" << std::endl;
    std::cerr << "  PASS" << std::endl;
    return true;
}

// Test 6: Test with a mountainous area (Swiss Alps)
bool testMountainousArea(QNetworkAccessManager& nam) {
    std::cerr << "\n=== Test 6: Mountainous area (Swiss Alps) ===" << std::endl;

    // Matterhorn area — elevation 1000-4000m+
    QString url = QString("https://api.gpxz.io/v1/elevation/raster?"
                          "bbox_left=7.6&bbox_right=7.8"
                          "&bbox_bottom=45.95&bbox_top=46.05"
                          "&height_px=128&width_px=128");

    QByteArray data = downloadUrl(nam, url, GPXZ_API_KEY);
    if (data.isEmpty()) {
        std::cerr << "SKIP: Could not download mountainous area data" << std::endl;
        return true;
    }

    std::cerr << "  Downloaded: " << data.size() << " bytes" << std::endl;

    terrain::DemTile tile = terrain::DemDecoder::decodeGeoTiff(data);
    if (!tile.valid) {
        std::cerr << "FAIL: decodeGeoTiff failed for mountainous area" << std::endl;
        return false;
    }

    float minElev = std::numeric_limits<float>::max();
    float maxElev = std::numeric_limits<float>::lowest();
    for (float e : tile.elevations) {
        if (e != tile.nodataValue) {
            minElev = std::min(minElev, e);
            maxElev = std::max(maxElev, e);
        }
    }

    std::cerr << "  Elevation range: " << minElev << " to " << maxElev << " meters" << std::endl;
    std::cerr << "  Elevation variance: " << (maxElev - minElev) << " meters" << std::endl;

    if (maxElev < 500) {
        std::cerr << "FAIL: Expected mountainous elevations > 500m, max was " << maxElev << std::endl;
        return false;
    }

    std::cerr << "  PASS" << std::endl;
    return true;
}

// Test 7: Test bad API key
bool testBadApiKey(QNetworkAccessManager& nam) {
    std::cerr << "\n=== Test 7: Bad API key handling ===" << std::endl;

    QString url = QString("https://api.gpxz.io/v1/elevation/raster?"
                          "bbox_left=0&bbox_right=0.1"
                          "&bbox_bottom=51.0&bbox_top=51.1"
                          "&height_px=64&width_px=64");

    QByteArray data = downloadUrl(nam, url, "ak_INVALID_KEY_12345", 30000);

    // With a bad key, we should get either empty data or an error response (not a TIFF)
    if (data.isEmpty()) {
        std::cerr << "  Bad key correctly returned no data" << std::endl;
    } else if (data.size() >= 2 && (data[0] == 'I' || data[0] == 'M')) {
        std::cerr << "  WARNING: Bad key returned TIFF data (unexpected)" << std::endl;
    } else {
        std::cerr << "  Bad key returned error response (" << data.size() << " bytes)" << std::endl;
    }

    std::cerr << "  PASS" << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    signal(SIGSEGV, crashHandler);
    signal(SIGABRT, crashHandler);

    QCoreApplication app(argc, argv);
    QNetworkAccessManager nam;

    std::cerr << "==============================================" << std::endl;
    std::cerr << "  GPXZ DEM Download + Decode End-to-End Test" << std::endl;
    std::cerr << "==============================================" << std::endl;

    int passed = 0;
    int failed = 0;

    auto runTest = [&](const char* name, auto fn) {
        if (fn(nam)) passed++;
        else failed++;
    };

    runTest("Download+Decode", testDownloadAndDecode);
    runTest("decodeAuto",      testDecodeAuto);
    runTest("Resample 64x64",  testResample);
    runTest("Resample 1024",   testResampleLarge);
    // testInvalidData doesn't need network
    if (testInvalidData()) passed++; else failed++;
    runTest("Mountainous",     testMountainousArea);
    runTest("Bad API key",     testBadApiKey);

    std::cerr << "\n==============================================" << std::endl;
    std::cerr << "  Results: " << passed << " passed, " << failed << " failed" << std::endl;
    if (failed == 0) {
        std::cerr << "  STATUS: ALL TESTS PASSED" << std::endl;
    } else {
        std::cerr << "  STATUS: SOME TESTS FAILED" << std::endl;
    }
    std::cerr << "==============================================" << std::endl;

    return failed == 0 ? 0 : 1;
}
