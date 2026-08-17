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
#include <QPainter>
#include <QFile>

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
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);
    setObjectName(QStringLiteral("contentBrowser"));

    // Header: category filter + search + import + browse
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(6);

    m_categoryCombo = new QComboBox(this);
    m_categoryCombo->addItems({"All", "Placeable Actors", "Meshes", "Textures"});
    m_categoryCombo->setToolTip("Filter the asset library by category");
    connect(m_categoryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ContentBrowser::onCategoryChanged);
    headerLayout->addWidget(m_categoryCombo);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Search assets…");
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &ContentBrowser::onSearchChanged);
    headerLayout->addWidget(m_search, 1);

    m_importBtn = new QPushButton("Import…", this);
    m_importBtn->setToolTip("Copy mesh or texture files into the project asset folder");
    connect(m_importBtn, &QPushButton::clicked, this, &ContentBrowser::onImportClicked);
    headerLayout->addWidget(m_importBtn);

    m_browseBtn = new QPushButton("Folder…", this);
    m_browseBtn->setToolTip("Choose a different asset folder to browse");
    connect(m_browseBtn, &QPushButton::clicked, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Asset Directory");
        if (!dir.isEmpty()) setAssetDirectory(dir);
    });
    headerLayout->addWidget(m_browseBtn);
    layout->addLayout(headerLayout);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setStyleSheet("QLabel { color: #7d8590; font-size: 10px; }");
    layout->addWidget(m_pathLabel);

    // Asset grid
    m_list = new QListWidget(this);
    m_list->setViewMode(QListWidget::IconMode);
    m_list->setIconSize(QSize(48, 48));
    m_list->setGridSize(QSize(96, 86));
    m_list->setResizeMode(QListWidget::Adjust);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setWordWrap(true);
    m_list->setSpacing(6);
    m_list->setToolTip("Double-click a placeable actor to add it to the scene at the camera focus");
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ContentBrowser::onItemDoubleClicked);
    layout->addWidget(m_list, 1);

    setLayout(layout);
}

QPixmap ContentBrowser::makeSwatch(const QColor& color, const QString& glyph)
{
    QPixmap pm(64, 64);
    pm.fill(color);
    QPainter p(&pm);
    p.setPen(Qt::white);
    QFont f = p.font();
    f.setPixelSize(30);
    f.setBold(true);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, glyph);
    return pm;
}

void ContentBrowser::addActorEntry(const QString& label, world::ActorType type, const QColor& color)
{
    auto* item = new QListWidgetItem(label, m_list);
    item->setIcon(QIcon(makeSwatch(color, label.left(1))));
    item->setData(Qt::UserRole, QStringLiteral("actor"));
    item->setData(Qt::UserRole + 1, static_cast<int>(type));
    item->setToolTip(QString("Double-click to place a %1 at the camera focus").arg(label));
}

void ContentBrowser::addFileEntry(const QFileInfo& fi)
{
    auto* item = new QListWidgetItem(fi.fileName(), m_list);
    const QString path = fi.absoluteFilePath();
    const QString ext = fi.suffix().toLower();
    item->setData(Qt::UserRole, QStringLiteral("file"));
    item->setData(Qt::UserRole + 2, path);
    if (ext == "png" || ext == "jpg" || ext == "jpeg")
        item->setIcon(QIcon(path));
    else if (ext == "tif" || ext == "tiff" || ext == "dds" || ext == "tga")
        item->setIcon(QIcon(makeSwatch(QColor(56, 108, 176), "T")));
    else
        item->setIcon(QIcon(makeSwatch(QColor(47, 79, 79), "M")));
    item->setToolTip(QString("Double-click to place as a prop:\n%1").arg(path));
}

void ContentBrowser::setAssetDirectory(const QString& path)
{
    m_currentDir = path;
    // Make sure imports have somewhere to land
    if (!m_currentDir.isEmpty())
        QDir().mkpath(m_currentDir);
    m_pathLabel->setText(m_currentDir);
    refresh();
}

