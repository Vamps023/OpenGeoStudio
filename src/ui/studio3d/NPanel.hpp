#pragma once

// ============================================================
// NPanel — Blender-style viewport overlay panel (toggled with N)
//
// A compact panel that floats over the right edge of the viewport,
// showing quick transform and item properties for the selected
// actor. It mirrors the Object tab of the PropertiesEditor but in
// a slimmer form factor designed for quick edits without leaving
// the viewport.
// ============================================================

#include <QWidget>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QGroupBox>

#include "../../core/world/World.hpp"

class OgreWidget;

class NPanel : public QWidget {
    Q_OBJECT
public:
    explicit NPanel(OgreWidget* ogre, QWidget* parent = nullptr);

    void setActor(const QString& actorId);
    void clear();

signals:
    void actorModified(const QString& id);

private slots:
    void onTransformChanged();

private:
    void refresh();

    OgreWidget* m_ogre;
    QString m_currentActorId;
    bool m_updating = false;

    QLabel* m_titleLabel = nullptr;
    QDoubleSpinBox *m_posX, *m_posY, *m_posZ = nullptr;
    QDoubleSpinBox *m_rotX, *m_rotY, *m_rotZ = nullptr;
    QDoubleSpinBox *m_scaleX, *m_scaleY, *m_scaleZ = nullptr;
};
