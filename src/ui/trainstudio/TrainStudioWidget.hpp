#pragma once

// ============================================================
// TrainStudioWidget — Embeds LaneMaker's MainWindow directly
// ============================================================
//
// Mirrors RoadStudioWidget. Provides the same LaneMaker-based
// editor UI but with railway OSM import instead of road OSM import.
//

#include <QWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>

// LaneMaker's MainWindow — contains the full editor
#include "main_window.h"

// Forward declaration — full include is in the .cpp
namespace osm { class RailOsmImportDialog; }

class TrainStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit TrainStudioWidget(QWidget* parent = nullptr);

    MainWindow* laneMakerWindow() { return m_lmMainWindow; }

private slots:
    void onImportOsmRail();

private:
    MainWindow* m_lmMainWindow = nullptr;
};
