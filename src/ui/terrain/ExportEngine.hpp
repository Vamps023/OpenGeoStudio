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
#include "DemDecoder.hpp"
#include "../../core/logger/Logger.hpp"

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QMap>
#include <QImage>
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
    void writeMergedOutputs(const QString& dir);
    void processNextTile();

    // ─── Slippy-tile mosaic download ───
    // Tiled sources (AWS Terrarium, Mapbox, Google, ArcGIS, ...) must cover
    // the full tile bounds, not just the slippy tile containing the center.
    // All sub-tiles covering the bounds are fetched and stitched, then sampled
    // to the exact geo-referenced output extent.
    struct MosaicState {
        bool active = false;
        bool isDem = false;
        int zoom = 0;
        int x0 = 0, y0 = 0, nx = 0, ny = 0;  // slippy tile range
        int done = 0;                          // finished sub-requests
        int failed = 0;                        // failed sub-requests
        QMap<qint64, QByteArray> blobs;        // key = iy * nx + ix
        QString outputPath;
        terrain::Tile tile;
    };
    MosaicState m_demMosaic;
    MosaicState m_imgMosaic;

    // ─── Copernicus multi-cell fetch ───
    // Copernicus GLO-30 comes as 1°x1° COG cells. A tile that straddles a
    // cell boundary (e.g. lat 17.999..18.006) needs BOTH cells; sampling
    // only one clamps the other side of the tile to the cell's edge row.
    struct CopernicusFetch {
        bool active = false;
        terrain::Tile tile;
        QString outputPath;
        int latFrom = 0, latTo = 0;   // inclusive cell lat indices (SW corner)
        int lonFrom = 0, lonTo = 0;   // inclusive cell lon indices
        int pending = 0;
    };
    CopernicusFetch m_copFetch;
    // Decoded cells reused across tiles of one export (each cell is ~52 MB)
    QMap<QString, terrain::DemTile> m_copCellCache;

    void startCopernicusDownload(const terrain::Tile& tile, const QString& outputPath);
    void fetchCopernicusCell(int cellLat, int cellLon);
    void finishCopernicusDownload();
    static QString copernicusCellName(int cellLat, int cellLon);

    void startDemMosaic(const terrain::Tile& tile, const QString& outputPath);
    void startImageryMosaic(const terrain::Tile& tile, const QString& outputPath);
    void finishDemMosaic();
    void finishImageryMosaic();
    void fetchMosaicSubTile(MosaicState& mosaic, int ix, int iy, const QUrl& url,
                            const QMap<QByteArray, QByteArray>& headers);
    static int autoZoomForBounds(const terrain::GeoBounds& b, int targetRes, int maxZoom);
    static void slippyRangeForBounds(const terrain::GeoBounds& b, int zoom,
                                     int& x0, int& y0, int& nx, int& ny);

    // QGIS-style DEM output using RasterWriter
    bool writeDemOutput(const std::vector<float>& elevations,
                        int width, int height,
                        const terrain::Tile& tile,
                        const QString& outputPath);

    // QGIS-style imagery output using RasterWriter
    bool writeImageryOutput(const QImage& img,
                            const terrain::Tile& tile,
                            const QString& outputPath);

    // Build RasterExtent from tile bounds and CRS settings
    terrain::RasterExtent buildRasterExtent(const terrain::Tile& tile) const;


    TerrainStore* m_store;
    QNetworkAccessManager* m_network;

    QString m_exportDir;
    QList<terrain::Tile> m_pendingTiles;
    int m_totalTiles = 0;
    int m_completedTiles = 0;
    bool m_demDownloaded = false;
    bool m_imageryDownloaded = false;
    terrain::Tile m_currentTile;
    QMap<QString, std::vector<float>> m_tileDemData;
    QMap<QString, QImage> m_tileAlbedoData;
    Logger m_log{"ExportEngine"};

    // URL for one slippy sub-tile of a tiled source (z/x/y); bbox-based
    // providers ignore z/x/y and return their area URL for the tile.
    QString demTileUrl(const terrain::Tile& tile, int z, int x, int y) const;
    QString imageryTileUrl(int z, int x, int y, const terrain::Tile& tile) const;
};
