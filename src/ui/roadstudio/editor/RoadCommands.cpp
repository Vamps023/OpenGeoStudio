// RoadCommands — QUndoCommand implementations

#include "RoadCommands.hpp"

// ─── SnapshotCommand ──────────────────────────────────────────

SnapshotCommand::SnapshotCommand(RoadStudioStore* store,
                                   QList<roads::Road> before,
                                   QList<roads::Road> after,
                                   const QString& description)
    : QUndoCommand(description),
      m_store(store),
      m_before(std::move(before)),
      m_after(std::move(after)) {
}

void SnapshotCommand::undo() {
    m_store->applyRoads(m_before);
}

void SnapshotCommand::redo() {
    if (m_firstRedo) {
        // The mutation already happened inline before this command
        // was pushed onto the stack. Skip the first redo.
        m_firstRedo = false;
        return;
    }
    m_store->applyRoads(m_after);
}

// ─── SetRoadPropertyCommand ───────────────────────────────────

SetRoadPropertyCommand::SetRoadPropertyCommand(RoadStudioStore* store,
                                                 const QString& roadId,
                                                 Property property,
                                                 const QVariant& oldValue,
                                                 const QVariant& newValue,
                                                 const QString& description)
    : QUndoCommand(description),
      m_store(store),
      m_roadId(roadId),
      m_property(property),
      m_oldValue(oldValue),
      m_newValue(newValue) {
}

void SetRoadPropertyCommand::applyValue(const QVariant& value) {
    auto* road = m_store->getRoad(m_roadId);
    if (!road) return;

    switch (m_property) {
        case Property::Name:
            road->name = value.toString();
            break;
        case Property::Width:
            road->width = value.toDouble();
            break;
        case Property::LaneCount:
            road->laneCount = value.toInt();
            break;
        case Property::Color:
            road->color = value.toString();
            break;
        case Property::ProfileType:
            road->profile.type = value.toString();
            break;
    }
    emit m_store->roadsChanged();
}

void SetRoadPropertyCommand::undo() {
    applyValue(m_oldValue);
}

void SetRoadPropertyCommand::redo() {
    applyValue(m_newValue);
}

// ─── SetControlPointPropertyCommand ───────────────────────────

SetControlPointPropertyCommand::SetControlPointPropertyCommand(
        RoadStudioStore* store,
        const QString& roadId,
        int pointIndex,
        Property property,
        const QVariant& oldValue,
        const QVariant& newValue,
        const QString& description)
    : QUndoCommand(description),
      m_store(store),
      m_roadId(roadId),
      m_pointIndex(pointIndex),
      m_property(property),
      m_oldValue(oldValue),
      m_newValue(newValue) {
}

void SetControlPointPropertyCommand::applyValue(const QVariant& value) {
    auto* road = m_store->getRoad(m_roadId);
    if (!road || m_pointIndex < 0 || m_pointIndex >= road->points.size()) return;

    auto& cp = road->points[m_pointIndex];
    switch (m_property) {
        case Property::Latitude:
            cp.lat = value.toDouble();
            break;
        case Property::Longitude:
            cp.lon = value.toDouble();
            break;
        case Property::Elevation:
            cp.z = value.toDouble();
            break;
        case Property::Type:
            cp.type = roads::ControlPoint::typeFromStr(value.toString());
            break;
    }
    emit m_store->roadsChanged();
}

void SetControlPointPropertyCommand::undo() {
    applyValue(m_oldValue);
}

void SetControlPointPropertyCommand::redo() {
    applyValue(m_newValue);
}
