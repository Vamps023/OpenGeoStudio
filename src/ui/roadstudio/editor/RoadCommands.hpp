#pragma once

// ============================================================
// RoadCommands — QUndoCommand subclasses for road operations
// ============================================================
//
// Each command encapsulates a single editable operation with
// undo/redo support. Commands are pushed onto the QUndoStack
// in RoadStudioStore.
//
// Pattern:
//   - SnapshotCommand: generic before/after snapshot (used by
//     existing CRUD methods during migration)
//   - Specific commands (SetRoadPropertyCommand, etc.) for
//     fine-grained undo with minimal state copying
//
// The inspector uses specific commands so that property edits
// are properly undoable.
//

#include "../RoadTypes.hpp"
#include "../RoadStudioStore.hpp"

#include <QUndoCommand>
#include <QString>
#include <QVariant>

// ============================================================
// SnapshotCommand — Generic before/after road list snapshot
// ============================================================
//
// Used by existing CRUD methods (startNewRoad, addControlPoint,
// deleteRoad, etc.) during the migration from the custom snapshot
// system to QUndoStack.
//
// The first redo() is a no-op because the mutation already
// happened inline (before the command was pushed). Subsequent
// redo() calls (after an undo) apply the after-state.
//

class SnapshotCommand : public QUndoCommand {
public:
    SnapshotCommand(RoadStudioStore* store,
                     QList<roads::Road> before,
                     QList<roads::Road> after,
                     const QString& description);

    void undo() override;
    void redo() override;

    int id() const override { return 0; }

private:
    RoadStudioStore* m_store;
    QList<roads::Road> m_before;
    QList<roads::Road> m_after;
    bool m_firstRedo = true;
};

// ============================================================
// SetRoadPropertyCommand — Change a single road property
// ============================================================
//
// Used by the RoadInspector for: name, width, laneCount, color,
// profile type. Stores the road ID and the old/new value.
//

class SetRoadPropertyCommand : public QUndoCommand {
public:
    enum class Property {
        Name,
        Width,
        LaneCount,
        Color,
        ProfileType
    };

    SetRoadPropertyCommand(RoadStudioStore* store,
                            const QString& roadId,
                            Property property,
                            const QVariant& oldValue,
                            const QVariant& newValue,
                            const QString& description);

    void undo() override;
    void redo() override;

private:
    void applyValue(const QVariant& value);

    RoadStudioStore* m_store;
    QString m_roadId;
    Property m_property;
    QVariant m_oldValue;
    QVariant m_newValue;
};

// ============================================================
// SetControlPointPropertyCommand — Change a control point property
// ============================================================
//
// Used by the RoadInspector for: lat, lon, z, type.
//

class SetControlPointPropertyCommand : public QUndoCommand {
public:
    enum class Property {
        Latitude,
        Longitude,
        Elevation,
        Type
    };

    SetControlPointPropertyCommand(RoadStudioStore* store,
                                    const QString& roadId,
                                    int pointIndex,
                                    Property property,
                                    const QVariant& oldValue,
                                    const QVariant& newValue,
                                    const QString& description);

    void undo() override;
    void redo() override;

private:
    void applyValue(const QVariant& value);

    RoadStudioStore* m_store;
    QString m_roadId;
    int m_pointIndex;
    Property m_property;
    QVariant m_oldValue;
    QVariant m_newValue;
};
