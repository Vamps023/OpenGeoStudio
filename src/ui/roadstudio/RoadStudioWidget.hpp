#pragma once

// ============================================================
// RoadStudioWidget — Embeds LaneMaker's MainWindow directly
// ============================================================
//
// LaneMaker is now the authoritative road editor. This widget
// creates LaneMaker's MainWindow (which contains MapViewGL,
// MainWidget with all toolbar buttons, LaneConfigWidget,
// DrawOptionDialog, road drawing sessions, etc.) and embeds
// it within the OpenGeoStudio application shell.
//
// The old RoadStudioStore/RoadViewport2D/RoadViewport3D/
// RoadEngineService stack is no longer used for road editing.
// LaneMaker's LM::World, RoadDrawingSession, MapViewGL, and
// ChangeTracker handle everything.
//

#include <QWidget>
#include <QVBoxLayout>

// LaneMaker's MainWindow — contains the full road editor
#include "main_window.h"

class RoadStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit RoadStudioWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Create LaneMaker's MainWindow — this sets up:
        //   - g_mainWindow global
        //   - MainWidget (with MapViewGL, toolbar, LaneConfigWidget)
        //   - Menu bar (File, Edit, Replay, Simulation)
        //   - Status bars (hint, FPS)
        //   - VehicleManager
        //   - ChangeTracker
        //   - ActionManager
        //   - All road drawing sessions
        m_lmMainWindow = new MainWindow(this);
        layout->addWidget(m_lmMainWindow);
    }

    MainWindow* laneMakerWindow() { return m_lmMainWindow; }

private:
    MainWindow* m_lmMainWindow = nullptr;
};
