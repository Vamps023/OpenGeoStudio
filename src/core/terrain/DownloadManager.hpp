#pragma once

// ============================================================
// DownloadManager — Parallel HTTP download with retry and caching
// ============================================================

#include "TerrainPipelineTypes.hpp"
#include "CacheManager.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QEventLoop>
#include <QFile>
#include <QDir>
#include <QList>
#include <QObject>
#include <QPointer>
#include <functional>

namespace terrain_pipeline {

class DownloadManager : public QObject {
    Q_OBJECT

public:
    explicit DownloadManager(CacheManager* cache, QObject* parent = nullptr)
        : QObject(parent), m_cache(cache) {
        m_network = new QNetworkAccessManager(this);
        m_network->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    }

    // Download a single request synchronously (with event loop)
    DownloadResult downloadSync(const DownloadRequest& req) {
        DownloadResult result;
        result.cacheKey = req.cacheKey;

        // Check cache first
        if (m_cache && m_cache->exists(req.cacheKey)) {
            if (req.outputPath.isEmpty()) {
                result.data = m_cache->read(req.cacheKey);
            } else {
                result.outputPath = req.outputPath;
                // Copy from cache to output
                QFile::copy(m_cache->cachePath(req.cacheKey), req.outputPath);
            }
            result.success = true;
            result.fromCache = true;
            return result;
        }

        // Download with retries
        for (int attempt = 0; attempt < req.maxRetries; attempt++) {
            QNetworkRequest networkReq;
            networkReq.setUrl(QUrl(req.url));
            networkReq.setTransferTimeout(req.timeoutMs);
            networkReq.setHeader(QNetworkRequest::UserAgentHeader,
                                  "OpenGeoStudio/1.0");

            for (auto it = req.headers.begin(); it != req.headers.end(); ++it) {
                if (it.key() == "data") continue;
                networkReq.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
            }

            QNetworkReply* reply = nullptr;
            if (req.headers.contains("data")) {
                QByteArray postData = req.headers.value("data").toUtf8();
                reply = m_network->post(networkReq, postData);
            } else {
                reply = m_network->get(networkReq);
            }

            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            // Timeout
            QTimer::singleShot(req.timeoutMs, &loop, &QEventLoop::quit);
            loop.exec();

            if (reply->error() == QNetworkReply::NoError) {
                result.data = reply->readAll();
                result.httpStatus = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                result.success = (result.httpStatus == 200 || result.httpStatus == 302);

                if (result.success && !result.data.isEmpty()) {
                    // Save to cache
                    if (m_cache)
                        m_cache->write(req.cacheKey, result.data);
                    // Save to output path if specified
                    if (!req.outputPath.isEmpty()) {
                        QDir().mkpath(QFileInfo(req.outputPath).absolutePath());
                        QFile f(req.outputPath);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(result.data);
                            f.close();
                            result.outputPath = req.outputPath;
                        }
                    }
                    reply->deleteLater();
                    return result;
                }
            } else {
                result.error = reply->errorString();
                result.httpStatus = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
            }
            reply->deleteLater();

            // Wait before retry
            if (attempt < req.maxRetries - 1)
                QThread::msleep(1000 * (attempt + 1));
        }

        result.success = false;
        return result;
    }

    // Download multiple requests sequentially
    QList<DownloadResult> downloadAll(const QList<DownloadRequest>& requests) {
        QList<DownloadResult> results;
        int completed = 0;
        int total = requests.size();

        for (const auto& req : requests) {
            DownloadResult result = downloadSync(req);
            results.append(result);
            completed++;
            emit downloadProgress(completed, total);
            emit stageMessage(QString("Downloaded %1/%2: %3")
                .arg(completed).arg(total).arg(req.datasetName));
        }
        return results;
    }

signals:
    void downloadProgress(int completed, int total);
    void stageMessage(const QString& msg);

private:
    CacheManager* m_cache;
    QNetworkAccessManager* m_network;
};

} // namespace terrain_pipeline
