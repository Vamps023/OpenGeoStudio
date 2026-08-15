#pragma once

// ============================================================
// RoadTypes — Core road data model types
// ============================================================
//
// Mirrors modules/road-studio/shared/types.ts.
// These types are used by the RoadStudioStore and the 2D/3D viewports.
//

#include <QString>
#include <QList>
#include <QPointF>
#include <QJsonObject>
#include <QJsonArray>
#include <QColor>
#include <optional>
#include <cmath>

namespace roads {

// Vec2 — 2D vector in local meters (for handles)
struct Vec2 {
    double x = 0, y = 0;

    QJsonObject toJson() const { return {{"x", x}, {"y", y}}; }
    static Vec2 fromJson(const QJsonObject& j) {
        return {j["x"].toDouble(0), j["y"].toDouble(0)};
    }
};

// Point2D — point in local meters
using Point2D = Vec2;

// SegmentMetadata — describes the geometry of a road segment
struct SegmentMetadata {
    enum class Kind { Line, Bezier, Arc, Spiral };

    Kind kind = Kind::Line;
    int version = 1;
    double startHeading = 0;
    double curvature = 0;
    double arcLength = 0;
    double curvatureStart = 0;
    double curvatureEnd = 0;
    double segmentLength = 0;

    QString kindStr() const {
        switch (kind) {
            case Kind::Line: return "line";
            case Kind::Bezier: return "bezier";
            case Kind::Arc: return "arc";
            case Kind::Spiral: return "spiral";
        }
        return "line";
    }

    static Kind kindFromStr(const QString& s) {
        if (s == "bezier") return Kind::Bezier;
        if (s == "arc") return Kind::Arc;
        if (s == "spiral") return Kind::Spiral;
        return Kind::Line;
    }

    QJsonObject toJson() const {
        return {
            {"kind", kindStr()}, {"version", version},
            {"startHeading", startHeading}, {"curvature", curvature},
            {"arcLength", arcLength}, {"curvatureStart", curvatureStart},
            {"curvatureEnd", curvatureEnd}, {"segmentLength", segmentLength}
        };
    }

    static SegmentMetadata fromJson(const QJsonObject& j) {
        SegmentMetadata s;
        s.kind = kindFromStr(j["kind"].toString("line"));
        s.version = j["version"].toInt(1);
        s.startHeading = j["startHeading"].toDouble(0);
        s.curvature = j["curvature"].toDouble(0);
        s.arcLength = j["arcLength"].toDouble(0);
        s.curvatureStart = j["curvatureStart"].toDouble(0);
        s.curvatureEnd = j["curvatureEnd"].toDouble(0);
        s.segmentLength = j["segmentLength"].toDouble(0);
        return s;
    }
};

// ControlPoint — a point on a road with optional bezier handles
struct ControlPoint {
    QString id;
    double lat = 0, lon = 0, z = 0;
    std::optional<Vec2> handleIn;
    std::optional<Vec2> handleOut;
    enum class Type { Smooth, Corner } type = Type::Smooth;
    std::optional<SegmentMetadata> segmentMeta;

    QString typeStr() const { return type == Type::Smooth ? "smooth" : "corner"; }
    static Type typeFromStr(const QString& s) {
        return s == "corner" ? Type::Corner : Type::Smooth;
    }

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        j["lat"] = lat;
        j["lon"] = lon;
        j["z"] = z;
        if (handleIn) j["handleIn"] = handleIn->toJson();
        if (handleOut) j["handleOut"] = handleOut->toJson();
        j["type"] = typeStr();
        if (segmentMeta) j["segmentMeta"] = segmentMeta->toJson();
        return j;
    }

    static ControlPoint fromJson(const QJsonObject& j) {
        ControlPoint cp;
        cp.id = j["id"].toString();
        cp.lat = j["lat"].toDouble();
        cp.lon = j["lon"].toDouble();
        cp.z = j["z"].toDouble(0);
        if (j.contains("handleIn") && j["handleIn"].isObject())
            cp.handleIn = Vec2::fromJson(j["handleIn"].toObject());
        if (j.contains("handleOut") && j["handleOut"].isObject())
            cp.handleOut = Vec2::fromJson(j["handleOut"].toObject());
        cp.type = typeFromStr(j["type"].toString("smooth"));
        if (j.contains("segmentMeta") && j["segmentMeta"].isObject())
            cp.segmentMeta = SegmentMetadata::fromJson(j["segmentMeta"].toObject());
        return cp;
    }
};

