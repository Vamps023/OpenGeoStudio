#pragma once

// ============================================================
// TrainStudioStore — Train Studio state management
// ============================================================
//
// Replaces the Zustand store in modules/train-studio/client/store/
// trainStudioStore.ts. Simpler than Road Studio — no debug layers,
// no LaneMaker workflow, no 3D.
//

#include "TrainTypes.hpp"
#include "../../core/events/EventBus.hpp"
#include "../../core/logger/Logger.hpp"

#include <QObject>
#include <QUuid>
#include <QDateTime>
#include <QPointF>
#include <QNetworkAccessManager>

class TrainStudioStore : public QObject {
    Q_OBJECT

public:
    explicit TrainStudioStore(EventBus* bus, QObject* parent = nullptr);

    const QList<trains::Track>& tracks() const { return m_tracks; }
    trains::Tool tool() const { return m_tool; }
    const trains::Selection& selection() const { return m_selection; }
    double refLat() const { return m_refLat; }
    double refLon() const { return m_refLon; }
    bool snapEnabled() const { return m_snapEnabled; }
    double gridSize() const { return m_gridSize; }

    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    // Arc drawing state
    bool isArcDrawing() const { return m_arcDrawing; }
    const std::optional<trains::ControlPoint>& arcStart() const { return m_arcStart; }
    const std::optional<QPointF>& arcStartDir() const { return m_arcStartDir; } // local meters

    // --- Actions ---
    void setTool(trains::Tool tool);
    void setRefOrigin(double lat, double lon);
    void setSnapEnabled(bool enabled);
    void setGridSize(double size);

    QString startNewTrack(double lat, double lon);
    void addControlPoint(const QString& trackId, double lat, double lon);
    void updateControlPoint(const QString& trackId, int index, double lat, double lon);
    void deleteControlPoint(const QString& trackId, int index);
    void deleteTrack(const QString& trackId);
    void clearAll();

    void setSelection(const trains::Selection& sel);
    void clearSelection();

    void pushHistory(const QString& description);
    void undo();
    void redo();

    // Arc drawing workflow
    void startArc(trains::ControlPoint start, QPointF dir);
    void finishArc(double lat, double lon);
    void cancelArc();

    // XML export
    QString exportNetworkXml() const;

    // OSM railway import via Overpass API
    void importOsmRailways(double south, double west, double north, double east);

signals:
    void tracksChanged();
    void toolChanged(trains::Tool tool);
    void selectionChanged(const trains::Selection& sel);
    void historyChanged();
    void arcStateChanged();
    void osmImportStarted();
    void osmImportFinished(bool success, const QString& message);

private:
    EventBus* m_bus;
    Logger m_log;

    QList<trains::Track> m_tracks;
    trains::Tool m_tool = trains::Tool::Select;
    trains::Selection m_selection;
    QString m_drawingTrackId;

    double m_refLat = 18.52;
    double m_refLon = 73.85;
    bool m_snapEnabled = true;
    double m_gridSize = 10.0;

    QList<trains::HistorySnapshot> m_undoStack;
    QList<trains::HistorySnapshot> m_redoStack;

    // Arc drawing state
    bool m_arcDrawing = false;
    std::optional<trains::ControlPoint> m_arcStart;
    std::optional<QPointF> m_arcStartDir; // direction in local meters

    // OSM import
    QNetworkAccessManager* m_network;

    QString generateId() const {
        return QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
};
