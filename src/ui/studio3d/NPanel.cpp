#include "NPanel.hpp"
#include "OgreWidget.hpp"

static QDoubleSpinBox* makeSpinBox(float step, float minVal, float maxVal, int decimals)
{
    auto* sb = new QDoubleSpinBox();
    sb->setSingleStep(step);
    sb->setRange(minVal, maxVal);
    sb->setDecimals(decimals);
    sb->setButtonSymbols(QDoubleSpinBox::NoButtons);
    return sb;
}

NPanel::NPanel(OgreWidget* ogre, QWidget* parent)
    : QWidget(parent), m_ogre(ogre)
{
    setFixedWidth(240);
    setObjectName("npanel");

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->setSpacing(4);

    m_titleLabel = new QLabel("Item");
    m_titleLabel->setObjectName("npanelTitle");
    lay->addWidget(m_titleLabel);

    auto* transformGroup = new QGroupBox("Transform");
    auto* form = new QFormLayout(transformGroup);
    form->setContentsMargins(4, 4, 4, 4);
    form->setSpacing(2);

    auto* posRow = new QHBoxLayout();
    m_posX = makeSpinBox(0.1f, -100000, 100000, 2);
    m_posY = makeSpinBox(0.1f, -100000, 100000, 2);
    m_posZ = makeSpinBox(0.1f, -100000, 100000, 2);
    posRow->addWidget(new QLabel("X")); posRow->addWidget(m_posX);
    posRow->addWidget(new QLabel("Y")); posRow->addWidget(m_posY);
    posRow->addWidget(new QLabel("Z")); posRow->addWidget(m_posZ);
    form->addRow("Loc:", posRow);

    auto* rotRow = new QHBoxLayout();
    m_rotX = makeSpinBox(1.0f, -360, 360, 1);
    m_rotY = makeSpinBox(1.0f, -360, 360, 1);
    m_rotZ = makeSpinBox(1.0f, -360, 360, 1);
    rotRow->addWidget(new QLabel("X")); rotRow->addWidget(m_rotX);
    rotRow->addWidget(new QLabel("Y")); rotRow->addWidget(m_rotY);
    rotRow->addWidget(new QLabel("Z")); rotRow->addWidget(m_rotZ);
    form->addRow("Rot:", rotRow);

    auto* scaleRow = new QHBoxLayout();
    m_scaleX = makeSpinBox(0.1f, 0.01f, 1000, 3);
    m_scaleY = makeSpinBox(0.1f, 0.01f, 1000, 3);
    m_scaleZ = makeSpinBox(0.1f, 0.01f, 1000, 3);
    scaleRow->addWidget(new QLabel("X")); scaleRow->addWidget(m_scaleX);
    scaleRow->addWidget(new QLabel("Y")); scaleRow->addWidget(m_scaleY);
    scaleRow->addWidget(new QLabel("Z")); scaleRow->addWidget(m_scaleZ);
    form->addRow("Scale:", scaleRow);

    lay->addWidget(transformGroup);
    lay->addStretch();

    connect(m_posX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
    connect(m_posY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
    connect(m_posZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
    connect(m_rotX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
    connect(m_rotY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
    connect(m_rotZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
    connect(m_scaleX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
    connect(m_scaleY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
    connect(m_scaleZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NPanel::onTransformChanged);
}

void NPanel::setActor(const QString& actorId)
{
    m_currentActorId = actorId;
    refresh();
}

void NPanel::clear()
{
    m_currentActorId.clear();
    m_titleLabel->setText("Item");
    m_updating = true;
    m_posX->setValue(0); m_posY->setValue(0); m_posZ->setValue(0);
    m_rotX->setValue(0); m_rotY->setValue(0); m_rotZ->setValue(0);
    m_scaleX->setValue(1); m_scaleY->setValue(1); m_scaleZ->setValue(1);
    m_updating = false;
}

void NPanel::refresh()
{
    if (m_currentActorId.isEmpty()) { clear(); return; }
    world::Actor* a = m_ogre->world()->findActor(m_currentActorId);
    if (!a) { clear(); return; }

    m_updating = true;
    m_titleLabel->setText(a->name);
    m_posX->setValue(a->transform.posX);
    m_posY->setValue(a->transform.posY);
    m_posZ->setValue(a->transform.posZ);
    m_rotX->setValue(a->transform.rotX);
    m_rotY->setValue(a->transform.rotY);
    m_rotZ->setValue(a->transform.rotZ);
    m_scaleX->setValue(a->transform.scaleX);
    m_scaleY->setValue(a->transform.scaleY);
    m_scaleZ->setValue(a->transform.scaleZ);
    m_updating = false;
}

void NPanel::onTransformChanged()
{
    if (m_updating || m_currentActorId.isEmpty()) return;
    m_ogre->updateActorTransform(m_currentActorId,
        float(m_posX->value()), float(m_posY->value()), float(m_posZ->value()),
        float(m_rotY->value()),
        float(m_scaleX->value()), float(m_scaleY->value()), float(m_scaleZ->value()));
    emit actorModified(m_currentActorId);
}
