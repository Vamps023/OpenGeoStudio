#pragma once

// ============================================================
// RoadStudioStore — Road Studio state management
// ============================================================
//
// Replaces the Zustand store in modules/road-studio/client/store/
// roadStudioStore.ts. Uses Qt signals for state change notification.
//
// Manages: roads, tools, selection, undo/redo, debug layers,
// LaneMaker workflow state, and reference origin.
//

#include "RoadTypes.hpp"
#include "../../core/events/EventBus.hpp"
#include "../../core/logger/Logger.hpp"

#include <QObject>
#include <QMap>
#include <QSet>
#include <QUuid>
#include <QDateTime>
#include <QUndoStack>

class RoadStudioStore : public QObject {
    Q_OBJECT

public:
    // Debug layers (18 total, matching the reference app)
    enum class DebugLayer {
        Centerline, LeftEdge, RightEdge, LaneBoundaries,
        OffsetCurves, RoadPolygon, FilletArcs, TangentPoints,
        TrimPoints, IntersectionPolygon, Triangulation, VertexNormals,
        UVGrid, SamplePoints, LaneCenters, LaneBoundaryLines,
        LaneIds, MeshWireframe
    };

    explicit RoadStudioStore(EventBus* bus, QObject* parent = nullptr);

    // --- State accessors ---

    const QList<roads::Road>& roads() const { return m_roads; }
    roads::Tool tool() const { return m_tool; }
    roads::ViewMode viewMode() const { return m_viewMode; }
    const roads::Selection& selection() const { return m_selection; }
    const QStringList& selectedRoadIds() const { return m_selectedRoadIds; }

    double refLat() const { return m_refLat; }
    double refLon() const { return m_refLon; }
    double gridSize() const { return m_gridSize; }
    bool snapEnabled() const { return m_snapEnabled; }
    double defaultWidth() const { return m_defaultWidth; }
    int defaultLaneCount() const { return m_defaultLaneCount; }

    bool debugMode() const { return m_debugMode; }
    bool debugLayerEnabled(DebugLayer layer) const { return m_debugLayers.contains(layer); }

    bool canUndo() const { return m_undoStack.canUndo(); }
    bool canRedo() const { return m_undoStack.canRedo(); }

    // QUndoStack access (for pushing commands from inspector/editor)
    QUndoStack& undoStack() { return m_undoStack; }

    // LaneMaker workflow state
    bool isLmRoadActive() const { return m_lmRoadActive; }
    const std::optional<roads::Point2D>& lmRoadStart() const { return m_lmRoadStart; }
    const std::optional<roads::Vec2>& lmRoadStartDir() const { return m_lmRoadStartDir; }
    const std::optional<roads::Point2D>& lmRoadEnd() const { return m_lmRoadEnd; }
    const std::optional<roads::Vec2>& lmRoadEndDir() const { return m_lmRoadEndDir; }
    const std::optional<roads::Point2D>& previewPoint() const { return m_previewPoint; }

    // Staged geometry (multi-click road creation)
    const QList<roads::StagedGeometry>& stagedGeometries() const { return m_stagedGeometries; }
    const roads::LaneConfig& laneConfig() const { return m_laneConfig; }
    bool isDirectionHandleActive() const { return m_directionHandleActive; }
    double directionHandleAngle() const { return m_directionHandleAngle; }
    const std::optional<roads::Point2D>& directionHandlePos() const { return m_directionHandlePos; }

    // Snap state
    bool isSnappingToRoad() const { return m_snapToRoad; }
    const QString& snapRoadId() const { return m_snapRoadId; }
    double snapS() const { return m_snapS; }
    bool snapIsExtend() const { return m_snapIsExtend; }

    // --- Tool / view mode ---

    void setTool(roads::Tool tool);
    void setViewMode(roads::ViewMode mode);

    // --- Road CRUD ---

    QString startNewRoad(double lat, double lon);
    void addControlPoint(const QString& roadId, double lat, double lon);
    void updateControlPoint(const QString& roadId, int index, double lat, double lon);
    void updateControlPointElevation(const QString& roadId, int index, double z);
    void deleteControlPoint(const QString& roadId, int index);
    void deleteRoad(const QString& roadId);
    void clearAll();

    // Create a demo road (200m test road, matching reference app)
    QString createDemoRoad();

    // --- Selection ---

    void setSelection(const roads::Selection& sel);
    void clearSelection();
    void toggleRoadSelection(const QString& roadId);

