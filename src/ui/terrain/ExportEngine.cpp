// ExportEngine — DEM and imagery download + file writing implementation

#include "ExportEngine.hpp"
#include "RasterWriter.hpp"
#include "../../core/PathHelper.hpp"
#include "DemDecoder.hpp"
#include "../../gis/crs/CRSManager.hpp"
#include "../../gis/crs/CoordinateTransform.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QAuthenticator>
#include <QProcessEnvironment>
#include <cmath>
#include <utility>
#include <limits>
#include <QRegularExpression>

// libtiff for GeoTIFF output
#include <tiffio.h>

#ifndef TIFFTAG_GEOPIXELSCALE
#define TIFFTAG_GEOPIXELSCALE 33550
#endif
#ifndef TIFFTAG_GEOTIEPOINTS
#define TIFFTAG_GEOTIEPOINTS 33922
#endif
#ifndef TIFFTAG_GEOKEYDIRECTORY
#define TIFFTAG_GEOKEYDIRECTORY 34735
#endif

// ============================================================
// Credential helpers — load from environment variables, never hardcoded
// ============================================================

// GLAD SRTM credentials: loaded from GLAD_USER and GLAD_PASSWORD env vars.
// Returns empty QByteArray if not set (caller should skip GLAD auth).
static QByteArray gladAuthHeader() {
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString user = env.value("GLAD_USER");
    QString pass = env.value("GLAD_PASSWORD");
    if (user.isEmpty() || pass.isEmpty())
        return {};
    return "Basic " + (user + ":" + pass).toUtf8().toBase64();
}

// GPXZ API key: loaded from GPXZ_API_KEY env var if not set in settings.
// Returns the key from settings, or env var fallback, or empty.
static QString resolveGpxzApiKey(const QString& settingsKey) {
    if (!settingsKey.isEmpty())
        return settingsKey;
    return QProcessEnvironment::systemEnvironment().value("GPXZ_API_KEY");
}

// ============================================================
// Shared DEM sampling helpers
// ============================================================

// Bilinear sample with invalid-value renormalization (NaN or nodata)
static float sampleBilinear(const std::vector<float>& grid, int w, int h,
                            double fx, double fy,
                            float nodata = std::numeric_limits<float>::quiet_NaN()) {
    fx = qBound(0.0, fx, static_cast<double>(w - 1) - 1e-3);
    fy = qBound(0.0, fy, static_cast<double>(h - 1) - 1e-3);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double gx = fx - x0;
    const double gy = fy - y0;
    const float vs[4] = {
        grid[static_cast<size_t>(y0) * w + x0], grid[static_cast<size_t>(y0) * w + x1],
        grid[static_cast<size_t>(y1) * w + x0], grid[static_cast<size_t>(y1) * w + x1]
    };
    const double ws[4] = {
        (1.0 - gx) * (1.0 - gy), gx * (1.0 - gy),
        (1.0 - gx) * gy, gx * gy
    };
    double acc = 0.0, wsum = 0.0;
    for (int i = 0; i < 4; ++i) {
        const bool invalid = std::isnan(vs[i]) || vs[i] == nodata;
        if (!invalid) {
            acc += static_cast<double>(vs[i]) * ws[i];
            wsum += ws[i];
        }
    }
    if (wsum <= 0.0) return -9999.0f;
    return static_cast<float>(acc / wsum);
}

// Crop an area-provider DEM raster (which may cover MORE than the requested
// tile — e.g. the Copernicus 1°x1° COG cell) to the tile bounds and sample
// to the target resolution. Without this, elevation from outside the
// selection is stretched over the tile and misaligns with the albedo.
static terrain::DemTile cropDemToTile(const terrain::DemTile& src,
                                      double rasterWest, double rasterNorth,
                                      double rasterEast, double rasterSouth,
                                      const terrain::GeoBounds& target, int res) {
    terrain::DemTile out;
    out.width = res;
    out.height = res;
    out.nodataValue = -9999.0f;
    out.valid = true;
    out.elevations.resize(static_cast<size_t>(res) * res);

    const double spanX = rasterEast - rasterWest;
    const double spanY = rasterNorth - rasterSouth;
    for (int py = 0; py < res; ++py) {
        const double lat = target.north -
            (target.north - target.south) * ((py + 0.5) / res);
        const double fy = (rasterNorth - lat) / spanY * src.height;
        for (int px = 0; px < res; ++px) {
            const double lon = target.west +
                (target.east - target.west) * ((px + 0.5) / res);
            const double fx = (lon - rasterWest) / spanX * src.width;
            out.elevations[static_cast<size_t>(py) * res + px] =
                sampleBilinear(src.elevations, src.width, src.height, fx, fy,
                               src.nodataValue);
        }
    }
    return out;
}
ExportEngine::ExportEngine(TerrainStore* store, QObject* parent)
    : QObject(parent), m_store(store) {
    m_network = new QNetworkAccessManager(this);
}

void ExportEngine::exportToDirectory(const QString& dir) {
    m_exportDir = dir;

    // Collect selected tiles
    m_pendingTiles.clear();
    m_tileDemData.clear();
    m_tileAlbedoData.clear();
    const auto& grid = m_store->tileGrid();
    const auto& selected = m_store->selectedTiles();
    for (const auto& tile : grid.tiles) {
        if (selected.contains(tile.id())) {
            m_pendingTiles.append(tile);
        }
    }

    m_totalTiles = m_pendingTiles.size();
    m_completedTiles = 0;

    if (m_totalTiles == 0) {
        emit finished(false, "No tiles selected for export.");
        return;
    }

    // Create subdirectories
    QDir(m_exportDir).mkdir("heightmaps");
    QDir(m_exportDir).mkdir("albedo");

    emit progress(0, "Starting export...");
    processNextTile();
}

void ExportEngine::processNextTile() {
    if (m_pendingTiles.isEmpty()) {
        writeMergedOutputs(m_exportDir);
        writeManifest(m_exportDir);
        emit progress(100, "Export complete!");
        emit finished(true, QString("Exported %1 tiles to %2").arg(m_completedTiles).arg(m_exportDir));
        return;
    }

    m_currentTile = m_pendingTiles.takeFirst();
    m_demDownloaded = false;
    m_imageryDownloaded = false;

    int percent = static_cast<int>(100.0 * m_completedTiles / m_totalTiles);
    emit progress(percent, QString("Exporting tile %1/%2: %3")
        .arg(m_completedTiles + 1).arg(m_totalTiles).arg(m_currentTile.id()));

    // Download DEM (heightmap) or load from local file. Heightmap-less
    // exports ("albedo only") skip the DEM stage instead of stalling.
    const bool wantDem =
        m_store->exportSettings().heightmapFormat != terrain::HeightmapFormat::None;
    QString demExt = ".png";
    if (wantDem) {
        switch (m_store->exportSettings().heightmapFormat) {
        case terrain::HeightmapFormat::GeoTIFF_Float32:
        case terrain::HeightmapFormat::GeoTIFF_Int16:
        case terrain::HeightmapFormat::GeoTIFF_UInt16:
            demExt = ".tif";
            break;
        case terrain::HeightmapFormat::PNG16:
            demExt = ".png";
            break;
        case terrain::HeightmapFormat::R16:
            demExt = ".r16";
            break;
        case terrain::HeightmapFormat::None:
        default:
            demExt = ".png";
            break;
        }
    }
    QString demPath = m_exportDir + "/heightmaps/tile_" + m_currentTile.id() + demExt;
    if (!wantDem) {
        m_demDownloaded = true;
    } else if (m_store->exportSettings().demSource == terrain::DemSource::Local_File) {
        loadLocalDemForTile(m_currentTile, demPath);
    } else {
        downloadDemForTile(m_currentTile, demPath);
    }

    // Download imagery (albedo) or load from local file
    QString albExt = (m_store->exportSettings().albedoFormat == terrain::AlbedoFormat::GeoTIFF_RGB)
        ? ".tif" : ".png";
    QString albedoPath = m_exportDir + "/albedo/tile_" + m_currentTile.id() + albExt;
    if (m_store->exportSettings().imagerySource == terrain::ImagerySource::Local_File) {
        loadLocalImageryForTile(m_currentTile, albedoPath);
    } else {
        downloadImageryForTile(m_currentTile, albedoPath);
    }
}

