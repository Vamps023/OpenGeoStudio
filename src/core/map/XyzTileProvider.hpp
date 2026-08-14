#pragma once

// ============================================================
// XyzTileProvider — XYZ tile URL template + download
// Inspired by QGIS formatXYZUrlTemplate() and QgsWmsProvider XYZ mode
// ============================================================

#include "TileMatrix.hpp"
#include "TileCache.hpp"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QImage>
#include <QObject>
#include <QUrl>
#include <functional>

namespace map {

class XyzTileProvider : public QObject {
public:
    // URL template supports {x}, {y}, {-y} (TMS), {z} placeholders
    // Example: "https://mt1.google.com/vt/lyrs=s&x={x}&y={y}&z={z}"
    explicit XyzTileProvider(const QString& urlTemplate, QObject* parent = nullptr)
        : QObject(parent), m_urlTemplate(urlTemplate)
    {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setTransferTimeout(15000);  // 15 second timeout
    }

    // Format URL for a specific tile
    QString urlForTile(const TileXYZ& tile, int matrixHeight = 0) const {
        QString url = m_urlTemplate;
        url.replace("{x}", QString::number(tile.x));
        if (url.contains("{-y}")) {
            // TMS convention: {-y} = matrixHeight - y - 1
            int mh = matrixHeight > 0 ? matrixHeight : (1 << tile.z);
            url.replace("{-y}", QString::number(mh - tile.y - 1));
        } else {
            url.replace("{y}", QString::number(tile.y));
        }
        url.replace("{z}", QString::number(tile.z));
        return url;
    }

    // Request a tile. Callback is called with the image (or null on failure).
    void requestTile(const TileXYZ& tile,
                     std::function<void(const TileXYZ&, const QImage&)> callback) {
        QString urlStr = urlForTile(tile);
        QUrl url(urlStr);
        QImage cached;

        // Check cache first
        if (TileCache::instance().tile(url, cached)) {
            callback(tile, cached);
            return;
        }

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio/1.0");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);

        auto* reply = m_nam->get(request);

        connect(reply, &QNetworkReply::finished, this, [this, tile, url, reply, callback]() {
            QImage image;
            if (reply->error() == QNetworkReply::NoError) {
                if (image.loadFromData(reply->readAll())) {
                    image = image.convertToFormat(QImage::Format_RGB32);
                    TileCache::instance().insertTile(url, image);
                }
            }
            callback(tile, image);
            reply->deleteLater();
        });

        // Track pending requests
        m_pending[reply] = tile;
    }

    // Request multiple tiles for a range
    void requestTiles(const TileRange& range, int zoom,
                      std::function<void(const TileXYZ&, const QImage&)> callback) {
        for (int y = range.startRow; y <= range.endRow; ++y) {
            for (int x = range.startCol; x <= range.endCol; ++x) {
                requestTile(TileXYZ(x, y, zoom), callback);
            }
        }
    }

    int pendingCount() const { return m_pending.size(); }

    QString urlTemplate() const { return m_urlTemplate; }

private:
    QString m_urlTemplate;
    QNetworkAccessManager* m_nam = nullptr;
    QMap<QNetworkReply*, TileXYZ> m_pending;
};

} // namespace map
