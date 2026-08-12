// RoadStudioToolbar — Toolbar implementation

#include "RoadStudioToolbar.hpp"
#include "road_engine.hpp"

#include <QIcon>
#include <QActionGroup>
#include <QWidgetAction>
#include <QLabel>
#include <QHBoxLayout>

RoadStudioToolbar::RoadStudioToolbar(RoadStudioStore* store, QWidget* parent)
    : QToolBar("Road Studio", parent), m_store(store) {
    setMovable(false);
    setupActions();
    updateActionStates();

    connect(m_store, &RoadStudioStore::historyChanged, this, &RoadStudioToolbar::updateActionStates);
    connect(m_store, &RoadStudioStore::toolChanged, this, &RoadStudioToolbar::updateActionStates);
}

void RoadStudioToolbar::setupActions() {
    // Tool selection
    auto* toolGroup = new QActionGroup(this);

    QAction* selectAct = addAction("Select (V)");
    selectAct->setShortcut(QKeySequence("V"));
    selectAct->setCheckable(true);
    selectAct->setChecked(m_store->tool() == roads::Tool::Select);
    toolGroup->addAction(selectAct);
    connect(selectAct, &QAction::triggered, this, &RoadStudioToolbar::onToolSelect);

    QAction* roadAct = addAction("Road (R)");
    roadAct->setShortcut(QKeySequence("R"));
    roadAct->setCheckable(true);
    roadAct->setChecked(m_store->tool() == roads::Tool::Road);
    toolGroup->addAction(roadAct);
    connect(roadAct, &QAction::triggered, this, &RoadStudioToolbar::onToolRoad);

    addSeparator();

    // View mode toggle
    QAction* viewModeAct = addAction("2D/3D");
    viewModeAct->setShortcut(QKeySequence("Tab"));
    connect(viewModeAct, &QAction::triggered, this, &RoadStudioToolbar::onViewModeToggle);

    addSeparator();

    // Undo/Redo
    m_undoAct = addAction("Undo");
    m_undoAct->setShortcut(QKeySequence::Undo);
    connect(m_undoAct, &QAction::triggered, this, &RoadStudioToolbar::onUndo);

    m_redoAct = addAction("Redo");
    m_redoAct->setShortcut(QKeySequence::Redo);
    connect(m_redoAct, &QAction::triggered, this, &RoadStudioToolbar::onRedo);

    addSeparator();

    // Snapping toggle
    auto* snapWidget = new QWidget(this);
    auto* snapLayout = new QHBoxLayout(snapWidget);
    snapLayout->setContentsMargins(4, 0, 4, 0);
    m_snapCheck = new QCheckBox("Snap", snapWidget);
    m_snapCheck->setChecked(m_store->snapEnabled());
    connect(m_snapCheck, &QCheckBox::toggled, this, &RoadStudioToolbar::onSnapToggled);
    snapLayout->addWidget(m_snapCheck);
    auto* snapAction = new QWidgetAction(this);
    snapAction->setDefaultWidget(snapWidget);
    addAction(snapAction);

    // Grid size
    auto* gridWidget = new QWidget(this);
    auto* gridLayout = new QHBoxLayout(gridWidget);
    gridLayout->setContentsMargins(4, 0, 4, 0);
    gridLayout->addWidget(new QLabel("Grid:", gridWidget));
    m_gridSizeSpin = new QDoubleSpinBox(gridWidget);
    m_gridSizeSpin->setRange(1.0, 100.0);
    m_gridSizeSpin->setValue(m_store->gridSize());
    m_gridSizeSpin->setSuffix(" m");
    connect(m_gridSizeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &RoadStudioToolbar::onGridSizeChanged);
    gridLayout->addWidget(m_gridSizeSpin);
    auto* gridAction = new QWidgetAction(this);
    gridAction->setDefaultWidget(gridWidget);
    addAction(gridAction);

    addSeparator();

    // Lane config
    auto* widthWidget = new QWidget(this);
    auto* widthLayout = new QHBoxLayout(widthWidget);
    widthLayout->setContentsMargins(4, 0, 4, 0);
    widthLayout->addWidget(new QLabel("Width:", widthWidget));
    m_widthSpin = new QDoubleSpinBox(widthWidget);
    m_widthSpin->setRange(2.0, 30.0);
    m_widthSpin->setValue(m_store->defaultWidth());
    m_widthSpin->setSuffix(" m");
    connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &RoadStudioToolbar::onWidthChanged);
    widthLayout->addWidget(m_widthSpin);
    auto* widthAction = new QWidgetAction(this);
    widthAction->setDefaultWidget(widthWidget);
    addAction(widthAction);

    auto* laneWidget = new QWidget(this);
    auto* laneLayout = new QHBoxLayout(laneWidget);
    laneLayout->setContentsMargins(4, 0, 4, 0);
    laneLayout->addWidget(new QLabel("Lanes:", laneWidget));
    m_laneCountSpin = new QSpinBox(laneWidget);
    m_laneCountSpin->setRange(1, 8);
    m_laneCountSpin->setValue(m_store->defaultLaneCount());
    connect(m_laneCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &RoadStudioToolbar::onLaneCountChanged);
    laneLayout->addWidget(m_laneCountSpin);
    auto* laneAction = new QWidgetAction(this);
    laneAction->setDefaultWidget(laneWidget);
    addAction(laneAction);

    addSeparator();

    // Demo road
    QAction* demoAct = addAction("Demo Road");
    connect(demoAct, &QAction::triggered, this, &RoadStudioToolbar::onCreateDemoRoad);

    // Delete selected
    m_deleteAct = addAction("Delete Selected");
    m_deleteAct->setShortcut(QKeySequence::Delete);
    connect(m_deleteAct, &QAction::triggered, this, &RoadStudioToolbar::onDeleteSelected);

    // Clear all
    QAction* clearAct = addAction("Clear All");
    connect(clearAct, &QAction::triggered, this, &RoadStudioToolbar::onClearAll);

    addSeparator();

    // Debug mode toggle
    QAction* debugAct = addAction("Debug (Ctrl+Shift+G)");
    debugAct->setShortcut(QKeySequence("Ctrl+Shift+G"));
    debugAct->setCheckable(true);
    debugAct->setChecked(m_store->debugMode());
    connect(debugAct, &QAction::triggered, this, &RoadStudioToolbar::onToggleDebug);

    addSeparator();

    // Engine status indicator
    auto* engineWidget = new QWidget(this);
    auto* engineLayout = new QHBoxLayout(engineWidget);
    engineLayout->setContentsMargins(4, 0, 4, 0);
    const QString engineVersion = QString::fromLatin1(road_engine::versionString());
    m_engineLabel = new QLabel("C++ Engine v" + engineVersion, engineWidget);
    m_engineLabel->setStyleSheet("color: #4a4; font-weight: bold;");
    engineLayout->addWidget(m_engineLabel);
    auto* engineAction = new QWidgetAction(this);
    engineAction->setDefaultWidget(engineWidget);
    addAction(engineAction);
}

