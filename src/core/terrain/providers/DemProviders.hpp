#pragma once

// ============================================================
// DemProviders — Concrete DEM data providers
// ============================================================
//
// Implements DemProvider for:
//   - TerrariumProvider (AWS/Mapzen elevation tiles, no API key)
//   - MapboxTerrainProvider (Mapbox Terrain-RGB, requires token)
//   - CopernicusProvider (Copernicus DEM GLO30, free)
//   - OpenTopographyProvider (multiple DEM sources, requires API key)
//   - GPXZProvider (high-res LiDAR, requires API key)
//   - GLADSrtmProvider (GLAD SRTM, free)
//   - LocalDemProvider (local file)
//

#include "TerrainDataProvider.hpp"
#include "../../../ui/terrain/DemDecoder.hpp"
#include <cmath>

namespace terrain_pipeline {

// ============================================================
// TerrariumProvider — AWS/Mapzen elevation tiles (free, no key)
// ============================================================

class TerrariumProvider : public DemProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit TerrariumProvider(QObject* parent = nullptr) : DemProvider(parent) {}

    ProviderInfo info() const override {
        return {"terrarium", "AWS/Mapzen Terrarium elevation tiles",
                ProviderCapability::DEM, "1.0", "CC0", false, "", "© Mapzen, AWS"};
    }
    QString name() const override { return "terrarium"; }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        // Determine zoom level
        double latSpan = maxLat - minLat;
        double lonSpan = maxLon - minLon;
        int z = 12;  // Default zoom for terrarium
        if (resolution > 512) z = 13;
        if (resolution > 1024) z = 14;

        int minX = lonToTileX(minLon, z);
        int maxX = lonToTileX(maxLon, z);
        int minY = latToTileY(maxLat, z);
        int maxY = latToTileY(minLat, z);

        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                DownloadRequest req;
                req.url = QString("https://s3.amazonaws.com/elevation-tiles-prod/terrarium/%1/%2/%3.png")
                    .arg(z).arg(x).arg(y);
                req.cacheKey = QString("terrarium_%1_%2_%3").arg(z).arg(x).arg(y);
                req.providerName = "terrarium";
                req.datasetName = "dem";
                requests.append(req);
            }
        }
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QImage img(path);
        return !img.isNull() && img.width() > 0 && img.height() > 0;
    }

    RasterGrid decodeElevation(const QByteArray& data) const override {
        terrain::DemTile dt = terrain::DemDecoder::decodeTerrarium(data);
        RasterGrid grid;
        grid.width = dt.width;
        grid.height = dt.height;
        grid.data = std::move(dt.elevations);
        grid.nodataValue = dt.nodataValue;
        return grid;
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
// MapboxTerrainProvider — Mapbox Terrain-RGB (requires token)
// ============================================================

class MapboxTerrainProvider : public DemProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit MapboxTerrainProvider(const QString& token, QObject* parent = nullptr)
        : DemProvider(parent), m_token(token) {}

    ProviderInfo info() const override {
        return {"mapbox-terrain-rgb", "Mapbox Terrain-RGB elevation tiles",
                ProviderCapability::DEM, "1.0", "Mapbox ToS",
                true, "mapbox_token", "© Mapbox"};
    }
    QString name() const override { return "mapbox-terrain-rgb"; }
    bool isAvailable() const override { return !m_token.isEmpty(); }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        if (m_token.isEmpty()) return requests;

        int z = 13;
        if (resolution > 1024) z = 14;

        int minX = lonToTileX(minLon, z);
        int maxX = lonToTileX(maxLon, z);
        int minY = latToTileY(maxLat, z);
        int maxY = latToTileY(minLat, z);

        for (int x = minX; x <= maxX; x++) {
            for (int y = minY; y <= maxY; y++) {
                DownloadRequest req;
                req.url = QString("https://api.mapbox.com/raster-dem/v1/mapbox.terrain-rgb/%1/%2/%3.pngraw?access_token=%4")
                    .arg(z).arg(x).arg(y).arg(m_token);
                req.cacheKey = QString("mapbox_dem_%1_%2_%3").arg(z).arg(x).arg(y);
                req.providerName = "mapbox-terrain-rgb";
                req.datasetName = "dem";
                requests.append(req);
            }
        }
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QImage img(path);
        return !img.isNull();
    }

    RasterGrid decodeElevation(const QByteArray& data) const override {
        terrain::DemTile dt = terrain::DemDecoder::decodeMapboxTerrainRgb(data);
        RasterGrid grid;
        grid.width = dt.width;
        grid.height = dt.height;
        grid.data = std::move(dt.elevations);
        grid.nodataValue = dt.nodataValue;
        return grid;
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
// CopernicusProvider — Copernicus DEM GLO30 (free, no key)
// ============================================================

class CopernicusProvider : public DemProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit CopernicusProvider(QObject* parent = nullptr) : DemProvider(parent) {}

    ProviderInfo info() const override {
        return {"copernicus", "Copernicus DEM GLO30 (free)",
                ProviderCapability::DEM, "GLO30", "Copernicus", false, "",
                "© Copernicus DEM"};
    }
    QString name() const override { return "copernicus"; }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        // Copernicus DEM tiles are 1° x 1°
        int minLatInt = static_cast<int>(std::floor(minLat));
        int maxLatInt = static_cast<int>(std::floor(maxLat));
        int minLonInt = static_cast<int>(std::floor(minLon));
        int maxLonInt = static_cast<int>(std::floor(maxLon));

        for (int lat = minLatInt; lat <= maxLatInt; lat++) {
            for (int lon = minLonInt; lon <= maxLonInt; lon++) {
                QString tileName = formatTileName(lat, lon);
                DownloadRequest req;
                req.url = QString("https://prism-dem-open.copernicus.eu/pd-desk-open-access/prism-dem/COP30/%1/%2/Copernicus_DSM_30_%3.tif")
                    .arg(getSubDir(lat, lon)).arg(getSubDir2(lat, lon)).arg(tileName);
                req.cacheKey = QString("copernicus_%1").arg(tileName);
                req.providerName = "copernicus";
                req.datasetName = "dem";
                requests.append(req);
            }
        }
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QFile f(path);
        return f.exists() && f.size() > 1024;
    }

    RasterGrid decodeElevation(const QByteArray& data) const override {
        terrain::DemTile dt = terrain::DemDecoder::decodeGeoTiff(data);
        RasterGrid grid;
        grid.width = dt.width;
        grid.height = dt.height;
        grid.data = std::move(dt.elevations);
        grid.nodataValue = dt.nodataValue;
        return grid;
    }