// RoadProfile — SCANeR-style road profile
struct RoadProfile {
    QString type = "city_2x1";  // profile type key
    QString surfaceTexture;
    QString markingTexture;
    double laneWidth = 3.5;
    bool hasSidewalk = false;
    bool hasCurb = false;
    int leftLanes = 1;       // lanes on left side
    int rightLanes = 1;      // lanes on right side
    int leftOffsetX2 = 0;    // left offset (×2)
    int rightOffsetX2 = 0;   // right offset (×2)
    double speedLimit = 50;  // km/h
    QString description;

    QJsonObject toJson() const {
        return {
            {"type", type}, {"surfaceTexture", surfaceTexture},
            {"markingTexture", markingTexture}, {"laneWidth", laneWidth},
            {"hasSidewalk", hasSidewalk}, {"hasCurb", hasCurb},
            {"leftLanes", leftLanes}, {"rightLanes", rightLanes},
            {"leftOffsetX2", leftOffsetX2}, {"rightOffsetX2", rightOffsetX2},
            {"speedLimit", speedLimit}, {"description", description}
        };
    }

    static RoadProfile fromJson(const QJsonObject& j) {
        RoadProfile p;
        p.type = j["type"].toString("city_2x1");
        p.surfaceTexture = j["surfaceTexture"].toString();
        p.markingTexture = j["markingTexture"].toString();
        p.laneWidth = j["laneWidth"].toDouble(3.5);
        p.hasSidewalk = j["hasSidewalk"].toBool(false);
        p.hasCurb = j["hasCurb"].toBool(false);
        p.leftLanes = j["leftLanes"].toInt(1);
        p.rightLanes = j["rightLanes"].toInt(1);
        p.leftOffsetX2 = j["leftOffsetX2"].toInt(0);
        p.rightOffsetX2 = j["rightOffsetX2"].toInt(0);
        p.speedLimit = j["speedLimit"].toDouble(50);
        p.description = j["description"].toString();
        return p;
    }
};

