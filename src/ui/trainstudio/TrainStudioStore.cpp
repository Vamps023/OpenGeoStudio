// TrainStudioStore — State management implementation

#include "TrainStudioStore.hpp"
#include "../roadstudio/GeoConvert.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QXmlStreamReader>

TrainStudioStore::TrainStudioStore(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus), m_log("TrainStudioStore") {
    m_network = new QNetworkAccessManager(this);
}

void TrainStudioStore::setTool(trains::Tool tool) {
    if (m_tool == tool) return;
    m_tool = tool;
    if (tool != trains::Tool::Arc) cancelArc();
    emit toolChanged(tool);
}

void TrainStudioStore::setRefOrigin(double lat, double lon) {
    m_refLat = lat;
    m_refLon = lon;
}

void TrainStudioStore::setSnapEnabled(bool enabled) { m_snapEnabled = enabled; }
void TrainStudioStore::setGridSize(double size) { m_gridSize = size; }

QString TrainStudioStore::startNewTrack(double lat, double lon) {
    pushHistory("Start new track");

    trains::Track track;
    track.id = generateId();
    track.name = "Track " + QString::number(m_tracks.size() + 1);

    trains::ControlPoint cp;
    cp.id = generateId();
    cp.lat = lat;
    cp.lon = lon;
    track.points.append(cp);

    m_tracks.append(track);
    m_drawingTrackId = track.id;
    m_log.info("Started new track:", track.id);
    emit tracksChanged();
    return track.id;
}

void TrainStudioStore::addControlPoint(const QString& trackId, double lat, double lon) {
    for (auto& t : m_tracks) {
        if (t.id == trackId) {
            pushHistory("Add control point");
            trains::ControlPoint cp;
            cp.id = generateId();
            cp.lat = lat;
            cp.lon = lon;
            t.points.append(cp);
            emit tracksChanged();
            return;
        }
    }
}

void TrainStudioStore::updateControlPoint(const QString& trackId, int index,
                                           double lat, double lon) {
    for (auto& t : m_tracks) {
        if (t.id == trackId && index >= 0 && index < t.points.size()) {
            t.points[index].lat = lat;
            t.points[index].lon = lon;
            emit tracksChanged();
            return;
        }
    }
}

void TrainStudioStore::deleteControlPoint(const QString& trackId, int index) {
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == trackId) {
            if (index < 0 || index >= m_tracks[i].points.size()) return;
            pushHistory("Delete control point");
            m_tracks[i].points.removeAt(index);
            if (m_tracks[i].points.isEmpty()) {
                m_tracks.removeAt(i);
                if (m_drawingTrackId == trackId) m_drawingTrackId.clear();
            }
            emit tracksChanged();
            return;
        }
    }
}

void TrainStudioStore::deleteTrack(const QString& trackId) {
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == trackId) {
            pushHistory("Delete track");
            m_tracks.removeAt(i);
            if (m_selection.trackId == trackId) m_selection.clear();
            if (m_drawingTrackId == trackId) m_drawingTrackId.clear();
            emit tracksChanged();
            emit selectionChanged(m_selection);
            return;
        }
    }
}

void TrainStudioStore::clearAll() {
    if (m_tracks.isEmpty()) return;
    pushHistory("Clear all tracks");
    m_tracks.clear();
    m_selection.clear();
    m_drawingTrackId.clear();
    emit tracksChanged();
    emit selectionChanged(m_selection);
}

void TrainStudioStore::setSelection(const trains::Selection& sel) {
    m_selection = sel;
    emit selectionChanged(m_selection);
}

void TrainStudioStore::clearSelection() {
    m_selection.clear();
    emit selectionChanged(m_selection);
}

void TrainStudioStore::pushHistory(const QString& description) {
    trains::HistorySnapshot snap;
    snap.tracks = m_tracks;
    snap.description = description;
    snap.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_undoStack.append(snap);
    m_redoStack.clear();
    if (m_undoStack.size() > 50) m_undoStack.removeFirst();
    emit historyChanged();
}

void TrainStudioStore::undo() {
    if (m_undoStack.isEmpty()) return;
    trains::HistorySnapshot current;
    current.tracks = m_tracks;
    current.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_redoStack.append(current);
    auto snap = m_undoStack.takeLast();
    m_tracks = snap.tracks;
    m_selection.clear();
    m_drawingTrackId.clear();
    emit tracksChanged();
    emit selectionChanged(m_selection);
    emit historyChanged();
}