void ExportEngine::downloadDemForTile(const terrain::Tile& tile, const QString& outputPath) {
    const auto src = m_store->exportSettings().demSource;

    // Tiled raster sources must cover the whole tile bounds. Downloading only
    // the slippy tile containing the tile center and stretching it exports
    // geographically wrong elevation, so fetch a mosaic instead.
    if (src == terrain::DemSource::AWS_Terrarium ||
        src == terrain::DemSource::Mapzen_Terrarium ||
        src == terrain::DemSource::Mapbox_TerrainRGB) {
        startDemMosaic(tile, outputPath);
        return;
    }

    // Copernicus cells are 1°x1°; a tile may straddle several cells, so all
    // overlapping cells are fetched and sampled per-pixel.
    if (src == terrain::DemSource::NASA_EarthData_Copernicus) {
        startCopernicusDownload(tile, outputPath);
        return;
    }

    // Area providers (OpenTopography, GPXZ, GLAD): one request
    // already covers the tile bounds.
    const QString url = demTileUrl(tile, 0, 0, 0);
    if (url.isEmpty()) {
        // No URL available — API key missing or source not configured
        QString reason;
        if (src == terrain::DemSource::Mapbox_TerrainRGB)
            reason = "Mapbox token is required for Terrain-RGB DEM source";
        else if (src == terrain::DemSource::GPXZ_LiDAR)
            reason = "GPXZ API key is required for LiDAR DEM source";
        else if (src == terrain::DemSource::Local_File)
            reason = "Local DEM file path is not set — select a file in the export panel";
        else
            reason = "DEM source URL is empty — check API key and source settings";

        emit finished(false, QString("DEM export failed for tile %1: %2")
                          .arg(tile.id()).arg(reason));
        return;
    }

    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");
    if (m_store->exportSettings().demSource == terrain::DemSource::GLAD_SRTM) {
        // GLAD uses basic auth — credentials from env vars GLAD_USER/GLAD_PASSWORD
        QByteArray auth = gladAuthHeader();
        if (!auth.isEmpty()) {
            request.setRawHeader("Authorization", auth);
        }
    }
    if (!m_store->exportSettings().openTopoApiKey.isEmpty() &&
        m_store->exportSettings().demSource != terrain::DemSource::GLAD_SRTM &&
        m_store->exportSettings().demSource != terrain::DemSource::GPXZ_LiDAR) {
        request.setRawHeader("api-key", m_store->exportSettings().openTopoApiKey.toUtf8());
    }
    // GPXZ uses x-api-key header (more secure than URL parameter)
    if (m_store->exportSettings().demSource == terrain::DemSource::GPXZ_LiDAR) {
        QString gpxzKey = resolveGpxzApiKey(m_store->exportSettings().gpxzApiKey);
        if (!gpxzKey.isEmpty()) {
            request.setRawHeader("x-api-key", gpxzKey.toUtf8());
        }
    }

    QNetworkReply* reply = m_network->get(request);
    // Set a 60-second timeout for DEM downloads
    QTimer::singleShot(60000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, outputPath, tile]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            m_log.warn("DEM download for tile", tile.id(), "HTTP", status, "error", reply->errorString());
            emit finished(false, QString("DEM download failed for tile %1 (HTTP %2): %3\n%4")
                              .arg(tile.id()).arg(status).arg(reply->errorString())
                              .arg(QString::fromUtf8(data)));
            return;
        }

        m_log.info("DEM download for tile", tile.id(), ":", data.size(), "bytes, HTTP", status);

        try {
            const int res = m_store->exportSettings().heightmapResolution;

            // Determine source name for auto-detection
            QString sourceName;
            auto demSrc = m_store->exportSettings().demSource;
            if (demSrc == terrain::DemSource::AWS_Terrarium ||
                demSrc == terrain::DemSource::Mapzen_Terrarium)
                sourceName = "terrarium";
            else if (demSrc == terrain::DemSource::Mapbox_TerrainRGB)
                sourceName = "mapbox-terrain";
            else
                sourceName = "dem";

            // Decode DEM data using DemDecoder (QGIS-style auto-detection)
            m_log.info("Decoding DEM for tile", tile.id(), "size", data.size());
            terrain::DemTile demTile = terrain::DemDecoder::decodeAuto(data, sourceName);
            m_log.info("DEM decoded for tile", tile.id(), "->", demTile.width, "x", demTile.height, "valid:", demTile.valid);

            if (!demTile.valid) {
                // Decoding failed — report error with details, do NOT write fake data
                QString dataType = "unknown";
                if (data.size() >= 4) {
                    if (data[0] == 0x89 && data[1] == 'P') dataType = "PNG";
                    else if (data[0] == 'I' && data[1] == 'I') dataType = "TIFF (little-endian)";
                    else if (data[0] == 'M' && data[1] == 'M') dataType = "TIFF (big-endian)";
                    else if (data[0] == 'n') dataType = "AAIGrid (ASCII)";
                    else dataType = QString("unknown (first bytes: %1 %2)")
                                        .arg(static_cast<int>(data[0]), 2, 16, QChar('0'))
                                        .arg(static_cast<int>(data[1]), 2, 16, QChar('0'));
                }
                emit finished(false, QString("Failed to decode DEM data for tile %1 — "
                                  "received %2 bytes of type %3, source: %4")
                                  .arg(tile.id()).arg(data.size()).arg(dataType).arg(sourceName));
                return;
            }

            // Resample to target resolution (QGIS bilinear interpolation pattern).
            // Copernicus is an area provider whose raster spans a whole 1°x1°
            // cell — crop it to the tile bounds first, or the cell's elevation
            // gets stretched over the tile and misaligns with the albedo.
            //
            // Warn when the native source resolution is far coarser than the
            // requested tile — a small selected tile over a coarse-resolution
            // DEM (e.g. a few hundred meters against 30m Copernicus pixels)
            // gets bilinearly blown up into a smooth gradient with no real
            // terrain detail. This is not a decode/crash bug — it means the
            // source simply doesn't have enough native pixels for that tile.
            {
                double latMid = (tile.bounds.north + tile.bounds.south) * 0.5;
                double tileWidthM = (tile.bounds.east - tile.bounds.west) * 111320.0 * std::cos(latMid * M_PI / 180.0);
                double tileHeightM = (tile.bounds.north - tile.bounds.south) * 111320.0;
                double nativePixelsAcrossTile = 0.0;
                if (demSrc == terrain::DemSource::NASA_EarthData_Copernicus) {
                    // demTile still covers the full 1x1 degree cell here — the
                    // fraction of it spanned by the tile determines native pixels used.
                    double tileWidthDeg = tile.bounds.east - tile.bounds.west;
                    nativePixelsAcrossTile = tileWidthDeg * demTile.width;
                } else {
                    nativePixelsAcrossTile = demTile.width;
                }
                if (nativePixelsAcrossTile > 0.0 && res > nativePixelsAcrossTile * 4.0) {
                    double nativeResM = tileWidthM / nativePixelsAcrossTile;
                    m_log.warn("Tile", tile.id(), ": requested", res, "x", res,
                               "heightmap but source DEM only has ~", (int)nativePixelsAcrossTile,
                               "native pixels across this", (int)tileWidthM, "x", (int)tileHeightM,
                               "m tile (~", nativeResM, "m/pixel). Output will look smooth/",
                               "gradient-like — select a larger tile area or a higher-resolution",
                               "DEM source (e.g. GPXZ LiDAR) for real terrain detail.");
                }
            }
            if (demTile.width != res || demTile.height != res) {
                m_log.info("Resampling DEM for tile", tile.id(), "from", demTile.width, "x", demTile.height, "to", res, "x", res);
                demTile = terrain::DemDecoder::resample(demTile, res, res);
                m_log.info("DEM resampled for tile", tile.id());
            }
            m_log.info("Writing DEM output for tile", tile.id(), "->", outputPath);
            if (!writeDemOutput(demTile.elevations, res, res, tile, outputPath)) {
                emit finished(false, QString("Failed to write DEM output for tile %1: %2")
                                  .arg(tile.id(), outputPath));
                return;
            }
            m_log.info("DEM output written for tile", tile.id());

            m_tileDemData[tile.id()] = std::move(demTile.elevations);
            m_demDownloaded = true;
            if (m_imageryDownloaded) {
                m_completedTiles++;
                processNextTile();
            }
        } catch (const std::exception& e) {
            m_log.error("DEM processing exception for tile", tile.id(), ":", e.what());
            emit finished(false, QString("DEM processing crashed for tile %1: %2")
                              .arg(tile.id()).arg(QString::fromUtf8(e.what())));
        } catch (...) {
            m_log.error("Unknown DEM processing exception for tile", tile.id());
            emit finished(false, QString("DEM processing crashed for tile %1 (unknown exception)")
                              .arg(tile.id()));
        }
    });
}

void ExportEngine::loadLocalDemForTile(const terrain::Tile& tile, const QString& outputPath) {
    const QString& localPath = m_store->exportSettings().localDemFilePath;
    const int res = m_store->exportSettings().heightmapResolution;

    if (localPath.isEmpty()) {
        emit finished(false, QString("Local DEM file path is empty — select a DEM file in the export panel"));
        return;
    }
    if (!QFile::exists(localPath)) {
        emit finished(false, QString("Local DEM file does not exist: %1").arg(localPath));
        return;
    }

    // Load the GeoTIFF/PNG DEM file using DemDecoder
    QImage srcImg(localPath);
    if (srcImg.isNull()) {
        emit finished(false, QString("Failed to load local DEM file (unsupported format or corrupted): %1")
                          .arg(localPath));
        return;
    }

    // Decode as GeoTIFF DEM and resample
    QByteArray imgData;
    QFile f(localPath);
    if (f.open(QIODevice::ReadOnly)) {
        imgData = f.readAll();
        f.close();
    }
    terrain::DemTile demTile = terrain::DemDecoder::decodeGeoTiff(imgData);
    if (!demTile.valid) {
        // Not a GeoTIFF — try as grayscale 16-bit image
        demTile.width = srcImg.width();
        demTile.height = srcImg.height();
        demTile.elevations.resize(demTile.width * demTile.height);
        QImage gray = srcImg.convertToFormat(QImage::Format_Grayscale16);
        for (int y = 0; y < demTile.height; ++y) {
            for (int x = 0; x < demTile.width; ++x) {
                QRgba64 px = gray.pixelColor(x, y).rgba64();
                demTile.elevations[y * demTile.width + x] = static_cast<float>(px.red());
            }
        }
        demTile.valid = true;
    }

    // Resample to target resolution
    if (demTile.width != res || demTile.height != res) {
        demTile = terrain::DemDecoder::resample(demTile, res, res);
    }
    if (!writeDemOutput(demTile.elevations, res, res, tile, outputPath)) {
        emit finished(false, QString("Failed to write DEM output for tile %1: %2")
                          .arg(tile.id(), outputPath));
        return;
    }

    m_tileDemData[tile.id()] = std::move(demTile.elevations);
    m_demDownloaded = true;
    if (m_imageryDownloaded) {
        m_completedTiles++;
        processNextTile();
    }
}

