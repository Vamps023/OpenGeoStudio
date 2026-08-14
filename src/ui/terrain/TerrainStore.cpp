// TerrainStore — State management implementation

#include "TerrainStore.hpp"
#include <cmath>

TerrainStore::TerrainStore(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus), m_log("TerrainStore") {
}

void TerrainStore::setBounds(const terrain::GeoBounds& bounds) {
    m_bounds = bounds;
    // Note: do NOT call makeSquare() — the Electron version doesn't modify
    // bounds after drag selection. The screen-square constraint already
    // produces approximately square geographic bounds.
    computeTileGrid();
    emit boundsChanged(m_bounds);
}

void TerrainStore::clearBounds() {
    m_bounds = {};
    m_tileGrid = {};
    m_selectedTiles.clear();
    emit boundsChanged(m_bounds);
    emit tileGridChanged(m_tileGrid);
    emit tileSelectionChanged();
}

void TerrainStore::setTileSizeKm(double km) {
    // Valid tile sizes: 1, 2, 4, 8, 16 km
    if (km != 1 && km != 2 && km != 4 && km != 8 && km != 16) km = 2;
    m_tileSizeKm = km;
    computeTileGrid();
    emit tileGridChanged(m_tileGrid);
}

void TerrainStore::setZoomLocked(bool locked) { m_zoomLocked = locked; }
void TerrainStore::setSelecting(bool selecting) {
    m_isSelecting = selecting;
    emit selectingChanged(selecting);
}

void TerrainStore::toggleTile(const QString& tileId) {
    if (m_selectedTiles.contains(tileId)) {
        m_selectedTiles.remove(tileId);
    } else {
        m_selectedTiles.insert(tileId);
    }
    emit tileSelectionChanged();
}

void TerrainStore::selectAllTiles() {
    m_selectedTiles.clear();
    for (const auto& tile : m_tileGrid.tiles) {
        m_selectedTiles.insert(tile.id());
    }
    emit tileSelectionChanged();
}

void TerrainStore::clearTileSelection() {
    m_selectedTiles.clear();
    emit tileSelectionChanged();
}

void TerrainStore::setExportSettings(const terrain::ExportSettings& settings) {
    m_exportSettings = settings;
    emit exportSettingsChanged();
}

void TerrainStore::setHeightmapFormat(terrain::HeightmapFormat fmt) {
    m_exportSettings.heightmapFormat = fmt;
    emit exportSettingsChanged();
}

void TerrainStore::setAlbedoFormat(terrain::AlbedoFormat fmt) {
    m_exportSettings.albedoFormat = fmt;
    emit exportSettingsChanged();
}

void TerrainStore::setDemSource(terrain::DemSource src) {
    m_exportSettings.demSource = src;
    emit exportSettingsChanged();
}

void TerrainStore::setImagerySource(terrain::ImagerySource src) {
    m_exportSettings.imagerySource = src;
    emit exportSettingsChanged();
}

void TerrainStore::setCrsSource(terrain::CrsSource src) {
    m_exportSettings.crsSource = src;
    emit exportSettingsChanged();
}

void TerrainStore::setOpenTopoApiKey(const QString& key) {
    m_exportSettings.openTopoApiKey = key;
}

void TerrainStore::setMapboxToken(const QString& key) {
    m_exportSettings.mapboxToken = key;
}

void TerrainStore::setMaptilerToken(const QString& key) {
    m_exportSettings.maptilerToken = key;
}

void TerrainStore::setGpxzApiKey(const QString& key) {
    m_exportSettings.gpxzApiKey = key;
}

void TerrainStore::setStadiaApiKey(const QString& key) {
    m_exportSettings.stadiaApiKey = key;
}

void TerrainStore::setImageryZoomLevel(int level) {
    m_exportSettings.imageryZoomLevel = level;
    emit exportSettingsChanged();
}

void TerrainStore::setGladArdInterval(int interval) {
    m_exportSettings.gladArdInterval = interval;
    emit exportSettingsChanged();
}

void TerrainStore::setLocalDemFilePath(const QString& path) {
    m_exportSettings.localDemFilePath = path;
    emit exportSettingsChanged();
}

void TerrainStore::setLocalImageryFilePath(const QString& path) {
    m_exportSettings.localImageryFilePath = path;
    emit exportSettingsChanged();
}

