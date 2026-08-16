#include "EditorPanels.hpp"
#include "OgreWidget.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QColorDialog>
#include <QGroupBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QSplitter>

// ============================================================
// WorldOutliner
// ============================================================

WorldOutliner::WorldOutliner(OgreWidget* ogre, QWidget* parent)
    : QWidget(parent), m_ogre(ogre)
{
    auto* layout = new QVBoxLayout(this);

    // Search bar
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search actors...");
    connect(m_search, &QLineEdit::textChanged, this, &WorldOutliner::onSearchChanged);
    layout->addWidget(m_search);

    // Tree
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"Name", "Type"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, &WorldOutliner::onItemChanged);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &WorldOutliner::onItemSelectionChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &WorldOutliner::onCustomContextMenu);
    layout->addWidget(m_tree);

    setLayout(layout);
}

void WorldOutliner::refresh()
{
    m_updating = true;
    m_tree->clear();
    buildTree();
    m_updating = false;
}

void WorldOutliner::buildTree()
{
    if (!m_ogre) return;
    const world::World* w = m_ogre->world();

    // Add root actors (no parent)
    for (const auto& actor : w->actors) {
        if (actor.parentId.isEmpty()) {
            auto* item = createTreeItem(actor);
            m_tree->addTopLevelItem(item);
            addChildren(item, actor.id);
        }
    }
}

QTreeWidgetItem* WorldOutliner::createTreeItem(const world::Actor& actor)
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, actor.name);
    item->setText(1, world::actorTypeToString(actor.type));
    item->setData(0, Qt::UserRole, actor.id);
    item->setCheckState(0, actor.visible ? Qt::Checked : Qt::Unchecked);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);

    // Color based on type
    QColor color;
    switch (actor.type) {
    case world::ActorType::Building: color = QColor(150, 150, 150); break;
    case world::ActorType::Tree: color = QColor(80, 160, 80); break;
    case world::ActorType::Water: color = QColor(80, 120, 200); break;
    case world::ActorType::Road: color = QColor(80, 80, 80); break;
    case world::ActorType::Light: color = QColor(255, 220, 100); break;
    case world::ActorType::Terrain: color = QColor(120, 100, 60); break;
    default: color = QColor(180, 180, 180); break;
    }
    // Create a small colored icon
    QPixmap px(16, 16);
    px.fill(color);
    item->setIcon(0, QIcon(px));

    return item;
}

void WorldOutliner::addChildren(QTreeWidgetItem* parent, const QString& parentId)
{
    const world::World* w = m_ogre->world();
    auto children = w->children(parentId);
    for (const auto* child : children) {
        auto* item = createTreeItem(*child);
        parent->addChild(item);
        addChildren(item, child->id);
    }
}

void WorldOutliner::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updating) return;
    QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) return;

    if (column == 0) {
        // Name or visibility changed
        if (item->checkState(0) == Qt::Checked || item->checkState(0) == Qt::Unchecked) {
            // Visibility toggled
            bool visible = item->checkState(0) == Qt::Checked;
            m_ogre->updateActorVisibility(id, visible);
            emit actorVisibilityToggled(id, visible);
        } else {
            // Name changed
            QString newName = item->text(0);
            m_ogre->renameActor(id, newName);
        }
    }
}

void WorldOutliner::onItemSelectionChanged()
{
    auto items = m_tree->selectedItems();
    if (items.isEmpty()) return;

    QString id = items.first()->data(0, Qt::UserRole).toString();
    if (!id.isEmpty()) {
        m_ogre->selectActor(id);
        emit actorSelected(id);
    }
}

void WorldOutliner::onSearchChanged(const QString& text)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); i++) {
        filterTree(text, m_tree->topLevelItem(i));
    }
}

void WorldOutliner::filterTree(const QString& text, QTreeWidgetItem* item)
{
    bool match = item->text(0).contains(text, Qt::CaseInsensitive);
    bool anyChildMatch = false;

    for (int i = 0; i < item->childCount(); i++) {
        filterTree(text, item->child(i));
        if (item->child(i)->isHidden() == false)
            anyChildMatch = true;
    }

    item->setHidden(!match && !anyChildMatch);
}

