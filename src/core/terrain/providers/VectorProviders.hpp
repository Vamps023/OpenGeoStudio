#pragma once

// ============================================================
// VectorProviders — Road, Water, Building, LandCover providers
// ============================================================
//
// Road data: OpenStreetMap via Overpass API
// Water data: OpenStreetMap via Overpass API
// Building data: OpenStreetMap via Overpass API
// LandCover: ESA WorldCover (free, no key)
//

#include "TerrainDataProvider.hpp"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QXmlStreamReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>

namespace terrain_pipeline {

// ============================================================
// OSMOverpassProvider — Base for OSM Overpass API providers
// ============================================================

class OSMOverpassProvider : public TerrainDataProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit OSMOverpassProvider(QObject* parent = nullptr)
        : TerrainDataProvider(parent) {}

protected:
    // Build Overpass QL query for the given bounding box
    QString buildOverpassQuery(const QString& selector,
                                double minLat, double maxLat,
                                double minLon, double maxLon) const {
        return QString("[out:xml][timeout:30];(%1(%2,%3,%4,%5););out geom;")
            .arg(selector)
            .arg(minLat, 0, 'f', 6).arg(minLon, 0, 'f', 6)
            .arg(maxLat, 0, 'f', 6).arg(maxLon, 0, 'f', 6);
    }

    // Parse OSM XML geometry (ways with nd refs + geometry)
    struct OsmWay {
        QList<QPair<double, double>> coords;  // lat, lon
        QMap<QString, QString> tags;
    };

    QList<OsmWay> parseOsmXml(const QByteArray& xml) const {
        QList<OsmWay> ways;
        QXmlStreamReader reader(xml);

        OsmWay currentWay;
        bool inWay = false;

        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()) {
                if (reader.name() == "way") {
                    currentWay = OsmWay();
                    inWay = true;
                } else if (reader.name() == "nd" && inWay) {
                    double lat = reader.attributes().value("lat").toDouble();
                    double lon = reader.attributes().value("lon").toDouble();
                    currentWay.coords.append(qMakePair(lat, lon));
                } else if (reader.name() == "tag" && inWay) {
                    QString k = reader.attributes().value("k").toString();
                    QString v = reader.attributes().value("v").toString();
                    currentWay.tags[k] = v;
                }
            } else if (reader.isEndElement()) {
                if (reader.name() == "way") {
                    if (!currentWay.coords.isEmpty())
                        ways.append(currentWay);
                    inWay = false;
                }
            }
        }
        return ways;
    }
};

// ============================================================
// OSMRoadProvider — Road data from OpenStreetMap
// ============================================================

class OSMRoadProvider : public RoadProvider, public OSMOverpassProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit OSMRoadProvider(QObject* parent = nullptr)
        : RoadProvider(parent), OSMOverpassProvider() {}

    ProviderInfo info() const override {
        return {"osm-roads", "OpenStreetMap roads via Overpass API",
                ProviderCapability::Roads, "1.0", "ODbL", false, "",
                "© OpenStreetMap contributors"};
    }
    QString name() const override { return "osm-roads"; }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int) const override {
        QList<DownloadRequest> requests;
        DownloadRequest req;
        QString query = buildOverpassQuery(
            "way[\"highway\"];", minLat, maxLat, minLon, maxLon);
        req.url = "https://overpass-api.de/api/interpreter";
        req.cacheKey = QString("osm_roads_%1_%2_%3_%4")
            .arg(minLat).arg(maxLat).arg(minLon).arg(maxLon);
        req.providerName = "osm-roads";
        req.datasetName = "roads";
        // POST body stored in headers for simplicity
        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        req.headers["data"] = "data=" + QUrl::toPercentEncoding(query);
        requests.append(req);
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QFile f(path);
        return f.exists() && f.size() > 50;
    }

    QList<RoadSegment> getRoads(double minLat, double maxLat,
                                 double minLon, double maxLon) const override {
        Q_UNUSED(minLat) Q_UNUSED(maxLat) Q_UNUSED(minLon) Q_UNUSED(maxLon)
        // Actual implementation would fetch and parse OSM XML
        // For now, returns empty — populated by download+parse flow
        return {};
    }

    QList<RoadSegment> parseRoads(const QByteArray& osmXml) const {
        QList<RoadSegment> roads;
        auto ways = parseOsmXml(osmXml);
        for (const auto& way : ways) {
            RoadSegment seg;
            seg.type = way.tags.value("highway", "residential");
            seg.name = way.tags.value("name", "");
            for (const auto& coord : way.coords) {
                // coord.first = lat, coord.second = lon
                seg.coordinates.append(qMakePair(coord.second, coord.first));
            }
            roads.append(seg);
        }
        return roads;
    }
};

