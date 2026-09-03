#pragma once

#include "../osm/CoordinateConverter.hpp"
#include "../osm/RoadNetworkBuilder.hpp"

#include <QFile>
#include <QMap>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

namespace lanelet2io {

struct Node {
    qint64 id = 0;
    double lat = 0;
    double lon = 0;
    double ele = 0;
    bool hasElevation = false;
    QMap<QString, QString> tags;
};

struct Way {
    qint64 id = 0;
    std::vector<qint64> nodes;
    QMap<QString, QString> tags;
};

struct RelationMember {
    QString type;
    qint64 ref = 0;
    QString role;
};

struct Relation {
    qint64 id = 0;
    std::vector<RelationMember> members;
    QMap<QString, QString> tags;
};

struct Map {
    std::map<qint64, Node> nodes;
    std::map<qint64, Way> ways;
    std::map<qint64, Relation> relations;
    QString projection;
};

class Lanelet2IO {
public:
    static bool load(const QString& path, Map& map, QString* error = nullptr)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (error) *error = QString("Cannot read Lanelet2 file: %1").arg(path);
            return false;
        }
        QXmlStreamReader xml(&file);
        map = {};
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            if (xml.name() == QLatin1String("meta")) {
                map.projection = xml.attributes().value("projection").toString();
            } else if (xml.name() == QLatin1String("node")) {
                Node node;
                node.id = xml.attributes().value("id").toLongLong();
                node.lat = xml.attributes().value("lat").toDouble();
                node.lon = xml.attributes().value("lon").toDouble();
                readTags(xml, "node", node.tags);
                if (node.tags.contains("ele")) {
                    node.ele = node.tags["ele"].toDouble();
                    node.hasElevation = true;
                }
                map.nodes[node.id] = node;
            } else if (xml.name() == QLatin1String("way")) {
                Way way;
                way.id = xml.attributes().value("id").toLongLong();
                while (!(xml.isEndElement() && xml.name() == QLatin1String("way")) && !xml.atEnd()) {
                    xml.readNext();
                    if (!xml.isStartElement()) continue;
                    if (xml.name() == QLatin1String("nd"))
                        way.nodes.push_back(xml.attributes().value("ref").toLongLong());
                    else if (xml.name() == QLatin1String("tag"))
                        way.tags[xml.attributes().value("k").toString()] =
                            xml.attributes().value("v").toString();
                }
                map.ways[way.id] = way;
            } else if (xml.name() == QLatin1String("relation")) {
                Relation relation;
                relation.id = xml.attributes().value("id").toLongLong();
                while (!(xml.isEndElement() && xml.name() == QLatin1String("relation")) && !xml.atEnd()) {
                    xml.readNext();
                    if (!xml.isStartElement()) continue;
                    if (xml.name() == QLatin1String("member")) {
                        relation.members.push_back({xml.attributes().value("type").toString(),
                            xml.attributes().value("ref").toLongLong(),
                            xml.attributes().value("role").toString()});
                    } else if (xml.name() == QLatin1String("tag")) {
                        relation.tags[xml.attributes().value("k").toString()] =
                            xml.attributes().value("v").toString();
                    }
                }
                map.relations[relation.id] = relation;
            }
        }
        if (xml.hasError()) {
            if (error) *error = xml.errorString();
            return false;
        }
        return true;
    }

    static bool save(const QString& path, const Map& map, QString* error = nullptr)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error) *error = QString("Cannot write Lanelet2 file: %1").arg(path);
            return false;
        }
        QXmlStreamWriter xml(&file);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeStartElement("osm");
        xml.writeAttribute("version", "0.6");
        xml.writeAttribute("generator", "OpenGeoStudio");
        if (!map.projection.isEmpty()) {
            xml.writeEmptyElement("meta");
            xml.writeAttribute("projection", map.projection);
        }
        for (const auto& [id, node] : map.nodes) {
            xml.writeStartElement("node");
            xml.writeAttribute("id", QString::number(id));
            xml.writeAttribute("visible", "true");
            xml.writeAttribute("lat", QString::number(node.lat, 'f', 10));
            xml.writeAttribute("lon", QString::number(node.lon, 'f', 10));
            auto tags = node.tags;
            if (node.hasElevation) tags["ele"] = QString::number(node.ele, 'f', 3);
            writeTags(xml, tags);
            xml.writeEndElement();
        }
        for (const auto& [id, way] : map.ways) {
            xml.writeStartElement("way");
            xml.writeAttribute("id", QString::number(id));
            xml.writeAttribute("visible", "true");
            for (qint64 node : way.nodes) {
                xml.writeEmptyElement("nd");
                xml.writeAttribute("ref", QString::number(node));
            }
            writeTags(xml, way.tags);
            xml.writeEndElement();
        }
        for (const auto& [id, relation] : map.relations) {
            xml.writeStartElement("relation");
            xml.writeAttribute("id", QString::number(id));
            xml.writeAttribute("visible", "true");
            for (const auto& member : relation.members) {
                xml.writeEmptyElement("member");
                xml.writeAttribute("type", member.type);
                xml.writeAttribute("ref", QString::number(member.ref));
                xml.writeAttribute("role", member.role);
            }
            writeTags(xml, relation.tags);
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndDocument();
        return true;
    }

    static Map fromRoadNetwork(const osm::RoadNetworkBuilder::Result& network,
                               const osm::CoordinateConverter& converter,
                               int samplesPerRoad = 32)
    {
        Map map;
        map.projection = QString::fromStdString(converter.projString());
        qint64 nextNode = 1;
        qint64 nextWay = 1;
        qint64 nextRelation = 1;
        for (const auto& road : network.roads) {
            if (road.totalLength() <= 0 || road.numLaneSections() == 0) continue;
            const auto& section = road.laneSection(0);
            std::vector<geo::Lane> lanes = section.lanes();
            std::sort(lanes.begin(), lanes.end(), [](const auto& a, const auto& b) {
                return a.id < b.id;
            });
            double leftOffset = 0;
            double rightOffset = 0;
            for (const auto& lane : lanes) {
                if (lane.id == 0 || lane.type == geo::LaneType::None || lane.type == geo::LaneType::Border)
                    continue;
                const double width = std::max(0.1, lane.widthAt(0));
                double innerOffset;
                double outerOffset;
                bool reverse = lane.id < 0;
                if (lane.id > 0) {
                    innerOffset = -rightOffset;
                    rightOffset += width;
                    outerOffset = -rightOffset;
                } else {
                    innerOffset = leftOffset;
                    leftOffset += width;
                    outerOffset = leftOffset;
                }
                Way inner;
                inner.id = nextWay++;
                inner.tags = {{"type", "line_thin"}, {"subtype", "solid"}};
                Way outer;
                outer.id = nextWay++;
                outer.tags = {{"type", "line_thin"}, {"subtype", "solid"}};
                const int sampleCount = std::max(2, samplesPerRoad);
                for (int i = 0; i <= sampleCount; ++i) {
                    const int sample = reverse ? sampleCount - i : i;
                    const double s = road.totalLength() * sample / sampleCount;
                    const auto center = road.geometry().positionAt(s);
                    const auto normal = road.geometry().normalAt(s);
                    auto addNode = [&](double offset) {
                        Node node;
                        node.id = nextNode++;
                        const double x = center.x + normal.x * offset;
                        const double y = center.y + normal.y * offset;
                        converter.toGeo(x, y, node.lat, node.lon);
                        map.nodes[node.id] = node;
                        return node.id;
                    };
                    inner.nodes.push_back(addNode(innerOffset));
                    outer.nodes.push_back(addNode(outerOffset));
                }
                map.ways[inner.id] = inner;
                map.ways[outer.id] = outer;
                Relation relation;
                relation.id = nextRelation++;
                if (lane.id > 0) {
                    relation.members.push_back({"way", inner.id, "left"});
                    relation.members.push_back({"way", outer.id, "right"});
                } else {
                    relation.members.push_back({"way", outer.id, "left"});
                    relation.members.push_back({"way", inner.id, "right"});
                }
                relation.tags = {{"type", "lanelet"}, {"subtype", "road"},
                                 {"location", "urban"}, {"one_way", "yes"},
                                 {"source_road", QString::fromStdString(road.id)}};
                if (const auto* metadata = osm::RoadNetworkBuilder::getMetadata(road.id);
                    metadata && metadata->maxspeed > 0)
                    relation.tags["speed_limit"] = QString::number(metadata->maxspeed);
                map.relations[relation.id] = relation;
            }
        }
        return map;
    }

    static osm::RoadNetworkBuilder::Result toRoadNetwork(const Map& map,
                                                          const osm::CoordinateConverter& converter)
    {
        osm::RoadNetworkBuilder::Result result;
        qint64 nextNetworkNode = 1;
        std::map<std::pair<long long, long long>, qint64> endpointIds;
        for (const auto& [relationId, relation] : map.relations) {
            if (relation.tags.value("type") != "lanelet") continue;
            qint64 leftId = 0;
            qint64 rightId = 0;
            for (const auto& member : relation.members) {
                if (member.role == "left") leftId = member.ref;
                else if (member.role == "right") rightId = member.ref;
            }
            auto leftIt = map.ways.find(leftId);
            auto rightIt = map.ways.find(rightId);
            if (leftIt == map.ways.end() || rightIt == map.ways.end()) continue;
            const size_t count = std::min(leftIt->second.nodes.size(), rightIt->second.nodes.size());
            if (count < 2) continue;
            std::vector<geo::Point2D> centers;
            double widthSum = 0;
            for (size_t i = 0; i < count; ++i) {
                auto leftNode = map.nodes.find(leftIt->second.nodes[i]);
                auto rightNode = map.nodes.find(rightIt->second.nodes[i]);
                if (leftNode == map.nodes.end() || rightNode == map.nodes.end()) continue;
                double lx, ly, rx, ry;
                converter.toLocal(leftNode->second.lat, leftNode->second.lon, lx, ly);
                converter.toLocal(rightNode->second.lat, rightNode->second.lon, rx, ry);
                centers.emplace_back((lx + rx) * 0.5, (ly + ry) * 0.5);
                widthSum += std::hypot(lx - rx, ly - ry);
            }
            if (centers.size() < 2) continue;
            geo::RoadV2 road;
            road.id = "lanelet_" + std::to_string(relationId);
            road.name = road.id;
            road.profileName = "lanelet2";
            for (size_t i = 1; i < centers.size(); ++i)
                if (centers[i - 1].distanceTo(centers[i]) > 0.01)
                    road.addSegment<geo::LineSegment>(centers[i - 1], centers[i]);
            if (road.numSegments() == 0) continue;
            geo::Lane lane;
            lane.id = 1;
            lane.type = geo::LaneType::Driving;
            lane.width = geo::Polynomial3(widthSum / centers.size());
            geo::LaneSection section;
            section.addLane(lane);
            road.addLaneSection(section);
            road.laneCount = 1;
            road.width = lane.widthAt(0);

            auto endpointId = [&](const geo::Point2D& point) {
                const auto key = std::make_pair(std::llround(point.x * 100),
                                                std::llround(point.y * 100));
                auto it = endpointIds.find(key);
                if (it != endpointIds.end()) return it->second;
                const qint64 id = nextNetworkNode++;
                endpointIds[key] = id;
                osm::NetworkNode node;
                node.osmNodeId = id;
                node.x = point.x;
                node.y = point.y;
                converter.toGeo(point.x, point.y, node.lat, node.lon);
                result.nodes[id] = node;
                return id;
            };
            const qint64 startId = endpointId(centers.front());
            const qint64 endId = endpointId(centers.back());
            result.nodes[startId].roadIds.push_back(QString::fromStdString(road.id));
            result.nodes[endId].roadIds.push_back(QString::fromStdString(road.id));
            result.roads.push_back(std::move(road));
        }
        for (auto& [id, node] : result.nodes) {
            node.isJunction = node.roadIds.size() >= 3;
            node.isEndPoint = node.roadIds.size() == 1;
            if (node.isJunction) result.junctionNodeIds.push_back(id);
            if (node.isEndPoint) result.endPointNodeIds.push_back(id);
        }
        result.roadsCreated = int(result.roads.size());
        result.nodesCreated = int(result.nodes.size());
        result.junctionsDetected = int(result.junctionNodeIds.size());
        result.endPointsDetected = int(result.endPointNodeIds.size());
        for (const auto& road : result.roads) result.segmentsCreated += road.numSegments();
        return result;
    }

private:
    static void readTags(QXmlStreamReader& xml, const QString& end,
                         QMap<QString, QString>& tags)
    {
        while (!(xml.isEndElement() && xml.name() == end) && !xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("tag"))
                tags[xml.attributes().value("k").toString()] =
                    xml.attributes().value("v").toString();
        }
    }

    static void writeTags(QXmlStreamWriter& xml, const QMap<QString, QString>& tags)
    {
        for (auto it = tags.cbegin(); it != tags.cend(); ++it) {
            xml.writeEmptyElement("tag");
            xml.writeAttribute("k", it.key());
            xml.writeAttribute("v", it.value());
        }
    }
};

} // namespace lanelet2io
