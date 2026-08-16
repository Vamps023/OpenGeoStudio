// ExportEngine — DEM and imagery download + file writing implementation

#include "ExportEngine.hpp"
#include "RasterWriter.hpp"
#include "../../core/PathHelper.hpp"
#include "DemDecoder.hpp"

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

    // Download DEM (heightmap) or load from local file
    QString demExt;
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
    QString demPath = m_exportDir + "/heightmaps/tile_" + m_currentTile.id() + demExt;
    if (m_store->exportSettings().demSource == terrain::DemSource::Local_File) {
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
    const QString url = demUrlForTile(tile);
    if (url.isEmpty()) {
        // No URL available — API key missing or source not configured
        QString reason;
        auto src = m_store->exportSettings().demSource;
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
        // GLAD uses basic auth: glad/ardpas
        QString header = "glad:ardpas";
        request.setRawHeader("Authorization", "Basic " + header.toUtf8().toBase64());
    }
    if (!m_store->exportSettings().openTopoApiKey.isEmpty() &&
        m_store->exportSettings().demSource != terrain::DemSource::GLAD_SRTM &&
        m_store->exportSettings().demSource != terrain::DemSource::GPXZ_LiDAR) {
        request.setRawHeader("api-key", m_store->exportSettings().openTopoApiKey.toUtf8());
    }
    // GPXZ uses x-api-key header (more secure than URL parameter)
    if (m_store->exportSettings().demSource == terrain::DemSource::GPXZ_LiDAR &&
        !m_store->exportSettings().gpxzApiKey.isEmpty()) {
        request.setRawHeader("x-api-key", m_store->exportSettings().gpxzApiKey.toUtf8());
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
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(false, QString("DEM download failed for tile %1: %2")
                              .arg(tile.id()).arg(reply->errorString()));
            return;
        }

        QByteArray data = reply->readAll();
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
        terrain::DemTile demTile = terrain::DemDecoder::decodeAuto(data, sourceName);

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

        // Resample to target resolution (QGIS bilinear interpolation pattern)
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
    // Compute tile coordinates for the tile center
    double centerLat = (tile.bounds.north + tile.bounds.south) / 2.0;
    double centerLon = (tile.bounds.east + tile.bounds.west) / 2.0;

    // Compute zoom level — use manual override if set, otherwise auto-calculate
    int manualZoom = m_store->exportSettings().imageryZoomLevel;
    int zoom;
    if (manualZoom > 0) {
        zoom = manualZoom;
    } else {
        // Auto-calculate based on tile size
        double tileSizeKm = m_store->tileSizeKm();
        if (tileSizeKm <= 1) zoom = 15;
        else if (tileSizeKm <= 2) zoom = 14;
        else if (tileSizeKm <= 4) zoom = 13;
        else if (tileSizeKm <= 8) zoom = 12;
        else zoom = 11; // 16km tiles
    }

    // Compute tile X/Y from lat/lon at the given zoom
    double n = std::pow(2.0, zoom);
    int x = static_cast<int>((centerLon + 180.0) / 360.0 * n);
    double latRad = centerLat * M_PI / 180.0;
    int y = static_cast<int>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n);

    QString url = imageryTileUrl(zoom, x, y, tile);
    if (url.isEmpty()) {
        // No URL — API key missing or source not configured
        QString reason;
        auto src = m_store->exportSettings().imagerySource;
        if (src == terrain::ImagerySource::Mapbox_Satellite)
            reason = "Mapbox token is required for Mapbox Satellite imagery";
        else if (src == terrain::ImagerySource::MapTiler_Satellite)
            reason = "MapTiler token is required for MapTiler Satellite imagery";
        else if (src == terrain::ImagerySource::Local_File)
            reason = "Local imagery file path is not set — select a file in the export panel";
        else
            reason = "Imagery source URL is empty — check source settings";

        emit finished(false, QString("Imagery export failed for tile %1: %2")
                          .arg(tile.id()).arg(reason));
        return;
    }

    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, outputPath, tile]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(false, QString("Imagery download failed for tile %1: %2")
                              .arg(tile.id()).arg(reply->errorString()));
            return;
        }

        QByteArray data = reply->readAll();
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
                            Qt::KeepAspectRatioByExpanding,
                            Qt::SmoothTransformation);
        }
        // Use QGIS-style imagery output (PNG+world file or GeoTIFF RGB)
        if (!writeImageryOutput(img, tile, outputPath)) {
            emit finished(false, QString("Failed to write imagery output for tile %1: %2")
                              .arg(tile.id(), outputPath));
            return;
        }

        m_tileAlbedoData[tile.id()] = img;
        m_imageryDownloaded = true;
        if (m_demDownloaded) {
            m_completedTiles++;
            processNextTile();
        }
    });
}

