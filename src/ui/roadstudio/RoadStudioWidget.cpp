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
#include <QDir>
#include <QFileInfo>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>

#include "../../theme/Theme.hpp"
#include "main_widget.h"        // full MainWidget def — needed for setDrapeProvider
#include "DrapeOntoTerrain.hpp"

// ponytail: searches cwd / road-file dir upward for the merged DEM; if the
// project layout changes, replace with a path from the Project settings store.
static QString findProjectHeightmap()
{
    QStringList dirs;
    dirs << QDir::currentPath();
    if (g_mainWindow && !g_mainWindow->loadedFilePath().isEmpty())
        dirs << QFileInfo(g_mainWindow->loadedFilePath()).absolutePath();
    for (const QString& d : dirs)
    {
        QDir dir(d);
        for (int i = 0; i < 6 && dir.exists(); ++i)
        {
            const QStringList candidates = {
                dir.filePath("Terrain/heightmap_merged.tif"),
                dir.filePath("terrain/heightmap_merged.tif"),
                dir.filePath("heightmap_merged.tif"),
            };
            for (const QString& c : candidates)
                if (QFileInfo::exists(c)) return c;
            if (!dir.cdUp()) break;
        }
    }
    return {};
}

RoadStudioWidget::RoadStudioWidget(ApplicationContext* ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx)
{
    setObjectName(QStringLiteral("roadStudioWorkspace"));
    setStyleSheet(
        QStringLiteral("QWidget#roadStudioWorkspace { background: %1; }"
        "QFrame#roadCanvasFrame { background: %2; border: 1px solid %3;"
        "border-radius: 6px; }")
            .arg(ogs::theme::c::BgBase, ogs::theme::c::BgSurface, ogs::theme::c::Border));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Toolbar with OSM import + SCANeR data set export
    auto* toolbar = new QToolBar(this);
    auto* importOsmAction = new QAction("Import OSM...", this);
    importOsmAction->setToolTip("Import road network from OpenStreetMap data");
    connect(importOsmAction, &QAction::triggered, this, &RoadStudioWidget::onImportOsm);
    toolbar->addAction(importOsmAction);
    auto* exportScanerAction = new QAction("Export \u2192 SCANeR data set\u2026", this);
    exportScanerAction->setToolTip(
        "Write the road network (.xodr) and terrain (GeoTIFF + .earth) "
        "into a SCANeR studio data set folder");
    connect(exportScanerAction, &QAction::triggered, this, &RoadStudioWidget::onExportScanerDataSet);
    toolbar->addAction(exportScanerAction);
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

    // Wire the terrain-drape provider (app-layer implementation). LaneMaker
    // itself never links against roadstudio — dependency inversion.
    if (auto* mw = m_lmMainWindow->getMainWidget())
    {
        mw->setDrapeProvider(
            [](const odr::RefLine& refLine, double centerLat, double centerLon,
               QString& outError, std::map<double, double>& outPoints) -> bool
            {
                const auto r = roadstudio::drapeRoadOntoTerrain(
                    refLine, centerLat, centerLon, findProjectHeightmap());
                if (!r.ok)
                {
                    outError = r.error;
                    return false;
                }
                outPoints = r.points;
                return true;
            });
    }
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

void RoadStudioWidget::onExportScanerDataSet()
{
    // 1. Pick the data-sets root (default to SCANeR's standard location)
    const QString defaultRoot = QStringLiteral("C:/AVSimulation/workspaces/data_sets");
    const QString root = QFileDialog::getExistingDirectory(
        this, tr("Select SCANeR data_sets folder"),
        QDir(defaultRoot).exists() ? defaultRoot : QDir::homePath());
    if (root.isEmpty()) return;

    // 2. Data set name
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Export to SCANeR data set"),
        tr("Data set name:"), QLineEdit::Normal,
        QStringLiteral("OpenGeoStudioExport"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;

    const QString roadDir = root + "/" + name + "/road";
    const QString terrainDir = root + "/" + name + "/terrain";
    QDir().mkpath(roadDir);
    QDir().mkpath(terrainDir);

    QStringList written;

    // 3. Road — LaneMaker's native save is OpenDRIVE (.xodr), which is
    //    exactly what SCANeR's road importer consumes.
    const QString xodrPath = roadDir + "/" + name + ".xodr";
    if (QFile::exists(xodrPath)) QFile::remove(xodrPath);
    m_lmMainWindow->saveToPath(xodrPath);
    if (QFile::exists(xodrPath))
        written << tr("road: %1.xodr").arg(name);

    // 4. Terrain — copy the merged DEM, .earth map and albedo out of the
    //    Terrain Studio export directory (found by the same upward search
    //    the drape feature uses).
    const QString demPath = findProjectHeightmap();
    if (!demPath.isEmpty())
    {
        const QFileInfo demFi(demPath);
        const QDir exportDir = demFi.absoluteDir();
        QStringList toCopy { demFi.absoluteFilePath() };
        for (const QFileInfo& entry : exportDir.entryInfoList(
                 QStringList() << "*.earth" << "albedo_merged.*", QDir::Files))
            toCopy << entry.absoluteFilePath();
        for (const QString& src : toCopy)
        {
            const QString dst = terrainDir + "/" + QFileInfo(src).fileName();
            if (QFile::exists(dst)) QFile::remove(dst);
            if (QFile::copy(src, dst))
                written << tr("terrain: %1").arg(QFileInfo(src).fileName());
        }
    }

    // 5. Summary
    QString msg = tr("Exported to:\n%1").arg(root + "/" + name);
    msg += QLatin1String("\n\n") + (written.isEmpty()
        ? tr("Nothing written — draw a road or export terrain first.")
        : written.join(QLatin1String("\n")));
    if (demPath.isEmpty())
        msg += QLatin1String("\n\n") +
               tr("Note: no terrain export found — run Terrain Studio's export "
                  "first if you want the DEM in the data set.");
    QMessageBox::information(this, tr("Export to SCANeR data set"), msg);
}