void TerrainStore::computeTileGrid() {
    m_tileGrid.tiles.clear();
    m_selectedTiles.clear();

    if (!m_bounds.isValid()) {
        m_tileGrid.rows = 0;
        m_tileGrid.cols = 0;
        return;
    }

    // Convert tile size from km to degrees (approximate at mid-latitude)
    const double midLat = (m_bounds.north + m_bounds.south) / 2.0;
    const double kmPerDegLat = 111.32;
    const double kmPerDegLon = 111.32 * std::cos(midLat * M_PI / 180.0);
    const double tileSizeDegLat = m_tileSizeKm / kmPerDegLat;
    const double tileSizeDegLon = m_tileSizeKm / kmPerDegLon;

    // Compute required rows/cols, then force square grid (matching Electron)
    const int requiredRows = static_cast<int>(std::ceil(m_bounds.heightDeg() / tileSizeDegLat));
    const int requiredCols = static_cast<int>(std::ceil(m_bounds.widthDeg() / tileSizeDegLon));
    const int tilesNeeded = std::max(requiredRows, requiredCols);
    const int rows = tilesNeeded;
    const int cols = tilesNeeded;

    m_tileGrid.rows = rows;
    m_tileGrid.cols = cols;
    m_tileGrid.tileSizeDeg = tileSizeDegLat;

    // Actual tile dimensions in degrees (divides bounds evenly)
    const double actualTileWidth = m_bounds.widthDeg() / cols;
    const double actualTileHeight = m_bounds.heightDeg() / rows;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            terrain::Tile tile;
            tile.row = r;
            tile.col = c;
            // Tiles grow south-to-north (row 0 at south, matching Electron)
            tile.bounds.south = m_bounds.south + r * actualTileHeight;
            tile.bounds.north = (r == rows - 1) ? m_bounds.north : m_bounds.south + (r + 1) * actualTileHeight;
            tile.bounds.west = m_bounds.west + c * actualTileWidth;
            tile.bounds.east = (c == cols - 1) ? m_bounds.east : m_bounds.west + (c + 1) * actualTileWidth;
            m_tileGrid.tiles.append(tile);
        }
    }

    emit tileGridChanged(m_tileGrid);
    emit tileSelectionChanged();
}

QJsonObject TerrainStore::toJson() const {
    QJsonObject j;
    j["bounds"] = m_bounds.toJson();
    j["tileSizeKm"] = m_tileSizeKm;
    j["zoomLocked"] = m_zoomLocked;

    QJsonArray tiles;
    for (const auto& t : m_tileGrid.tiles) {
        if (m_selectedTiles.contains(t.id())) {
            tiles.append(t.id());
        }
    }
    j["selectedTiles"] = tiles;

    QJsonObject exp;
    exp["heightmapFormat"] = m_exportSettings.heightmapFormatStr();
    exp["demSource"] = m_exportSettings.demSourceStr();
    exp["heightmapResolution"] = m_exportSettings.heightmapResolution;
    exp["albedoResolution"] = m_exportSettings.albedoResolution;
    exp["openTopoApiKey"] = m_exportSettings.openTopoApiKey;
    j["exportSettings"] = exp;

    return j;
}

void TerrainStore::fromJson(const QJsonObject& j) {
    if (j.contains("bounds")) {
        m_bounds = terrain::GeoBounds::fromJson(j["bounds"].toObject());
    }
    m_tileSizeKm = j["tileSizeKm"].toDouble(2.0);
    m_zoomLocked = j["zoomLocked"].toBool(false);

    if (j.contains("selectedTiles")) {
        const QJsonArray tiles = j["selectedTiles"].toArray();
        for (const auto& v : tiles) {
            m_selectedTiles.insert(v.toString());
        }
    }

    if (j.contains("exportSettings")) {
        const QJsonObject exp = j["exportSettings"].toObject();
        m_exportSettings.openTopoApiKey = exp["openTopoApiKey"].toString();
        m_exportSettings.heightmapResolution = exp["heightmapResolution"].toInt(1024);
        m_exportSettings.albedoResolution = exp["albedoResolution"].toInt(1024);
    }

    if (m_bounds.isValid()) {
        computeTileGrid();
    }
    emit boundsChanged(m_bounds);
    emit exportSettingsChanged();
}
