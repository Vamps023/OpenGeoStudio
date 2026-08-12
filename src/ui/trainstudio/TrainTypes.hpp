#pragma once

// ============================================================
// TrainTypes — Railway data model types
// ============================================================
//
// Mirrors modules/train-studio/shared/types.ts.
// Simplified Track model with control points (no segment metadata).
//

#include <QString>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <optional>

namespace trains {

struct ControlPoint {
    QString id;
    double lat = 0, lon = 0, z = 0;

    QJsonObject toJson() const {
        return {{"id", id}, {"lat", lat}, {"lon", lon}, {"z", z}};
    }
    static ControlPoint fromJson(const QJsonObject& j) {
        return {j["id"].toString(), j["lat"].toDouble(), j["lon"].toDouble(), j["z"].toDouble(0)};
    }
};

struct Track {
    QString id;
    QString name;
    QList<ControlPoint> points;
    double gauge = 1.435;  // standard gauge in meters
    QString color = "#aa6622";
    enum class Kind { Line, Arc } kind = Kind::Line;
    std::optional<double> arcRadius;  // only for kind=Arc

    bool isNull() const { return id.isEmpty(); }

    QString kindStr() const { return kind == Kind::Arc ? "arc" : "line"; }
    static Kind kindFromStr(const QString& s) { return s == "arc" ? Kind::Arc : Kind::Line; }

    QJsonObject toJson() const {
        QJsonArray pts;
        for (const auto& p : points) pts.append(p.toJson());
        QJsonObject j;
        j["id"] = id;
        j["name"] = name;
        j["points"] = pts;
        j["gauge"] = gauge;
        j["color"] = color;
        j["kind"] = kindStr();
        if (arcRadius) j["arcRadius"] = *arcRadius;
        return j;
    }

    static Track fromJson(const QJsonObject& j) {
        Track t;
        t.id = j["id"].toString();
        t.name = j["name"].toString();
        const QJsonArray pts = j["points"].toArray();
        for (const auto& v : pts) t.points.append(ControlPoint::fromJson(v.toObject()));
        t.gauge = j["gauge"].toDouble(1.435);
        t.color = j["color"].toString("#aa6622");
        t.kind = kindFromStr(j["kind"].toString("line"));
        if (j.contains("arcRadius")) t.arcRadius = j["arcRadius"].toDouble();
        return t;
    }
};

struct Selection {
    QString trackId;
    QList<int> pointIndices;

    bool isEmpty() const { return trackId.isEmpty(); }
    void clear() { trackId.clear(); pointIndices.clear(); }
};

struct HistorySnapshot {
    QList<Track> tracks;
    QString description;
    qint64 timestamp = 0;
};

enum class Tool { Select, Line, Arc };

} // namespace trains