void WorldOutliner::onCustomContextMenu(const QPoint& pos)
{
    auto* item = m_tree->itemAt(pos);
    if (!item) return;

    QString id = item->data(0, Qt::UserRole).toString();
    QMenu menu(this);

    QAction* renameAct = menu.addAction("Rename");
    QAction* deleteAct = menu.addAction("Delete");
    menu.addSeparator();
    QAction* duplicateAct = menu.addAction("Duplicate");

    QAction* selected = menu.exec(m_tree->mapToGlobal(pos));
    if (selected == renameAct) {
        m_tree->editItem(item, 0);
    } else if (selected == deleteAct) {
        m_ogre->removeActor(id);
        refresh();
    } else if (selected == duplicateAct) {
        // Duplicate actor
        world::Actor* src = m_ogre->world()->findActor(id);
        if (src) {
            world::Actor dup = *src;
            dup.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            dup.name = src->name + "_copy";
            dup.transform.posX += 10;
            m_ogre->world()->addActor(dup);
            refresh();
        }
    }
}

// ============================================================
// Inspector
// ============================================================

Inspector::Inspector(OgreWidget* ogre, QWidget* parent)
    : QWidget(parent), m_ogre(ogre)
{
    auto* layout = new QVBoxLayout(this);

    // Name
    auto* nameGroup = new QGroupBox("Actor", this);
    auto* nameLayout = new QFormLayout(nameGroup);
    m_nameEdit = new QLineEdit(this);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &Inspector::onNameChanged);
    nameLayout->addRow("Name:", m_nameEdit);
    m_visibleCheck = new QCheckBox("Visible", this);
    connect(m_visibleCheck, &QCheckBox::toggled, this, &Inspector::onVisibilityToggled);
    nameLayout->addRow("", m_visibleCheck);
    m_layerCombo = new QComboBox(this);
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &Inspector::onLayerChanged);
    nameLayout->addRow("Layer:", m_layerCombo);
    layout->addWidget(nameGroup);

    // Transform
    auto* transformGroup = new QGroupBox("Transform", this);
    auto* transformLayout = new QFormLayout(transformGroup);

    m_posX = new QDoubleSpinBox(this); m_posX->setRange(-100000, 100000); m_posX->setDecimals(2);
    m_posY = new QDoubleSpinBox(this); m_posY->setRange(-100000, 100000); m_posY->setDecimals(2);
    m_posZ = new QDoubleSpinBox(this); m_posZ->setRange(-100000, 100000); m_posZ->setDecimals(2);
    transformLayout->addRow("Pos X:", m_posX);
    transformLayout->addRow("Pos Y:", m_posY);
    transformLayout->addRow("Pos Z:", m_posZ);

    m_rotX = new QDoubleSpinBox(this); m_rotX->setRange(-360, 360); m_rotX->setDecimals(2);
    m_rotY = new QDoubleSpinBox(this); m_rotY->setRange(-360, 360); m_rotY->setDecimals(2);
    m_rotZ = new QDoubleSpinBox(this); m_rotZ->setRange(-360, 360); m_rotZ->setDecimals(2);
    transformLayout->addRow("Rot X:", m_rotX);
    transformLayout->addRow("Rot Y:", m_rotY);
    transformLayout->addRow("Rot Z:", m_rotZ);

    m_scaleX = new QDoubleSpinBox(this); m_scaleX->setRange(0.01, 1000); m_scaleX->setDecimals(3); m_scaleX->setSingleStep(0.1);
    m_scaleY = new QDoubleSpinBox(this); m_scaleY->setRange(0.01, 1000); m_scaleY->setDecimals(3); m_scaleY->setSingleStep(0.1);
    m_scaleZ = new QDoubleSpinBox(this); m_scaleZ->setRange(0.01, 1000); m_scaleZ->setDecimals(3); m_scaleZ->setSingleStep(0.1);
    transformLayout->addRow("Scale X:", m_scaleX);
    transformLayout->addRow("Scale Y:", m_scaleY);
    transformLayout->addRow("Scale Z:", m_scaleZ);

    for (auto* spin : {m_posX, m_posY, m_posZ, m_rotX, m_rotY, m_rotZ, m_scaleX, m_scaleY, m_scaleZ}) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &Inspector::onTransformChanged);
    }

    layout->addWidget(transformGroup);

    // Color
    auto* colorGroup = new QGroupBox("Color", this);
    auto* colorLayout = new QFormLayout(colorGroup);
    m_colorR = new QDoubleSpinBox(this); m_colorR->setRange(0, 1); m_colorR->setDecimals(3); m_colorR->setSingleStep(0.05);
    m_colorG = new QDoubleSpinBox(this); m_colorG->setRange(0, 1); m_colorG->setDecimals(3); m_colorG->setSingleStep(0.05);
    m_colorB = new QDoubleSpinBox(this); m_colorB->setRange(0, 1); m_colorB->setDecimals(3); m_colorB->setSingleStep(0.05);
    colorLayout->addRow("R:", m_colorR);
    colorLayout->addRow("G:", m_colorG);
    colorLayout->addRow("B:", m_colorB);
    layout->addWidget(colorGroup);

    layout->addStretch();
    setLayout(layout);
}