void TrainStudioStore::redo() {
    if (m_redoStack.isEmpty()) return;
    trains::HistorySnapshot current;
    current.tracks = m_tracks;
    current.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_undoStack.append(current);
    auto snap = m_redoStack.takeLast();
    m_tracks = snap.tracks;
    m_selection.clear();
    m_drawingTrackId.clear();
    emit tracksChanged();
    emit selectionChanged(m_selection);
    emit historyChanged();
}

void TrainStudioStore::startArc(trains::ControlPoint start, QPointF dir) {
    m_arcDrawing = true;
    m_arcStart = start;
    m_arcStartDir = dir;
    emit arcStateChanged();
}

void TrainStudioStore::finishArc(double lat, double lon) {
    if (!m_arcDrawing || !m_arcStart) {
        cancelArc();
        return;
    }

    pushHistory("Finish arc track");

    trains::Track track;
    track.id = generateId();
    track.name = "Arc Track " + QString::number(m_tracks.size() + 1);
    track.kind = trains::Track::Kind::Arc;
    track.color = "#aa6622";

    // Start and end points (simplified — no arc sampling for now)
    track.points.append(*m_arcStart);

    trains::ControlPoint end;
    end.id = generateId();
    end.lat = lat;
    end.lon = lon;
    track.points.append(end);

    m_tracks.append(track);
    emit tracksChanged();
    cancelArc();
}

void TrainStudioStore::cancelArc() {
    m_arcDrawing = false;
    m_arcStart = std::nullopt;
    m_arcStartDir = std::nullopt;
    emit arcStateChanged();
}

QString TrainStudioStore::exportNetworkXml() const {
    // Export to Oksygen Track Mesh Builder XML format
    // This is a simplified version — the full format is in
    // modules/train-studio/shared/networkDefinitionExporter.ts
    QString xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<network>\n";
    for (const auto& track : m_tracks) {
        xml += QString("  <track id=\"%1\" name=\"%2\" gauge=\"%3\">\n")
            .arg(track.id, track.name).arg(track.gauge);
        for (const auto& cp : track.points) {
            xml += QString("    <node lat=\"%1\" lon=\"%2\" z=\"%3\"/>\n")
                .arg(cp.lat, 0, 'f', 8).arg(cp.lon, 0, 'f', 8).arg(cp.z);
        }
        xml += "  </track>\n";
    }
    xml += "</network>\n";
    return xml;
}

void TrainStudioStore::importOsmRailways(double south, double west, double north, double east) {
    // Overpass API query for railways in the bounding box
    QString query = QString(
        "[out:xml][timeout:25];\n"
        "(\n"
        "  way[railway=rail](%1,%2,%3,%4);\n"
        "  way[railway=light_rail](%1,%2,%3,%4);\n"
        "  way[railway=tram](%1,%2,%3,%4);\n"
        ");\n"
        "(._;>;);\n"
        "out;"
    ).arg(south, 0, 'f', 6).arg(west, 0, 'f', 6).arg(north, 0, 'f', 6).arg(east, 0, 'f', 6);

    QUrl url("https://overpass-api.de/api/interpreter");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    request.setRawHeader("User-Agent", "OpenGeoStudio-Qt/1.0");

    QByteArray postData = "data=" + QUrl::toPercentEncoding(query);
    QNetworkReply* reply = m_network->post(request, postData);

    emit osmImportStarted();
    m_log.info("OSM import started for bbox:", south, west, north, east);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit osmImportFinished(false, reply->errorString());
            return;
        }

        QByteArray data = reply->readAll();
        QXmlStreamReader xml(data);

        // Parse OSM XML: nodes and ways
        QMap<QString, QPointF> nodes; // id -> (lat, lon)
        int trackCount = 0;

        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                if (xml.name() == "node") {
                    QString id = xml.attributes().value("id").toString();
                    double lat = xml.attributes().value("lat").toDouble();
                    double lon = xml.attributes().value("lon").toDouble();
                    nodes[id] = QPointF(lat, lon);
                } else if (xml.name() == "way") {
                    trains::Track track;
                    track.id = generateId();
                    track.name = QString("OSM Railway %1").arg(++trackCount);
                    track.gauge = 1.435;
                    track.color = "#ff6600";

                    while (!xml.atEnd()) {
                        xml.readNext();
                        if (xml.isEndElement() && xml.name() == "way") break;
                        if (xml.isStartElement() && xml.name() == "nd") {
                            QString ref = xml.attributes().value("ref").toString();
                            if (nodes.contains(ref)) {
                                QPointF pt = nodes[ref];
                                trains::ControlPoint cp;
                                cp.id = generateId();
                                cp.lat = pt.x();
                                cp.lon = pt.y();
                                cp.z = 0;
                                track.points.append(cp);
                            }
                        }
                    }

                    if (track.points.size() >= 2) {
                        pushHistory("Import OSM railway");
                        m_tracks.append(track);
                    }
                }
            }
        }

        if (xml.hasError()) {
            emit osmImportFinished(false, QString("XML parse error: %1").arg(xml.errorString()));
        } else {
            emit osmImportFinished(true, QString("Imported %1 railway(s)").arg(trackCount));
            m_log.info("OSM import finished:", trackCount, "tracks");
            emit tracksChanged();
        }
    });
}

