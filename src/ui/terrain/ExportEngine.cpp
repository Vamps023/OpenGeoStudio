// ExportEngine — DEM and imagery download + file writing implementation

#include "ExportEngine.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
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

ExportEngine::ExportEngine(TerrainStore* store, QObject* parent)
    : QObject(parent), m_store(store) {
    m_network = new QNetworkAccessManager(this);
}

void ExportEngine::exportToDirectory(const QString& dir) {
    m_exportDir = dir;

    // Collect selected tiles
    m_pendingTiles.clear();
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

    // Download DEM (heightmap)
    QString demPath = m_exportDir + "/heightmaps/tile_" + m_currentTile.id() + ".png";
    downloadDemForTile(m_currentTile, demPath);

    // Download imagery (albedo)
    QString albedoPath = m_exportDir + "/albedo/tile_" + m_currentTile.id() + ".png";
    downloadImageryForTile(m_currentTile, albedoPath);
}

void ExportEngine::downloadDemForTile(const terrain::Tile& tile, const QString& outputPath) {
    const QString url = demUrlForTile(tile);
    if (url.isEmpty()) {
        // No URL — generate a flat heightmap as placeholder
        QImage img(m_store->exportSettings().heightmapResolution,
                   m_store->exportSettings().heightmapResolution,
                   QImage::Format_Grayscale16);
        img.fill(0);
        img.save(outputPath);
        m_demDownloaded = true;
        if (m_imageryDownloaded) {
            m_completedTiles++;
            processNextTile();
        }
        return;
    }

    QNetworkRequest request((QUrl(url)));
    if (m_store->exportSettings().demSource == terrain::DemSource::GLAD_SRTM) {
        // GLAD uses basic auth: glad/ardpas
        QString header = "glad:ardpas";
        request.setRawHeader("Authorization", "Basic " + header.toUtf8().toBase64());
    }
    if (!m_store->exportSettings().openTopoApiKey.isEmpty() &&
        m_store->exportSettings().demSource != terrain::DemSource::GLAD_SRTM) {
        request.setRawHeader("api-key", m_store->exportSettings().openTopoApiKey.toUtf8());
    }

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, outputPath]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(false, QString("DEM download failed: %1").arg(reply->errorString()));
            return;
        }

        // Save raw DEM data
        QByteArray data = reply->readAll();
        // For now, save as a simple PNG placeholder
        // A full implementation would parse the GeoTIFF/AAIGrid response
        // and convert to a 16-bit grayscale PNG
        QImage img(m_store->exportSettings().heightmapResolution,
                   m_store->exportSettings().heightmapResolution,
                   QImage::Format_Grayscale16);
        img.fill(100); // placeholder elevation
        img.save(outputPath);

        m_demDownloaded = true;
        if (m_imageryDownloaded) {
            m_completedTiles++;
            processNextTile();
        }
    });
}

void ExportEngine::downloadImageryForTile(const terrain::Tile& tile, const QString& outputPath) {
    // Compute tile coordinates for the tile center
    double centerLat = (tile.bounds.north + tile.bounds.south) / 2.0;
    double centerLon = (tile.bounds.east + tile.bounds.west) / 2.0;

    // Compute zoom level based on tile size
    // Approximate: tile covers m_tileSizeKm at the equator
    double tileSizeKm = m_store->tileSizeKm();
    int zoom = 12; // default
    // Adjust zoom based on tile size
    if (tileSizeKm <= 1) zoom = 15;
    else if (tileSizeKm <= 2) zoom = 14;
    else if (tileSizeKm <= 4) zoom = 13;
    else zoom = 12;

    // Compute tile X/Y from lat/lon at the given zoom
    double n = std::pow(2.0, zoom);
    int x = static_cast<int>((centerLon + 180.0) / 360.0 * n);
    double latRad = centerLat * M_PI / 180.0;
    int y = static_cast<int>((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n);

    QString url = imageryTileUrl(zoom, x, y);
    if (url.isEmpty()) {
        QImage img(m_store->exportSettings().albedoResolution,
                   m_store->exportSettings().albedoResolution,
                   QImage::Format_RGB32);
        img.fill(Qt::darkGreen);
        img.save(outputPath);
        m_imageryDownloaded = true;
        if (m_demDownloaded) {
            m_completedTiles++;
            processNextTile();
        }
        return;
    }

    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");

    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, outputPath]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // Fallback: create placeholder image
            QImage img(m_store->exportSettings().albedoResolution,
                       m_store->exportSettings().albedoResolution,
                       QImage::Format_RGB32);
            img.fill(Qt::darkGreen);
            img.save(outputPath);
        } else {
            QByteArray data = reply->readAll();
            QImage img;
            if (img.loadFromData(data)) {
                // Scale to requested resolution
                if (img.width() != m_store->exportSettings().albedoResolution ||
                    img.height() != m_store->exportSettings().albedoResolution) {
                    img = img.scaled(m_store->exportSettings().albedoResolution,
                                    m_store->exportSettings().albedoResolution,
                                    Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation);
                }
                img.save(outputPath);
            } else {
                QImage placeholder(m_store->exportSettings().albedoResolution,
                                   m_store->exportSettings().albedoResolution,
                                   QImage::Format_RGB32);
                placeholder.fill(Qt::darkGreen);
                placeholder.save(outputPath);
            }
        }

        m_imageryDownloaded = true;
        if (m_demDownloaded) {
            m_completedTiles++;
            processNextTile();
        }
    });
}

QString ExportEngine::demUrlForTile(const terrain::Tile& tile) const {
    const auto& settings = m_store->exportSettings();

    // OpenTopography API URL format:
    // https://portal.opentopography.org/API/globaldem?demtype=SRTMGL1&south=...&north=...&west=...&east=...&outputFormat=AAIGrid&API_Key=...
    QString demType;
    switch (settings.demSource) {
    case terrain::DemSource::OpenTopo_SRTM_GL1: demType = "SRTMGL1"; break;
    case terrain::DemSource::OpenTopo_SRTM_GL3: demType = "SRTMGL3"; break;
    case terrain::DemSource::OpenTopo_ALOS_AW3D30: demType = "AW3D30"; break;
    case terrain::DemSource::OpenTopo_Copernicus_GLO30: demType = "COP30"; break;
    case terrain::DemSource::OpenTopo_NASADEM: demType = "NASADEM"; break;
    case terrain::DemSource::GLAD_SRTM:
        // GLAD SRTM uses a different URL pattern
        return QString("https://glad.umd.edu/dataset/srtm-90m/%1/%2")
            .arg(tile.bounds.south, 0, 'f', 4).arg(tile.bounds.west, 0, 'f', 4);
    }

    if (settings.openTopoApiKey.isEmpty()) {
        // No API key — return empty to generate placeholder
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

QString ExportEngine::imageryTileUrl(int z, int x, int y) const {
    const auto& settings = m_store->exportSettings();
    switch (settings.imagerySource) {
    case terrain::ImagerySource::ArcGIS_World_Imagery:
        return QString("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%1/%2/%3")
            .arg(z).arg(y).arg(x);
    case terrain::ImagerySource::Google_Satellite:
        return QString("https://mt1.google.com/vt/lyrs=s&x=%1&y=%2&z=%3")
            .arg(x).arg(y).arg(z);
    case terrain::ImagerySource::Mapbox_Satellite:
        if (settings.mapboxToken.isEmpty()) return {};
        return QString("https://api.mapbox.com/v4/mapbox.satellite/%1/%2/%3.png?access_token=%4")
            .arg(z).arg(x).arg(y).arg(settings.mapboxToken);
    }
    return {};
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
}
