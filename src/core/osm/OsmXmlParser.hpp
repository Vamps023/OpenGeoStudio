#pragma once

// ============================================================
// OsmXmlParser — Parse .osm (XML) files
// ============================================================
//
// Parses OSM XML format into OsmData.
// No external dependencies — uses QXmlStreamReader.
//
// OSM XML format:
//   <osm version="0.6" generator="...">
//     <bounds minlat="..." minlon="..." maxlat="..." maxlon="..."/>
//     <node id="..." lat="..." lon="..." version="...">
//       <tag k="highway" v="traffic_signals"/>
//     </node>
//     <way id="..." version="...">
//       <nd ref="123456"/>
//       <nd ref="123457"/>
//       <tag k="highway" v="primary"/>
//       <tag k="name" v="Main Street"/>
//     </way>
//     <relation id="...">
//       <member type="way" ref="123" role="from"/>
//       <member type="node" ref="456" role="via"/>
//       <tag k="type" v="restriction"/>
//       <tag k="restriction" v="no_left_turn"/>
//     </relation>
//   </osm>
//

#include "OsmTypes.hpp"
#include <QFile>
#include <QXmlStreamReader>
#include <QDebug>
#include <QString>
#include <functional>

namespace osm {

class OsmXmlParser {
public:
    // Parse an OSM XML file into OsmData
    // Returns true on success, false on parse error
    static bool parseFile(const QString& path, OsmData& outData, QString* errorMsg = nullptr) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            if (errorMsg) *errorMsg = QString("Cannot open file: %1").arg(path);
            return false;
        }
        outData.sourceFile = path;
        return parseContent(file.readAll(), outData, errorMsg);
    }

    // Parse OSM XML content from a string
    static bool parseContent(const QString& xml, OsmData& outData, QString* errorMsg = nullptr) {
        return parseContent(xml.toUtf8(), outData, errorMsg);
    }

    // Parse OSM XML content from bytes
    static bool parseContent(const QByteArray& xmlData, OsmData& outData, QString* errorMsg = nullptr) {
        QXmlStreamReader xml(xmlData);

        int nodesParsed = 0, waysParsed = 0, relationsParsed = 0;

        while (!xml.atEnd()) {
            xml.readNext();

            if (xml.isStartElement()) {
                QStringView name = xml.name();

                if (name == QLatin1String("osm")) {
                    outData.sourceVersion = xml.attributes().value("version").toString();
                    outData.generator = xml.attributes().value("generator").toString();
                }
                else if (name == QLatin1String("bounds")) {
                    outData.minLat = xml.attributes().value("minlat").toDouble();
                    outData.minLon = xml.attributes().value("minlon").toDouble();
                    outData.maxLat = xml.attributes().value("maxlat").toDouble();
                    outData.maxLon = xml.attributes().value("maxlon").toDouble();
                    outData.hasBounds = true;
                }
                else if (name == QLatin1String("node")) {
                    Node node;
                    node.id = xml.attributes().value("id").toLongLong();
                    node.lat = xml.attributes().value("lat").toDouble();
                    node.lon = xml.attributes().value("lon").toDouble();
                    QString visible = xml.attributes().value("visible").toString();
                    if (visible == "false") continue;

                    // Parse tags and elevation inside node
                    while (!(xml.isEndElement() && xml.name() == QLatin1String("node"))) {
                        xml.readNext();
                        if (xml.isStartElement()) {
                            if (xml.name() == QLatin1String("tag")) {
                                Tag tag;
                                tag.key = xml.attributes().value("k").toString();
                                tag.value = xml.attributes().value("v").toString();
                                node.tags.append(tag);
                            }
                            else if (xml.name() == QLatin1String("ele") || xml.name() == QLatin1String("tag")) {
                                // Check for elevation tag
                            }
                        }
                    }

                    // Extract elevation from tags
                    for (const auto& t : node.tags) {
                        if (t.key == "ele") {
                            bool ok;
                            double e = t.value.toDouble(&ok);
                            if (ok) {
                                node.ele = e;
                                node.hasElevation = true;
                            }
                        }
                    }

                    outData.nodes[node.id] = node;
                    nodesParsed++;
                }
                else if (name == QLatin1String("way")) {
                    Way way;
                    way.id = xml.attributes().value("id").toLongLong();
                    QString visible = xml.attributes().value("visible").toString();
                    way.visible = visible != "false";

                    // Parse nd refs and tags inside way
                    while (!(xml.isEndElement() && xml.name() == QLatin1String("way"))) {
                        xml.readNext();
                        if (xml.isStartElement()) {
                            if (xml.name() == QLatin1String("nd")) {
                                qint64 ref = xml.attributes().value("ref").toLongLong();
                                way.nodeRefs.append(ref);
                            }
                            else if (xml.name() == QLatin1String("tag")) {
                                Tag tag;
                                tag.key = xml.attributes().value("k").toString();
                                tag.value = xml.attributes().value("v").toString();
                                way.tags.append(tag);
                            }
                        }
                    }

                    outData.ways[way.id] = way;
                    waysParsed++;
                }
                else if (name == QLatin1String("relation")) {
                    Relation rel;
                    rel.id = xml.attributes().value("id").toLongLong();

                    // Parse members and tags inside relation
                    while (!(xml.isEndElement() && xml.name() == QLatin1String("relation"))) {
                        xml.readNext();
                        if (xml.isStartElement()) {
                            if (xml.name() == QLatin1String("member")) {
                                RelationMember m;
                                m.ref = xml.attributes().value("ref").toLongLong();
                                QString type = xml.attributes().value("type").toString();
                                m.type = type == "way" ? RelationMember::Type::Way :
                                        type == "relation" ? RelationMember::Type::Relation :
                                                              RelationMember::Type::Node;
                                m.role = xml.attributes().value("role").toString();
                                rel.members.append(m);
                            }
                            else if (xml.name() == QLatin1String("tag")) {
                                Tag tag;
                                tag.key = xml.attributes().value("k").toString();
                                tag.value = xml.attributes().value("v").toString();
                                rel.tags.append(tag);
                            }
                        }
                    }

                    outData.relations[rel.id] = rel;
                    relationsParsed++;
                }
            }

            if (xml.hasError()) {
                if (errorMsg) *errorMsg = QString("XML parse error at line %1: %2")
                    .arg(xml.lineNumber()).arg(xml.errorString());
                return false;
            }
        }

        if (xml.hasError()) {
            if (errorMsg) *errorMsg = QString("XML parse error: %1").arg(xml.errorString());
            return false;
        }

        qDebug() << "[OSM] Parsed:" << nodesParsed << "nodes,"
                 << waysParsed << "ways," << relationsParsed << "relations";
        return true;
    }
};

} // namespace osm
