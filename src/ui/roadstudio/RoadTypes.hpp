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
    QString type = "city_2x1";  // city_2x1, city_2x2, country_2x1, highway_2x3, custom
    QString surfaceTexture;
    QString markingTexture;
    double laneWidth = 3.5;
    bool hasSidewalk = false;
    bool hasCurb = false;

    QJsonObject toJson() const {
        return {
            {"type", type}, {"surfaceTexture", surfaceTexture},
            {"markingTexture", markingTexture}, {"laneWidth", laneWidth},
            {"hasSidewalk", hasSidewalk}, {"hasCurb", hasCurb}
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
        return p;
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