void ExportEngine::loadLocalImageryForTile(const terrain::Tile& tile, const QString& outputPath) {
    const QString& localPath = m_store->exportSettings().localImageryFilePath;
    const int res = m_store->exportSettings().albedoResolution;

    if (localPath.isEmpty()) {
        emit finished(false, QString("Local imagery file path is empty — select an imagery file in the export panel"));
        return;
    }
    if (!QFile::exists(localPath)) {
        emit finished(false, QString("Local imagery file does not exist: %1").arg(localPath));
        return;
    }

    QImage srcImg(localPath);
    if (srcImg.isNull()) {
        emit finished(false, QString("Failed to load local imagery file (unsupported format or corrupted): %1")
                          .arg(localPath));
        return;
    }

    QImage scaled = srcImg.scaled(res, res, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (!writeImageryOutput(scaled, tile, outputPath)) {
        emit finished(false, QString("Failed to write imagery output for tile %1: %2")
                          .arg(tile.id(), outputPath));
        return;
    }

    m_tileAlbedoData[tile.id()] = scaled;
    m_imageryDownloaded = true;
    if (m_demDownloaded) {
        m_completedTiles++;
        processNextTile();
    }
}

void ExportEngine::downloadImageryForTile(const terrain::Tile& tile, const QString& outputPath) {
    const auto src = m_store->exportSettings().imagerySource;

    // All satellite imagery sources are slippy-tiled; mosaic every sub-tile
    // covering the bounds so the exported albedo matches the selected area.
    if (src != terrain::ImagerySource::GLAD_ARD_Landsat &&
        src != terrain::ImagerySource::Local_File) {
        startImageryMosaic(tile, outputPath);
        return;
    }

    // GLAD ARD Landsat: single center-point GeoTIFF with basic auth
    QString url = imageryTileUrl(0, 0, 0, tile);
    if (url.isEmpty()) {
        QString reason;
        if (src == terrain::ImagerySource::Local_File)
            reason = "Local imagery file path is not set — select a file in the export panel";
        else
            reason = "Imagery source URL is empty — check source settings";
        emit finished(false, QString("Imagery export failed for tile %1: %2")
                          .arg(tile.id()).arg(reason));
        return;
    }

    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");
    // GLAD imagery uses basic auth — credentials from env vars GLAD_USER/GLAD_PASSWORD
    QByteArray gladAuth = gladAuthHeader();
    if (!gladAuth.isEmpty()) {
        request.setRawHeader("Authorization", gladAuth);
    }

    QNetworkReply* reply = m_network->get(request);
    QTimer::singleShot(60000, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, outputPath, tile]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            m_log.warn("Imagery download for tile", tile.id(), "HTTP", status, "error", reply->errorString());
            emit finished(false, QString("Imagery download failed for tile %1 (HTTP %2): %3\n%4")
                              .arg(tile.id()).arg(status).arg(reply->errorString())
                              .arg(QString::fromUtf8(data)));
            return;
        }

        m_log.info("Imagery download for tile", tile.id(), ":", data.size(), "bytes, HTTP", status);

        try {
            QImage img;
            if (!img.loadFromData(data)) {
                emit finished(false, QString("Failed to decode imagery image for tile %1 — "
                                  "received %2 bytes, format not recognized")
                                  .arg(tile.id()).arg(data.size()));
                return;
            }

            // Scale to requested resolution
            if (img.width() != m_store->exportSettings().albedoResolution ||
                img.height() != m_store->exportSettings().albedoResolution) {
                img = img.scaled(m_store->exportSettings().albedoResolution,
                                m_store->exportSettings().albedoResolution,
                                Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation);
            }
            // Use QGIS-style imagery output (PNG+world file or GeoTIFF RGB)
            if (!writeImageryOutput(img, tile, outputPath)) {
                emit finished(false, QString("Failed to write imagery output for tile %1: %2")
                                  .arg(tile.id()).arg(outputPath));
                return;
            }

            m_tileAlbedoData[tile.id()] = img;
            m_imageryDownloaded = true;
            if (m_demDownloaded) {
                m_completedTiles++;
                processNextTile();
            }
        } catch (const std::exception& e) {
            m_log.error("Imagery processing exception for tile", tile.id(), ":", e.what());
            emit finished(false, QString("Imagery processing crashed for tile %1: %2")
                              .arg(tile.id()).arg(QString::fromUtf8(e.what())));
        } catch (...) {
            m_log.error("Unknown imagery processing exception for tile", tile.id());
            emit finished(false, QString("Imagery processing crashed for tile %1 (unknown exception)")
                              .arg(tile.id()));
        }
    });
}

QString ExportEngine::demTileUrl(const terrain::Tile& tile, int z, int x, int y) const {
    const auto& settings = m_store->exportSettings();

    QString demType;
    switch (settings.demSource) {
    // Tiled DEM sources (no API key needed) — z/x/y sub-tiles
    case terrain::DemSource::AWS_Terrarium:
    case terrain::DemSource::Mapzen_Terrarium:
        return QString("https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%1/%2/%3.png")
            .arg(z).arg(x).arg(y);
    case terrain::DemSource::Mapbox_TerrainRGB:
        if (settings.mapboxToken.isEmpty()) return {};
        // Current Mapbox raster DEM endpoint (classic v4 is retired)
        return QString("https://api.mapbox.com/raster-dem/v1/mapbox.terrain-rgb/%1/%2/%3.pngraw?access_token=%4")
            .arg(z).arg(x).arg(y).arg(settings.mapboxToken);
    case terrain::DemSource::NASA_EarthData_Copernicus: {
        // Copernicus DEM GLO-30 via AWS S3 (free, no key)
        // Tiles are named by SOUTHWEST (lower-left) corner, 1x1 degree grid
        // Format: Copernicus_DSM_COG_10_[N|S]xx_00_[E|W]xxx_00_DEM
        // S3 path: {tile_name}/{tile_name}.tif
        int lat = static_cast<int>(std::floor(tile.bounds.south));
        int lon = static_cast<int>(std::floor(tile.bounds.west));
        QString latStr = (lat >= 0)
            ? QString("N%1_00").arg(lat, 2, 10, QChar('0'))
            : QString("S%1_00").arg(-lat, 2, 10, QChar('0'));
        QString lonStr = (lon >= 0)
            ? QString("E%1_00").arg(lon, 3, 10, QChar('0'))
            : QString("W%1_00").arg(-lon, 3, 10, QChar('0'));
        QString tileName = QString("Copernicus_DSM_COG_10_%1_%2_DEM")
            .arg(latStr, lonStr);
        return QString("https://copernicus-dem-30m.s3.amazonaws.com/%1/%2.tif")
            .arg(tileName, tileName);
    }
    // OpenTopography API sources
    case terrain::DemSource::OpenTopo_SRTM_GL1: demType = "SRTMGL1"; break;
    case terrain::DemSource::OpenTopo_SRTM_GL3: demType = "SRTMGL3"; break;
    case terrain::DemSource::OpenTopo_ALOS_AW3D30: demType = "AW3D30"; break;
    case terrain::DemSource::OpenTopo_Copernicus_GLO30: demType = "COP30"; break;
    case terrain::DemSource::OpenTopo_NASADEM: demType = "NASADEM"; break;
    case terrain::DemSource::OpenTopo_USGS_3DEP: demType = "USGS10m"; break;
    case terrain::DemSource::GPXZ_LiDAR:
        if (resolveGpxzApiKey(settings.gpxzApiKey).isEmpty()) return {};
        // API key is passed via x-api-key header (set in downloadDemForTile).
        // GPXZ requires resolution_m between 0.5 and 1000 metres, so compute
        // an approximate resolution from the tile dimensions and clamp it.
        {
            double latMid = (tile.bounds.north + tile.bounds.south) * 0.5;
            double widthM = (tile.bounds.east - tile.bounds.west) * 111320.0 * std::cos(latMid * M_PI / 180.0);
            double heightM = (tile.bounds.north - tile.bounds.south) * 111320.0;
            double tileSizeM = std::max(widthM, heightM);
            // GPXZ limits output to < 2500 px, so cap the requested source size.
            int srcPixels = std::min(settings.heightmapResolution, 2490);
            double resM = tileSizeM / srcPixels;
            if (resM < 0.5) resM = 0.5;
            if (resM > 1000.0) resM = 1000.0;
            return QString("https://api.gpxz.io/v1/elevation/raster?"
                           "bbox_left=%1&bbox_right=%2&bbox_bottom=%3&bbox_top=%4"
                           "&resolution_m=%5&projection=epsg:4326")
                .arg(tile.bounds.west, 0, 'f', 6)
                .arg(tile.bounds.east, 0, 'f', 6)
                .arg(tile.bounds.south, 0, 'f', 6)
                .arg(tile.bounds.north, 0, 'f', 6)
                .arg(resM, 0, 'f', 6);
        }
    case terrain::DemSource::GLAD_SRTM:
        // GLAD SRTM tiles are addressed by integer degree of the SW corner
        return QString("https://glad.umd.edu/dataset/srtm-90m/%1/%2")
            .arg(std::floor(tile.bounds.south), 0, 'f', 0)
            .arg(std::floor(tile.bounds.west), 0, 'f', 0);
    case terrain::DemSource::Local_File:
        return {}; // handled separately via local file path
    }

    if (settings.openTopoApiKey.isEmpty()) {
        return {};
    }

    QUrl url("https://portal.opentopography.org/API/globaldem");
    QUrlQuery query;
    query.addQueryItem("demtype", demType);
    query.addQueryItem("south", QString::number(tile.bounds.south, 'f', 6));
    query.addQueryItem("north", QString::number(tile.bounds.north, 'f', 6));
    query.addQueryItem("west", QString::number(tile.bounds.west, 'f', 6));
    query.addQueryItem("east", QString::number(tile.bounds.east, 'f', 6));
    query.addQueryItem("outputFormat", "AAIGrid");
    query.addQueryItem("API_Key", settings.openTopoApiKey);
    url.setQuery(query);
    return url.toString();
}

QString ExportEngine::imageryTileUrl(int z, int x, int y, const terrain::Tile& tile) const {
    const auto& settings = m_store->exportSettings();
    switch (settings.imagerySource) {
    case terrain::ImagerySource::Google_Satellite:
        return QString("https://mt1.google.com/vt/lyrs=s&x=%1&y=%2&z=%3")
            .arg(x).arg(y).arg(z);
    case terrain::ImagerySource::ArcGIS_World_Imagery:
        return QString("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%1/%2/%3")
            .arg(z).arg(y).arg(x);
    case terrain::ImagerySource::Mapbox_Satellite:
        if (settings.mapboxToken.isEmpty()) return {};
        // Current Mapbox styles endpoint (classic v4 is retired)
        return QString("https://api.mapbox.com/styles/v1/mapbox/satellite-v9/tiles/256/%1/%2/%3?access_token=%4")
            .arg(z).arg(x).arg(y).arg(settings.mapboxToken);
    case terrain::ImagerySource::MapTiler_Satellite:
        if (settings.maptilerToken.isEmpty()) return {};
        // Current dataset is satellite-v2
        return QString("https://api.maptiler.com/tiles/satellite-v2/%1/%2/%3.jpg?key=%4")
            .arg(z).arg(x).arg(y).arg(settings.maptilerToken);
    case terrain::ImagerySource::GLAD_ARD_Landsat: {
        // GLAD ARD Landsat — uses UMD GLAD tile scheme
        // Format: https://glad.umd.edu/ardapid/landsat/NDVI/{interval}/{lat}/{lon}.tif
        int interval = m_store->exportSettings().gladArdInterval;
        if (interval <= 0) interval = 920;
        double centerLat = (tile.bounds.north + tile.bounds.south) / 2.0;
        double centerLon = (tile.bounds.east + tile.bounds.west) / 2.0;
        return QString("https://glad.umd.edu/ardapid/landsat/NDVI/%1/%2/%3.tif")
            .arg(interval)
            .arg(centerLat, 0, 'f', 4)
            .arg(centerLon, 0, 'f', 4);
    }
    case terrain::ImagerySource::Local_File:
        return {};
    }
    return {};
}