QString ExportEngine::demUrlForTile(const terrain::Tile& tile) const {
    const auto& settings = m_store->exportSettings();

    QString demType;
    switch (settings.demSource) {
    // Tiled DEM sources (no API key needed)
    case terrain::DemSource::AWS_Terrarium:
    case terrain::DemSource::Mapzen_Terrarium:
    case terrain::DemSource::Mapbox_TerrainRGB: {
        double centerLat = (tile.bounds.north + tile.bounds.south) / 2.0;
        double centerLon = (tile.bounds.east + tile.bounds.west) / 2.0;
        double tileSizeKm = m_store->tileSizeKm();
        int zoom;
        if (tileSizeKm <= 1) zoom = 15;
        else if (tileSizeKm <= 2) zoom = 14;
        else if (tileSizeKm <= 4) zoom = 13;
        else if (tileSizeKm <= 8) zoom = 12;
        else zoom = 11;
        double n = std::pow(2.0, zoom);
        int x = static_cast<int>((centerLon + 180.0) / 360.0 * n);
        double latRad = centerLat * M_PI / 180.0;
        int y = static_cast<int>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n);

        if (settings.demSource == terrain::DemSource::Mapbox_TerrainRGB) {
            if (settings.mapboxToken.isEmpty()) return {};
            return QString("https://api.mapbox.com/v4/mapbox.terrain-rgb/%1/%2/%3.png?access_token=%4")
                .arg(zoom).arg(x).arg(y).arg(settings.mapboxToken);
        }
        return QString("https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%1/%2/%3.png")
            .arg(zoom).arg(x).arg(y);
    }
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
        if (settings.gpxzApiKey.isEmpty()) return {};
        // API key is passed via x-api-key header (set in downloadDemForTile)
        return QString("https://api.gpxz.io/v1/elevation/raster?"
                       "bbox_left=%1&bbox_right=%2&bbox_bottom=%3&bbox_top=%4"
                       "&height_px=%5&width_px=%6")
            .arg(tile.bounds.west, 0, 'f', 6)
            .arg(tile.bounds.east, 0, 'f', 6)
            .arg(tile.bounds.south, 0, 'f', 6)
            .arg(tile.bounds.north, 0, 'f', 6)
            .arg(settings.heightmapResolution)
            .arg(settings.heightmapResolution);
    case terrain::DemSource::GLAD_SRTM:
        return QString("https://glad.umd.edu/dataset/srtm-90m/%1/%2")
            .arg(tile.bounds.south, 0, 'f', 4).arg(tile.bounds.west, 0, 'f', 4);
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
        return QString("https://api.mapbox.com/v4/mapbox.satellite/%1/%2/%3.png?access_token=%4")
            .arg(z).arg(x).arg(y).arg(settings.mapboxToken);
    case terrain::ImagerySource::MapTiler_Satellite:
        if (settings.maptilerToken.isEmpty()) return {};
        return QString("https://api.maptiler.com/tiles/satellite/%1/%2/%3.jpg?key=%4")
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

// Lat/Lon to UTM conversion (returns zone + easting/northing)
static void latLonToUtm(double lat, double lon, int& zone, bool& north,
                        double& easting, double& northing) {
    zone = static_cast<int>((lon + 180.0) / 6.0) + 1;
    north = lat >= 0.0;

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

terrain::RasterExtent ExportEngine::buildRasterExtent(const terrain::Tile& tile) const {
    terrain::RasterExtent ext;

    // Source bounds are always in WGS84 lat/lon
    double west = tile.bounds.west;
    double east = tile.bounds.east;
    double north = tile.bounds.north;
    double south = tile.bounds.south;

    auto crs = m_store->exportSettings().crsSource;
    switch (crs) {
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
        // Compute UTM zone from centroid
        double clat = (north + south) / 2.0;
        double clon = (west + east) / 2.0;
        int zone;
        bool isNorth;
        double e_w, n_n, e_e, n_s;
        latLonToUtm(north, west, zone, isNorth, e_w, n_n);
        latLonToUtm(south, east, zone, isNorth, e_e, n_s);
        ext.crsMode = terrain::GeoCrsMode::UTM;
        ext.utmEpsg = isNorth ? (32600 + zone) : (32700 + zone);
        ext.west = e_w;
        ext.east = e_e;
        ext.north = n_n;
        ext.south = n_s;
        break;
    }

    case terrain::CrsSource::EPSG_32633:
    case terrain::CrsSource::EPSG_32634:
    case terrain::CrsSource::EPSG_32635:
    case terrain::CrsSource::EPSG_25832:
    case terrain::CrsSource::EPSG_25833:
    {
        // Fixed UTM zone — convert lat/lon to UTM meters
        int targetZone;
        bool isNorth;
        if (crs == terrain::CrsSource::EPSG_32633) { targetZone = 33; isNorth = true; ext.utmEpsg = 32633; }
        else if (crs == terrain::CrsSource::EPSG_32634) { targetZone = 34; isNorth = true; ext.utmEpsg = 32634; }
        else if (crs == terrain::CrsSource::EPSG_32635) { targetZone = 35; isNorth = true; ext.utmEpsg = 32635; }
        else if (crs == terrain::CrsSource::EPSG_25832) { targetZone = 32; isNorth = true; ext.utmEpsg = 25832; }
        else { targetZone = 33; isNorth = true; ext.utmEpsg = 25833; }

        // Use the target zone for conversion (not auto-computed)
        // We need to compute UTM with the specific zone
        double clon = (west + east) / 2.0;
        (void)clon;  // suppress unused warning

        // Convert all 4 corners using the target zone
        auto convertPoint = [&](double lat, double lon) -> std::pair<double, double> {
            const double a = 6378137.0;
            const double f = 1.0 / 298.257223563;
            const double k0 = 0.9996;
            const double e2 = f * (2.0 - f);
            const double e2sq = e2 * e2;
            double latRad = lat * M_PI / 180.0;
            double lonRad = lon * M_PI / 180.0;
            double lonOrigin = (targetZone - 1) * 6.0 - 180.0 + 3.0;
            double lonOriginRad = lonOrigin * M_PI / 180.0;
            double N = a / sqrt(1.0 - e2 * sin(latRad) * sin(latRad));
            double T = tan(latRad) * tan(latRad);
            double C = e2sq * cos(latRad) * cos(latRad) / (1.0 - e2);
            double A = cos(latRad) * (lonRad - lonOriginRad);
            double M = a * ((1.0 - e2/4.0 - 3.0*e2sq/64.0) * latRad
                      - (3.0*e2/8.0 + 3.0*e2sq/32.0) * sin(2.0*latRad)
                      + (15.0*e2sq/256.0) * sin(4.0*latRad));
            double easting = k0 * N * (A + (1.0 - T + C) * A*A*A / 6.0) + 500000.0;
            double northing = k0 * (M + N * tan(latRad) * (A*A / 2.0
                      + (5.0 - T + 9.0*C) * A*A*A*A / 24.0));
            if (!isNorth) northing += 10000000.0;
            return {easting, northing};
        };

        auto [e_w, n_n] = convertPoint(north, west);
        auto [e_e, n_s] = convertPoint(south, east);
        ext.crsMode = terrain::GeoCrsMode::UTM;
        ext.west = e_w;
        ext.east = e_e;
        ext.north = n_n;
        ext.south = n_s;
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
    auto writePreviewPng = [&]() {
        if (elevations.empty() || width <= 0 || height <= 0) return false;
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
        return RasterWriter::writePngWithWorldFile(outputPath, preview, ext) &&
               QFileInfo::exists(outputPath);
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
        // No heightmap output (albedo only)
        break;
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