void Inspector::setActor(const QString& actorId)
{
    m_currentActorId = actorId;
    if (actorId.isEmpty()) {
        clear();
        return;
    }

    world::Actor* a = m_ogre->world()->findActor(actorId);
    if (!a) {
        clear();
        return;
    }

    m_updating = true;

    m_nameEdit->setText(a->name);
    m_visibleCheck->setChecked(a->visible);

    // Populate layer combo
    m_layerCombo->clear();
    for (const auto& l : m_ogre->world()->layers) {
        m_layerCombo->addItem(l.name, l.id);
        if (l.id == a->layerId)
            m_layerCombo->setCurrentIndex(m_layerCombo->count() - 1);
    }

    m_posX->setValue(a->transform.posX);
    m_posY->setValue(a->transform.posY);
    m_posZ->setValue(a->transform.posZ);
    m_rotX->setValue(a->transform.rotX);
    m_rotY->setValue(a->transform.rotY);
    m_rotZ->setValue(a->transform.rotZ);
    m_scaleX->setValue(a->transform.scaleX);
    m_scaleY->setValue(a->transform.scaleY);
    m_scaleZ->setValue(a->transform.scaleZ);

    m_colorR->setValue(a->colorR);
    m_colorG->setValue(a->colorG);
    m_colorB->setValue(a->colorB);

    m_updating = false;
}

void Inspector::clear()
{
    m_currentActorId.clear();
    m_updating = true;
    m_nameEdit->clear();
    m_visibleCheck->setChecked(false);
    m_layerCombo->clear();
    for (auto* spin : {m_posX, m_posY, m_posZ, m_rotX, m_rotY, m_rotZ, m_scaleX, m_scaleY, m_scaleZ})
        spin->setValue(0);
    m_updating = false;
}

void Inspector::onTransformChanged()
{
    if (m_updating || m_currentActorId.isEmpty()) return;
    m_ogre->updateActorTransform(m_currentActorId,
        float(m_posX->value()), float(m_posY->value()), float(m_posZ->value()),
        float(m_rotY->value()),
        float(m_scaleX->value()), float(m_scaleY->value()), float(m_scaleZ->value()));
    emit actorModified(m_currentActorId);
}

void Inspector::onNameChanged()
{
    if (m_updating || m_currentActorId.isEmpty()) return;
    m_ogre->renameActor(m_currentActorId, m_nameEdit->text());
    emit actorModified(m_currentActorId);
}

void Inspector::onVisibilityToggled(bool visible)
{
    if (m_updating || m_currentActorId.isEmpty()) return;
    m_ogre->updateActorVisibility(m_currentActorId, visible);
}

void Inspector::onLayerChanged(int index)
{
    if (m_updating || m_currentActorId.isEmpty() || index < 0) return;
    QString layerId = m_layerCombo->itemData(index).toString();
    m_ogre->updateActorLayer(m_currentActorId, layerId);
}

// ============================================================
// LayerPanel
// ============================================================

