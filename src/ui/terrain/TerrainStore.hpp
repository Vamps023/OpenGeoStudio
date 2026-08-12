#pragma once

// ============================================================
// TerrainStore — Terrain area selection and export state
// ============================================================
//
// Replaces modules/terrain/client/store.ts (Zustand).
// Manages selected bounds, tile grid, export settings.
//

#include "TerrainTypes.hpp"
#include "../../core/events/EventBus.hpp"
#include "../../core/logger/Logger.hpp"

#include <QObject>
#include <QSet>

class TerrainStore : public QObject {
    Q_OBJECT

public:
    explicit TerrainStore(EventBus* bus, QObject* parent = nullptr);

    const terrain::GeoBounds& selectedBounds() const { return m_bounds; }
    const terrain::TileGrid& tileGrid() const { return m_tileGrid; }
    const QSet<QString>& selectedTiles() const { return m_selectedTiles; }
    double tileSizeKm() const { return m_tileSizeKm; }
    const terrain::ExportSettings& exportSettings() const { return m_exportSettings; }
    bool isSelecting() const { return m_isSelecting; }
    bool zoomLocked() const { return m_zoomLocked; }

    // Actions
    void setBounds(const terrain::GeoBounds& bounds);
    void clearBounds();
    void setTileSizeKm(double km);
    void setZoomLocked(bool locked);
    void setSelecting(bool selecting);

    void toggleTile(const QString& tileId);
    void selectAllTiles();
    void clearTileSelection();

    void setExportSettings(const terrain::ExportSettings& settings);
    void setHeightmapFormat(terrain::HeightmapFormat fmt);
    void setAlbedoFormat(terrain::AlbedoFormat fmt);
    void setDemSource(terrain::DemSource src);
    void setImagerySource(terrain::ImagerySource src);
    void setOpenTopoApiKey(const QString& key);
    void setMapboxToken(const QString& key);

    // Compute tile grid from bounds
    void computeTileGrid();

    // Serialize for project persistence
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& j);

signals:
    void boundsChanged(const terrain::GeoBounds& bounds);
    void tileGridChanged(const terrain::TileGrid& grid);
    void tileSelectionChanged();
    void exportSettingsChanged();
    void selectingChanged(bool selecting);

private:
    EventBus* m_bus;
    Logger m_log;

    terrain::GeoBounds m_bounds;
    terrain::TileGrid m_tileGrid;
    QSet<QString> m_selectedTiles;
    double m_tileSizeKm = 2.0;
    terrain::ExportSettings m_exportSettings;
    bool m_isSelecting = false;
    bool m_zoomLocked = false;
};
