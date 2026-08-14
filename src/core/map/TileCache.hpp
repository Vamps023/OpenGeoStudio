#pragma once

// ============================================================
// TileCache — In-memory tile image cache (inspired by QgsTileCache)
// Thread-safe with QMutex, uses QCache for LRU eviction
// ============================================================

#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QImage>
#include <QUrl>
#include <QString>

namespace map {

class TileCache {
public:
    static TileCache& instance() {
        static TileCache s_cache;
        return s_cache;
    }

    // Insert a tile image into cache
    void insertTile(const QUrl& url, const QImage& image) {
        QMutexLocker locker(&m_mutex);
        m_cache.insert(url, new QImage(image));
    }

    // Retrieve a tile image from cache
    bool tile(const QUrl& url, QImage& outImage) const {
        QMutexLocker locker(&m_mutex);
        if (const QImage* img = m_cache.object(url)) {
            outImage = *img;
            return true;
        }
        return false;
    }

    bool hasTile(const QUrl& url) const {
        QMutexLocker locker(&m_mutex);
        return m_cache.contains(url);
    }

    void clear() {
        QMutexLocker locker(&m_mutex);
        m_cache.clear();
    }

    int totalCost() const {
        QMutexLocker locker(&m_mutex);
        return m_cache.totalCost();
    }

    int maxCost() const {
        QMutexLocker locker(&m_mutex);
        return m_cache.maxCost();
    }

    void setMaxCost(int max) {
        QMutexLocker locker(&m_mutex);
        m_cache.setMaxCost(max);
    }

private:
    TileCache() : m_cache(256) {}  // Default: 256 tiles
    TileCache(const TileCache&) = delete;
    TileCache& operator=(const TileCache&) = delete;

    mutable QMutex m_mutex;
    QCache<QUrl, QImage> m_cache;
};

} // namespace map
