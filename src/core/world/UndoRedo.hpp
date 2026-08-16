#pragma once

// ============================================================
// UndoRedo — Command-based undo/redo system
// ============================================================

#include "World.hpp"
#include <QStack>
#include <QUndoCommand>

namespace world {

// Base command that operates on the World
class WorldCommand : public QUndoCommand {
public:
    WorldCommand(World* world, const QString& text)
        : QUndoCommand(text), m_world(world) {}
    virtual ~WorldCommand() = default;

protected:
    World* m_world;
};

// ============================================================
// AddActorCommand
// ============================================================

class AddActorCommand : public WorldCommand {
public:
    AddActorCommand(World* world, const Actor& actor)
        : WorldCommand(world, "Add Actor: " + actor.name), m_actor(actor) {}

    void undo() override {
        m_world->removeActor(m_actor.id);
    }
    void redo() override {
        m_world->addActor(m_actor);
    }
private:
    Actor m_actor;
};

// ============================================================
// RemoveActorCommand
// ============================================================

class RemoveActorCommand : public WorldCommand {
public:
    RemoveActorCommand(World* world, const QString& actorId)
        : WorldCommand(world, "Remove Actor"), m_actorId(actorId) {
        // Store actor for undo
        const Actor* a = world->findActor(actorId);
        if (a) m_storedActor = *a;
    }

    void undo() override {
        if (m_storedActor.id == m_actorId)
            m_world->addActor(m_storedActor);
    }
    void redo() override {
        m_world->removeActor(m_actorId);
    }
private:
    QString m_actorId;
    Actor m_storedActor;
};

// ============================================================
// TransformActorCommand
// ============================================================

class TransformActorCommand : public WorldCommand {
public:
    TransformActorCommand(World* world, const QString& actorId,
                           const Transform& oldTransform, const Transform& newTransform)
        : WorldCommand(world, "Transform Actor"), m_actorId(actorId),
          m_oldTransform(oldTransform), m_newTransform(newTransform) {}

    void undo() override {
        Actor* a = m_world->findActor(m_actorId);
        if (a) { a->transform = m_oldTransform; a->touch(); }
    }
    void redo() override {
        Actor* a = m_world->findActor(m_actorId);
        if (a) { a->transform = m_newTransform; a->touch(); }
    }
private:
    QString m_actorId;
    Transform m_oldTransform, m_newTransform;
};

// ============================================================
// RenameActorCommand
// ============================================================

class RenameActorCommand : public WorldCommand {
public:
    RenameActorCommand(World* world, const QString& actorId,
                        const QString& oldName, const QString& newName)
        : WorldCommand(world, "Rename Actor"), m_actorId(actorId),
          m_oldName(oldName), m_newName(newName) {}

    void undo() override {
        Actor* a = m_world->findActor(m_actorId);
        if (a) { a->name = m_oldName; a->touch(); }
    }
    void redo() override {
        Actor* a = m_world->findActor(m_actorId);
        if (a) { a->name = m_newName; a->touch(); }
    }
private:
    QString m_actorId, m_oldName, m_newName;
};

// ============================================================
// SetParentCommand
// ============================================================

class SetParentCommand : public WorldCommand {
public:
    SetParentCommand(World* world, const QString& actorId,
                      const QString& oldParent, const QString& newParent)
        : WorldCommand(world, "Set Parent"), m_actorId(actorId),
          m_oldParent(oldParent), m_newParent(newParent) {}

    void undo() override {
        m_world->setParent(m_actorId, m_oldParent);
    }
    void redo() override {
        m_world->setParent(m_actorId, m_newParent);
    }
private:
    QString m_actorId, m_oldParent, m_newParent;
};

// ============================================================
// SetLayerCommand
// ============================================================

class SetLayerCommand : public WorldCommand {
public:
    SetLayerCommand(World* world, const QString& actorId,
                     const QString& oldLayer, const QString& newLayer)
        : WorldCommand(world, "Set Layer"), m_actorId(actorId),
          m_oldLayer(oldLayer), m_newLayer(newLayer) {}

    void undo() override {
        Actor* a = m_world->findActor(m_actorId);
        if (a) { a->layerId = m_oldLayer; a->touch(); }
    }
    void redo() override {
        Actor* a = m_world->findActor(m_actorId);
        if (a) { a->layerId = m_newLayer; a->touch(); }
    }
private:
    QString m_actorId, m_oldLayer, m_newLayer;
};

// ============================================================
// SetVisibilityCommand
// ============================================================

class SetVisibilityCommand : public WorldCommand {
public:
    SetVisibilityCommand(World* world, const QString& actorId, bool visible)
        : WorldCommand(world, visible ? "Show Actor" : "Hide Actor"),
          m_actorId(actorId), m_visible(visible) {}

    void undo() override {
        Actor* a = m_world->findActor(m_actorId);
        if (a) { a->visible = !m_visible; a->touch(); }
    }
    void redo() override {
        Actor* a = m_world->findActor(m_actorId);
        if (a) { a->visible = m_visible; a->touch(); }
    }
private:
    QString m_actorId;
    bool m_visible;
};

// ============================================================
// AddSplineCommand
// ============================================================

class AddSplineCommand : public WorldCommand {
public:
    AddSplineCommand(World* world, const Spline& spline)
        : WorldCommand(world, "Add Spline: " + spline.name), m_spline(spline) {}

    void undo() override { m_world->removeSpline(m_spline.id); }
    void redo() override { m_world->addSpline(m_spline.type, m_spline.name)->id = m_spline.id; }
private:
    Spline m_spline;
};

// ============================================================
// AddLayerCommand
// ============================================================

class AddLayerCommand : public WorldCommand {
public:
    AddLayerCommand(World* world, const QString& name)
        : WorldCommand(world, "Add Layer: " + name), m_name(name) {}

    void undo() override {
        if (!m_layerId.isEmpty()) m_world->removeLayer(m_layerId);
    }
    void redo() override {
        Layer* l = m_world->addLayer(m_name);
        m_layerId = l->id;
    }
private:
    QString m_name;
    QString m_layerId;
};

// ============================================================
// SelectionCommand
// ============================================================

class SelectionCommand : public WorldCommand {
public:
    SelectionCommand(World* world, const QSet<QString>& oldSelection,
                      const QSet<QString>& newSelection)
        : WorldCommand(world, "Change Selection"),
          m_oldSelection(oldSelection), m_newSelection(newSelection) {}

    void undo() override { m_world->selectedActorIds = m_oldSelection; }
    void redo() override { m_world->selectedActorIds = m_newSelection; }
private:
    QSet<QString> m_oldSelection, m_newSelection;
};

} // namespace world
