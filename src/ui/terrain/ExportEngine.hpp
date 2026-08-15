#pragma once

// ============================================================
// ExportEngine — DEM and imagery download + file writing
// ============================================================
//
// Replaces modules/export/shared/exportEngine.ts.
// Uses Qt Network (QNetworkAccessManager) for HTTP downloads.
// Uses Qt QImage for image processing.
// Writes PNG heightmaps and albedo images.
//

#include "TerrainStore.hpp"
#include "RasterWriter.hpp"
#include "../../core/logger/Logger.hpp"

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <vector>

class ExportEngine : public QObject {
    Q_OBJECT

public:
    explicit ExportEngine(TerrainStore* store, QObject* parent = nullptr);

    void exportToDirectory(const QString& dir);

signals:
    void progress(int percent, const QString& stage);
    void finished(bool success, const QString& message);

private:
    void downloadDemForTile(const terrain::Tile& tile, const QString& outputPath);
    void downloadImageryForTile(const terrain::Tile& tile, const QString& outputPath);
    void loadLocalDemForTile(const terrain::Tile& tile, const QString& outputPath);
    void loadLocalImageryForTile(const terrain::Tile& tile, const QString& outputPath);
    void writeManifest(const QString& dir);
    void writeGeoTiff(const QString& path, const QImage& heightmap,
                      const terrain::GeoBounds& bounds);
    void processNextTile();

    // QGIS-style DEM output using RasterWriter
    void writeDemOutput(const std::vector<float>& elevations,
                        int width, int height,
                        const terrain::Tile& tile,
                        const QString& outputPath);

    // QGIS-style imagery output using RasterWriter
    void writeImageryOutput(const QImage& img,
                            const terrain::Tile& tile,
                            const QString& outputPath);

    // Build RasterExtent from tile bounds and CRS settings
    terrain::RasterExtent buildRasterExtent(const terrain::Tile& tile) const;

    // GeoTIFF writing using libtiff (legacy, kept for compatibility)
    bool writeGeoTiffHeightmap(const QString& path, const QImage& img,
                               double north, double south, double east, double west);
    bool writeGeoTiffRgb(const QString& path, const QImage& img,
                         double north, double south, double east, double west);
    bool writeR16Heightmap(const QString& path, const QImage& img);

    TerrainStore* m_store;
    QNetworkAccessManager* m_network;

    QString m_exportDir;
    QList<terrain::Tile> m_pendingTiles;
    int m_totalTiles = 0;
    int m_completedTiles = 0;
    bool m_demDownloaded = false;
    bool m_imageryDownloaded = false;
    terrain::Tile m_currentTile;
    Logger m_log{"ExportEngine"};

    QString demUrlForTile(const terrain::Tile& tile) const;
    QString imageryTileUrl(int z, int x, int y, const terrain::Tile& tile) const;
};