private:
    static QString formatTileName(int lat, int lon) {
        QString latStr = QString::number(std::abs(lat)).rightJustified(2, '0');
        QString lonStr = QString::number(std::abs(lon)).rightJustified(3, '0');
        return QString("N%1W%2").arg(latStr).arg(lonStr);  // Simplified
    }
    static QString getSubDir(int lat, int lon) {
        return QString("%1").arg(lon / 10 * 10, 3, 10, QChar('0'));
    }
    static QString getSubDir2(int lat, int lon) {
        return QString("%1").arg(lat / 10 * 10, 2, 10, QChar('0'));
    }
};

// ============================================================
// OpenTopographyProvider — Multiple DEM sources via OpenTopography API
// ============================================================

class OpenTopographyProvider : public DemProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit OpenTopographyProvider(const QString& apiKey, const QString& datasetName,
                                     QObject* parent = nullptr)
        : DemProvider(parent), m_apiKey(apiKey), m_datasetName(datasetName) {}

    ProviderInfo info() const override {
        return {"opentopo-" + m_datasetName, "OpenTopography " + m_datasetName,
                ProviderCapability::DEM, "1.0", "OpenTopography ToS",
                true, "opentopo_api_key", "© OpenTopography"};
    }
    QString name() const override { return "opentopo-" + m_datasetName; }
    bool isAvailable() const override { return !m_apiKey.isEmpty(); }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        if (m_apiKey.isEmpty()) return requests;

        DownloadRequest req;
        req.url = QString("https://portal.opentopography.org/API/globaldem?demtype=%1&south=%2&north=%3&west=%4&east=%5&outputFormat=AAIGrid&API_Key=%6")
            .arg(m_datasetName)
            .arg(minLat, 0, 'f', 6).arg(maxLat, 0, 'f', 6)
            .arg(minLon, 0, 'f', 6).arg(maxLon, 0, 'f', 6)
            .arg(m_apiKey);
        req.cacheKey = QString("opentopo_%1_%2_%3_%4_%5")
            .arg(m_datasetName).arg(minLat).arg(maxLat).arg(minLon).arg(maxLon);
        req.providerName = name();
        req.datasetName = "dem";
        requests.append(req);
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QFile f(path);
        return f.exists() && f.size() > 100;
    }

    RasterGrid decodeElevation(const QByteArray& data) const override {
        terrain::DemTile dt = terrain::DemDecoder::decodeAaiGrid(data);
        RasterGrid grid;
        grid.width = dt.width;
        grid.height = dt.height;
        grid.data = std::move(dt.elevations);
        grid.nodataValue = dt.nodataValue;
        return grid;
    }

private:
    QString m_apiKey;
    QString m_datasetName;
};

// ============================================================
// GPXZProvider — High-res LiDAR via GPXZ API
// ============================================================

