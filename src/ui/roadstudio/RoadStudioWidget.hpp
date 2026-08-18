#pragma once

// ============================================================
// RoadStudioWidget — Embeds LaneMaker's MainWindow directly
// ============================================================

#include <QWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>

// LaneMaker's MainWindow — contains the full road editor
#include "main_window.h"

// Forward declaration — full include is in the .cpp to avoid
// pulling the full RoadV2 model into the main app's include chain
namespace osm { class OsmImportDialog; }
class ApplicationContext;

class RoadStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit RoadStudioWidget(ApplicationContext* ctx = nullptr, QWidget* parent = nullptr);

    MainWindow* laneMakerWindow() { return m_lmMainWindow; }

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void onImportOsm();

private:
    ApplicationContext* m_ctx = nullptr;
    MainWindow* m_lmMainWindow = nullptr;
};
