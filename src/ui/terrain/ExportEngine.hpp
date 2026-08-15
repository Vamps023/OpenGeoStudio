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
#include "../../core/logger/Logger.hpp"

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

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

    // GeoTIFF writing using libtiff
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