// ============================================================
// Slippy-tile mosaic download — full coverage of the tile bounds
// ============================================================

static double slippyLonToX(double lon, int zoom) {
    return (lon + 180.0) / 360.0 * std::ldexp(1.0, zoom);
}

static double slippyLatToY(double lat, int zoom) {
    const double latRad = lat * M_PI / 180.0;
    return (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0
        * std::ldexp(1.0, zoom);
}

// Keep the mosaic at most 8x8 sub-tiles so request counts stay bounded
static int clampMosaicZoom(const terrain::GeoBounds& b, int zoom) {
    while (zoom > 3) {
        const double spanX = std::abs(slippyLonToX(b.east, zoom) - slippyLonToX(b.west, zoom));
        const double spanY = std::abs(slippyLatToY(b.south, zoom) - slippyLatToY(b.north, zoom));
        if (std::ceil(spanX) <= 8.0 && std::ceil(spanY) <= 8.0) return zoom;
        --zoom;
    }
    return 3;
}

int ExportEngine::autoZoomForBounds(const terrain::GeoBounds& b, int targetRes, int maxZoom) {
    const double latMid = (b.north + b.south) * 0.5;
    const double widthM = (b.east - b.west) * 111320.0 * std::cos(latMid * M_PI / 180.0);
    const double heightM = (b.north - b.south) * 111320.0;
    const double tileM = std::max(std::max(widthM, heightM), 1.0);
    const double targetMpp = tileM / static_cast<double>(std::max(targetRes, 1));
    const double equatorMpp = 156543.03392 * std::cos(latMid * M_PI / 180.0);
    int zoom = targetMpp > 0.0
        ? static_cast<int>(std::ceil(std::log2(std::max(equatorMpp / targetMpp, 1.0))))
        : 12;
    zoom = qBound(3, zoom, maxZoom);
    return clampMosaicZoom(b, zoom);
}

void ExportEngine::slippyRangeForBounds(const terrain::GeoBounds& b, int zoom,
                                        int& x0, int& y0, int& nx, int& ny) {
    const int maxIdx = (1 << zoom) - 1;
    const int xa = qBound(0, static_cast<int>(std::floor(slippyLonToX(b.west, zoom))), maxIdx);
    const int xb = qBound(0, static_cast<int>(std::floor(slippyLonToX(b.east, zoom))), maxIdx);
    const int ya = qBound(0, static_cast<int>(std::floor(slippyLatToY(b.north, zoom))), maxIdx);
    const int yb = qBound(0, static_cast<int>(std::floor(slippyLatToY(b.south, zoom))), maxIdx);
    x0 = std::min(xa, xb);
    nx = std::abs(xb - xa) + 1;
    y0 = std::min(ya, yb);
    ny = std::abs(yb - ya) + 1;
}

void ExportEngine::fetchMosaicSubTile(MosaicState& mosaic, int ix, int iy,
                                      const QUrl& url,
                                      const QMap<QByteArray, QByteArray>& headers) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");
    for (auto it = headers.constBegin(); it != headers.constEnd(); ++it)
        request.setRawHeader(it.key(), it.value());

    const qint64 key = static_cast<qint64>(iy) * mosaic.nx + ix;
    const bool isDem = mosaic.isDem;

    QNetworkReply* reply = m_network->get(request);
    QTimer::singleShot(60000, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, isDem]() {
        reply->deleteLater();
        MosaicState& mosaic = isDem ? m_demMosaic : m_imgMosaic;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            mosaic.blobs.insert(key, reply->readAll());
        } else {
            mosaic.failed++;
            m_log.warn("Mosaic sub-tile failed (HTTP", status, "):", reply->errorString());
        }
        mosaic.done++;
        if (mosaic.done >= mosaic.nx * mosaic.ny) {
            if (isDem) finishDemMosaic();
            else finishImageryMosaic();
        }
    });
}

void ExportEngine::startDemMosaic(const terrain::Tile& tile, const QString& outputPath) {
    const auto& settings = m_store->exportSettings();
    if (settings.demSource == terrain::DemSource::Mapbox_TerrainRGB &&
        settings.mapboxToken.isEmpty()) {
        emit finished(false, QString("DEM export failed for tile %1: "
                          "Mapbox token is required for Terrain-RGB DEM source")
                          .arg(tile.id()));
        return;
    }

    const int zoom = autoZoomForBounds(tile.bounds, settings.heightmapResolution, 15);
    MosaicState& m = m_demMosaic;
    m = MosaicState{};
    m.active = true;
    m.isDem = true;
    m.zoom = zoom;
    m.outputPath = outputPath;
    m.tile = tile;
    slippyRangeForBounds(tile.bounds, zoom, m.x0, m.y0, m.nx, m.ny);

    m_log.info("DEM mosaic for tile", tile.id(), ": zoom", zoom,
               m.nx, "x", m.ny, "sub-tiles");
    const QMap<QByteArray, QByteArray> noHeaders;
    for (int iy = 0; iy < m.ny; ++iy) {
        for (int ix = 0; ix < m.nx; ++ix) {
            const QString url = demTileUrl(tile, zoom, m.x0 + ix, m.y0 + iy);
            if (url.isEmpty()) {
                emit finished(false, QString("DEM export failed for tile %1: "
                                  "DEM source URL is empty — check API key and source settings")
                                  .arg(tile.id()));
                return;
            }
            fetchMosaicSubTile(m, ix, iy, QUrl(url), noHeaders);
        }
    }
}

void ExportEngine::startImageryMosaic(const terrain::Tile& tile, const QString& outputPath) {
    const auto& settings = m_store->exportSettings();
    if ((settings.imagerySource == terrain::ImagerySource::Mapbox_Satellite &&
         settings.mapboxToken.isEmpty()) ||
        (settings.imagerySource == terrain::ImagerySource::MapTiler_Satellite &&
         settings.maptilerToken.isEmpty())) {
        emit finished(false, QString("Imagery export failed for tile %1: "
                          "API token is required for the selected imagery source")
                          .arg(tile.id()));
        return;
    }

    int zoom;
    if (settings.imageryZoomLevel > 0)
        zoom = terrain::normalizeImageryZoom(settings.imageryZoomLevel);
    else
        zoom = autoZoomForBounds(tile.bounds, settings.albedoResolution, 19);

    MosaicState& m = m_imgMosaic;
    m = MosaicState{};
    m.active = true;
    m.isDem = false;
    m.zoom = zoom;
    m.outputPath = outputPath;
    m.tile = tile;
    slippyRangeForBounds(tile.bounds, zoom, m.x0, m.y0, m.nx, m.ny);

    m_log.info("Imagery mosaic for tile", tile.id(), ": zoom", zoom,
               m.nx, "x", m.ny, "sub-tiles");
    const QMap<QByteArray, QByteArray> noHeaders;
    for (int iy = 0; iy < m.ny; ++iy) {
        for (int ix = 0; ix < m.nx; ++ix) {
            const QString url = imageryTileUrl(zoom, m.x0 + ix, m.y0 + iy, tile);
            if (url.isEmpty()) {
                emit finished(false, QString("Imagery export failed for tile %1: "
                                  "imagery source URL is empty — check API token and source settings")
                                  .arg(tile.id()));
                return;
            }
            fetchMosaicSubTile(m, ix, iy, QUrl(url), noHeaders);
        }
    }
}

void ExportEngine::finishDemMosaic() {
    MosaicState& m = m_demMosaic;
    m.active = false;
    const auto& settings = m_store->exportSettings();
    const int res = settings.heightmapResolution;

    QString sourceName = "terrarium";
    if (settings.demSource == terrain::DemSource::Mapbox_TerrainRGB)
        sourceName = "mapbox-terrain";

    const int mw = m.nx * 256;
    const int mh = m.ny * 256;
    std::vector<float> mosaic(static_cast<size_t>(mw) * mh,
                              std::numeric_limits<float>::quiet_NaN());
    bool anyValid = false;
    for (auto it = m.blobs.constBegin(); it != m.blobs.constEnd(); ++it) {
        const int ix = static_cast<int>(it.key() % m.nx);
        const int iy = static_cast<int>(it.key() / m.nx);
        terrain::DemTile t = terrain::DemDecoder::decodeAuto(it.value(), sourceName);
        if (!t.valid) {
            m_log.warn("DEM sub-tile", ix, iy, "failed to decode for tile", m.tile.id());
            continue;
        }
        const int tw = std::min(t.width, 256);
        const int th = std::min(t.height, 256);
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x)
                mosaic[static_cast<size_t>(iy * 256 + y) * mw + (ix * 256 + x)] =
                    t.elevations[static_cast<size_t>(y) * t.width + x];
        anyValid = true;
    }
    if (!anyValid) {
        emit finished(false, QString("DEM download failed for tile %1 — all %2 sub-tile requests failed")
                          .arg(m.tile.id()).arg(m.nx * m.ny));
        return;
    }
    if (m.failed > 0)
        m_log.warn("DEM mosaic for tile", m.tile.id(), ":", m.failed, "of",
                   m.nx * m.ny, "sub-tiles missing (filled as nodata)");

    // Sample the mosaic to exactly the geo-referenced output extent.
    // X is linear in longitude; Y is linear in Web-Mercator tile space.
    const double xw = slippyLonToX(m.tile.bounds.west, m.zoom);
    const double xe = slippyLonToX(m.tile.bounds.east, m.zoom);
    const double yn = slippyLatToY(m.tile.bounds.north, m.zoom);
    const double ys = slippyLatToY(m.tile.bounds.south, m.zoom);

    std::vector<float> out(static_cast<size_t>(res) * res, -9999.0f);
    for (int py = 0; py < res; ++py) {
        // Corner-aligned sampling (GeoTerrain resizeDEM's (src-1)/(dst-1)
        // mapping) — consistent with the PixelIsPoint GeoTIFF we write
        const double ty = yn + (ys - yn) * (static_cast<double>(py) / (res - 1));
        const double fy = ty * 256.0 - m.y0 * 256.0;
        for (int px = 0; px < res; ++px) {
            const double tx = xw + (xe - xw) * (static_cast<double>(px) / (res - 1));
            const double fx = tx * 256.0 - m.x0 * 256.0;
            out[static_cast<size_t>(py) * res + px] = sampleBilinear(mosaic, mw, mh, fx, fy);
        }
    }

    if (!writeDemOutput(out, res, res, m.tile, m.outputPath)) {
        emit finished(false, QString("Failed to write DEM output for tile %1: %2")
                          .arg(m.tile.id()).arg(m.outputPath));
        return;
    }
    m_log.info("DEM mosaic written for tile", m.tile.id(), "->", m.outputPath);

    m_tileDemData[m.tile.id()] = std::move(out);
    m_demDownloaded = true;
    if (m_imageryDownloaded) {
        m_completedTiles++;
        processNextTile();
    }
}