// ─── SCANeR-style Road Profile Catalog ──────────────────────
// Comprehensive set of predefined road profiles based on
// SCANeR Studio .rndProfile files and real-world road standards.
struct RoadProfileCatalog {
    // Get all predefined profiles as a map (key → profile)
    static QMap<QString, RoadProfile> all() {
        return {
            // ─── Urban / City ───────────────────────────
            {"city_2x1", {
                "city_2x1", "asphalt", "marking", 3.5,
                true, true, 1, 1, 0, 0, 50,
                "City road — 1 lane each way, sidewalk, curb"
            }},
            {"city_2x2", {
                "city_2x2", "asphalt", "marking", 3.5,
                true, true, 2, 2, 0, 0, 50,
                "City road — 2 lanes each way, sidewalk, curb"
            }},
            {"city_2x3", {
                "city_2x3", "asphalt", "marking", 3.5,
                true, true, 3, 3, 0, 0, 60,
                "City boulevard — 3 lanes each way, sidewalk, curb"
            }},
            {"city_oneway_1x2", {
                "city_oneway_1x2", "asphalt", "marking", 3.5,
                true, true, 0, 2, 0, 0, 40,
                "City one-way — 2 lanes, sidewalk, curb"
            }},
            {"city_oneway_1x3", {
                "city_oneway_1x3", "asphalt", "marking", 3.5,
                true, true, 0, 3, 0, 0, 40,
                "City one-way — 3 lanes, sidewalk, curb"
            }},

            // ─── Rural / Country ────────────────────────
            {"country_2x1", {
                "country_2x1", "asphalt", "marking", 3.5,
                false, false, 1, 1, 0, 0, 80,
                "Country road — 1 lane each way, no sidewalk"
            }},
            {"country_2x2", {
                "country_2x2", "asphalt", "marking", 3.5,
                false, false, 2, 2, 0, 0, 80,
                "Country road — 2 lanes each way, no sidewalk"
            }},
            {"rural_narrow_2x1", {
                "rural_narrow_2x1", "asphalt", "marking", 3.0,
                false, false, 1, 1, 0, 0, 70,
                "Rural narrow — 1 lane each way, 3.0m lanes"
            }},

            // ─── Highway / Motorway ─────────────────────
            {"highway_2x2", {
                "highway_2x2", "asphalt", "marking", 3.75,
                false, false, 2, 2, 0, 0, 120,
                "Highway — 2 lanes each way, 3.75m lanes"
            }},
            {"highway_2x3", {
                "highway_2x3", "asphalt", "marking", 3.75,
                false, false, 3, 3, 0, 0, 120,
                "Highway — 3 lanes each way, 3.75m lanes"
            }},
            {"highway_2x4", {
                "highway_2x4", "asphalt", "marking", 3.75,
                false, false, 4, 4, 0, 0, 120,
                "Major highway — 4 lanes each way, 3.75m lanes"
            }},

            // ─── Ramp / Interchange ─────────────────────
            {"ramp_1x1", {
                "ramp_1x1", "asphalt", "marking", 4.0,
                false, false, 1, 0, 0, 0, 60,
                "Exit ramp — 1 lane, 4.0m width"
            }},
            {"ramp_1x2", {
                "ramp_1x2", "asphalt", "marking", 4.0,
                false, false, 2, 0, 0, 0, 50,
                "Wide ramp — 2 lanes, 4.0m width"
            }},

            // ─── Roundabout / Circle ────────────────────
            {"roundabout_1x1", {
                "roundabout_1x1", "asphalt", "marking", 4.5,
                true, true, 1, 0, 0, 0, 30,
                "Roundabout — 1 lane, 4.5m width, sidewalk"
            }},
            {"roundabout_2x1", {
                "roundabout_2x1", "asphalt", "marking", 4.5,
                true, true, 2, 0, 0, 0, 25,
                "Roundabout — 2 lanes, 4.5m width, sidewalk"
            }},

            // ─── Parking / Service ──────────────────────
            {"parking_1x1", {
                "parking_1x1", "asphalt", "marking", 3.0,
                true, true, 1, 0, 0, 0, 15,
                "Parking road — 1 lane, 3.0m, sidewalk"
            }},
            {"service_1x1", {
                "service_1x1", "asphalt", "marking", 3.0,
                false, false, 1, 0, 0, 0, 20,
                "Service road — 1 lane, 3.0m"
            }},

            // ─── Asymmetric / Divided ───────────────────
            {"divided_2x3", {
                "divided_2x3", "asphalt", "marking", 3.5,
                false, false, 2, 3, 0, 0, 80,
                "Asymmetric divided — 2 left, 3 right"
            }},
            {"divided_1x2", {
                "divided_1x2", "asphalt", "marking", 3.5,
                false, false, 1, 2, 0, 0, 60,
                "Asymmetric divided — 1 left, 2 right"
            }},

            // ─── Custom ─────────────────────────────────
            {"custom", {
                "custom", "asphalt", "marking", 3.5,
                false, false, 1, 1, 0, 0, 50,
                "Custom — user-defined configuration"
            }},
        };
    }

    // Get a list of (key, description) pairs for UI dropdowns
    static QStringList profileNames() {
        QMap<QString, RoadProfile> profiles = all();
        QStringList names;
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            names << it.key();
        }
        return names;
    }

    // Get profile by key, returns custom if not found
    static RoadProfile get(const QString& key) {
        auto profiles = all();
        if (profiles.contains(key)) return profiles[key];
        return profiles["custom"];
    }

    // Get human-readable label for dropdown
    static QString label(const QString& key) {
        auto profiles = all();
        if (!profiles.contains(key)) return "Custom";
        return key + " — " + profiles[key].description;
    }
};