class GPXZProvider : public DemProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit GPXZProvider(const QString& apiKey, QObject* parent = nullptr)
        : DemProvider(parent), m_apiKey(apiKey) {}

    ProviderInfo info() const override {
        return {"gpxz", "GPXZ high-resolution LiDAR DEM",
                ProviderCapability::DEM, "1.0", "GPXZ ToS",
                true, "gpxz_api_key", "© GPXZ"};
    }
    QString name() const override { return "gpxz"; }
    bool isAvailable() const override { return !m_apiKey.isEmpty(); }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        if (m_apiKey.isEmpty()) return requests;

        DownloadRequest req;
        req.url = QString("https://data.gpxz.io/api/v1/height-raster?api_key=%1&bbox=%2,%3,%4,%5&format=geotiff&resolution=%6")
            .arg(m_apiKey)
            .arg(minLon, 0, 'f', 6).arg(minLat, 0, 'f', 6)
            .arg(maxLon, 0, 'f', 6).arg(maxLat, 0, 'f', 6)
            .arg(resolution);
        req.cacheKey = QString("gpxz_%1_%2_%3_%4_%5")
            .arg(minLat).arg(maxLat).arg(minLon).arg(maxLon).arg(resolution);
        req.providerName = "gpxz";
        req.datasetName = "dem";
        requests.append(req);
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QFile f(path);
        return f.exists() && f.size() > 1024;
    }

    RasterGrid decodeElevation(const QByteArray& data) const override {
        terrain::DemTile dt = terrain::DemDecoder::decodeGeoTiff(data);
        RasterGrid grid;
        grid.width = dt.width;
        grid.height = dt.height;
        grid.data = std::move(dt.elevations);
        grid.nodataValue = dt.nodataValue;
        return grid;
    }

private:
    QString m_apiKey;
};

// ============================================================
// GLADSrtmProvider — GLAD SRTM (free, no key)
// ============================================================

class GLADSrtmProvider : public DemProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit GLADSrtmProvider(QObject* parent = nullptr) : DemProvider(parent) {}

    ProviderInfo info() const override {
        return {"glad-srtm", "GLAD SRTM elevation",
                ProviderCapability::DEM, "SRTM", "CC BY 4.0", false, "",
                "© GLAD, University of Maryland"};
    }
    QString name() const override { return "glad-srtm"; }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        QList<DownloadRequest> requests;
        // GLAD SRTM tiles are 5° x 5°
        int minLat5 = static_cast<int>(std::floor(minLat / 5.0)) * 5;
        int maxLat5 = static_cast<int>(std::floor(maxLat / 5.0)) * 5;
        int minLon5 = static_cast<int>(std::floor(minLon / 5.0)) * 5;
        int maxLon5 = static_cast<int>(std::floor(maxLon / 5.0)) * 5;

        for (int lat = minLat5; lat <= maxLat5; lat += 5) {
            for (int lon = minLon5; lon <= maxLon5; lon += 5) {
                DownloadRequest req;
                req.url = QString("https://glad.umd.edu/dataset/known/srtm30m/%1_%2.tif")
                    .arg(lat).arg(lon);
                req.cacheKey = QString("glad_srtm_%1_%2").arg(lat).arg(lon);
                req.providerName = "glad-srtm";
                req.datasetName = "dem";
                requests.append(req);
            }
        }
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QFile f(path);
        return f.exists() && f.size() > 1024;
    }

    RasterGrid decodeElevation(const QByteArray& data) const override {
        terrain::DemTile dt = terrain::DemDecoder::decodeGeoTiff(data);
        RasterGrid grid;
        grid.width = dt.width;
        grid.height = dt.height;
        grid.data = std::move(dt.elevations);
        grid.nodataValue = dt.nodataValue;
        return grid;
    }
};

// ============================================================
// LocalDemProvider — Local DEM file
// ============================================================

class LocalDemProvider : public DemProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit LocalDemProvider(const QString& filePath, QObject* parent = nullptr)
        : DemProvider(parent), m_filePath(filePath) {}

    ProviderInfo info() const override {
        return {"local-file", "Local DEM file",
                ProviderCapability::DEM, "1.0", "User", false, "", ""};
    }
    QString name() const override { return "local-file"; }
    bool isAvailable() const override { return QFile::exists(m_filePath); }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int resolution) const override {
        Q_UNUSED(minLat) Q_UNUSED(maxLat) Q_UNUSED(minLon) Q_UNUSED(maxLon)
        Q_UNUSED(resolution)
        QList<DownloadRequest> requests;
        DownloadRequest req;
        req.url = "file://" + m_filePath;
        req.cacheKey = "local_dem_" + m_filePath;
        req.providerName = "local-file";
        req.datasetName = "dem";
        req.outputPath = m_filePath;
        requests.append(req);
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        return QFile::exists(path);
    }

    RasterGrid decodeElevation(const QByteArray& data) const override {
        // Try GeoTIFF first, then AAIGrid
        terrain::DemTile dt = terrain::DemDecoder::decodeGeoTiff(data);
        if (!dt.valid) dt = terrain::DemDecoder::decodeAaiGrid(data);
        RasterGrid grid;
        grid.width = dt.width;
        grid.height = dt.height;
        grid.data = std::move(dt.elevations);
        grid.nodataValue = dt.nodataValue;
        return grid;
    }

private:
    QString m_filePath;
};

} // namespace terrain_pipeline
