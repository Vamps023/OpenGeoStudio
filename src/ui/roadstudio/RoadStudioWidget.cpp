// RoadStudioWidget implementation
#include "RoadStudioWidget.hpp"

// OSM import dialog - included here (not in header) to avoid
// pulling the full RoadV2 model into the main app's include chain.
// The OSM headers include the full road_v2.hpp which conflicts
// with the public placeholder road_v2.hpp used by road_engine.hpp.
#include "OsmImportDialog.hpp"

#include <QFrame>
#include <QSizePolicy>
#include <QSettings>

RoadStudioWidget::RoadStudioWidget(ApplicationContext* ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx)
{
    setObjectName(QStringLiteral("roadStudioWorkspace"));
    setStyleSheet(
        "QWidget#roadStudioWorkspace { background: #0d1117; }"
        "QFrame#roadCanvasFrame { background: #161b22; border: 1px solid #30363d;"
        "border-radius: 6px; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar with OSM import button
    auto* toolbar = new QToolBar(this);
    auto* importOsmAction = new QAction("Import OSM...", this);
    importOsmAction->setToolTip("Import road network from OpenStreetMap data");
    connect(importOsmAction, &QAction::triggered, this, &RoadStudioWidget::onImportOsm);
    toolbar->addAction(importOsmAction);
    layout->addWidget(toolbar);

    auto* canvasFrame = new QFrame(this);
    canvasFrame->setObjectName(QStringLiteral("roadCanvasFrame"));
    canvasFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* canvasLayout = new QVBoxLayout(canvasFrame);
    canvasLayout->setContentsMargins(8, 8, 8, 8);
    canvasLayout->setSpacing(0);

    m_lmMainWindow = new MainWindow(canvasFrame);
    // Ensure road mode (in case a previous Train Studio session left rail mode active)
    m_lmMainWindow->setRailMode(false);
    // Use persisted map view (defaults to a land-based satellite view on first launch)
    {
        QSettings s;
        const double lat = s.value("map/default_lat", 18.52).toDouble();
        const double lon = s.value("map/default_lon", 73.85).toDouble();
        const double zoom = s.value("map/default_zoom", 15.0).toDouble();
        m_lmMainWindow->useSharedSatelliteView(lat, lon, zoom);
    }
    m_lmMainWindow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    canvasLayout->addWidget(m_lmMainWindow, 1);
    layout->addWidget(canvasFrame, 1);
}

void RoadStudioWidget::onImportOsm() {
    osm::OsmImportDialog dialog(this, m_ctx);
    dialog.exec();

    // Load the freshly exported network straight into the editor when the
    // user asked for it (Road Studio page is already visible, so LaneMaker's
    // GL context is ready).
    if (dialog.openInEditorRequested() && !dialog.lastExportedXodr().isEmpty()) {
        m_lmMainWindow->loadFromPath(dialog.lastExportedXodr());
    }
}