// Road — a road with control points
struct Road {
    QString id;
    QString name;
    QList<ControlPoint> points;
    double width = 7.0;       // total road width in meters
    int laneCount = 2;
    QString color = "#4488cc";
    RoadProfile profile;
    QString startIntersectionId;
    QString endIntersectionId;
    int formatVersion = 2;

    bool isNull() const { return id.isEmpty(); }

    QJsonObject toJson() const {
        QJsonArray pts;
        for (const auto& p : points) pts.append(p.toJson());
        QJsonObject j;
        j["id"] = id;
        j["name"] = name;
        j["points"] = pts;
        j["width"] = width;
        j["laneCount"] = laneCount;
        j["color"] = color;
        j["profile"] = profile.toJson();
        if (!startIntersectionId.isEmpty()) j["startIntersectionId"] = startIntersectionId;
        if (!endIntersectionId.isEmpty()) j["endIntersectionId"] = endIntersectionId;
        j["formatVersion"] = formatVersion;
        return j;
    }

    static Road fromJson(const QJsonObject& j) {
        Road r;
        r.id = j["id"].toString();
        r.name = j["name"].toString();
        const QJsonArray pts = j["points"].toArray();
        for (const auto& v : pts) r.points.append(ControlPoint::fromJson(v.toObject()));
        r.width = j["width"].toDouble(7.0);
        r.laneCount = j["laneCount"].toInt(2);
        r.color = j["color"].toString("#4488cc");
        r.profile = RoadProfile::fromJson(j["profile"].toObject());
        r.startIntersectionId = j["startIntersectionId"].toString();
        r.endIntersectionId = j["endIntersectionId"].toString();
        r.formatVersion = j["formatVersion"].toInt(2);
        return r;
    }
};

// Selection — what is currently selected
struct Selection {
    QString roadId;
    QList<int> pointIndices;
    QString handle; // "in", "out", or empty

    bool isEmpty() const { return roadId.isEmpty(); }
    void clear() { roadId.clear(); pointIndices.clear(); handle.clear(); }
};

// HistorySnapshot — for undo/redo
struct HistorySnapshot {
    QList<Road> roads;
    QString description;
    qint64 timestamp = 0;
};

// Tool — available tools (LaneMaker-style edit modes)
enum class Tool {
    Select,     // Drag/pan mode
    Road,       // Create road mode
    Lane,       // Create lanes (ramps/splits)
    Destroy,    // Destroy road segment
    Modify      // Modify lane profile
};

// ViewMode — 2D top or 3D perspective
enum class ViewMode { Top, Perspective };

// StagedGeometry — a confirmed geometry segment in the multi-click workflow
struct StagedGeometry {
    Point2D startPos;
    Vec2 startDir;
    Point2D endPos;
    Vec2 endDir;
    double length = 0;
    // Sampled points along the geometry
    QList<Point2D> samples;
};

// LanePlan — lane configuration for one side of the road
struct LanePlan {
    int laneCount = 1;
    double offsetx2 = 0;  // 2x offset from center

    bool operator==(const LanePlan& o) const {
        return laneCount == o.laneCount && std::abs(offsetx2 - o.offsetx2) < 1e-6;
    }
    bool operator!=(const LanePlan& o) const { return !(*this == o); }
};

// LaneConfig — full lane configuration (left + right)
struct LaneConfig {
    LanePlan left;
    LanePlan right;
    bool roadMode = true;  // true=road, false=lane creation
};

// MeshData — triangle mesh from the C++ engine
struct MeshData {
    QList<float> positions;  // x,y,z interleaved
    QList<float> normals;    // nx,ny,nz interleaved
    QList<float> uvs;        // u,v interleaved
    QList<unsigned int> indices;
    int vertexCount = 0;

    bool isEmpty() const { return positions.isEmpty(); }
};

// RoadMeshes — lane and roadmark meshes for a road
struct RoadMeshes {
    MeshData laneMesh;
    MeshData roadmarkMesh;
};

// Junction — detected junction between roads
struct Junction {
    QString junctionId;
    QStringList roadIds;
    Point2D center;
    double overlap = 0;
};

} // namespace roads
