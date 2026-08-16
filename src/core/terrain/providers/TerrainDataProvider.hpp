#pragma once

// ============================================================
// TerrainDataProvider — Abstract base class for terrain data providers
// ============================================================
//
// Provider abstraction for DEM, imagery, land-cover, water, road,
// and building data. Each provider type implements discovery,
// download, and validation for its specific data source.
//
// Architecture:
//   TerrainDataProvider (abstract)
//     ├── DemProvider (elevation data)
//     ├── ImageryProvider (satellite imagery)
//     ├── LandCoverProvider (land cover classification)
//     ├── WaterProvider (water bodies)
//     ├── RoadProvider (road vectors)
//     └── BuildingProvider (building footprints)
//

#include "../TerrainPipelineTypes.hpp"
#include <QString>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QImage>
#include <functional>

namespace terrain_pipeline {

// ============================================================
// ProviderCapability — What a provider can deliver
// ============================================================

enum class ProviderCapability {
    DEM,
    Imagery,
    LandCover,
    Water,
    Roads,
    Buildings,
};

// ============================================================
// DownloadRequest — A single download request
// ============================================================

struct DownloadRequest {
    QString url;
    QString cacheKey;       // deterministic cache key
    QString outputPath;     // where to save the file
    QString providerName;
    QString datasetName;
    QMap<QString, QString> headers;
    int timeoutMs = 30000;
    int maxRetries = 3;
};

// ============================================================
// DownloadResult — Result of a download
// ============================================================

struct DownloadResult {
    bool success = false;
    QString cacheKey;
    QString outputPath;
    QByteArray data;
    QString error;
    int httpStatus = 0;
    bool fromCache = false;
};

// ============================================================
// ProviderInfo — Metadata about a provider
// ============================================================

struct ProviderInfo {
    QString name;
    QString description;
    ProviderCapability capability;
    QString version;
    QString license;
    bool requiresApiKey = false;
    QString apiKeyName;
    QString attribution;
};

// ============================================================
// TerrainDataProvider — Abstract base
// ============================================================

class TerrainDataProvider : public QObject {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit TerrainDataProvider(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~TerrainDataProvider() = default;

    // Provider identification
    virtual ProviderInfo info() const = 0;
    virtual QString name() const = 0;

    // Discover required tiles/downloads for the given area
    virtual QList<DownloadRequest> discoverTiles(
        double minLat, double maxLat,
        double minLon, double maxLon,
        int resolution) const = 0;

    // Validate a downloaded file
    virtual bool validateDownload(const QString& path) const = 0;

    // Process downloaded data into a raster grid
    // Default implementation returns an invalid grid
    virtual RasterGrid processDEM(const QList<QString>& downloadedPaths,
                                   double minLat, double maxLat,
                                   double minLon, double maxLon,
                                   int targetWidth, int targetHeight) const {
        Q_UNUSED(downloadedPaths) Q_UNUSED(minLat) Q_UNUSED(maxLat)
        Q_UNUSED(minLon) Q_UNUSED(maxLon)
        Q_UNUSED(targetWidth) Q_UNUSED(targetHeight)
        return RasterGrid();
    }

    // Process downloaded imagery into a QImage
    virtual QImage processImagery(const QList<QString>& downloadedPaths,
                                   int targetWidth, int targetHeight) const {
        Q_UNUSED(downloadedPaths) Q_UNUSED(targetWidth) Q_UNUSED(targetHeight)
        return QImage();
    }

    // Check if API key is required and available
    virtual bool isAvailable() const { return true; }

    // Get cache key prefix for this provider
    virtual QString cachePrefix() const { return name(); }

signals:
    void downloadProgress(int completed, int total);
    void stageMessage(const QString& msg);
};

// ============================================================
// DemProvider — Abstract base for DEM providers
// ============================================================

class DemProvider : public TerrainDataProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit DemProvider(QObject* parent = nullptr) : TerrainDataProvider(parent) {}

    ProviderCapability capability() const { return ProviderCapability::DEM; }

    // DEM-specific: decode elevation from downloaded data
    virtual RasterGrid decodeElevation(const QByteArray& data) const = 0;

    // Get the elevation value range for this provider
    virtual float minElevation() const { return -500.0f; }
    virtual float maxElevation() const { return 9000.0f; }
};

// ============================================================
// ImageryProvider — Abstract base for imagery providers
// ============================================================

class ImageryProvider : public TerrainDataProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit ImageryProvider(QObject* parent = nullptr) : TerrainDataProvider(parent) {}

