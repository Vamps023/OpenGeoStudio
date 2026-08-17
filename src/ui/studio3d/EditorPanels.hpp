#pragma once

// ============================================================
// WorldOutliner — Tree view of all actors in the world
// ============================================================

#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QHeaderView>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>

#include "../../core/world/World.hpp"

class OgreWidget;

class WorldOutliner : public QWidget {
    Q_OBJECT
public:
    explicit WorldOutliner(OgreWidget* ogre, QWidget* parent = nullptr);

    void refresh();

private slots:
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onItemSelectionChanged();
    void onSearchChanged(const QString& text);
    void onCustomContextMenu(const QPoint& pos);

signals:
    void actorSelected(const QString& id);
    void actorVisibilityToggled(const QString& id, bool visible);

private:
    void buildTree();
    QTreeWidgetItem* createTreeItem(const world::Actor& actor);
    void addChildren(QTreeWidgetItem* parent, const QString& parentId);
    void filterTree(const QString& text, QTreeWidgetItem* item);

    OgreWidget* m_ogre;
    QTreeWidget* m_tree;
    QLineEdit* m_search;
    bool m_updating = false;
};

// ============================================================
// Inspector — Properties of selected actor
// ============================================================

class Inspector : public QWidget {
    Q_OBJECT
public:
    explicit Inspector(OgreWidget* ogre, QWidget* parent = nullptr);

    void setActor(const QString& actorId);
    void clear();

private slots:
    void onTransformChanged();
    void onNameChanged();
    void onVisibilityToggled(bool visible);
    void onLayerChanged(int index);

signals:
    void actorModified(const QString& id);

private:
    OgreWidget* m_ogre;
    QString m_currentActorId;

    QLineEdit* m_nameEdit;
    QCheckBox* m_visibleCheck;
    QComboBox* m_layerCombo;

    // Transform
    QDoubleSpinBox* m_posX, *m_posY, *m_posZ;
    QDoubleSpinBox* m_rotX, *m_rotY, *m_rotZ;
    QDoubleSpinBox* m_scaleX, *m_scaleY, *m_scaleZ;

    // Color
    QDoubleSpinBox* m_colorR, *m_colorG, *m_colorB;

    bool m_updating = false;
};

// ============================================================
// LayerPanel — Layer management
// ============================================================

class LayerPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerPanel(OgreWidget* ogre, QWidget* parent = nullptr);

    void refresh();

private slots:
    void onAddLayer();
    void onRemoveLayer();
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onItemSelectionChanged();
    void onCustomContextMenu(const QPoint& pos);

signals:
    void layerChanged(const QString& layerId);

private:
    OgreWidget* m_ogre;
    QTreeWidget* m_tree;
    QPushButton* m_addBtn;
    QPushButton* m_removeBtn;
    bool m_updating = false;
};

// ============================================================
// ContentBrowser — Asset library (Unreal-style content browser)
//
// Combines a built-in placeable-actor palette with the project's
// asset files. Double-click places an actor into the viewport.
// ============================================================

class ContentBrowser : public QWidget {
    Q_OBJECT
public:
    explicit ContentBrowser(OgreWidget* ogre, QWidget* parent = nullptr);

    void setAssetDirectory(const QString& path);
    void refresh();

private slots:
    void onItemDoubleClicked(QListWidgetItem* item);
    void onSearchChanged(const QString& text);
    void onCategoryChanged(int index);
    void onImportClicked();

signals:
    // Built-in palette entries emit type "actor" with the ActorType value
    // as a string; file entries emit "mesh"/"texture" with the file path.
    void assetRequested(const QString& path, const QString& type);

private:
    void addActorEntry(const QString& label, world::ActorType type, const QColor& color);
    void addFileEntry(const QFileInfo& fi);
    void applyFilter();
    static QPixmap makeSwatch(const QColor& color, const QString& glyph);

    OgreWidget* m_ogre;
    QListWidget* m_list;
    QLineEdit* m_search;
    QComboBox* m_categoryCombo;
    QPushButton* m_importBtn;
    QPushButton* m_browseBtn;
    QLabel* m_pathLabel;
    QString m_currentDir;
};