TrainStudioStore::ValidationResult TrainStudioStore::validateNetwork() const {
    ValidationResult result;

    if (m_tracks.isEmpty()) {
        result.valid = false;
        result.errors.append("No tracks in network");
        return result;
    }

    for (const auto& track : m_tracks) {
        if (track.points.size() < 2) {
            result.valid = false;
            result.errors.append(QString("Track '%1' has fewer than 2 control points")
                .arg(track.name));
        }

        if (track.name.isEmpty()) {
            result.warnings.append("Track has no name");
        }

        if (track.gauge <= 0) {
            result.valid = false;
            result.errors.append(QString("Track '%1' has invalid gauge: %2")
                .arg(track.name).arg(track.gauge));
        }

        // Check for duplicate consecutive points
        for (int i = 1; i < track.points.size(); ++i) {
            double dlat = track.points[i].lat - track.points[i-1].lat;
            double dlon = track.points[i].lon - track.points[i-1].lon;
            if (std::hypot(dlat, dlon) < 1e-9) {
                result.warnings.append(QString("Track '%1' has duplicate points at index %2")
                    .arg(track.name).arg(i));
            }
        }

        // Check for invalid coordinates
        for (int i = 0; i < track.points.size(); ++i) {
            const auto& cp = track.points[i];
            if (cp.lat < -90 || cp.lat > 90) {
                result.valid = false;
                result.errors.append(QString("Track '%1' point %2 has invalid latitude: %3")
                    .arg(track.name).arg(i).arg(cp.lat));
            }
            if (cp.lon < -180 || cp.lon > 180) {
                result.valid = false;
                result.errors.append(QString("Track '%1' point %2 has invalid longitude: %3")
                    .arg(track.name).arg(i).arg(cp.lon));
            }
        }
    }

    // Check for track connectivity (tracks that don't connect to any other)
    if (m_tracks.size() > 1) {
        for (const auto& track : m_tracks) {
            bool connected = false;
            // Simple check: see if any endpoint is close to another track's endpoint
            if (track.points.size() >= 2) {
                const auto& start = track.points.first();
                const auto& end = track.points.last();
                for (const auto& other : m_tracks) {
                    if (other.id == track.id || other.points.size() < 2) continue;
                    const auto& oStart = other.points.first();
                    const auto& oEnd = other.points.last();
                    double d1 = std::hypot(start.lat - oStart.lat, start.lon - oStart.lon);
                    double d2 = std::hypot(start.lat - oEnd.lat, start.lon - oEnd.lon);
                    double d3 = std::hypot(end.lat - oStart.lat, end.lon - oStart.lon);
                    double d4 = std::hypot(end.lat - oEnd.lat, end.lon - oEnd.lon);
                    if (d1 < 0.001 || d2 < 0.001 || d3 < 0.001 || d4 < 0.001) {
                        connected = true;
                        break;
                    }
                }
            }
            if (!connected && m_tracks.size() > 1) {
                result.warnings.append(QString("Track '%1' is not connected to any other track")
                    .arg(track.name));
            }
        }
    }

    return result;
}
