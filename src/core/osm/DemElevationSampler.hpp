#pragma once

// ============================================================
// DemElevationSampler — road elevation from the project's DEM
// ============================================================
//
// Port of GeoTerrain's demSampler.ts: loads the project's exported
// heightmap GeoTIFF (Terrain Studio output) and samples elevation at
// (lon, lat) so the OpenDRIVE exporter can write real elevation
// profiles instead of flat roads.
//
// Lookup order for bounds:
//   1. terrain-manifest.json next to the heightmap (exact WGS84 bounds)
//   2. the GeoTIFF's own ModelTiepoint/ModelPixelScale tags
//
// The GeoTIFF decoder (terrain::DemDecoder) needs libtiff; targets
// without libtiff (e.g. test_osm_pipeline) still get the sampler via
// loadFromGrid() and compile — only file loading is unavailable there.
//

#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdint>

// ─── Conditional GeoTIFF decoding via terrain::DemDecoder ───
// DemDecoder.hpp pulls in <tiffio.h> and defines terrain::DemTile.
// It MUST be included OUTSIDE namespace osm so that terrain::DemTile
// lands in the global ::terrain namespace (not osm::terrain).
#if defined(__has_include)
#  if __has_include(<tiffio.h>)
#    define OGS_SAMPLER_HAVE_DEMCODER 1
#  endif
#endif

#ifdef OGS_SAMPLER_HAVE_DEMCODER
#include "../../ui/terrain/DemDecoder.hpp"
#endif

namespace osm {

class DemElevationSampler {
public:
    bool valid() const { return m_valid; }
    QString sourcePath() const { return m_path; }

    // Elevation in meters at lon/lat, or NaN when unavailable/out of bounds.
    // Nearest-pixel sampling, same as GeoTerrain's sampler.
    double sampleLonLat(double lon, double lat) const {
        if (!m_valid || m_w <= 0 || m_h <= 0) return std::nan("");
        if (lon < m_west || lon > m_east || lat < m_south || lat > m_north)
            return std::nan("");
        const double fx = (lon - m_west) / (m_east - m_west) * m_w;
        const double fy = (m_north - lat) / (m_north - m_south) * m_h;
        const int px = qBoundInt(0, int(fx), m_w - 1);
        const int py = qBoundInt(0, int(fy), m_h - 1);
        const double e = m_elev[size_t(py) * m_w + px];
        return e;
    }

    // Direct grid injection (tests / callers with decoded data already)
    bool loadFromGrid(std::vector<float> elevations, int width, int height,
                      double west, double south, double east, double north) {
        if (width <= 0 || height <= 0 ||
            elevations.size() < size_t(width) * size_t(height)) return false;
        if (!(west < east) || !(south < north)) return false;
        m_elev = std::move(elevations);
        m_w = width; m_h = height;
        m_west = west; m_east = east; m_north = north; m_south = south;
        m_valid = true;
        m_path = "(grid)";
        return true;
    }

    // Load the project's exported terrain heightmap (GeoTerrain
    // findCachedHeightmap pattern, adapted to our export layout).
    bool loadFromProject(const QString& projectPath) {
        if (projectPath.isEmpty()) return false;
        const QDir proj(projectPath);
        const QStringList candidates = {
            proj.filePath("Terrain/heightmap_merged.tif"),
            proj.filePath("Exports/terrain_heightmap_merged.tif"),
            proj.filePath("Terrain/heightmaps/tile_0,0.tif"),
            proj.filePath("heightmap.tif"),
            proj.filePath("terrain/heightmap.tif"),
        };
        for (const QString& c : candidates) {
            if (!QFile::exists(c)) continue;
            if (loadFromGeoTiff(c)) return true;
        }
        return false;
    }

    // Load one GeoTIFF. Bounds come from terrain-manifest.json when
    // present next to the file, else from the file's geo tags.
    bool loadFromGeoTiff(const QString& path) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return false;
        const QByteArray data = f.readAll();
        f.close();

        if (!decodeGeoTiffData(data)) return false;
        m_path = path;

        // Exact WGS84 bounds from the export manifest when available
        QFileInfo info(path);
        const QString manifestCandidates[] = {
            info.dir().filePath("terrain-manifest.json"),
            info.dir().filePath("../terrain-manifest.json"),
        };
        for (const QString& mp : manifestCandidates) {
            QFile mf(mp);
            if (!mf.open(QIODevice::ReadOnly)) continue;
            const QJsonDocument doc = QJsonDocument::fromJson(mf.readAll());
            mf.close();
            if (!doc.isObject()) continue;
            QJsonObject bounds;
            if (doc.object().contains("bounds")) {
                bounds = doc.object()["bounds"].toObject();
            } else {
                // per-tile file: use the matching tile bounds (id from name)
                const QString tileId = info.completeBaseName();
                if (tileId.startsWith("tile_")) {
                    const QJsonArray tiles = doc.object()["tiles"].toArray();
                    for (const auto& tv : tiles) {
                        if (tv.toObject()["id"].toString() == tileId.mid(5)) {
                            bounds = tv.toObject()["bounds"].toObject();
                            break;
                        }
                    }
                }
            }
            const double w = bounds["west"].toDouble();
            const double s = bounds["south"].toDouble();
            const double e = bounds["east"].toDouble();
            const double n = bounds["north"].toDouble();
            if (w < e && s < n) {
                m_west = w; m_east = e; m_north = n; m_south = s;
                return true;
            }
        }

