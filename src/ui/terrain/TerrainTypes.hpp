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

enum class HeightmapFormat { PNG16, R16, GeoTIFF_Int16, GeoTIFF_UInt16, GeoTIFF_Float32 };
enum class AlbedoFormat { PNG, GeoTIFF_RGB };
enum class DemSource { OpenTopo_SRTM_GL1, OpenTopo_SRTM_GL3, OpenTopo_ALOS_AW3D30,
                       OpenTopo_Copernicus_GLO30, OpenTopo_NASADEM, GLAD_SRTM };
enum class ImagerySource { ArcGIS_World_Imagery, Google_Satellite, Mapbox_Satellite };
enum class CrsSource { WGS84, UTM, WebMercator };

struct ExportSettings {
    HeightmapFormat heightmapFormat = HeightmapFormat::PNG16;
    AlbedoFormat albedoFormat = AlbedoFormat::PNG;
    DemSource demSource = DemSource::OpenTopo_SRTM_GL1;
    ImagerySource imagerySource = ImagerySource::ArcGIS_World_Imagery;
    CrsSource crsSource = CrsSource::WGS84;
    int heightmapResolution = 1024;
    int albedoResolution = 1024;
    bool compressDeflate = false;
    QString openTopoApiKey;
    QString mapboxToken;

    QString heightmapFormatStr() const {
        switch (heightmapFormat) {
        case HeightmapFormat::PNG16: return "png16";
        case HeightmapFormat::R16: return "r16";
        case HeightmapFormat::GeoTIFF_Int16: return "geotiff_int16";
        case HeightmapFormat::GeoTIFF_UInt16: return "geotiff_uint16";
        case HeightmapFormat::GeoTIFF_Float32: return "geotiff_float32";
        }
        return "png16";
    }

    QString demSourceStr() const {
        switch (demSource) {
        case DemSource::OpenTopo_SRTM_GL1: return "opentopo_srtm_gl1";
        case DemSource::OpenTopo_SRTM_GL3: return "opentopo_srtm_gl3";
        case DemSource::OpenTopo_ALOS_AW3D30: return "opentopo_alos_aw3d30";
        case DemSource::OpenTopo_Copernicus_GLO30: return "opentopo_copernicus_glo30";
        case DemSource::OpenTopo_NASADEM: return "opentopo_nasadem";
        case DemSource::GLAD_SRTM: return "glad_srtm";
        }
        return "opentopo_srtm_gl1";
    }
};

} // namespace terrain
