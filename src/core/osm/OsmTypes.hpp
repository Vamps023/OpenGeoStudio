#pragma once

// ============================================================
// OsmTypes — OpenStreetMap data model
// ============================================================
//
// Core OSM primitives: Node, Way, Relation, Tag.
// Mirrors the OSM XML/PBF data model.
//
// This is the raw imported data — before any road processing.
// It is kept separate from the road engine model (RoadV2, etc.)
// so that the original OSM data is always available for inspection
// and re-processing with different settings.
//

#include <QString>
#include <QHash>
#include <QMap>
#include <QList>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace osm {

// ─── Tag — key/value pair ───
struct Tag {
    QString key;
    QString value;

    QJsonObject toJson() const { return {{"key", key}, {"value", value}}; }
    static Tag fromJson(const QJsonObject& j) {
        return {j["key"].toString(), j["value"].toString()};
    }
};

// ─── Node — a point on the Earth ───
struct Node {
    qint64 id = 0;
    double lat = 0.0;
    double lon = 0.0;
    double ele = 0.0;       // elevation (meters), 0 if not in OSM
    bool hasElevation = false;
    QList<Tag> tags;

    // Local projected coordinates (filled by CoordinateConverter)
    double localX = 0.0;
    double localY = 0.0;
    bool projected = false;

    // ─── Tag helpers ───
    QString tag(const QString& key) const {
        for (const auto& t : tags)
            if (t.key == key) return t.value;
        return {};
    }

    bool hasTag(const QString& key) const {
        for (const auto& t : tags)
            if (t.key == key) return true;
        return false;
    }

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        j["lat"] = lat;
        j["lon"] = lon;
        if (hasElevation) j["ele"] = ele;
        if (projected) {
            j["localX"] = localX;
            j["localY"] = localY;
        }
        if (!tags.isEmpty()) {
            QJsonArray arr;
            for (const auto& t : tags) arr.append(t.toJson());
            j["tags"] = arr;
        }
        return j;
    }

    static Node fromJson(const QJsonObject& j) {
        Node n;
        n.id = j["id"].toVariant().toLongLong();
        n.lat = j["lat"].toDouble();
        n.lon = j["lon"].toDouble();
        n.ele = j["ele"].toDouble(0);
        n.hasElevation = j.contains("ele");
        n.localX = j["localX"].toDouble(0);
        n.localY = j["localY"].toDouble(0);
        n.projected = j.contains("localX");
        if (j.contains("tags")) {
            for (const auto& v : j["tags"].toArray())
                n.tags.append(Tag::fromJson(v.toObject()));
        }
        return n;
    }
};

// ─── Way — an ordered list of node references ───
struct Way {
    qint64 id = 0;
    QList<qint64> nodeRefs;   // references to Node.id
    QList<Tag> tags;
    bool visible = true;

    // ─── Tag helpers ───
    QString tag(const QString& key) const {
        for (const auto& t : tags)
            if (t.key == key) return t.value;
        return {};
    }

    bool hasTag(const QString& key) const {
        for (const auto& t : tags)
            if (t.key == key) return true;
        return false;
    }

    bool isHighway() const { return hasTag("highway"); }

    QString highwayType() const { return tag("highway"); }

    bool isRailway() const { return hasTag("railway"); }

    QString railwayType() const { return tag("railway"); }

    bool isRoundabout() const {
        return tag("junction") == "roundabout" ||
               tag("junction") == "circular";
    }

    bool isBridge() const {
        QString b = tag("bridge");
        return !b.isEmpty() && b != "no";
    }

    bool isTunnel() const {
        QString t = tag("tunnel");
        return !t.isEmpty() && t != "no";
    }

    bool isOneWay() const {
        QString ow = tag("oneway");
        return ow == "yes" || ow == "true" || ow == "1";
    }

    bool isOneWayReverse() const {
        return tag("oneway") == "-1";
    }

    bool isArea() const {
        return tag("area") == "yes";
    }

    int lanes() const {
        bool ok;
        int n = tag("lanes").toInt(&ok);
        return ok ? n : -1;
    }

    int lanesForward() const {
        bool ok;
        int n = tag("lanes:forward").toInt(&ok);
        return ok ? n : -1;
    }

    int lanesBackward() const {
        bool ok;
        int n = tag("lanes:backward").toInt(&ok);
        return ok ? n : -1;
    }

    double maxspeed() const {
        QString s = tag("maxspeed");
        if (s.isEmpty()) return -1;
        if (s.endsWith("mph")) {
            bool ok;
            double mph = s.left(s.length() - 3).trimmed().toDouble(&ok);
            return ok ? mph * 1.609344 : -1;
        }
        bool ok;
        double v = s.toDouble(&ok);
        return ok ? v : -1;
    }

    double width() const {
        QString w = tag("width");
        if (w.isEmpty()) return -1;
        // OSM width can be in meters (e.g., "7.5")
        bool ok;
        double v = w.toDouble(&ok);
        return ok ? v : -1;
    }

    QString name() const { return tag("name"); }
    QString ref() const { return tag("ref"); }
    QString surface() const { return tag("surface"); }
    QString layer() const { return tag("layer"); }

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        QJsonArray refs;
        for (qint64 r : nodeRefs) refs.append(qint64(r));
        j["nodeRefs"] = refs;
        j["visible"] = visible;
        if (!tags.isEmpty()) {
            QJsonArray arr;
            for (const auto& t : tags) arr.append(t.toJson());
            j["tags"] = arr;
        }
        return j;
    }

    static Way fromJson(const QJsonObject& j) {
        Way w;
        w.id = j["id"].toVariant().toLongLong();
        w.visible = j["visible"].toBool(true);
        for (const auto& v : j["nodeRefs"].toArray())
            w.nodeRefs.append(v.toVariant().toLongLong());
        if (j.contains("tags")) {
            for (const auto& v : j["tags"].toArray())
                w.tags.append(Tag::fromJson(v.toObject()));
        }
        return w;
    }
};