        // Fall back to the GeoTIFF's own tiepoint/scale tags
        double west = 0, north = 0, scale_x = 0, scale_y = 0;
        if (readGeoTags(data, west, north, scale_x, scale_y) &&
            scale_x > 0 && scale_y > 0) {
            m_west = west;
            m_east = west + scale_x * m_w;
            m_north = north;
            m_south = north - scale_y * m_h;
            return true;
        }

        m_valid = false;
        return false;
    }

private:
    static int qBoundInt(int lo, int v, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    bool decodeGeoTiffData(const QByteArray& data);
    static bool readGeoTags(const QByteArray& data, double& west, double& north,
                            double& scaleX, double& scaleY);

    std::vector<float> m_elev;
    int m_w = 0, m_h = 0;
    double m_west = 0, m_east = 0, m_north = 0, m_south = 0;
    bool m_valid = false;
    QString m_path;
};

// ─── Implementation ───

#ifdef OGS_SAMPLER_HAVE_DEMCODER

inline bool DemElevationSampler::decodeGeoTiffData(const QByteArray& data) {
    const terrain::DemTile tile = terrain::DemDecoder::decodeAuto(data, "dem");
    if (!tile.valid || tile.width <= 0 || tile.height <= 0) return false;
    m_elev = tile.elevations;
    m_w = tile.width;
    m_h = tile.height;
    m_valid = true;
    return true;
}
#else
inline bool DemElevationSampler::decodeGeoTiffData(const QByteArray&) {
    // No libtiff in this target — use loadFromGrid()/loadFromProject is
    // unavailable; sampling from an already-decoded grid still works.
    return false;
}
#endif

// Minimal classic-TIFF IFD walk for the geo tags (no libtiff needed).
inline bool DemElevationSampler::readGeoTags(const QByteArray& data,
                                             double& west, double& north,
                                             double& scaleX, double& scaleY) {
    if (data.size() < 8) return false;
    const char* b = data.constData();
    bool le;
    if (b[0] == 'I' && b[1] == 'I') le = true;
    else if (b[0] == 'M' && b[1] == 'M') le = false;
    else return false;

    auto u16 = [&](qint64 off) -> int {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(b + off);
        return le ? int(p[0] | (p[1] << 8)) : int((p[0] << 8) | p[1]);
    };
    auto u32 = [&](qint64 off) -> quint32 {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(b + off);
        quint32 v = 0;
        for (int i = 0; i < 4; ++i)
            v = (v << 8) | p[le ? 3 - i : i];
        return v;
    };
    auto f64 = [&](qint64 off) -> double {
        // TIFF doubles are always big-endian... no: they follow the file's
        // byte order.
        double d = 0;
        std::memcpy(&d, b + off, 8);
        if (le) return d;  // host little-endian read
        // big-endian file: byte-swap via quint64
        quint64 raw; std::memcpy(&raw, b + off, 8);
        quint64 sw = 0;
        for (int i = 0; i < 8; ++i) sw = (sw << 8) | ((raw >> (8 * i)) & 0xff);
        std::memcpy(&d, &sw, 8);
        return d;
    };

    const quint32 ifdOff = u32(4);
    if (ifdOff + 2 > quint32(data.size())) return false;
    const int numEntries = u16(ifdOff);
    bool haveScale = false, haveTie = false;
    for (int i = 0; i < numEntries; ++i) {
        const qint64 e = qint64(ifdOff) + 2 + qint64(i) * 12;
        if (e + 12 > data.size()) break;
        const int tag = u16(e);
        const int type = u16(e + 2);
        const quint32 count = u32(e + 4);
        if (type != 12) continue;  // TIFF_DOUBLE only
        const quint32 off = u32(e + 8);  // payload > 4 bytes → offset field
        if (tag == 33550 && count == 3 && off + 24 <= quint32(data.size())) {
            scaleX = f64(off);
            scaleY = f64(off + 8);
            haveScale = true;
        } else if (tag == 33922 && count == 6 && off + 48 <= quint32(data.size())) {
            // [i, j, k, x, y, z] — raster (0,0) → world (west, north)
            west = f64(off + 24);
            north = f64(off + 32);
            haveTie = true;
        }
    }
    return haveScale && haveTie;
}

} // namespace osm
