// RoadStudioStore — State management implementation

#include "RoadStudioStore.hpp"
#include "GeoConvert.hpp"
#include "LaneMakerService.hpp"

RoadStudioStore::RoadStudioStore(EventBus* bus, QObject* parent)
    : QObject(parent), m_bus(bus), m_log("RoadStudioStore") {
    // Enable all debug layers by default when debug mode is on
    // (matching reference app behavior)
}

// --- Tool / view mode ---

void RoadStudioStore::setTool(roads::Tool tool) {
    if (m_tool == tool) return;
    m_tool = tool;

    const char* toolName = "unknown";
    switch (tool) {
        case roads::Tool::Select: toolName = "select/drag"; break;
        case roads::Tool::Road: toolName = "road create"; break;
        case roads::Tool::Lane: toolName = "lane create"; break;
        case roads::Tool::Destroy: toolName = "destroy"; break;
        case roads::Tool::Modify: toolName = "modify"; break;
    }
    m_log.info("Tool changed:", toolName);

    // Cancel any active LaneMaker workflow when switching tools
    if (m_lmRoadActive) cancelLmRoad();
    m_stagedGeometries.clear();
    m_directionHandleActive = false;
    m_directionHandlePos = std::nullopt;

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

    pushHistory("Update control point");
    road->points[index].lat = lat;
    road->points[index].lon = lon;
    emit roadsChanged();
}

void RoadStudioStore::updateControlPointElevation(const QString& roadId, int index, double z) {
    auto* road = getRoad(roadId);
    if (!road || index < 0 || index >= road->points.size()) return;

    pushHistory("Update elevation");
    road->points[index].z = z;
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
        if (m_selection.roadId == roadId) m_selection.roadId.clear();
    } else {
        m_selectedRoadIds.append(roadId);
        m_selection.roadId = roadId;
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
    m_selectedRoadIds.clear();
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
    m_selectedRoadIds.clear();
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
    // If we have staged geometries, build the road from them
    if (!m_stagedGeometries.isEmpty()) {
        pushHistory("Finish LaneMaker road (staged)");

        roads::Road road;
        road.id = generateId();
        road.name = "Road " + QString::number(m_roads.size() + 1);
        road.width = m_defaultWidth;
        road.laneCount = m_defaultLaneCount;
        road.color = "#4488ff";
        road.formatVersion = 2;

        // Collect all sampled points from staged geometries
        for (const auto& staged : m_stagedGeometries) {
            for (const auto& pt : staged.samples) {
                double lat, lon;
                roads::localToGeo(pt.x, pt.y, m_refLat, m_refLon, lat, lon);
                roads::ControlPoint cp;
                cp.id = generateId();
                cp.lat = lat;
                cp.lon = lon;
                cp.z = 0;
                cp.type = roads::ControlPoint::Type::Smooth;
                road.points.append(cp);
            }
        }

        if (road.points.size() >= 2) {
            m_roads.append(road);
            m_log.info("Finished staged LaneMaker road:", road.id, "with", road.points.size(), "control points");
            emit roadsChanged();
        }
        cancelLmRoad();
        return;
    }

    // Fallback: simple 2-point road
    if (!m_lmRoadActive || !m_lmRoadStart || !m_lmRoadEnd) {
        cancelLmRoad();
        return;
    }

    pushHistory("Finish LaneMaker road");

    // Convert local meters back to geo for the road
    double startLat, startLon, endLat, endLon;
    roads::localToGeo(m_lmRoadStart->x, m_lmRoadStart->y, m_refLat, m_refLon, startLat, startLon);
    roads::localToGeo(m_lmRoadEnd->x, m_lmRoadEnd->y, m_refLat, m_refLon, endLat, endLon);

    // Use LaneMaker's ConnectRays to generate proper road geometry
    double startDirX = m_lmRoadStartDir ? m_lmRoadStartDir->x : 1.0;
    double startDirY = m_lmRoadStartDir ? m_lmRoadStartDir->y : 0.0;
    double endDirX = m_lmRoadEndDir ? m_lmRoadEndDir->x : (m_lmRoadEnd->x - m_lmRoadStart->x);
    double endDirY = m_lmRoadEndDir ? m_lmRoadEndDir->y : (m_lmRoadEnd->y - m_lmRoadStart->y);

    roads::Road road = LaneMakerService::generateRoad(
        startLat, startLon, startDirX, startDirY,
        endLat, endLon, endDirX, endDirY,
        m_refLat, m_refLon, m_defaultWidth, m_defaultLaneCount, 32);

    road.name = "Road " + QString::number(m_roads.size() + 1);

    m_roads.append(road);
    m_log.info("Finished LaneMaker road:", road.id, "with", road.points.size(), "control points");
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
    m_stagedGeometries.clear();
    m_directionHandleActive = false;
    m_directionHandlePos = std::nullopt;
    m_snapToRoad = false;
    emit lmRoadStateChanged();
}

// --- Staged geometry workflow ---

void RoadStudioStore::stageGeometry(roads::StagedGeometry geo) {
    m_stagedGeometries.append(std::move(geo));
    m_log.info("Staged geometry segment, total:", m_stagedGeometries.size());
    emit lmRoadStateChanged();
}

void RoadStudioStore::popStagedGeometry() {
    if (!m_stagedGeometries.isEmpty()) {
        m_stagedGeometries.removeLast();
        m_log.info("Popped staged geometry, remaining:", m_stagedGeometries.size());
        emit lmRoadStateChanged();
    }
}

void RoadStudioStore::clearStagedGeometry() {
    m_stagedGeometries.clear();
    emit lmRoadStateChanged();
}

void RoadStudioStore::setDirectionHandle(roads::Point2D pos, double angle) {
    m_directionHandleActive = true;
    m_directionHandlePos = pos;
    m_directionHandleAngle = angle;
    emit lmRoadStateChanged();
}

void RoadStudioStore::updateDirectionHandleAngle(double angle) {
    m_directionHandleAngle = angle;
    emit lmRoadStateChanged();
}

void RoadStudioStore::clearDirectionHandle() {
    m_directionHandleActive = false;
    m_directionHandlePos = std::nullopt;
    emit lmRoadStateChanged();
}

// --- Lane config ---

void RoadStudioStore::setLaneConfig(const roads::LaneConfig& config) {
    m_laneConfig = config;
    emit configChanged();
}

void RoadStudioStore::setLeftLaneCount(int count) {
    m_laneConfig.left.laneCount = std::max(0, count);
    emit configChanged();
}

void RoadStudioStore::setRightLaneCount(int count) {
    m_laneConfig.right.laneCount = std::max(0, count);
    emit configChanged();
}

// --- Snap ---

void RoadStudioStore::setSnapToRoad(bool snapping, const QString& roadId, double s, bool isExtend) {
    m_snapToRoad = snapping;
    m_snapRoadId = roadId;
    m_snapS = s;
    m_snapIsExtend = isExtend;
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