    // --- Config ---

    void setRefOrigin(double lat, double lon);
    void setGridSize(double size);
    void setSnapEnabled(bool enabled);
    void setDefaultWidth(double width);
    void setDefaultLaneCount(int count);

    // --- Debug ---

    void toggleDebugMode();
    void toggleDebugLayer(DebugLayer layer);

    // --- Undo/Redo ---

    // Capture the current road state before a mutation (call before changing roads)
    void pushHistory(const QString& description);

    // Commit the captured state as a SnapshotCommand onto the QUndoStack
    // (call after the mutation is complete)
    void commitHistory();

    void undo();
    void redo();

    // Apply a road list directly (used by SnapshotCommand::undo/redo)
    void applyRoads(const QList<roads::Road>& roads);

    // --- LaneMaker workflow ---

    void startLmRoad(roads::Point2D start, roads::Vec2 dir);
    void setLmRoadEnd(roads::Point2D end);
    void setLmRoadEndDir(roads::Vec2 dir);
    void setPreviewPoint(roads::Point2D pt);
    void finishLmRoad();
    void cancelLmRoad();

    // --- Staged geometry workflow (multi-click road creation) ---

    void stageGeometry(roads::StagedGeometry geo);
    void popStagedGeometry();
    void clearStagedGeometry();
    void setDirectionHandle(roads::Point2D pos, double angle);
    void updateDirectionHandleAngle(double angle);
    void clearDirectionHandle();

    // --- Lane config ---

    void setLaneConfig(const roads::LaneConfig& config);
    void setLeftLaneCount(int count);
    void setRightLaneCount(int count);

    // --- Snap ---

    void setSnapToRoad(bool snapping, const QString& roadId = {}, double s = 0, bool isExtend = false);

    // --- Helpers ---

    roads::Road* getRoad(const QString& id);
    roads::Road* getDrawingRoad();
    int getRoadIndex(const QString& id) const;

signals:
    void roadsChanged();
    void toolChanged(roads::Tool tool);
    void viewModeChanged(roads::ViewMode mode);
    void selectionChanged(const roads::Selection& sel);
    void debugModeChanged(bool enabled);
    void debugLayerChanged(DebugLayer layer, bool enabled);
    void refOriginChanged(double lat, double lon);
    void configChanged();
    void historyChanged();
    void lmRoadStateChanged();

private:
    EventBus* m_bus;
    Logger m_log;

    // Roads and meshes
    QList<roads::Road> m_roads;
    QMap<QString, roads::RoadMeshes> m_roadMeshes;

    // Tool and view
    roads::Tool m_tool = roads::Tool::Select;
    roads::ViewMode m_viewMode = roads::ViewMode::Top;

    // Selection
    roads::Selection m_selection;
    QStringList m_selectedRoadIds;

    // Editing config
    QString m_drawingRoadId;
    double m_gridSize = 10.0;
    bool m_snapEnabled = true;
    double m_defaultWidth = 7.0;
    int m_defaultLaneCount = 2;

    // LaneMaker workflow
    bool m_lmRoadActive = false;
    std::optional<roads::Point2D> m_lmRoadStart;
    std::optional<roads::Vec2> m_lmRoadStartDir;
    std::optional<roads::Point2D> m_lmRoadEnd;
    std::optional<roads::Vec2> m_lmRoadEndDir;
    std::optional<roads::Point2D> m_previewPoint;

    // Staged geometry (multi-click road creation)
    QList<roads::StagedGeometry> m_stagedGeometries;
    bool m_directionHandleActive = false;
    double m_directionHandleAngle = 0;
    std::optional<roads::Point2D> m_directionHandlePos;

    // Lane config
    roads::LaneConfig m_laneConfig;

    // Snap state
    bool m_snapToRoad = false;
    QString m_snapRoadId;
    double m_snapS = 0;
    bool m_snapIsExtend = false;

    // Debug
    bool m_debugMode = false;
    QSet<DebugLayer> m_debugLayers;

    // Undo/redo — QUndoStack replaces the custom snapshot lists
    QUndoStack m_undoStack;
    QList<roads::Road> m_pendingBefore;  // captured by pushHistory, used by commitHistory
    QString m_pendingDesc;

    // Geo reference origin
    double m_refLat = 18.52;
    double m_refLon = 73.85;

    QString generateId() const {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
};