// ============================================================
// OSMWaterProvider — Water bodies from OpenStreetMap
// ============================================================

class OSMWaterProvider : public WaterProvider, public OSMOverpassProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit OSMWaterProvider(QObject* parent = nullptr)
        : WaterProvider(parent), OSMOverpassProvider() {}

    ProviderInfo info() const override {
        return {"osm-water", "OpenStreetMap water bodies via Overpass API",
                ProviderCapability::Water, "1.0", "ODbL", false, "",
                "© OpenStreetMap contributors"};
    }
    QString name() const override { return "osm-water"; }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int) const override {
        QList<DownloadRequest> requests;
        DownloadRequest req;
        QString query = buildOverpassQuery(
            "way[\"water\"];way[\"waterway\"];relation[\"water\"];",
            minLat, maxLat, minLon, maxLon);
        req.url = "https://overpass-api.de/api/interpreter";
        req.cacheKey = QString("osm_water_%1_%2_%3_%4")
            .arg(minLat).arg(maxLat).arg(minLon).arg(maxLon);
        req.providerName = "osm-water";
        req.datasetName = "water";
        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        req.headers["data"] = "data=" + QUrl::toPercentEncoding(query);
        requests.append(req);
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QFile f(path);
        return f.exists() && f.size() > 50;
    }

    QList<WaterBody> getWaterBodies(double minLat, double maxLat,
                                     double minLon, double maxLon) const override {
        Q_UNUSED(minLat) Q_UNUSED(maxLat) Q_UNUSED(minLon) Q_UNUSED(maxLon)
        return {};
    }

    QList<WaterBody> parseWater(const QByteArray& osmXml) const {
        QList<WaterBody> bodies;
        auto ways = parseOsmXml(osmXml);
        for (const auto& way : ways) {
            WaterBody wb;
            if (way.tags.contains("waterway"))
                wb.type = way.tags.value("waterway", "river");
            else
                wb.type = way.tags.value("water", "lake");
            for (const auto& coord : way.coords) {
                wb.coordinates.append(qMakePair(coord.second, coord.first));
            }
            bodies.append(wb);
        }
        return bodies;
    }
};

// ============================================================
// OSMBuildingProvider — Building footprints from OpenStreetMap
// ============================================================

class OSMBuildingProvider : public BuildingProvider, public OSMOverpassProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit OSMBuildingProvider(QObject* parent = nullptr)
        : BuildingProvider(parent), OSMOverpassProvider() {}

    ProviderInfo info() const override {
        return {"osm-buildings", "OpenStreetMap building footprints via Overpass API",
                ProviderCapability::Buildings, "1.0", "ODbL", false, "",
                "© OpenStreetMap contributors"};
    }
    QString name() const override { return "osm-buildings"; }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int) const override {
        QList<DownloadRequest> requests;
        DownloadRequest req;
        QString query = buildOverpassQuery(
            "way[\"building\"];", minLat, maxLat, minLon, maxLon);
        req.url = "https://overpass-api.de/api/interpreter";
        req.cacheKey = QString("osm_buildings_%1_%2_%3_%4")
            .arg(minLat).arg(maxLat).arg(minLon).arg(maxLon);
        req.providerName = "osm-buildings";
        req.datasetName = "buildings";
        req.headers["Content-Type"] = "application/x-www-form-urlencoded";
        req.headers["data"] = "data=" + QUrl::toPercentEncoding(query);
        requests.append(req);
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QFile f(path);
        return f.exists() && f.size() > 50;
    }

    QList<BuildingFootprint> getBuildings(double minLat, double maxLat,
                                            double minLon, double maxLon) const override {
        Q_UNUSED(minLat) Q_UNUSED(maxLat) Q_UNUSED(minLon) Q_UNUSED(maxLon)
        return {};
    }

    QList<BuildingFootprint> parseBuildings(const QByteArray& osmXml) const {
        QList<BuildingFootprint> footprints;
        auto ways = parseOsmXml(osmXml);
        for (const auto& way : ways) {
            BuildingFootprint fp;
            fp.name = way.tags.value("name", "");
            // Parse height if available
            QString heightStr = way.tags.value("height", "");
            if (!heightStr.isEmpty()) {
                bool ok;
                float h = heightStr.toFloat(&ok);
                if (ok) fp.height = h;
            }
            // Parse building levels as fallback
            if (fp.height == 0) {
                QString levelsStr = way.tags.value("building:levels", "");
                if (!levelsStr.isEmpty()) {
                    bool ok;
                    float levels = levelsStr.toFloat(&ok);
                    if (ok) fp.height = levels * 3.0f;  // ~3m per level
                }
            }
            for (const auto& coord : way.coords) {
                fp.coordinates.append(qMakePair(coord.second, coord.first));
            }
            footprints.append(fp);
        }
        return footprints;
    }
};