void RoadStudioToolbar::updateActionStates() {
    m_undoAct->setEnabled(m_store->canUndo());
    m_redoAct->setEnabled(m_store->canRedo());
    m_deleteAct->setEnabled(!m_store->selection().isEmpty());
}

void RoadStudioToolbar::onToolSelect() {
    m_store->setTool(roads::Tool::Select);
}

void RoadStudioToolbar::onToolRoad() {
    m_store->setTool(roads::Tool::Road);
}

void RoadStudioToolbar::onViewModeToggle() {
    if (m_store->viewMode() == roads::ViewMode::Top) {
        m_store->setViewMode(roads::ViewMode::Perspective);
    } else {
        m_store->setViewMode(roads::ViewMode::Top);
    }
}

void RoadStudioToolbar::onUndo() {
    m_store->undo();
}

void RoadStudioToolbar::onRedo() {
    m_store->redo();
}

void RoadStudioToolbar::onDeleteSelected() {
    const auto& sel = m_store->selection();
    if (!sel.roadId.isEmpty()) {
        m_store->deleteRoad(sel.roadId);
    }
}

void RoadStudioToolbar::onClearAll() {
    m_store->clearAll();
}

void RoadStudioToolbar::onCreateDemoRoad() {
    m_store->createDemoRoad();
}

void RoadStudioToolbar::onToggleDebug() {
    m_store->toggleDebugMode();
}

void RoadStudioToolbar::onSnapToggled(bool enabled) {
    m_store->setSnapEnabled(enabled);
}

void RoadStudioToolbar::onGridSizeChanged(double size) {
    m_store->setGridSize(size);
}

void RoadStudioToolbar::onWidthChanged(double width) {
    m_store->setDefaultWidth(width);
}

void RoadStudioToolbar::onLaneCountChanged(int count) {
    m_store->setDefaultLaneCount(count);
}
