#pragma once

// ============================================================
// DownloadManager — Parallel HTTP download with retry and caching
// ============================================================
//
// Provides both synchronous (downloadSync, with QEventLoop) and
// asynchronous (downloadAllAsync, signal-based) download APIs.
//
// The async API is preferred for UI-facing code to avoid blocking
// the event loop. The sync API is retained for tests and pipelines
// that already run on worker threads.
//

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

    // ── Synchronous API (blocks with QEventLoop) ──────────────
    // Use only from worker threads or tests, never from the UI thread.

    DownloadResult downloadSync(const DownloadRequest& req) {
        DownloadResult result;
        result.cacheKey = req.cacheKey;

        // Check cache first
        if (m_cache && m_cache->exists(req.cacheKey)) {
            if (req.outputPath.isEmpty()) {
                result.data = m_cache->read(req.cacheKey);
            } else {
                result.outputPath = req.outputPath;
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
            QTimer::singleShot(req.timeoutMs, &loop, &QEventLoop::quit);
            loop.exec();

            if (reply->error() == QNetworkReply::NoError) {
                result.data = reply->readAll();
                result.httpStatus = reply->attribute(
                    QNetworkRequest::HttpStatusCodeAttribute).toInt();
                result.success = (result.httpStatus == 200 || result.httpStatus == 302);

                if (result.success && !result.data.isEmpty()) {
                    if (m_cache)
                        m_cache->write(req.cacheKey, result.data);
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

            if (attempt < req.maxRetries - 1)
                QThread::msleep(1000 * (attempt + 1));
        }

        result.success = false;
        return result;
    }

    // Download multiple requests sequentially (synchronous)
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

    // ── Asynchronous API (non-blocking, signal-based) ─────────
    // Starts downloading all requests. Progress and completion are
    // reported via signals. The caller should connect to:
    //   - downloadProgress(int completed, int total)
    //   - stageMessage(const QString& msg)
    //   - allDownloadsFinished(const QList<DownloadResult>& results)
    //
    // Only one async batch may be active at a time. Call
    // isAsyncBusy() to check.
    //
    void downloadAllAsync(const QList<DownloadRequest>& requests) {
        if (m_asyncBusy) return;
        m_asyncResults.clear();
        m_asyncQueue = requests;
        m_asyncTotal = requests.size();
        m_asyncCompleted = 0;
        m_asyncBusy = true;
        startNextAsyncDownload();
    }

    bool isAsyncBusy() const { return m_asyncBusy; }

signals:
    void downloadProgress(int completed, int total);
    void stageMessage(const QString& msg);
    void allDownloadsFinished(const QList<DownloadResult>& results);

private:
    CacheManager* m_cache;
    QNetworkAccessManager* m_network;

    // Async state
    bool m_asyncBusy = false;
    QList<DownloadRequest> m_asyncQueue;
    QList<DownloadResult> m_asyncResults;
    int m_asyncTotal = 0;
    int m_asyncCompleted = 0;
    int m_asyncRetry = 0;
    QPointer<QNetworkReply> m_asyncReply;
    QPointer<QTimer> m_asyncTimer;

    void startNextAsyncDownload() {
        if (m_asyncQueue.isEmpty()) {
            m_asyncBusy = false;
            emit allDownloadsFinished(m_asyncResults);
            return;
        }

        const DownloadRequest req = m_asyncQueue.first();
        m_asyncRetry = 0;
        attemptAsyncDownload(req);
    }

    void attemptAsyncDownload(const DownloadRequest& req) {
        // Check cache first
        if (m_cache && m_cache->exists(req.cacheKey)) {
            DownloadResult result;
            result.cacheKey = req.cacheKey;
            if (req.outputPath.isEmpty()) {
                result.data = m_cache->read(req.cacheKey);
            } else {
                result.outputPath = req.outputPath;
                QFile::copy(m_cache->cachePath(req.cacheKey), req.outputPath);
            }
            result.success = true;
            result.fromCache = true;
            finishAsyncDownload(req, result);
            return;
        }

        QNetworkRequest networkReq;
        networkReq.setUrl(QUrl(req.url));
        networkReq.setTransferTimeout(req.timeoutMs);
        networkReq.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio/1.0");

        for (auto it = req.headers.begin(); it != req.headers.end(); ++it) {
            if (it.key() == "data") continue;
            networkReq.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
        }

        if (req.headers.contains("data")) {
            QByteArray postData = req.headers.value("data").toUtf8();
            m_asyncReply = m_network->post(networkReq, postData);
        } else {
            m_asyncReply = m_network->get(networkReq);
        }

        // Timeout
        delete m_asyncTimer;
        m_asyncTimer = new QTimer(this);
        m_asyncTimer->setSingleShot(true);
        connect(m_asyncTimer, &QTimer::timeout, this, [this]() {
            if (m_asyncReply) m_asyncReply->abort();
        });
        m_asyncTimer->start(req.timeoutMs);

        connect(m_asyncReply, &QNetworkReply::finished, this, [this, req]() {
            onAsyncReplyFinished(req);
        });
    }

    void onAsyncReplyFinished(const DownloadRequest& req) {
        if (m_asyncTimer) m_asyncTimer->stop();

        DownloadResult result;
        result.cacheKey = req.cacheKey;

        if (m_asyncReply && m_asyncReply->error() == QNetworkReply::NoError) {
            result.data = m_asyncReply->readAll();
            result.httpStatus = m_asyncReply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            result.success = (result.httpStatus == 200 || result.httpStatus == 302);

            if (result.success && !result.data.isEmpty()) {
                if (m_cache)
                    m_cache->write(req.cacheKey, result.data);
                if (!req.outputPath.isEmpty()) {
                    QDir().mkpath(QFileInfo(req.outputPath).absolutePath());
                    QFile f(req.outputPath);
                    if (f.open(QIODevice::WriteOnly)) {
                        f.write(result.data);
                        f.close();
                        result.outputPath = req.outputPath;
                    }
                }
            }
        } else if (m_asyncReply) {
            result.error = m_asyncReply->errorString();
            result.httpStatus = m_asyncReply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
        }

        if (m_asyncReply) m_asyncReply->deleteLater();

        // Retry on failure
        if (!result.success && m_asyncRetry < req.maxRetries - 1) {
            m_asyncRetry++;
            // Delay before retry using a single-shot timer
            QTimer::singleShot(1000 * m_asyncRetry, this, [this, req]() {
                attemptAsyncDownload(req);
            });
            return;
        }

        finishAsyncDownload(req, result);
    }

    void finishAsyncDownload(const DownloadRequest& req, const DownloadResult& result) {
        m_asyncResults.append(result);
        m_asyncQueue.removeFirst();
        m_asyncCompleted++;
        emit downloadProgress(m_asyncCompleted, m_asyncTotal);
        emit stageMessage(QString("Downloaded %1/%2: %3")
            .arg(m_asyncCompleted).arg(m_asyncTotal).arg(req.datasetName));
        startNextAsyncDownload();
    }
};

} // namespace terrain_pipeline
