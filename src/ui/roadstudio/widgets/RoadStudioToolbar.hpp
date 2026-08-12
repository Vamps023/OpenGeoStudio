#pragma once

// ============================================================
// RoadStudioToolbar — Toolbar for Road Studio tools and config
// ============================================================
//
// Replaces RoadToolbar.tsx (modules/road-studio/client/RoadToolbar.tsx).
//

#include "RoadStudioStore.hpp"

#include <QToolBar>
#include <QAction>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>

class RoadStudioToolbar : public QToolBar {
    Q_OBJECT

public:
    explicit RoadStudioToolbar(RoadStudioStore* store, QWidget* parent = nullptr);

private slots:
    void onToolSelect();
    void onToolRoad();
    void onViewModeToggle();
    void onUndo();
    void onRedo();
    void onDeleteSelected();
    void onClearAll();
    void onCreateDemoRoad();
    void onToggleDebug();
    void onSnapToggled(bool enabled);
    void onGridSizeChanged(double size);
    void onWidthChanged(double width);
    void onLaneCountChanged(int count);

private:
    void setupActions();
    void updateActionStates();
    void setupDebugLayerButtons();

    RoadStudioStore* m_store;

    QAction* m_undoAct = nullptr;
    QAction* m_redoAct = nullptr;
    QAction* m_deleteAct = nullptr;
    QCheckBox* m_snapCheck = nullptr;
    QDoubleSpinBox* m_gridSizeSpin = nullptr;
    QDoubleSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_laneCountSpin = nullptr;
    QLabel* m_engineLabel = nullptr;
    QAction* m_debugAct = nullptr;
    QList<QAction*> m_debugLayerActions;
};
