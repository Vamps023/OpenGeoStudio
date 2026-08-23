#pragma once

// ============================================================
// TerrainTypes — Terrain data model types
// ============================================================
//
// Mirrors modules/terrain/shared/types.ts.
// GeoBounds, TileGrid, export settings.
//

#include <QString>
#include <QList>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>

namespace terrain {

struct GeoBounds {
    double north = 0, south = 0, east = 0, west = 0;

    bool isValid() const { return north != south && east != west; }
    double widthDeg() const { return east - west; }
    double heightDeg() const { return north - south; }

    // Make the bounds square (1:1 aspect ratio) centered on the same point
    void makeSquare() {
        const double w = widthDeg();
        const double h = heightDeg();
        const double size = std::max(w, h);
        const double cx = (east + west) / 2.0;
        const double cy = (north + south) / 2.0;
        north = cy + size / 2.0;
        south = cy - size / 2.0;
        east = cx + size / 2.0;
        west = cx - size / 2.0;
    }

    QJsonObject toJson() const {
        return {{"north", north}, {"south", south}, {"east", east}, {"west", west}};
    }
    static GeoBounds fromJson(const QJsonObject& j) {
        return {j["north"].toDouble(), j["south"].toDouble(),
                j["east"].toDouble(), j["west"].toDouble()};
    }
};

struct Tile {
    int row = 0, col = 0;
    GeoBounds bounds;
    QString id() const { return QString("%1,%2").arg(row).arg(col); }
};

struct TileGrid {
    QList<Tile> tiles;
    int rows = 0, cols = 0;
    double tileSizeDeg = 0;
};

// Full enum set matching the Electron version
enum class HeightmapFormat {
    None,               // Albedo only
    PNG16,              // PNG 16-bit grayscale
    R16,                // Raw 16-bit
    GeoTIFF_Int16,      // DEM (Int16 GeoTIFF)
    GeoTIFF_UInt16,     // UInt16 GeoTIFF (normalized)
    GeoTIFF_Float32     // Float32 GeoTIFF (full precision)
};

enum class AlbedoFormat { PNG, GeoTIFF_RGB };

enum class DemSource {
    // Tiled (no API key)
    AWS_Terrarium,
    Mapzen_Terrarium,
    Mapbox_TerrainRGB,
    // Copernicus (free, no key)
    NASA_EarthData_Copernicus,
    // OpenTopography (free API key)
    OpenTopo_Copernicus_GLO30,
    OpenTopo_NASADEM,
    OpenTopo_SRTM_GL1,
    OpenTopo_SRTM_GL3,
    OpenTopo_ALOS_AW3D30,
    OpenTopo_USGS_3DEP,
    // GPXZ (high-res, LiDAR)
    GPXZ_LiDAR,
    // GLAD (free, no key)
    GLAD_SRTM,
    // Local file
    Local_File
};

enum class ImagerySource {
    Google_Satellite,
    ArcGIS_World_Imagery,
    Mapbox_Satellite,
    MapTiler_Satellite,
    GLAD_ARD_Landsat,
    Local_File
};

enum class CrsSource {
    Project_CRS,    // Use the project's CRS (e.g. EPSG:32643)
    EPSG_4326,      // WGS84 (lat/lon)
    EPSG_3857,      // Web Mercator
    EPSG_32633,     // UTM Zone 33N
    EPSG_32634,     // UTM Zone 34N
    EPSG_32635,     // UTM Zone 35N
    EPSG_25832,     // ETRS89 UTM Zone 32N
    EPSG_25833,     // ETRS89 UTM Zone 33N
    Auto_UTM        // Auto-detect UTM from bounds centroid
};

struct ExportSettings {
    HeightmapFormat heightmapFormat = HeightmapFormat::GeoTIFF_Float32;
    AlbedoFormat albedoFormat = AlbedoFormat::PNG;
    DemSource demSource = DemSource::AWS_Terrarium; // GeoTerrain default — smooth SRTM-based tiles
    ImagerySource imagerySource = ImagerySource::Google_Satellite;
    CrsSource crsSource = CrsSource::Project_CRS; // Default to project CRS
    int projectCrsEpsg = 0;  // EPSG code from project (0 = not set, fall back to WGS84)
    int heightmapResolution = 1024;
    int albedoResolution = 1024;
    int imageryZoomLevel = 0;       // 0 = auto
    int gladArdInterval = 920;      // GLAD ARD 16-day interval
    bool compressDeflate = false;

    // API keys
    QString openTopoApiKey;
    QString mapboxToken;
    QString maptilerToken;
    QString gpxzApiKey;  // Loaded from env var GPXZ_API_KEY or user settings
    QString stadiaApiKey;

    // Local file paths
    QString localDemFilePath;
    QString localImageryFilePath;

