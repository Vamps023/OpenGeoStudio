// TrainStudioWidget implementation
#include "TrainStudioWidget.hpp"

// Rail OSM import dialog - included here (not in header) to avoid
// pulling the full RoadV2 model into the main app's include chain.
#include "RailOsmImportDialog.hpp"

#include <QFrame>
#include <QSizePolicy>
#include <QSettings>

TrainStudioWidget::TrainStudioWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("trainStudioWorkspace"));
    setStyleSheet(
        "QWidget#trainStudioWorkspace { background: #0d1117; }"
        "QFrame#trainCanvasFrame { background: #161b22; border: 1px solid #30363d;"
        "border-radius: 6px; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar with OSM rail import button
    auto* toolbar = new QToolBar(this);
    auto* importOsmAct = new QAction("Import OSM Railways...", this);
    importOsmAct->setToolTip("Import railway network from OpenStreetMap data");
    connect(importOsmAct, &QAction::triggered, this, &TrainStudioWidget::onImportOsmRail);
    toolbar->addAction(importOsmAct);
    layout->addWidget(toolbar);

    auto* canvasFrame = new QFrame(this);
    canvasFrame->setObjectName(QStringLiteral("trainCanvasFrame"));
    canvasFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* canvasLayout = new QVBoxLayout(canvasFrame);
    canvasLayout->setContentsMargins(8, 8, 8, 8);
    canvasLayout->setSpacing(0);

    m_lmMainWindow = new MainWindow(canvasFrame);
    // Switch LaneMaker into rail mode: rail profile catalog,
    // rail cross-section visual, rail terminology.
    m_lmMainWindow->setRailMode(true);
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

void TrainStudioWidget::onImportOsmRail() {
    osm::RailOsmImportDialog dialog(this);
    dialog.exec();
}