    ProviderCapability capability() const { return ProviderCapability::Imagery; }

    // Imagery-specific: get tile URL for XYZ tile scheme
    virtual QString tileUrl(int z, int x, int y) const = 0;

    // Get optimal zoom level for the given area
    virtual int optimalZoomLevel(double latSpan, double lonSpan, int targetPixels) const {
        // Calculate zoom level that gives closest to targetPixels
        for (int z = 19; z >= 0; z--) {
            double tileDegrees = 360.0 / (1 << z);
            int tilesNeeded = static_cast<int>(std::ceil(latSpan / tileDegrees));
            int pixels = tilesNeeded * 256;
            if (pixels >= targetPixels) return z;
        }
        return 0;
    }
};

// ============================================================
// LandCoverProvider — Abstract base for land-cover providers
// ============================================================

class LandCoverProvider : public TerrainDataProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit LandCoverProvider(QObject* parent = nullptr) : TerrainDataProvider(parent) {}

    ProviderCapability capability() const { return ProviderCapability::LandCover; }

    // Land-cover-specific: get class definitions
    struct LandCoverClass {
        int id;
        QString name;
        int colorR, colorG, colorB;
    };

    virtual QList<LandCoverClass> classes() const = 0;

    // Convert land-cover classes to masks using configurable mapping
    virtual ByteRaster classesToMask(const RasterGrid& classes,
                                      const QList<int>& targetClassIds) const {
        ByteRaster mask;
        mask.width = classes.width;
        mask.height = classes.height;
        mask.data.resize(mask.width * mask.height);
        mask.nodataValue = 0;
        mask.originX = classes.originX;
        mask.originY = classes.originY;
        mask.pixelSizeX = classes.pixelSizeX;
        mask.pixelSizeY = classes.pixelSizeY;
        mask.crs = classes.crs;

        for (int i = 0; i < mask.width * mask.height; i++) {
            float classVal = classes.data[i];
            bool match = false;
            for (int targetId : targetClassIds) {
                if (static_cast<int>(classVal) == targetId) { match = true; break; }
            }
            mask.data[i] = match ? 255 : 0;
        }
        return mask;
    }
};

// ============================================================
// WaterProvider — Abstract base for water data providers
// ============================================================

class WaterProvider : public TerrainDataProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit WaterProvider(QObject* parent = nullptr) : TerrainDataProvider(parent) {}

    ProviderCapability capability() const { return ProviderCapability::Water; }

    // Water-specific: get water bodies as vector geometries
    struct WaterBody {
        QString type;  // "lake", "river", "reservoir", "coastline"
        QList<QPair<double, double>> coordinates;  // lon, lat pairs
    };

    virtual QList<WaterBody> getWaterBodies(double minLat, double maxLat,
                                             double minLon, double maxLon) const = 0;
};

// ============================================================
// RoadProvider — Abstract base for road data providers
// ============================================================

class RoadProvider : public TerrainDataProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit RoadProvider(QObject* parent = nullptr) : TerrainDataProvider(parent) {}

    ProviderCapability capability() const { return ProviderCapability::Roads; }

    // Road-specific: get road geometries
    struct RoadSegment {
        QString type;  // "motorway", "primary", "secondary", "residential", etc.
        QString name;
        QList<QPair<double, double>> coordinates;  // lon, lat pairs
    };

    virtual QList<RoadSegment> getRoads(double minLat, double maxLat,
                                         double minLon, double maxLon) const = 0;
};

// ============================================================
// BuildingProvider — Abstract base for building data providers
// ============================================================

class BuildingProvider : public TerrainDataProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit BuildingProvider(QObject* parent = nullptr) : TerrainDataProvider(parent) {}

    ProviderCapability capability() const { return ProviderCapability::Buildings; }

    // Building-specific: get building footprints
    struct BuildingFootprint {
        QList<QPair<double, double>> coordinates;  // lon, lat polygon
        float height = 0.0f;  // meters (0 if unknown)
        QString name;
    };

    virtual QList<BuildingFootprint> getBuildings(double minLat, double maxLat,
                                                    double minLon, double maxLon) const = 0;
};

} // namespace terrain_pipeline