void ExportEngine::finishImageryMosaic() {
    MosaicState& m = m_imgMosaic;
    m.active = false;
    const auto& settings = m_store->exportSettings();
    const int res = settings.albedoResolution;

    const int mw = m.nx * 256;
    const int mh = m.ny * 256;
    QImage mosaicImg(mw, mh, QImage::Format_RGB888);
    mosaicImg.fill(QColor(28, 30, 34));
    int painted = 0;
    {
        QPainter painter(&mosaicImg);
        for (auto it = m.blobs.constBegin(); it != m.blobs.constEnd(); ++it) {
            const int ix = static_cast<int>(it.key() % m.nx);
            const int iy = static_cast<int>(it.key() / m.nx);
            QImage t;
            if (!t.loadFromData(it.value())) continue;
            painter.drawImage(ix * 256, iy * 256,
                              t.convertToFormat(QImage::Format_RGB888));
            painted++;
        }
        painter.end();
    }
    if (painted == 0) {
        emit finished(false, QString("Imagery download failed for tile %1 — all %2 sub-tile requests failed")
                          .arg(m.tile.id()).arg(m.nx * m.ny));
        return;
    }
    if (m.failed > 0)
        m_log.warn("Imagery mosaic for tile", m.tile.id(), ":", m.failed, "of",
                   m.nx * m.ny, "sub-tiles missing (filled dark)");

    // Crop the exact bounds out of the mosaic, then scale to the target res
    const double xwPx = slippyLonToX(m.tile.bounds.west, m.zoom) * 256.0 - m.x0 * 256.0;
    const double xePx = slippyLonToX(m.tile.bounds.east, m.zoom) * 256.0 - m.x0 * 256.0;
    const double ynPx = slippyLatToY(m.tile.bounds.north, m.zoom) * 256.0 - m.y0 * 256.0;
    const double ysPx = slippyLatToY(m.tile.bounds.south, m.zoom) * 256.0 - m.y0 * 256.0;
    const int cx = qBound(0, static_cast<int>(std::round(xwPx)), mw - 1);
    const int cy = qBound(0, static_cast<int>(std::round(ynPx)), mh - 1);
    const int cw = qBound(1, static_cast<int>(std::round(xePx - xwPx)), mw - cx);
    const int ch = qBound(1, static_cast<int>(std::round(ysPx - ynPx)), mh - cy);

    QImage out = mosaicImg.copy(cx, cy, cw, ch)
                     .scaled(res, res, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    if (!writeImageryOutput(out, m.tile, m.outputPath)) {
        emit finished(false, QString("Failed to write imagery output for tile %1: %2")
                          .arg(m.tile.id()).arg(m.outputPath));
        return;
    }
    m_log.info("Imagery mosaic written for tile", m.tile.id(), "->", m.outputPath);

    m_tileAlbedoData[m.tile.id()] = out;
    m_imageryDownloaded = true;
    if (m_demDownloaded) {
        m_completedTiles++;
        processNextTile();
    }
}

// ============================================================
// Copernicus multi-cell fetch — tiles may straddle 1° cell boundaries
// ============================================================

QString ExportEngine::copernicusCellName(int cellLat, int cellLon) {
    const QString latStr = (cellLat >= 0)
        ? QString("N%1_00").arg(cellLat, 2, 10, QChar('0'))
        : QString("S%1_00").arg(-cellLat, 2, 10, QChar('0'));
    const QString lonStr = (cellLon >= 0)
        ? QString("E%1_00").arg(cellLon, 3, 10, QChar('0'))
        : QString("W%1_00").arg(-cellLon, 3, 10, QChar('0'));
    return QString("Copernicus_DSM_COG_10_%1_%2_DEM").arg(latStr, lonStr);
}

void ExportEngine::startCopernicusDownload(const terrain::Tile& tile, const QString& outputPath) {
    m_copFetch = CopernicusFetch{};
    m_copFetch.active = true;
    m_copFetch.tile = tile;
    m_copFetch.outputPath = outputPath;
    m_copFetch.latFrom = static_cast<int>(std::floor(tile.bounds.south));
    m_copFetch.latTo = static_cast<int>(std::floor(tile.bounds.north));
    m_copFetch.lonFrom = static_cast<int>(std::floor(tile.bounds.west));
    m_copFetch.lonTo = static_cast<int>(std::floor(tile.bounds.east));

    int needed = 0;
    for (int la = m_copFetch.latFrom; la <= m_copFetch.latTo; ++la)
        for (int lo = m_copFetch.lonFrom; lo <= m_copFetch.lonTo; ++lo)
            if (!m_copCellCache.contains(copernicusCellName(la, lo)))
                needed++;

    m_log.info("Copernicus cells for tile", tile.id(), ":",
               m_copFetch.latTo - m_copFetch.latFrom + 1, "x",
               m_copFetch.lonTo - m_copFetch.lonFrom + 1,
               "(", needed, "download(s),", m_copCellCache.size(), "cached )");

    if (needed == 0) {
        finishCopernicusDownload();
        return;
    }
    m_copFetch.pending = needed;
    for (int la = m_copFetch.latFrom; la <= m_copFetch.latTo; ++la)
        for (int lo = m_copFetch.lonFrom; lo <= m_copFetch.lonTo; ++lo)
            if (!m_copCellCache.contains(copernicusCellName(la, lo)))
                fetchCopernicusCell(la, lo);
}

void ExportEngine::fetchCopernicusCell(int cellLat, int cellLon) {
    const QString name = copernicusCellName(cellLat, cellLon);
    const QUrl url(QString("https://copernicus-dem-30m.s3.amazonaws.com/%1/%2.tif")
                       .arg(name, name));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");
    QNetworkReply* reply = m_network->get(request);
    // A cell is ~47 MB — give it more headroom than slippy tiles
    QTimer::singleShot(180000, reply, [reply]() {
        if (reply->isRunning()) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, cellLat, cellLon]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();

        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            m_copFetch.active = false;
            emit finished(false, QString("Copernicus DEM download failed for cell %1 (HTTP %2): %3")
                              .arg(copernicusCellName(cellLat, cellLon))
                              .arg(status).arg(reply->errorString()));
            return;
        }

        terrain::DemTile cell = terrain::DemDecoder::decodeAuto(data, "dem");
        if (!cell.valid || cell.width <= 0 || cell.height <= 0) {
            m_copFetch.active = false;
            emit finished(false, QString("Failed to decode Copernicus DEM cell %1 (%2 bytes)")
                              .arg(copernicusCellName(cellLat, cellLon)).arg(data.size()));
            return;
        }

        // Keep memory bounded: each decoded cell is ~52 MB
        if (m_copCellCache.size() >= 4) m_copCellCache.clear();
        m_copCellCache.insert(copernicusCellName(cellLat, cellLon), std::move(cell));

        m_copFetch.pending--;
        if (m_copFetch.pending <= 0)
            finishCopernicusDownload();
    });
}

void ExportEngine::finishCopernicusDownload() {
    const terrain::Tile& tile = m_copFetch.tile;
    m_copFetch.active = false;
    const int res = m_store->exportSettings().heightmapResolution;

    terrain::DemTile out;
    out.width = res;
    out.height = res;
    out.nodataValue = -9999.0f;
    out.valid = true;
    out.elevations.resize(static_cast<size_t>(res) * res);

    for (int py = 0; py < res; ++py) {
        // Corner-aligned sampling — consistent with PixelIsPoint GeoTIFF
        const double lat = tile.bounds.north -
            (tile.bounds.north - tile.bounds.south) * (static_cast<double>(py) / (res - 1));
        // Pick the cell that actually contains this latitude; clamp to the
        // fetched range so edge rows extend instead of sampling nothing.
        const int cellLat = qBound(m_copFetch.latFrom,
                                   static_cast<int>(std::floor(lat)), m_copFetch.latTo);
        for (int px = 0; px < res; ++px) {
            const double lon = tile.bounds.west +
                (tile.bounds.east - tile.bounds.west) * (static_cast<double>(px) / (res - 1));
            const int cellLon = qBound(m_copFetch.lonFrom,
                                       static_cast<int>(std::floor(lon)), m_copFetch.lonTo);

            // QMap::value() would copy the ~52 MB cell — use a reference
            auto cellIt = m_copCellCache.constFind(copernicusCellName(cellLat, cellLon));
            if (cellIt == m_copCellCache.constEnd()) {
                out.elevations[static_cast<size_t>(py) * res + px] = -9999.0f;
                continue;
            }
            const terrain::DemTile& cell = cellIt.value();
            // Cell covers [cellLat, cellLat+1] x [cellLon, cellLon+1],
            // row 0 = north edge (cellLat + 1)
            const double fx = (lon - cellLon) * cell.width;
            const double fy = (cellLat + 1.0 - lat) * cell.height;
            out.elevations[static_cast<size_t>(py) * res + px] =
                sampleBilinear(cell.elevations, cell.width, cell.height, fx, fy,
                               cell.nodataValue);
        }
    }

    if (!writeDemOutput(out.elevations, res, res, tile, m_copFetch.outputPath)) {
        emit finished(false, QString("Failed to write DEM output for tile %1: %2")
                          .arg(tile.id()).arg(m_copFetch.outputPath));
        return;
    }
    m_log.info("Copernicus DEM written for tile", tile.id(), "->", m_copFetch.outputPath);

    m_tileDemData[tile.id()] = std::move(out.elevations);
    m_demDownloaded = true;
    if (m_imageryDownloaded) {
        m_completedTiles++;
        processNextTile();
    }
}

