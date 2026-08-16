#pragma once

// ============================================================
// ImageryProviders — Concrete satellite imagery providers
// ============================================================

#include "TerrainDataProvider.hpp"
#include <QPainter>
#include <cmath>

namespace terrain_pipeline {

// ============================================================
// ArcGisImageryProvider — ArcGIS World Imagery (free, no key)
// ============================================================

class ArcGisImageryProvider : public ImageryProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit ArcGisImageryProvider(QObject* parent = nullptr) : ImageryProvider(parent) {}

    ProviderInfo info() const override {
        return {"arcgis", "ArcGIS World Imagery",
                ProviderCapability::Imagery, "1.0", "Esri", false, "",
                "© Esri, Maxar, Earthstar Geographics"};
    }
    QString name() const override { return "arcgis"; }

    QString tileUrl(int z, int x, int y) const override {
        return QString("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/%1/%2/%3")
            .arg(z).arg(y).arg(x);
    }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        int z = optimalZoomLevel(maxLat - minLat, maxLon - minLon, resolution);

        int minX = lonToTileX(minLon, z);
        int maxX = lonToTileX(maxLon, z);
        int minY = latToTileY(maxLat, z);
        int maxY = latToTileY(minLat, z);

        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                DownloadRequest req;
                req.url = tileUrl(z, x, y);
                req.cacheKey = QString("arcgis_%1_%2_%3").arg(z).arg(x).arg(y);
                req.providerName = "arcgis";
                req.datasetName = "imagery";
                requests.append(req);
            }
        }
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QImage img(path);
        return !img.isNull() && img.width() > 0;
    }

    QImage processImagery(const QList<QString>& paths,
                           int targetWidth, int targetHeight) const override {
        // Mosaic tiles into a single image
        if (paths.isEmpty()) return QImage();
        // Simple mosaic: load all and stitch (assumes sorted by tile order)
        QImage result(targetWidth, targetHeight, QImage::Format_RGB32);
        result.fill(Qt::black);

        // For each tile, paint it into the appropriate position
        // This is simplified — real implementation would compute tile positions
        for (const QString& path : paths) {
            QImage tile(path);
            if (tile.isNull()) continue;
            // Paint tile into result (simplified: just scale first tile)
            QPainter painter(&result);
            painter.drawImage(result.rect(), tile);
            break;  // Simplified — use first tile
        }
        return result;
    }

private:
    static int lonToTileX(double lon, int z) {
        return static_cast<int>(std::floor((lon + 180.0) / 360.0 * (1 << z)));
    }
    static int latToTileY(double lat, int z) {
        double latRad = lat * M_PI / 180.0;
        return static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0/std::cos(latRad)) / M_PI) / 2.0 * (1 << z)));
    }
};

// ============================================================
// GoogleSatelliteProvider — Google Satellite imagery (no key)
// ============================================================

class GoogleSatelliteProvider : public ImageryProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit GoogleSatelliteProvider(QObject* parent = nullptr) : ImageryProvider(parent) {}

    ProviderInfo info() const override {
        return {"google", "Google Satellite",
                ProviderCapability::Imagery, "1.0", "Google ToS", false, "",
                "© Google"};
    }
    QString name() const override { return "google"; }

    QString tileUrl(int z, int x, int y) const override {
        QString server = "mt" + QString::number((x + y) % 3);
        return QString("https://%1.google.com/vt/lyrs=s&x=%2&y=%3&z=%4")
            .arg(server).arg(x).arg(y).arg(z);
    }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        int z = optimalZoomLevel(maxLat - minLat, maxLon - minLon, resolution);

        int minX = lonToTileX(minLon, z);
        int maxX = lonToTileX(maxLon, z);
        int minY = latToTileY(maxLat, z);
        int maxY = latToTileY(minLat, z);

        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                DownloadRequest req;
                req.url = tileUrl(z, x, y);
                req.cacheKey = QString("google_%1_%2_%3").arg(z).arg(x).arg(y);
                req.providerName = "google";
                req.datasetName = "imagery";
                requests.append(req);
            }
        }
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QImage img(path);
        return !img.isNull();
    }

private:
    static int lonToTileX(double lon, int z) {
        return static_cast<int>(std::floor((lon + 180.0) / 360.0 * (1 << z)));
    }
    static int latToTileY(double lat, int z) {
        double latRad = lat * M_PI / 180.0;
        return static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0/std::cos(latRad)) / M_PI) / 2.0 * (1 << z)));
    }
};

// ============================================================
// MapboxImageryProvider — Mapbox Satellite (requires token)
// ============================================================

class MapboxImageryProvider : public ImageryProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit MapboxImageryProvider(const QString& token, QObject* parent = nullptr)
        : ImageryProvider(parent), m_token(token) {}

    ProviderInfo info() const override {
        return {"mapbox", "Mapbox Satellite",
                ProviderCapability::Imagery, "1.0", "Mapbox ToS",
                true, "mapbox_token", "© Mapbox"};
    }
    QString name() const override { return "mapbox"; }
    bool isAvailable() const override { return !m_token.isEmpty(); }

    QString tileUrl(int z, int x, int y) const override {
        return QString("https://api.mapbox.com/v4/mapbox.satellite/%1/%2/%3@2x.pngraw?access_token=%4")
            .arg(z).arg(x).arg(y).arg(m_token);
    }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        if (m_token.isEmpty()) return requests;
        int z = optimalZoomLevel(maxLat - minLat, maxLon - minLon, resolution);

        int minX = lonToTileX(minLon, z);
        int maxX = lonToTileX(maxLon, z);
        int minY = latToTileY(maxLat, z);
        int maxY = latToTileY(minLat, z);

        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                DownloadRequest req;
                req.url = tileUrl(z, x, y);
                req.cacheKey = QString("mapbox_img_%1_%2_%3").arg(z).arg(x).arg(y);
                req.providerName = "mapbox";
                req.datasetName = "imagery";
                requests.append(req);
            }
        }
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QImage img(path);
        return !img.isNull();
    }

private:
    QString m_token;
    static int lonToTileX(double lon, int z) {
        return static_cast<int>(std::floor((lon + 180.0) / 360.0 * (1 << z)));
    }
    static int latToTileY(double lat, int z) {
        double latRad = lat * M_PI / 180.0;
        return static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0/std::cos(latRad)) / M_PI) / 2.0 * (1 << z)));
    }
};

// ============================================================
// LocalImageryProvider — Local imagery file
// ============================================================

class LocalImageryProvider : public ImageryProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit LocalImageryProvider(const QString& filePath, QObject* parent = nullptr)
        : ImageryProvider(parent), m_filePath(filePath) {}

    ProviderInfo info() const override {
        return {"local-file", "Local imagery file",
                ProviderCapability::Imagery, "1.0", "User", false, "", ""};
    }
    QString name() const override { return "local-file"; }
    bool isAvailable() const override { return QFile::exists(m_filePath); }

    QString tileUrl(int, int, int) const override { return "file://" + m_filePath; }

    QList<DownloadRequest> discoverTiles(double, double, double, double, int) const override {
        QList<DownloadRequest> requests;
        DownloadRequest req;
        req.url = "file://" + m_filePath;
        req.cacheKey = "local_img_" + m_filePath;
        req.providerName = "local-file";
        req.datasetName = "imagery";
        req.outputPath = m_filePath;
        requests.append(req);
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        return QFile::exists(path);
    }

private:
    QString m_filePath;
};

} // namespace terrain_pipeline