// ============================================================
// ESAWorldCoverProvider — ESA WorldCover land cover (free)
// ============================================================

class ESAWorldCoverProvider : public LandCoverProvider {
    // Q_OBJECT removed — header-only, no signals/slots needed

public:
    explicit ESAWorldCoverProvider(QObject* parent = nullptr)
        : LandCoverProvider(parent) {}

    ProviderInfo info() const override {
        return {"esa-worldcover", "ESA WorldCover 10m land cover",
                ProviderCapability::LandCover, "v100", "CC BY 4.0", false, "",
                "© ESA WorldCover"};
    }
    QString name() const override { return "esa-worldcover"; }

    QList<LandCoverClass> classes() const override {
        return {
            {10,  "Tree cover",            0,   100, 0},
            {20,  "Shrubland",              255, 187, 34},
            {30,  "Grassland",              255, 255, 76},
            {40,  "Cropland",               255, 255, 0},
            {50,  "Built-up",               185, 0,   0},
            {60,  "Bare / sparse vegetation", 243, 243, 243},
            {70,  "Snow and ice",           255, 255, 255},
            {80,  "Permanent water bodies", 0,   32,  255},
            {90,  "Herbaceous wetland",     0,   150, 255},
            {95,  "Mangroves",              0,   100, 200},
            {100, "Moss and lichen",        104, 171, 113},
        };
    }

    QList<DownloadRequest> discoverTiles(double minLat, double maxLat,
                                          double minLon, double maxLon,
                                          int) const override {
        QList<DownloadRequest> requests;
        // ESA WorldCover is distributed as 3° x 3° tiles
        int minLat3 = static_cast<int>(std::floor(minLat / 3.0)) * 3;
        int maxLat3 = static_cast<int>(std::floor(maxLat / 3.0)) * 3;
        int minLon3 = static_cast<int>(std::floor(minLon / 3.0)) * 3;
        int maxLon3 = static_cast<int>(std::floor(maxLon / 3.0)) * 3;

        for (int lat = minLat3; lat <= maxLat3; lat += 3) {
            for (int lon = minLon3; lon <= maxLon3; lon += 3) {
                QString tileName = formatTileName(lat, lon);
                DownloadRequest req;
                req.url = QString("https://esa-worldcover.s3.amazonaws.com/esa_worldcover_2020_%1_map.tif")
                    .arg(tileName);
                req.cacheKey = QString("esa_worldcover_%1").arg(tileName);
                req.providerName = "esa-worldcover";
                req.datasetName = "landcover";
                requests.append(req);
            }
        }
        return requests;
    }

    bool validateDownload(const QString& path) const override {
        QFile f(path);
        return f.exists() && f.size() > 1024;
    }

private:
    static QString formatTileName(int lat, int lon) {
        QString latStr = (lat >= 0 ? "N" : "S") +
            QString::number(std::abs(lat)).rightJustified(2, '0');
        QString lonStr = (lon >= 0 ? "E" : "W") +
            QString::number(std::abs(lon)).rightJustified(3, '0');
        return latStr + lonStr;
    }
};

} // namespace terrain_pipeline