void ExportEngine::writeGeoTiff(const QString& path, const QImage& heightmap,
                                  const terrain::GeoBounds& bounds) {
    const int width = heightmap.width();
    const int height = heightmap.height();

    TIFF* tif = TIFFOpen(PathHelper::toTiffPath(path).toUtf8().constData(), "w");
    if (!tif) return;

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);

    double pixelScaleX = (bounds.east - bounds.west) / width;
    double pixelScaleY = (bounds.north - bounds.south) / height;
    double pixelScale[3] = {pixelScaleX, pixelScaleY, 0.0};
    TIFFSetField(tif, TIFFTAG_GEOPIXELSCALE, 3, pixelScale);

    double tiepoint[6] = {0.0, 0.0, 0.0, bounds.west, bounds.north, 0.0};
    TIFFSetField(tif, TIFFTAG_GEOTIEPOINTS, 6, tiepoint);

    uint16_t geoKeys[16];
    geoKeys[0] = 1; geoKeys[1] = 1; geoKeys[2] = 0; geoKeys[3] = 3;
    geoKeys[4] = 1024; geoKeys[5] = 0; geoKeys[6] = 1; geoKeys[7] = 2;
    geoKeys[8] = 1025; geoKeys[9] = 0; geoKeys[10] = 1; geoKeys[11] = 1;
    geoKeys[12] = 2048; geoKeys[13] = 0; geoKeys[14] = 1; geoKeys[15] = 4326;
    TIFFSetField(tif, TIFFTAG_GEOKEYDIRECTORY, 16, geoKeys);

    std::vector<quint16> row(width);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            QRgba64 px = heightmap.pixelColor(x, y).rgba64();
            row[x] = px.red();
        }
        TIFFWriteScanline(tif, row.data(), y, 0);
    }

    TIFFClose(tif);
}
// ============================================================
// QGIS-style DEM output — uses RasterWriter for proper GeoTIFF
// ============================================================

// Lat/Lon to Web Mercator (EPSG:3857) conversion
static void latLonToWebMercator(double lat, double lon, double& x, double& y) {
    const double R = 6378137.0;  // WGS84 semi-major
    x = R * lon * M_PI / 180.0;
    y = R * log(tan(M_PI / 4.0 + lat * M_PI / 360.0));
}

// Lat/Lon to UTM using an explicitly given zone (both corners of a tile must
// use the SAME zone, or extents that straddle a zone boundary get garbage
// eastings from two different projections).
static void latLonToUtmZone(double lat, double lon, int zone, bool north,
                            double& easting, double& northing) {
    const double a = 6378137.0;
    const double f = 1.0 / 298.257223563;
    const double k0 = 0.9996;
    const double e2 = f * (2.0 - f);
    const double e2sq = e2 * e2;

    double latRad = lat * M_PI / 180.0;
    double lonRad = lon * M_PI / 180.0;
    double lonOrigin = (zone - 1) * 6.0 - 180.0 + 3.0;
    double lonOriginRad = lonOrigin * M_PI / 180.0;

    double N = a / sqrt(1.0 - e2 * sin(latRad) * sin(latRad));
    double T = tan(latRad) * tan(latRad);
    double C = e2sq * cos(latRad) * cos(latRad) / (1.0 - e2);
    double A = cos(latRad) * (lonRad - lonOriginRad);

    double M = a * ((1.0 - e2/4.0 - 3.0*e2sq/64.0) * latRad
              - (3.0*e2/8.0 + 3.0*e2sq/32.0) * sin(2.0*latRad)
              + (15.0*e2sq/256.0) * sin(4.0*latRad));

    easting = k0 * N * (A + (1.0 - T + C) * A*A*A / 6.0) + 500000.0;
    northing = k0 * (M + N * tan(latRad) * (A*A / 2.0
              + (5.0 - T + 9.0*C) * A*A*A*A / 24.0));
    if (!north) northing += 10000000.0;
}

// Lat/Lon to UTM conversion (auto zone from longitude)
static void latLonToUtm(double lat, double lon, int& zone, bool& north,
                        double& easting, double& northing) {
    zone = static_cast<int>((lon + 180.0) / 6.0) + 1;
    north = lat >= 0.0;
    latLonToUtmZone(lat, lon, zone, north, easting, northing);
}

terrain::RasterExtent ExportEngine::buildRasterExtent(const terrain::Tile& tile) const {
    terrain::RasterExtent ext;

    // Source bounds are always in WGS84 lat/lon
    double west = tile.bounds.west;
    double east = tile.bounds.east;
    double north = tile.bounds.north;
    double south = tile.bounds.south;

    auto crs = m_store->exportSettings().crsSource;
    // If Project_CRS is selected but no project CRS is set, fall back to WGS84
    if (crs == terrain::CrsSource::Project_CRS && m_store->exportSettings().projectCrsEpsg == 0) {
        crs = terrain::CrsSource::EPSG_4326;
    }

    auto projectToSharedGrid = [&](int epsg) {
        const auto& full = m_store->selectedBounds();
        if (!full.isValid() || full.widthDeg() <= 0.0 || full.heightDeg() <= 0.0) return false;

        auto srcCrs = gis::CRSManager::instance().fromEPSG(4326);
        auto dstCrs = gis::CRSManager::instance().fromEPSG(epsg);
        if (!srcCrs || !dstCrs) return false;

        gis::CoordinateTransform transform(*srcCrs, *dstCrs);
        auto fullSw = transform.transform({full.west, full.south});
        auto fullNe = transform.transform({full.east, full.north});
        if (!fullSw.success || !fullNe.success) return false;

        terrain::RasterExtent fullSource;
        fullSource.west = full.west;
        fullSource.east = full.east;
        fullSource.south = full.south;
        fullSource.north = full.north;

        terrain::RasterExtent subSource;
        subSource.west = west;
        subSource.east = east;
        subSource.south = south;
        subSource.north = north;

        terrain::RasterExtent fullTarget;
        fullTarget.west = fullSw.point.x;
        fullTarget.east = fullNe.point.x;
        fullTarget.south = fullSw.point.y;
        fullTarget.north = fullNe.point.y;

        const auto aligned = RasterWriter::alignedSubExtent(fullSource, subSource, fullTarget);
        ext.west = aligned.west;
        ext.east = aligned.east;
        ext.south = aligned.south;
        ext.north = aligned.north;
        return true;
    };

    switch (crs) {
    case terrain::CrsSource::Project_CRS:
    {
        // Use the project's CRS EPSG code for UTM projection
        int epsg = m_store->exportSettings().projectCrsEpsg;
        if (epsg >= 32601 && epsg <= 32660) {
            // UTM North
            ext.utmEpsg = epsg;
            ext.crsMode = terrain::GeoCrsMode::UTM;
            projectToSharedGrid(epsg);
        } else if (epsg >= 32701 && epsg <= 32760) {
            // UTM South
            ext.utmEpsg = epsg;
            ext.crsMode = terrain::GeoCrsMode::UTM;
            projectToSharedGrid(epsg);
        } else {
            // Unknown project CRS — fall back to WGS84
            ext.crsMode = terrain::GeoCrsMode::WGS84;
            ext.west = west;
            ext.east = east;
            ext.north = north;
            ext.south = south;
        }
        break;
    }

    case terrain::CrsSource::EPSG_4326:
        // Keep lat/lon as-is
        ext.crsMode = terrain::GeoCrsMode::WGS84;
        ext.west = west;
        ext.east = east;
        ext.north = north;
        ext.south = south;
        break;

    case terrain::CrsSource::EPSG_3857:
    {
        // Convert lat/lon to Web Mercator meters
        double mx_w, my_n, mx_e, my_s;
        latLonToWebMercator(north, west, mx_w, my_n);
        latLonToWebMercator(south, east, mx_e, my_s);
        ext.crsMode = terrain::GeoCrsMode::WebMercator;
        ext.west = mx_w;
        ext.east = mx_e;
        ext.north = my_n;
        ext.south = my_s;
        break;
    }

    case terrain::CrsSource::Auto_UTM:
    {
        // Compute UTM zone ONCE from the centroid and convert both corners
        // with that fixed zone. Using each corner's own zone breaks extents
        // that straddle a zone boundary (mismatched eastings).
        double clat = (north + south) / 2.0;
        double clon = (west + east) / 2.0;
        // Use PROJ's autoUtm for the correct EPSG code
        auto autoCrs = gis::CRSManager::instance().autoUtm(clat, clon);
        if (autoCrs) {
            int epsg = autoCrs->code;
            ext.utmEpsg = epsg;
            ext.crsMode = terrain::GeoCrsMode::UTM;
            projectToSharedGrid(epsg);
        } else {
            // Fallback: manual UTM zone calculation
            int zone = static_cast<int>((clon + 180.0) / 6.0) + 1;
            zone = qBound(1, zone, 60);
            bool isNorth = clat >= 0.0;
            double e_w, n_n, e_e, n_s;
            latLonToUtmZone(north, west, zone, isNorth, e_w, n_n);
            latLonToUtmZone(south, east, zone, isNorth, e_e, n_s);
            ext.crsMode = terrain::GeoCrsMode::UTM;
            ext.utmEpsg = isNorth ? (32600 + zone) : (32700 + zone);
            ext.west = e_w;
            ext.east = e_e;
            ext.north = n_n;
            ext.south = n_s;
        }
        break;
    }

    case terrain::CrsSource::Custom_EPSG:
    {
        // Use the user-selected EPSG code from the CRS selector dialog.
        // This supports ANY CRS in the PROJ database — UTM zones, national
        // grids, geographic CRS, etc. The transform is done via PROJ.
        int epsg = m_store->exportSettings().customEpsg;
        if (epsg <= 0) {
            // No custom EPSG set — fall back to WGS84
            ext.crsMode = terrain::GeoCrsMode::WGS84;
            ext.west = west;
            ext.east = east;
            ext.north = north;
            ext.south = south;
            break;
        }

        auto srcCrs = gis::CRSManager::instance().fromEPSG(4326);
        auto dstCrs = gis::CRSManager::instance().fromEPSG(epsg);
        if (srcCrs && dstCrs && projectToSharedGrid(epsg)) {
                // Determine the CRS mode from the destination CRS kind
                if (dstCrs->isProjected()) {
                    if (dstCrs->code >= 32601 && dstCrs->code <= 32760) {
                        ext.crsMode = terrain::GeoCrsMode::UTM;
                    } else {
                        // Other projected CRS — use UTM mode with the
                        // actual EPSG code for GeoTIFF metadata
                        ext.crsMode = terrain::GeoCrsMode::UTM;
                    }
                    ext.utmEpsg = epsg;
                } else if (dstCrs->isGeographic()) {
                    ext.crsMode = terrain::GeoCrsMode::WGS84;
                    ext.utmEpsg = epsg;
                } else {
                    // Default to UTM mode for other projected CRS
                    ext.crsMode = terrain::GeoCrsMode::UTM;
                    ext.utmEpsg = epsg;
                }
        } else {
            // CRS lookup or transform failed — fall back to WGS84
            ext.crsMode = terrain::GeoCrsMode::WGS84;
            ext.west = west;
            ext.east = east;
            ext.north = north;
            ext.south = south;
        }
        break;
    }

    default:
        // Fallback: WGS84
        ext.crsMode = terrain::GeoCrsMode::WGS84;
        ext.west = west;
        ext.east = east;
        ext.north = north;
        ext.south = south;
        break;
    }

    return ext;
}