    QString heightmapFormatStr() const {
        switch (heightmapFormat) {
        case HeightmapFormat::None:            return "none";
        case HeightmapFormat::PNG16:           return "png16";
        case HeightmapFormat::R16:             return "r16";
        case HeightmapFormat::GeoTIFF_Int16:   return "geotiff_int16";
        case HeightmapFormat::GeoTIFF_UInt16:  return "geotiff_uint16";
        case HeightmapFormat::GeoTIFF_Float32: return "geotiff_float32";
        }
        return "geotiff_float32";
    }

    QString albedoFormatStr() const {
        switch (albedoFormat) {
        case AlbedoFormat::PNG:         return "png";
        case AlbedoFormat::GeoTIFF_RGB: return "geotiff_rgb";
        }
        return "png";
    }

    QString demSourceStr() const {
        switch (demSource) {
        case DemSource::AWS_Terrarium:                return "aws-terrarium";
        case DemSource::Mapzen_Terrarium:             return "mapzen";
        case DemSource::Mapbox_TerrainRGB:            return "mapbox-terrain-rgb";
        case DemSource::NASA_EarthData_Copernicus:    return "nasa-earthdata";
        case DemSource::OpenTopo_Copernicus_GLO30:    return "opentopo-cop30";
        case DemSource::OpenTopo_NASADEM:             return "opentopo-nasadem";
        case DemSource::OpenTopo_SRTM_GL1:            return "opentopo-srtmgl1";
        case DemSource::OpenTopo_SRTM_GL3:            return "opentopo-srtmgl3";
        case DemSource::OpenTopo_ALOS_AW3D30:         return "opentopo-aw3d30";
        case DemSource::OpenTopo_USGS_3DEP:           return "opentopo-usgs10m";
        case DemSource::GPXZ_LiDAR:                   return "gpxz";
        case DemSource::GLAD_SRTM:                    return "glad-srtm";
        case DemSource::Local_File:                   return "local-file";
        }
        return "opentopo-cop30";
    }

    QString imagerySourceStr() const {
        switch (imagerySource) {
        case ImagerySource::Google_Satellite:        return "google";
        case ImagerySource::ArcGIS_World_Imagery:    return "arcgis";
        case ImagerySource::Mapbox_Satellite:        return "mapbox";
        case ImagerySource::MapTiler_Satellite:      return "maptiler";
        case ImagerySource::GLAD_ARD_Landsat:        return "glad-ard";
        case ImagerySource::Local_File:              return "local-file";
        }
        return "google";
    }

    QString crsSourceStr() const {
        switch (crsSource) {
        case CrsSource::EPSG_4326:   return "EPSG:4326";
        case CrsSource::EPSG_3857:   return "EPSG:3857";
        case CrsSource::EPSG_32633:  return "EPSG:32633";
        case CrsSource::EPSG_32634:  return "EPSG:32634";
        case CrsSource::EPSG_32635:  return "EPSG:32635";
        case CrsSource::EPSG_25832:  return "EPSG:25832";
        case CrsSource::EPSG_25833:  return "EPSG:25833";
        case CrsSource::Auto_UTM:    return "auto";
        }
        return "auto";
    }

    // Check if the selected DEM source needs an API key
    bool demNeedsApiKey() const {
        return demSource == DemSource::OpenTopo_Copernicus_GLO30 ||
               demSource == DemSource::OpenTopo_NASADEM ||
               demSource == DemSource::OpenTopo_SRTM_GL1 ||
               demSource == DemSource::OpenTopo_SRTM_GL3 ||
               demSource == DemSource::OpenTopo_ALOS_AW3D30 ||
               demSource == DemSource::OpenTopo_USGS_3DEP;
    }

    bool demNeedsMapboxToken() const {
        return demSource == DemSource::Mapbox_TerrainRGB;
    }

    bool demNeedsGpxzKey() const {
        return demSource == DemSource::GPXZ_LiDAR;
    }

    bool imageryNeedsMapboxToken() const {
        return imagerySource == ImagerySource::Mapbox_Satellite;
    }

    bool imageryNeedsMaptilerToken() const {
        return imagerySource == ImagerySource::MapTiler_Satellite;
    }

    bool imageryIsGladArd() const {
        return imagerySource == ImagerySource::GLAD_ARD_Landsat;
    }
};

// ─── Mask Generation ──────────────────────────────────────────
// Mirrors shared/types/terrain.ts MaskSettings

struct MaskSettings {
    bool generateRoadMask = false;
    bool generateWaterMask = false;
    bool generateVegetationMask = false;
    bool generateBuildingMask = false;
    bool generateCliffMask = false;
    int cliffThresholdDegrees = 45;  // 0-90
    int roadLineWidthPx = 3;         // 1-10
};

} // namespace terrain
