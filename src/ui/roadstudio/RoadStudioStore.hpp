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

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    // LaneMaker workflow state
    bool isLmRoadActive() const { return m_lmRoadActive; }
    const std::optional<roads::Point2D>& lmRoadStart() const { return m_lmRoadStart; }
    const std::optional<roads::Vec2>& lmRoadStartDir() const { return m_lmRoadStartDir; }
    const std::optional<roads::Point2D>& lmRoadEnd() const { return m_lmRoadEnd; }
    const std::optional<roads::Vec2>& lmRoadEndDir() const { return m_lmRoadEndDir; }
    const std::optional<roads::Point2D>& previewPoint() const { return m_previewPoint; }

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

    void pushHistory(const QString& description);
    void undo();
    void redo();

    // --- LaneMaker workflow ---

    void startLmRoad(roads::Point2D start, roads::Vec2 dir);
    void setLmRoadEnd(roads::Point2D end);
    void setLmRoadEndDir(roads::Vec2 dir);
    void setPreviewPoint(roads::Point2D pt);
    void finishLmRoad();
    void cancelLmRoad();

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

    // Debug
    bool m_debugMode = false;
    QSet<DebugLayer> m_debugLayers;

    // Undo/redo
    QList<roads::HistorySnapshot> m_undoStack;
    QList<roads::HistorySnapshot> m_redoStack;

    // Geo reference origin
    double m_refLat = 18.52;
    double m_refLon = 73.85;

    QString generateId() const {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
};