bool ExportEngine::writeDemOutput(const std::vector<float>& elevations,
                                   int width, int height,
                                   const terrain::Tile& tile,
                                   const QString& outputPath)
{
    const auto& settings = m_store->exportSettings();
    terrain::RasterExtent ext = buildRasterExtent(tile);
    bool written = false;

    // PNG fallback keeps the terrain usable by OGRE even if libtiff cannot
    // write to a synced/Unicode project path. It stores normalized elevation;
    // the selected GeoTIFF remains the preferred output when it succeeds.
    // The preview is written to a .png SIBLING — never into the .tif/.r16
    // path, because a mislabeled PNG breaks every GeoTIFF consumer
    // (Unigine/QGIS report it as an unreadable or non-reprojectable layer).
    auto writePreviewPng = [&]() {
        if (elevations.empty() || width <= 0 || height <= 0) return false;
        const QFileInfo outInfo(outputPath);
        QString pngPath = outInfo.absolutePath() + "/" + outInfo.completeBaseName() + ".png";
        float zMin = std::numeric_limits<float>::max();
        float zMax = std::numeric_limits<float>::lowest();
        for (float e : elevations) {
            if (e != -9999.0f) {
                zMin = std::min(zMin, e);
                zMax = std::max(zMax, e);
            }
        }
        if (zMax <= zMin) { zMin = 0.0f; zMax = 1.0f; }
        const float range = zMax - zMin;
        QImage preview(width, height, QImage::Format_Grayscale8);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float e = elevations[y * width + x];
                if (e == -9999.0f) e = zMin;
                const int value = qBound(0, int(((e - zMin) / range) * 255.0f), 255);
                preview.setPixelColor(x, y, QColor(value, value, value));
            }
        }
        // Clear any partial binary output before dropping a PNG next to it
        QFile::remove(outputPath);
        bool ok = RasterWriter::writePngWithWorldFile(pngPath, preview, ext) &&
                  QFileInfo::exists(pngPath);
        if (ok)
            m_log.warn("GeoTIFF write failed for", outputPath,
                       "— wrote normalized PNG preview instead:", pngPath);
        return ok;
    };

    // Compression setting
    terrain::Compression comp = settings.compressDeflate ?
        terrain::Compression::Deflate : terrain::Compression::None;

    // Write based on heightmap format (QGIS pattern: choose data type per format)
    QString geotiffPath = outputPath;
    geotiffPath.replace(".png", ".tif");

    switch (settings.heightmapFormat) {
    case terrain::HeightmapFormat::GeoTIFF_Float32:
        // Full precision elevation — QGIS preferred for DEM
        written = RasterWriter::writeFloat32GeoTiff(geotiffPath, elevations,
            width, height, ext, -9999.0f, comp) && QFileInfo::exists(geotiffPath);
        if (!written) written = writePreviewPng();
        break;

    case terrain::HeightmapFormat::GeoTIFF_Int16:
        {
            // Convert float elevations to Int16 (meters, clamped)
            std::vector<int16_t> int16Data(width * height);
            for (int i = 0; i < width * height; ++i) {
                float e = elevations[i];
                int16Data[i] = (e == -9999.0f) ? -9999 :
                    static_cast<int16_t>(std::max(-32768.0f, std::min(32767.0f, e)));
            }
            written = RasterWriter::writeInt16GeoTiff(geotiffPath, int16Data,
                width, height, ext, -9999, comp) && QFileInfo::exists(geotiffPath);
            if (!written) written = writePreviewPng();
        }
        break;

    case terrain::HeightmapFormat::GeoTIFF_UInt16:
        {
            // Normalize to 0-65535
            float zMin = std::numeric_limits<float>::max();
            float zMax = std::numeric_limits<float>::lowest();
            for (float e : elevations) {
                if (e != -9999.0f) {
                    zMin = std::min(zMin, e);
                    zMax = std::max(zMax, e);
                }
            }
            if (zMax <= zMin) { zMin = 0; zMax = 1; }
            float zRange = zMax - zMin;

            std::vector<uint16_t> uint16Data(width * height);
            for (int i = 0; i < width * height; ++i) {
                float e = elevations[i];
                if (e == -9999.0f) e = zMin;
                uint16Data[i] = static_cast<uint16_t>(((e - zMin) / zRange) * 65535.0f);
            }
            written = RasterWriter::writeUInt16GeoTiff(geotiffPath, uint16Data,
                width, height, ext, 0, comp) && QFileInfo::exists(geotiffPath);
            if (!written) written = writePreviewPng();
        }
        break;

    case terrain::HeightmapFormat::PNG16:
        {
            // PNG 16-bit + world file (QGIS layout exporter pattern)
            float zMin = std::numeric_limits<float>::max();
            float zMax = std::numeric_limits<float>::lowest();
            for (float e : elevations) {
                if (e != -9999.0f) {
                    zMin = std::min(zMin, e);
                    zMax = std::max(zMax, e);
                }
            }
            if (zMax <= zMin) { zMin = 0; zMax = 1; }
            float zRange = zMax - zMin;

            QImage img(width, height, QImage::Format_Grayscale16);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    float e = elevations[y * width + x];
                    if (e == -9999.0f) e = zMin;
                    uint16_t val = static_cast<uint16_t>(((e - zMin) / zRange) * 65535.0f);
                    img.setPixel(x, y, qRgba64(val, val, val, 65535));
                }
            }
            written = RasterWriter::writePngWithWorldFile(outputPath, img, ext) &&
                      QFileInfo::exists(outputPath);
        }
        break;

    case terrain::HeightmapFormat::R16:
        {
            // Raw R16 binary + world file
            QString r16Path = outputPath;
            r16Path.replace(".png", ".r16");
            QFile r16File(r16Path);
            bool r16Written = false;
            if (r16File.open(QIODevice::WriteOnly)) {
                float zMin = std::numeric_limits<float>::max();
                float zMax = std::numeric_limits<float>::lowest();
                for (float e : elevations) {
                    if (e != -9999.0f) { zMin = std::min(zMin, e); zMax = std::max(zMax, e); }
                }
                if (zMax <= zMin) { zMin = 0; zMax = 1; }
                float zRange = zMax - zMin;
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        float e = elevations[y * width + x];
                        if (e == -9999.0f) e = zMin;
                        uint16_t val = static_cast<uint16_t>(((e - zMin) / zRange) * 65535.0f);
                        r16File.write(reinterpret_cast<const char*>(&val), sizeof(uint16_t));
                    }
                }
                r16File.close();
                r16Written = true;
            }
            // Write world file for R16
            r16Written = r16Written &&
                         RasterWriter::writeWorldFile(r16Path, width, height, ext) &&
                         QFileInfo::exists(r16Path);
            written = r16Written || writePreviewPng();
        }
        break;

    case terrain::HeightmapFormat::None:
    default:
        // No heightmap output requested (albedo only) — nothing to write,
        // but not a failure either.
        return true;
    }
    return written;
}

bool ExportEngine::writeImageryOutput(const QImage& img,
                                       const terrain::Tile& tile,
                                       const QString& outputPath)
{
    const auto& settings = m_store->exportSettings();
    terrain::RasterExtent ext = buildRasterExtent(tile);
    bool written = false;

    switch (settings.albedoFormat) {
    case terrain::AlbedoFormat::GeoTIFF_RGB:
        {
            // RGB GeoTIFF with proper geo-referencing (QGIS pattern)
            terrain::Compression comp = settings.compressDeflate ?
                terrain::Compression::Deflate : terrain::Compression::None;
            QString geotiffPath = outputPath;
            geotiffPath.replace(".png", ".tif");
            written = RasterWriter::writeRgbGeoTiff(geotiffPath, img, ext, comp) &&
                      QFileInfo::exists(geotiffPath);
        }
        break;

    case terrain::AlbedoFormat::PNG:
    default:
        // PNG + world file (QGIS layout exporter pattern)
        written = RasterWriter::writePngWithWorldFile(outputPath, img, ext) &&
                  QFileInfo::exists(outputPath);
        break;
    }
    return written;
}

