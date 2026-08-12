// TrainStudioStore — State management implementation

#include "TrainStudioStore.hpp"
#include "../roadstudio/GeoConvert.hpp"

TrainStudioStore::TrainStudioStore(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus), m_log("TrainStudioStore") {
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
