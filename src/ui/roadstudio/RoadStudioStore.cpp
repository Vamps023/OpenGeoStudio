// RoadStudioStore — State management implementation

#include "RoadStudioStore.hpp"
#include "GeoConvert.hpp"

RoadStudioStore::RoadStudioStore(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus), m_log("RoadStudioStore") {
    // Enable all debug layers by default when debug mode is on
    // (matching reference app behavior)
}

// --- Tool / view mode ---

void RoadStudioStore::setTool(roads::Tool tool) {
    if (m_tool == tool) return;
    m_tool = tool;
    m_log.info("Tool changed:", tool == roads::Tool::Select ? "select" : "road");
    emit toolChanged(tool);
}

void RoadStudioStore::setViewMode(roads::ViewMode mode) {
    if (m_viewMode == mode) return;
    m_viewMode = mode;
    m_log.info("View mode changed:", mode == roads::ViewMode::Top ? "top" : "perspective");
    emit viewModeChanged(mode);
}

// --- Road CRUD ---

QString RoadStudioStore::startNewRoad(double lat, double lon) {
    pushHistory("Start new road");

    roads::Road road;
    road.id = generateId();
    road.name = "Road " + QString::number(m_roads.size() + 1);
    road.width = m_defaultWidth;
    road.laneCount = m_defaultLaneCount;

    roads::ControlPoint cp;
    cp.id = generateId();
    cp.lat = lat;
    cp.lon = lon;
    road.points.append(cp);

    m_roads.append(road);
    m_drawingRoadId = road.id;

    m_log.info("Started new road:", road.id, "at", lat, lon);
    emit roadsChanged();
    return road.id;
}

void RoadStudioStore::addControlPoint(const QString& roadId, double lat, double lon) {
    auto* road = getRoad(roadId);
    if (!road) return;

    pushHistory("Add control point");

    roads::ControlPoint cp;
    cp.id = generateId();
    cp.lat = lat;
    cp.lon = lon;
    road->points.append(cp);

    m_log.info("Added control point to road:", roadId, "at", lat, lon);
    emit roadsChanged();
}

void RoadStudioStore::updateControlPoint(const QString& roadId, int index,
                                          double lat, double lon) {
    auto* road = getRoad(roadId);
    if (!road || index < 0 || index >= road->points.size()) return;

    road->points[index].lat = lat;
    road->points[index].lon = lon;
    emit roadsChanged();
}

void RoadStudioStore::deleteControlPoint(const QString& roadId, int index) {
    auto* road = getRoad(roadId);
    if (!road || index < 0 || index >= road->points.size()) return;

    pushHistory("Delete control point");
    road->points.removeAt(index);

    if (road->points.isEmpty()) {
        m_roads.removeAt(getRoadIndex(roadId));
        if (m_drawingRoadId == roadId) m_drawingRoadId.clear();
    }

    emit roadsChanged();
}

void RoadStudioStore::deleteRoad(const QString& roadId) {
    int idx = getRoadIndex(roadId);
    if (idx < 0) return;

    pushHistory("Delete road");
    m_roads.removeAt(idx);
    m_roadMeshes.remove(roadId);

    if (m_selection.roadId == roadId) m_selection.clear();
    m_selectedRoadIds.removeAll(roadId);
    if (m_drawingRoadId == roadId) m_drawingRoadId.clear();

    m_log.info("Deleted road:", roadId);
    emit roadsChanged();
    emit selectionChanged(m_selection);
}

void RoadStudioStore::clearAll() {
    if (m_roads.isEmpty()) return;
    pushHistory("Clear all roads");
    m_roads.clear();
    m_roadMeshes.clear();
    m_selection.clear();
    m_selectedRoadIds.clear();
    m_drawingRoadId.clear();
    m_log.info("Cleared all roads");
    emit roadsChanged();
    emit selectionChanged(m_selection);
}

QString RoadStudioStore::createDemoRoad() {
    // Create a 200m test road (matching reference app)
    pushHistory("Create demo road");

    const double lat = m_refLat;
    const double lon = m_refLon;

    roads::Road road;
    road.id = generateId();
    road.name = "Demo Road";
    road.width = m_defaultWidth;
    road.laneCount = m_defaultLaneCount;

    // Start point
    roads::ControlPoint p1;
    p1.id = generateId();
    p1.lat = lat;
    p1.lon = lon;
    road.points.append(p1);

    // End point 200m east
    double endLat, endLon;
    roads::localToGeo(200.0, 0.0, lat, lon, endLat, endLon);
    roads::ControlPoint p2;
    p2.id = generateId();
    p2.lat = endLat;
    p2.lon = endLon;
    road.points.append(p2);

    m_roads.append(road);
    m_log.info("Created demo road:", road.id);
    emit roadsChanged();
    return road.id;
}

// --- Selection ---

void RoadStudioStore::setSelection(const roads::Selection& sel) {
    m_selection = sel;
    emit selectionChanged(m_selection);
}

void RoadStudioStore::clearSelection() {
    m_selection.clear();
    m_selectedRoadIds.clear();
    emit selectionChanged(m_selection);
}

void RoadStudioStore::toggleRoadSelection(const QString& roadId) {
    if (m_selectedRoadIds.contains(roadId)) {
        m_selectedRoadIds.removeAll(roadId);
    } else {
        m_selectedRoadIds.append(roadId);
    }
    emit selectionChanged(m_selection);
}

// --- Config ---

void RoadStudioStore::setRefOrigin(double lat, double lon) {
    m_refLat = lat;
    m_refLon = lon;
    m_log.info("Reference origin set:", lat, lon);
    emit refOriginChanged(lat, lon);
}