void ContentBrowser::refresh()
{
    m_list->clear();

    // Built-in placeable palette — always available, even with no files
    addActorEntry("Cube", world::ActorType::Prop, QColor(120, 128, 144));
    addActorEntry("Empty", world::ActorType::Empty, QColor(60, 66, 78));
    addActorEntry("Building", world::ActorType::Building, QColor(141, 153, 174));
    addActorEntry("Prop", world::ActorType::Prop, QColor(176, 125, 98));
    addActorEntry("Tree", world::ActorType::Tree, QColor(45, 106, 79));
    addActorEntry("Vegetation", world::ActorType::Vegetation, QColor(64, 145, 108));
    addActorEntry("Grass", world::ActorType::Grass, QColor(116, 198, 157));
    addActorEntry("Rock", world::ActorType::Rock, QColor(108, 117, 125));
    addActorEntry("Lake", world::ActorType::Water, QColor(67, 97, 238));
    addActorEntry("Sun Light", world::ActorType::SunLight, QColor(255, 209, 102));
    addActorEntry("Sky Light", world::ActorType::SkyLight, QColor(114, 239, 221));

    if (m_currentDir.isEmpty()) { applyFilter(); return; }

    QDir dir(m_currentDir);
    if (!dir.exists()) { applyFilter(); return; }

    QStringList filters;
    filters << "*.obj" << "*.fbx" << "*.mesh" << "*.glb" << "*.gltf"
            << "*.png" << "*.jpg" << "*.jpeg" << "*.tif" << "*.tiff"
            << "*.dds" << "*.tga";
    const QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files)
        addFileEntry(fi);

    applyFilter();
}

void ContentBrowser::applyFilter()
{
    const QString needle = m_search->text().trimmed();
    const int category = m_categoryCombo->currentIndex(); // 0 all, 1 actors, 2 meshes, 3 textures

    for (int i = 0; i < m_list->count(); ++i) {
        auto* item = m_list->item(i);
        const QString kind = item->data(Qt::UserRole).toString();
        bool visible = true;

        if (category == 1 && kind != "actor") visible = false;
        if (category == 2 || category == 3) {
            if (kind != "file") visible = false;
            const QString ext = QFileInfo(item->data(Qt::UserRole + 2).toString()).suffix().toLower();
            const bool isMesh = (ext == "obj" || ext == "fbx" || ext == "mesh" ||
                                 ext == "glb" || ext == "gltf");
            if (category == 2 && !isMesh) visible = false;
            if (category == 3 && isMesh) visible = false;
        }
        if (visible && !needle.isEmpty())
            visible = item->text().contains(needle, Qt::CaseInsensitive);

        item->setHidden(!visible);
    }
}

void ContentBrowser::onSearchChanged(const QString& text)
{
    Q_UNUSED(text);
    applyFilter();
}

void ContentBrowser::onCategoryChanged(int index)
{
    Q_UNUSED(index);
    applyFilter();
}

void ContentBrowser::onImportClicked()
{
    QString targetDir = m_currentDir;
    if (targetDir.isEmpty()) {
        targetDir = QFileDialog::getExistingDirectory(this, "Select Asset Directory to Import Into");
        if (targetDir.isEmpty()) return;
        setAssetDirectory(targetDir);
    }

    const QStringList files = QFileDialog::getOpenFileNames(this, "Import Assets", targetDir,
        "Assets (*.obj *.fbx *.mesh *.glb *.gltf *.png *.jpg *.jpeg *.tif *.tiff *.dds *.tga)");
    if (files.isEmpty()) return;

    int imported = 0;
    for (const QString& src : files) {
        const QString dst = QDir(targetDir).filePath(QFileInfo(src).fileName());
        if (QFile::exists(dst)) continue;
        if (QFile::copy(src, dst)) imported++;
    }
    refresh();
    if (imported > 0)
        m_pathLabel->setText(QString("Imported %1 file(s) → %2").arg(imported).arg(targetDir));
}

void ContentBrowser::onItemDoubleClicked(QListWidgetItem* item)
{
    const QString kind = item->data(Qt::UserRole).toString();
    if (kind == "actor") {
        const int typeValue = item->data(Qt::UserRole + 1).toInt();
        emit assetRequested(QString::number(typeValue), QStringLiteral("actor"));
        return;
    }

    const QString path = item->data(Qt::UserRole + 2).toString();
    const QString ext = QFileInfo(path).suffix().toLower();
    QString type = "mesh";
    if (ext == "png" || ext == "jpg" || ext == "jpeg" ||
        ext == "tif" || ext == "tiff" || ext == "dds" || ext == "tga")
        type = "texture";
    emit assetRequested(path, type);
}