void ExportEngine::writeManifest(const QString& dir) {
    QJsonObject manifest;
    manifest["version"] = "1.0";
    manifest["preset"] = "babylonjs";
    manifest["tileSizeKm"] = m_store->tileSizeKm();

    QJsonObject boundsObj = m_store->selectedBounds().toJson();
    manifest["bounds"] = boundsObj;

    QJsonObject settingsObj;
    settingsObj["heightmapFormat"] = m_store->exportSettings().heightmapFormatStr();
    settingsObj["demSource"] = m_store->exportSettings().demSourceStr();
    settingsObj["heightmapResolution"] = m_store->exportSettings().heightmapResolution;
    settingsObj["albedoResolution"] = m_store->exportSettings().albedoResolution;
    manifest["exportSettings"] = settingsObj;

    QJsonArray tilesArray;
    const auto& grid = m_store->tileGrid();
    const auto& selected = m_store->selectedTiles();
    for (const auto& tile : grid.tiles) {
        if (selected.contains(tile.id())) {
            QJsonObject tileObj;
            tileObj["id"] = tile.id();
            tileObj["row"] = tile.row;
            tileObj["col"] = tile.col;
            tileObj["bounds"] = tile.bounds.toJson();
            tilesArray.append(tileObj);
        }
    }
    manifest["tiles"] = tilesArray;

    QJsonDocument doc(manifest);
    QFile file(dir + "/terrain-manifest.json");
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }

    // Record merged products in the manifest when they were produced
    if (QFileInfo::exists(dir + "/heightmap_merged.png")) {
        QJsonObject merged;
        merged["heightmap"] = "heightmap_merged.png";
        if (QFileInfo::exists(dir + "/heightmap_merged.tif"))
            merged["heightmapGeotiff"] = "heightmap_merged.tif";
        if (QFileInfo::exists(dir + "/albedo_merged.png"))
            merged["albedo"] = "albedo_merged.png";
        if (QFileInfo::exists(dir + "/albedo_merged.tif"))
            merged["albedoGeotiff"] = "albedo_merged.tif";
        manifest["merged"] = merged;

        QJsonDocument outDoc(manifest);
        QFile outFile(dir + "/terrain-manifest.json");
        if (outFile.open(QIODevice::WriteOnly)) {
            outFile.write(outDoc.toJson(QJsonDocument::Indented));
            outFile.close();
        }
    }
}

// ============================================================
// Merged terrain product — assemble all exported tiles into one
// usable heightmap and albedo for 3D Studio and Unigine import.
// ============================================================

void ExportEngine::writeMergedOutputs(const QString& dir) {
    const auto& grid = m_store->tileGrid();
    const auto& settings = m_store->exportSettings();
    const int demRes = settings.heightmapResolution;
    const int albRes = settings.albedoResolution;

    if (grid.rows <= 0 || grid.cols <= 0) return;
    if (m_tileDemData.isEmpty() && m_tileAlbedoData.isEmpty()) return;

    const int mergedH = grid.rows * demRes;
    const int mergedW = grid.cols * demRes;
    const int mergedAlbH = grid.rows * albRes;
    const int mergedAlbW = grid.cols * albRes;

    // Assemble DEM merged data (row 0 = north)
    std::vector<float> mergedDem(mergedW * mergedH, -9999.0f);
    for (const auto& tile : grid.tiles) {
        if (!m_tileDemData.contains(tile.id())) continue;
        const auto& data = m_tileDemData[tile.id()];
        if (data.empty() || data.size() != static_cast<size_t>(demRes * demRes)) continue;
        const int baseX = tile.col * demRes;
        const int baseY = (grid.rows - 1 - tile.row) * demRes;
        for (int y = 0; y < demRes; ++y) {
            for (int x = 0; x < demRes; ++x) {
                mergedDem[(baseY + y) * mergedW + baseX + x] = data[y * demRes + x];
            }
        }
    }

    // Assemble albedo merged image
    QImage mergedAlb;
    if (!m_tileAlbedoData.isEmpty()) {
        mergedAlb = QImage(mergedAlbW, mergedAlbH, QImage::Format_RGB888);
        mergedAlb.fill(Qt::darkGray);
        QPainter painter(&mergedAlb);
        for (const auto& tile : grid.tiles) {
            if (!m_tileAlbedoData.contains(tile.id())) continue;
            const QImage& img = m_tileAlbedoData[tile.id()];
            if (img.isNull() || img.width() == 0 || img.height() == 0) continue;
            int baseX = tile.col * albRes;
            int baseY = (grid.rows - 1 - tile.row) * albRes;
            painter.drawImage(baseX, baseY, img);
        }
        painter.end();
    }

    // Build extent from full selected bounds
    terrain::Tile mergedTile;
    mergedTile.bounds = m_store->selectedBounds();
    terrain::RasterExtent mergedExt = buildRasterExtent(mergedTile);
    terrain::Compression comp = settings.compressDeflate ?
        terrain::Compression::Deflate : terrain::Compression::None;

    // Write merged 8-bit PNG preview for 3D Studio
    float zMin = std::numeric_limits<float>::max();
    float zMax = std::numeric_limits<float>::lowest();
    for (float e : mergedDem) {
        if (e != -9999.0f) {
            zMin = std::min(zMin, e);
            zMax = std::max(zMax, e);
        }
    }
    if (zMax <= zMin) { zMin = 0.0f; zMax = 1.0f; }
    const float range = zMax - zMin;
    QImage png(mergedW, mergedH, QImage::Format_Grayscale8);
    for (int y = 0; y < mergedH; ++y) {
        uint8_t* line = png.scanLine(y);
        for (int x = 0; x < mergedW; ++x) {
            float e = mergedDem[y * mergedW + x];
            if (e == -9999.0f) e = zMin;
            int v = qBound(0, static_cast<int>(((e - zMin) / range) * 255.0f), 255);
            line[x] = static_cast<uint8_t>(v);
        }
    }
    QString pngPath = dir + "/heightmap_merged.png";
    bool pngOk = RasterWriter::writePngWithWorldFile(pngPath, png, mergedExt) &&
                 QFileInfo::exists(pngPath);
    if (!pngOk) m_log.warn("Failed to write merged heightmap PNG:", pngPath);

    // Write full precision merged GeoTIFF for Unigine / QGIS
    if (settings.heightmapFormat != terrain::HeightmapFormat::None) {
        QString tifPath = dir + "/heightmap_merged.tif";
        bool tifOk = RasterWriter::writeFloat32GeoTiff(tifPath, mergedDem,
            mergedW, mergedH, mergedExt, -9999.0f, comp) &&
            QFileInfo::exists(tifPath);
        if (!tifOk) m_log.warn("Failed to write merged heightmap GeoTIFF:", tifPath);
    }

    // Write merged albedo PNG (and optionally GeoTIFF)
    if (!mergedAlb.isNull()) {
        QString albPngPath = dir + "/albedo_merged.png";
        bool albPngOk = RasterWriter::writePngWithWorldFile(albPngPath, mergedAlb, mergedExt) &&
                        QFileInfo::exists(albPngPath);
        if (!albPngOk) m_log.warn("Failed to write merged albedo PNG:", albPngPath);

        if (settings.albedoFormat == terrain::AlbedoFormat::GeoTIFF_RGB) {
            QString albTifPath = dir + "/albedo_merged.tif";
            bool albTifOk = RasterWriter::writeRgbGeoTiff(albTifPath, mergedAlb, mergedExt, comp) &&
                            QFileInfo::exists(albTifPath);
            if (!albTifOk) m_log.warn("Failed to write merged albedo GeoTIFF:", albTifPath);
        }
    }

    m_log.info("Merged terrain product:", mergedW, "x", mergedH,
               "DEM tiles:", m_tileDemData.size(),
               "albedo tiles:", m_tileAlbedoData.size());
}

// ============================================================
// GeoTIFF writing using libtiff
// ============================================================

bool ExportEngine::writeGeoTiffHeightmap(const QString& path, const QImage& img,
                                          double north, double south, double east, double west) {
    TIFF* tif = TIFFOpen(PathHelper::toTiffPath(path).toUtf8().constData(), "w");
    if (!tif) return false;

    int width = img.width();
    int height = img.height();

    // Set TIFF tags for grayscale 16-bit
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);

    // GeoTIFF tags (ModelPixelScale + ModelTiepoint)
    // Pixel scale: degrees per pixel
    double scaleX = (east - west) / width;
    double scaleY = (north - south) / height;
    double pixelScale[3] = {scaleX, scaleY, 0};
    TIFFSetField(tif, 33550, 3, pixelScale);

    // Tiepoint: upper-left corner maps to (west, north)
    double tiepoint[6] = {0, 0, 0, west, north, 0};
    TIFFSetField(tif, 33922, 6, tiepoint);

    // GeoKey directory header
    uint16_t geoKeyDir[4] = {1, 1, 0, 0}; // Version 1, revision 1
    TIFFSetField(tif, 34735, 4, geoKeyDir);

    // Write pixel data (16-bit grayscale from QImage)
    QImage grayImg = img.convertToFormat(QImage::Format_Grayscale16);
    for (int y = 0; y < height; ++y) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(grayImg.scanLine(y));
        TIFFWriteScanline(tif, const_cast<uint16_t*>(row), y, 0);
    }

    TIFFClose(tif);
    return true;
}

bool ExportEngine::writeGeoTiffRgb(const QString& path, const QImage& img,
                                    double north, double south, double east, double west) {
    TIFF* tif = TIFFOpen(PathHelper::toTiffPath(path).toUtf8().constData(), "w");
    if (!tif) return false;

    int width = img.width();
    int height = img.height();

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, 1);

    // GeoTIFF tags
    double scaleX = (east - west) / width;
    double scaleY = (north - south) / height;
    double pixelScale[3] = {scaleX, scaleY, 0};
    TIFFSetField(tif, 33550, 3, pixelScale);

    double tiepoint[6] = {0, 0, 0, west, north, 0};
    TIFFSetField(tif, 33922, 6, tiepoint);

    uint16_t geoKeyDir[4] = {1, 1, 0, 0};
    TIFFSetField(tif, 34735, 4, geoKeyDir);

    QImage rgbImg = img.convertToFormat(QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = rgbImg.scanLine(y);
        TIFFWriteScanline(tif, const_cast<unsigned char*>(row), y, 0);
    }

    TIFFClose(tif);
    return true;
}

bool ExportEngine::writeR16Heightmap(const QString& path, const QImage& img) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QImage grayImg = img.convertToFormat(QImage::Format_Grayscale16);
    for (int y = 0; y < grayImg.height(); ++y) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(grayImg.scanLine(y));
        file.write(reinterpret_cast<const char*>(row), grayImg.width() * sizeof(uint16_t));
    }

    file.close();
    return true;
}