LayerPanel::LayerPanel(OgreWidget* ogre, QWidget* parent)
    : QWidget(parent), m_ogre(ogre)
{
    auto* layout = new QVBoxLayout(this);

    // Button row
    auto* btnLayout = new QHBoxLayout();
    m_addBtn = new QPushButton("+", this);
    m_addBtn->setMaximumWidth(30);
    connect(m_addBtn, &QPushButton::clicked, this, &LayerPanel::onAddLayer);
    btnLayout->addWidget(m_addBtn);

    m_removeBtn = new QPushButton("-", this);
    m_removeBtn->setMaximumWidth(30);
    connect(m_removeBtn, &QPushButton::clicked, this, &LayerPanel::onRemoveLayer);
    btnLayout->addWidget(m_removeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    // Tree
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(3);
    m_tree->setHeaderLabels({"Name", "Vis", "Lock"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeWidget::itemChanged, this, &LayerPanel::onItemChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &LayerPanel::onCustomContextMenu);
    layout->addWidget(m_tree);

    setLayout(layout);
}

void LayerPanel::refresh()
{
    m_updating = true;
    m_tree->clear();

    for (const auto& l : m_ogre->world()->layers) {
        auto* item = new QTreeWidgetItem();
        item->setText(0, l.name);
        item->setData(0, Qt::UserRole, l.id);
        item->setCheckState(1, l.visible ? Qt::Checked : Qt::Unchecked);
        item->setCheckState(2, l.locked ? Qt::Checked : Qt::Unchecked);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEditable);
        if (l.isDefault) {
            QFont f = item->font(0); f.setBold(true);
            item->setFont(0, f);
        }
        m_tree->addTopLevelItem(item);
    }

    m_updating = false;
}

void LayerPanel::onAddLayer()
{
    QString name = QString("Layer_%1").arg(m_ogre->world()->layerCount());
    m_ogre->world()->addLayer(name);
    refresh();
}

void LayerPanel::onRemoveLayer()
{
    auto* item = m_tree->currentItem();
    if (!item) return;
    QString id = item->data(0, Qt::UserRole).toString();
    if (id == "default") {
        QMessageBox::warning(this, "Cannot Remove", "The default layer cannot be removed.");
        return;
    }
    m_ogre->removeLayer(id);
    refresh();
}

void LayerPanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updating) return;
    QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) return;

    if (column == 0) {
        // Name changed
        world::Layer* l = m_ogre->world()->findLayer(id);
        if (l) {
            l->name = item->text(0);
            emit layerChanged(id);
        }
    } else if (column == 1) {
        // Visibility
        bool visible = item->checkState(1) == Qt::Checked;
        m_ogre->setLayerVisible(id, visible);
        emit layerChanged(id);
    } else if (column == 2) {
        // Lock
        bool locked = item->checkState(2) == Qt::Checked;
        m_ogre->setLayerLocked(id, locked);
        emit layerChanged(id);
    }
}

void LayerPanel::onItemSelectionChanged()
{
    // Could emit selected layer
}

void LayerPanel::onCustomContextMenu(const QPoint& pos)
{
    auto* item = m_tree->itemAt(pos);
    if (!item) return;

    QString id = item->data(0, Qt::UserRole).toString();
    QMenu menu(this);
    QAction* renameAct = menu.addAction("Rename");
    QAction* selected = menu.exec(m_tree->mapToGlobal(pos));
    if (selected == renameAct) {
        m_tree->editItem(item, 0);
    }
}

// ============================================================
// ContentBrowser
// ============================================================

ContentBrowser::ContentBrowser(OgreWidget* ogre, QWidget* parent)
    : QWidget(parent), m_ogre(ogre)
{
    auto* layout = new QVBoxLayout(this);

    // Path bar
    auto* pathLayout = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_pathEdit->setReadOnly(true);
    pathLayout->addWidget(m_pathEdit);
    m_browseBtn = new QPushButton("...", this);
    m_browseBtn->setMaximumWidth(30);
    connect(m_browseBtn, &QPushButton::clicked, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Asset Directory");
        if (!dir.isEmpty()) setAssetDirectory(dir);
    });
    pathLayout->addWidget(m_browseBtn);
    layout->addLayout(pathLayout);

    // Asset list
    m_list = new QListWidget(this);
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(48, 48));
    m_list->setResizeMode(QListWidget::Adjust);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ContentBrowser::onItemDoubleClicked);
    layout->addWidget(m_list);

    setLayout(layout);
}

void ContentBrowser::setAssetDirectory(const QString& path)
{
    m_currentDir = path;
    m_pathEdit->setText(path);
    refresh();
}

void ContentBrowser::refresh()
{
    m_list->clear();
    if (m_currentDir.isEmpty()) return;

    QDir dir(m_currentDir);
    if (!dir.exists()) return;

    // List files
    QStringList filters;
    filters << "*.obj" << "*.fbx" << "*.mesh" << "*.glb" << "*.gltf" << "*.png" << "*.jpg" << "*.tif";
    QStringList files = dir.entryList(filters, QDir::Files);
    for (const auto& f : files) {
        auto* item = new QListWidgetItem(f, m_list);
        item->setData(Qt::UserRole, m_currentDir + "/" + f);
        // Set icon based on type
        QString ext = QFileInfo(f).suffix().toLower();
        if (ext == "png" || ext == "jpg" || ext == "tif")
            item->setIcon(QIcon(m_currentDir + "/" + f));
        else
            item->setIcon(QIcon::fromTheme("document"));
    }
}

void ContentBrowser::onItemDoubleClicked(QListWidgetItem* item)
{
    QString path = item->data(Qt::UserRole).toString();
    QString ext = QFileInfo(path).suffix().toLower();
    QString type = "mesh";
    if (ext == "png" || ext == "jpg" || ext == "tif") type = "texture";
    emit assetRequested(path, type);
}
