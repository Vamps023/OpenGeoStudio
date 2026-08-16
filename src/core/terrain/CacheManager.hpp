#pragma once

// ============================================================
// CacheManager — Deterministic cache for downloaded data
// ============================================================

#include "TerrainPipelineTypes.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDateTime>

namespace terrain_pipeline {

class CacheManager {
public:
    explicit CacheManager(const QString& cacheDir) : m_cacheDir(cacheDir) {
        QDir().mkpath(m_cacheDir);
    }

    // Generate a deterministic cache key
    static QString generateKey(const QString& provider, const QString& dataset,
                                const QString& tileId, const QString& version = "1.0",
                                int resolution = 0, int epsg = 0) {
        QString raw = QString("%1_%2_%3_%4_%5_%6")
            .arg(provider).arg(dataset).arg(tileId).arg(version)
            .arg(resolution).arg(epsg);
        return QString::fromLatin1(
            QCryptographicHash::hash(raw.toUtf8(), QCryptographicHash::Md5).toHex());
    }

    // Check if a cache entry exists
    bool exists(const QString& key) const {
        return QFile::exists(cachePath(key));
    }

    // Get the full path for a cache entry
    QString cachePath(const QString& key) const {
        return m_cacheDir + "/" + key;
    }

    // Read a cache entry
    QByteArray read(const QString& key) const {
        QFile f(cachePath(key));
        if (!f.open(QIODevice::ReadOnly)) return QByteArray();
        return f.readAll();
    }

    // Write a cache entry
    bool write(const QString& key, const QByteArray& data) const {
        QFile f(cachePath(key));
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(data);
        f.close();
        return true;
    }

    // Get cache entry info
    struct CacheEntryInfo {
        bool exists = false;
        qint64 size = 0;
        QDateTime created;
        QString path;
    };

    CacheEntryInfo info(const QString& key) const {
        CacheEntryInfo info;
        info.path = cachePath(key);
        QFileInfo fi(info.path);
        info.exists = fi.exists();
        info.size = fi.size();
        info.created = fi.birthTime();
        return info;
    }

    // Invalidate cache entries matching a pattern
    void invalidate(const QString& pattern) {
        QDir dir(m_cacheDir);
        auto entries = dir.entryList(QStringList() << "*" + pattern + "*",
                                      QDir::Files);
        for (const auto& entry : entries) {
            QFile::remove(m_cacheDir + "/" + entry);
        }
    }

    // Clear entire cache
    void clear() {
        QDir dir(m_cacheDir);
        dir.removeRecursively();
        dir.mkpath(".");
    }

    // Get cache size in bytes
    qint64 totalSize() const {
        qint64 size = 0;
        QDir dir(m_cacheDir);
        for (const auto& entry : dir.entryList(QDir::Files)) {
            size += QFileInfo(m_cacheDir + "/" + entry).size();
        }
        return size;
    }

    QString cacheDir() const { return m_cacheDir; }

private:
    QString m_cacheDir;
};

} // namespace terrain_pipeline