void RoadStudioStore::setGridSize(double size) {
    m_gridSize = size;
    emit configChanged();
}

void RoadStudioStore::setSnapEnabled(bool enabled) {
    m_snapEnabled = enabled;
    emit configChanged();
}

void RoadStudioStore::setDefaultWidth(double width) {
    m_defaultWidth = width;
    emit configChanged();
}

void RoadStudioStore::setDefaultLaneCount(int count) {
    m_defaultLaneCount = count;
    emit configChanged();
}

// --- Debug ---

void RoadStudioStore::toggleDebugMode() {
    m_debugMode = !m_debugMode;
    m_log.info("Debug mode:", m_debugMode ? "ON" : "OFF");
    emit debugModeChanged(m_debugMode);
}

void RoadStudioStore::toggleDebugLayer(DebugLayer layer) {
    if (m_debugLayers.contains(layer)) {
        m_debugLayers.remove(layer);
    } else {
        m_debugLayers.insert(layer);
    }
    emit debugLayerChanged(layer, m_debugLayers.contains(layer));
}

// --- Undo/Redo ---

void RoadStudioStore::pushHistory(const QString& description) {
    roads::HistorySnapshot snap;
    snap.roads = m_roads;
    snap.description = description;
    snap.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_undoStack.append(snap);
    m_redoStack.clear(); // clear redo on new action

    // Limit undo stack to 50 entries
    if (m_undoStack.size() > 50) {
        m_undoStack.removeFirst();
    }

    emit historyChanged();
}

void RoadStudioStore::undo() {
    if (m_undoStack.isEmpty()) return;
    roads::HistorySnapshot current;
    current.roads = m_roads;
    current.description = "before undo";
    current.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_redoStack.append(current);

    auto snap = m_undoStack.takeLast();
    m_roads = snap.roads;
    m_selection.clear();
    m_drawingRoadId.clear();
    m_log.info("Undo:", snap.description);
    emit roadsChanged();
    emit selectionChanged(m_selection);
    emit historyChanged();
}

void RoadStudioStore::redo() {
    if (m_redoStack.isEmpty()) return;
    roads::HistorySnapshot current;
    current.roads = m_roads;
    current.description = "before redo";
    current.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_undoStack.append(current);

    auto snap = m_redoStack.takeLast();
    m_roads = snap.roads;
    m_selection.clear();
    m_drawingRoadId.clear();
    m_log.info("Redo:", snap.description);
    emit roadsChanged();
    emit selectionChanged(m_selection);
    emit historyChanged();
}

// --- LaneMaker workflow ---

void RoadStudioStore::startLmRoad(roads::Point2D start, roads::Vec2 dir) {
    m_lmRoadActive = true;
    m_lmRoadStart = start;
    m_lmRoadStartDir = dir;
    m_lmRoadEnd = std::nullopt;
    m_lmRoadEndDir = std::nullopt;
    m_previewPoint = std::nullopt;
    m_log.info("LaneMaker road started at:", start.x, start.y);
    emit lmRoadStateChanged();
}

void RoadStudioStore::setLmRoadEnd(roads::Point2D end) {
    m_lmRoadEnd = end;
    emit lmRoadStateChanged();
}

void RoadStudioStore::setLmRoadEndDir(roads::Vec2 dir) {
    m_lmRoadEndDir = dir;
    emit lmRoadStateChanged();
}

void RoadStudioStore::setPreviewPoint(roads::Point2D pt) {
    m_previewPoint = pt;
    emit lmRoadStateChanged();
}

void RoadStudioStore::finishLmRoad() {
    if (!m_lmRoadActive || !m_lmRoadStart || !m_lmRoadEnd) {
        cancelLmRoad();
        return;
    }

    pushHistory("Finish LaneMaker road");

    // Convert local meters back to geo for the road
    double startLat, startLon, endLat, endLon;
    roads::localToGeo(m_lmRoadStart->x, m_lmRoadStart->y, m_refLat, m_refLon, startLat, startLon);
    roads::localToGeo(m_lmRoadEnd->x, m_lmRoadEnd->y, m_refLat, m_refLon, endLat, endLon);

    roads::Road road;
    road.id = generateId();
    road.name = "Road " + QString::number(m_roads.size() + 1);
    road.width = m_defaultWidth;
    road.laneCount = m_defaultLaneCount;

    roads::ControlPoint p1;
    p1.id = generateId();
    p1.lat = startLat;
    p1.lon = startLon;
    road.points.append(p1);

    roads::ControlPoint p2;
    p2.id = generateId();
    p2.lat = endLat;
    p2.lon = endLon;
    road.points.append(p2);

    m_roads.append(road);
    m_log.info("Finished LaneMaker road:", road.id);
    emit roadsChanged();

    cancelLmRoad();
}

void RoadStudioStore::cancelLmRoad() {
    m_lmRoadActive = false;
    m_lmRoadStart = std::nullopt;
    m_lmRoadStartDir = std::nullopt;
    m_lmRoadEnd = std::nullopt;
    m_lmRoadEndDir = std::nullopt;
    m_previewPoint = std::nullopt;
    emit lmRoadStateChanged();
}

// --- Helpers ---

roads::Road* RoadStudioStore::getRoad(const QString& id) {
    for (auto& r : m_roads) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

roads::Road* RoadStudioStore::getDrawingRoad() {
    if (m_drawingRoadId.isEmpty()) return nullptr;
    return getRoad(m_drawingRoadId);
}

int RoadStudioStore::getRoadIndex(const QString& id) const {
    for (int i = 0; i < m_roads.size(); ++i) {
        if (m_roads[i].id == id) return i;
    }
    return -1;
}