// ─── Relation — a collection of members with a type ───
struct RelationMember {
    enum class Type { Node, Way, Relation };
    qint64 ref = 0;
    Type type = Type::Node;
    QString role;

    QJsonObject toJson() const {
        QJsonObject j;
        j["ref"] = ref;
        j["type"] = type == Type::Node ? "node" :
                    type == Type::Way ? "way" : "relation";
        j["role"] = role;
        return j;
    }

    static RelationMember fromJson(const QJsonObject& j) {
        RelationMember m;
        m.ref = j["ref"].toVariant().toLongLong();
        QString t = j["type"].toString("node");
        m.type = t == "way" ? Type::Way :
                 t == "relation" ? Type::Relation : Type::Node;
        m.role = j["role"].toString();
        return m;
    }
};

struct Relation {
    qint64 id = 0;
    QList<RelationMember> members;
    QList<Tag> tags;

    QString tag(const QString& key) const {
        for (const auto& t : tags)
            if (t.key == key) return t.value;
        return {};
    }

    QJsonObject toJson() const {
        QJsonObject j;
        j["id"] = id;
        QJsonArray arr;
        for (const auto& m : members) arr.append(m.toJson());
        j["members"] = arr;
        if (!tags.isEmpty()) {
            QJsonArray ta;
            for (const auto& t : tags) ta.append(t.toJson());
            j["tags"] = ta;
        }
        return j;
    }

    static Relation fromJson(const QJsonObject& j) {
        Relation r;
        r.id = j["id"].toVariant().toLongLong();
        for (const auto& v : j["members"].toArray())
            r.members.append(RelationMember::fromJson(v.toObject()));
        if (j.contains("tags")) {
            for (const auto& v : j["tags"].toArray())
                r.tags.append(Tag::fromJson(v.toObject()));
        }
        return r;
    }
};

// ─── OsmData — the complete imported OSM dataset ───
struct OsmData {
    std::unordered_map<qint64, Node> nodes;
    std::unordered_map<qint64, Way> ways;
    std::unordered_map<qint64, Relation> relations;

    // Bounding box
    double minLat = 0, minLon = 0, maxLat = 0, maxLon = 0;
    bool hasBounds = false;

    // Source metadata
    QString sourceFile;
    QString sourceVersion;
    QString generator;
    QString timestamp;

    // ─── Queries ───

    int nodeCount() const { return int(nodes.size()); }
    int wayCount() const { return int(ways.size()); }
    int relationCount() const { return int(relations.size()); }

    // Get all highway ways
    std::vector<const Way*> highwayWays() const {
        std::vector<const Way*> result;
        for (const auto& [id, w] : ways)
            if (w.isHighway() && w.visible && !w.isArea())
                result.push_back(&w);
        return result;
    }

    // Get node by id (returns nullptr if not found)
    const Node* getNode(qint64 id) const {
        auto it = nodes.find(id);
        return it != nodes.end() ? &it->second : nullptr;
    }

    // Get way by id
    const Way* getWay(qint64 id) const {
        auto it = ways.find(id);
        return it != ways.end() ? &it->second : nullptr;
    }

    // ─── Serialization ───

    QJsonObject toJson() const {
        QJsonObject j;
        j["sourceFile"] = sourceFile;
        j["sourceVersion"] = sourceVersion;
        j["generator"] = generator;
        j["timestamp"] = timestamp;
        if (hasBounds) {
            QJsonObject b;
            b["minLat"] = minLat; b["minLon"] = minLon;
            b["maxLat"] = maxLat; b["maxLon"] = maxLon;
            j["bounds"] = b;
        }

        // Nodes
        QJsonArray nodeArr;
        for (const auto& [id, n] : nodes) nodeArr.append(n.toJson());
        j["nodes"] = nodeArr;

        // Ways
        QJsonArray wayArr;
        for (const auto& [id, w] : ways) wayArr.append(w.toJson());
        j["ways"] = wayArr;

        // Relations
        QJsonArray relArr;
        for (const auto& [id, r] : relations) relArr.append(r.toJson());
        j["relations"] = relArr;

        return j;
    }

    static OsmData fromJson(const QJsonObject& j) {
        OsmData d;
        d.sourceFile = j["sourceFile"].toString();
        d.sourceVersion = j["sourceVersion"].toString();
        d.generator = j["generator"].toString();
        d.timestamp = j["timestamp"].toString();

        if (j.contains("bounds")) {
            QJsonObject b = j["bounds"].toObject();
            d.minLat = b["minLat"].toDouble();
            d.minLon = b["minLon"].toDouble();
            d.maxLat = b["maxLat"].toDouble();
            d.maxLon = b["maxLon"].toDouble();
            d.hasBounds = true;
        }

        for (const auto& v : j["nodes"].toArray()) {
            Node n = Node::fromJson(v.toObject());
            d.nodes[n.id] = n;
        }
        for (const auto& v : j["ways"].toArray()) {
            Way w = Way::fromJson(v.toObject());
            d.ways[w.id] = w;
        }
        for (const auto& v : j["relations"].toArray()) {
            Relation r = Relation::fromJson(v.toObject());
            d.relations[r.id] = r;
        }

        return d;
    }
};

} // namespace osm
