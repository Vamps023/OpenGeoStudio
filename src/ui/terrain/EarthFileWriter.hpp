#pragma once

// ============================================================
// EarthFileWriter — SCANeR (osgEarth) .earth emitter
// ============================================================
// SCANeR's Terrain mode is osgEarth/GDAL based: a .earth file with
// <heightfield driver="gdal"> pointing at our merged GeoTIFF gives
// File > Import in one step, with the CRS pinned via <profile srs>.
// Header-only so the test target can exercise it directly.
// ============================================================

#include <QString>
#include <QFile>
#include <QTextStream>
#include "RasterWriter.hpp"

namespace terrain {

inline QString scanerSrsString(const RasterExtent& ext) {
    switch (ext.crsMode) {
    case GeoCrsMode::UTM:
        if (ext.utmEpsg > 0) return QStringLiteral("epsg:%1").arg(ext.utmEpsg);
        return QStringLiteral("auto-utm");
    case GeoCrsMode::WebMercator:
        return QStringLiteral("epsg:3857");
    case GeoCrsMode::WGS84:
    default:
        return QStringLiteral("epsg:4326");
    }
}

inline bool writeEarthFile(const QString& path, const RasterExtent& ext,
                           const QString& imageFile, const QString& heightFile) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);
    const QString srs = scanerSrsString(ext);
    const QString mapType = (ext.crsMode == GeoCrsMode::WGS84)
        ? QStringLiteral("geographic") : QStringLiteral("projected");
    out << "<?xml version=\"1.0\"?>\n"
        << "<map type=\"" << mapType << "\" version=\"2\">\n"
        << "  <options>\n"
        << "    <profile srs=\"" << srs << "\""
        << " xmin=\"" << qMin(ext.west, ext.east)
        << "\" ymin=\"" << qMin(ext.south, ext.north)
        << "\" xmax=\"" << qMax(ext.west, ext.east)
        << "\" ymax=\"" << qMax(ext.south, ext.north) << "\"/>\n"
        << "  </options>\n";
    if (!imageFile.isEmpty())
        out << "  <image driver=\"gdal\" name=\"imagery\">\n"
            << "    <url>" << imageFile << "</url>\n"
            << "  </image>\n";
    if (!heightFile.isEmpty())
        out << "  <heightfield driver=\"gdal\" name=\"elevation\">\n"
            << "    <url>" << heightFile << "</url>\n"
            << "  </heightfield>\n";
    out << "</map>\n";
    f.close();
    return true;
}

} // namespace terrain
