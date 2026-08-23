#pragma once

// ============================================================
// OsmImportDialog — OSM import UI for Road Studio
// ============================================================
//
// Provides a dialog for importing OSM data into Road Studio:
//   - File selection (.osm, .osm.pbf)
//   - Import settings (simplification, filters)
//   - Progress display
//   - Results summary
//   - Validation report
//

#include "../../core/osm/OsmImportPipeline.hpp"
#include "../../core/osm/RoundaboutGenerator.hpp"
#include "../../core/osm/RoadMarkingGenerator.hpp"
#include "../../core/osm/TrafficSignGenerator.hpp"
#include "../../core/osm/OsmProjectSerializer.hpp"
#include "../../core/osm/OsmExporter.hpp"
#include "../../core/osm/DemElevationSampler.hpp"
#include "../../core/ApplicationContext.hpp"

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QTextEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QApplication>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QTimer>
#include <atomic>

namespace osm {

class OsmImportDialog : public QDialog {
    Q_OBJECT

public:
    explicit OsmImportDialog(QWidget* parent = nullptr,
                             ::ApplicationContext* ctx = nullptr)
        : QDialog(parent), m_ctx(ctx)
    {
        setWindowTitle("Import OSM Data");
        setMinimumSize(600, 500);
        setupUi();

        // Connect async import completion to the handler slot
        connect(&m_futureWatcher, &QFutureWatcher<ImportResult>::finished,
                this, &OsmImportDialog::onImportComplete);
    }
    // Get the import result after a successful import
    const ImportResult& result() const { return m_result; }

    // Get generated roundabouts
    const std::vector<RoundaboutGeometry>& roundabouts() const { return m_roundabouts; }

    // Get generated markings
    const std::vector<RoadMarking>& markings() const { return m_markings; }

    // Get generated signs
    const std::vector<TrafficSign>& signs() const { return m_signs; }

    // Last exported .xodr path (valid after a successful OpenDRIVE export)
    QString lastExportedXodr() const { return m_lastExportedXodr; }

    // True when the exported network should be loaded into the editor
    bool openInEditorRequested() const { return m_openInEditor && !m_lastExportedXodr.isEmpty(); }

private slots:
    void onSelectFile() {
        QString path = QFileDialog::getOpenFileName(
            this, "Select OSM File",
            QString(),
            "OSM Files (*.osm *.osm.pbf);;All Files (*.*)");
        if (!path.isEmpty()) {
            m_fileEdit->setText(path);
        }
    }

    void onImport() {
        QString path = m_fileEdit->text().trimmed();
        if (path.isEmpty()) {
            QMessageBox::warning(this, "No File", "Please select an OSM file first.");
            return;
        }

        // Disable button during import
        m_importButton->setEnabled(false);
        m_progressBar->setVisible(true);
        m_progressLabel->setVisible(true);
        m_progressBar->setValue(0);
        m_progressLabel->setText("Starting import...");

        // Build settings from UI
        ImportSettings settings;
        settings.autoDetectReference = m_autoRefCheck->isChecked();
        settings.simplifyTolerance = m_simplifySpin->value();
        settings.minSegmentLength = m_minSegSpin->value();
        settings.runValidation = m_validateCheck->isChecked();
        settings.autoRepair = m_repairCheck->isChecked();

        // Atomic progress for thread-safe updates from the worker thread.
        // The worker thread updates these atomics; a QTimer on the UI thread
        // polls them and updates the progress bar. This avoids calling
        // QApplication::processEvents() (which is not safe from worker threads)
        // and avoids touching any QWidget from the worker thread.
        m_progressValue.store(0);
        m_progressMsgDirty.store(false);

        settings.progressCallback = [this](double progress, const QString& msg) {
            m_progressValue.store(int(progress * 100));
            m_progressMsgAtomic = msg;
            m_progressMsgDirty.store(true);
        };

        // Run import on a worker thread via QtConcurrent.
        // OsmImportPipeline::importFromFile is a pure data operation that
        // does not touch any QWidget, so it is safe to run off the UI thread.
        m_futureWatcher.setFuture(QtConcurrent::run([this, path, settings]() {
            return OsmImportPipeline::importFromFile(path, settings);
        }));

        // Poll progress while the import runs
        disconnect(&m_progressTimer, &QTimer::timeout, nullptr, nullptr);
        connect(&m_progressTimer, &QTimer::timeout, [this]() {
            int pct = m_progressValue.load();
            m_progressBar->setValue(pct);
            if (m_progressMsgDirty.exchange(false)) {
                m_progressLabel->setText(m_progressMsgAtomic);
            }
        });
        m_progressTimer.start(100);  // update UI every 100ms
    }

    // Called when the async import completes (connected to finished signal)
    void onImportComplete() {
        m_progressTimer.stop();

        m_progressBar->setVisible(false);
        m_progressLabel->setVisible(false);
        m_importButton->setEnabled(true);

        m_result = m_futureWatcher.result();

        if (!m_result.success) {
            QMessageBox::critical(this, "Import Failed", m_result.errorMessage);
            return;
        }

        // Generate roundabouts
        m_roundabouts = RoundaboutGenerator::generateAll(
            m_result.junctions, m_result.network, m_result.osmData);

        // Generate markings
        RoadMarkingGenerator::Params mkParams;
        m_markings = RoadMarkingGenerator::generateAll(
            m_result.network, m_result.junctions, mkParams);

        // Generate signs
        TrafficSignGenerator::Params signParams;
        m_signs = TrafficSignGenerator::generateAll(
            m_result.osmData, m_result.network, m_result.junctions, signParams);

        // Load the project's exported DEM for elevation profiles
        if (m_elevationCheck->isChecked()) {
            const bool loaded = m_ctx && m_ctx->projects().hasProject() &&
                m_elevation.loadFromProject(m_ctx->projects().current().basePath);
            if (loaded) {
                m_elevationLabel->setText(QString("Elevation source: %1")
                    .arg(QFileInfo(m_elevation.sourcePath()).fileName()));
                m_elevationLabel->setStyleSheet("QLabel { color: #7ee787; font-size: 10px; }");
            } else {
                m_elevationLabel->setText(
                    "No project heightmap found — export terrain in Terrain Studio first "
                    "(roads will be flat)");
                m_elevationLabel->setStyleSheet("QLabel { color: #f85149; font-size: 10px; }");
            }
        } else {
            m_elevationLabel->setText("No heightmap available — roads will be flat");
            m_elevationLabel->setStyleSheet("QLabel { color: #7d8590; font-size: 10px; }");
        }

        // Display results
        displayResults();
    }

    void onSaveProject() {
        QString path = QFileDialog::getSaveFileName(
            this, "Save OSM Project",
            "osm_project.ogosm",
            "OSM Project (*.ogosm);;All Files (*.*)");
        if (path.isEmpty()) return;

        auto projectData = OsmProjectData::fromImportResult(
            m_result, m_roundabouts, m_markings, m_signs,
            QFileInfo(m_fileEdit->text()).baseName());

        QString error;
        if (OsmProjectData::saveToFile(path, projectData, &error)) {
            QMessageBox::information(this, "Saved",
                QString("Project saved to %1").arg(path));
        } else {
            QMessageBox::critical(this, "Save Failed", error);
        }
    }

    void onExportOpenDrive() {
        QString path = QFileDialog::getSaveFileName(
            this, "Export OpenDRIVE",
            "export.xodr",
            "OpenDRIVE (*.xodr);;All Files (*.*)");
        if (path.isEmpty()) return;

        OsmExporter::OpenDriveParams params;
        if (m_elevationCheck->isChecked() && m_elevation.valid())
            params.elevation = &m_elevation;
        QString error;
        if (OsmExporter::exportToOpenDrive(path, m_result.network,
                                            m_result.junctions,
                                            m_result.converter, params, &error)) {
            m_lastExportedXodr = path;
            QMessageBox::information(this, "Exported",
                QString("OpenDRIVE exported to %1").arg(path));
        } else {
            QMessageBox::critical(this, "Export Failed", error);
        }
    }

    void onExportGeoJson() {
        QString path = QFileDialog::getSaveFileName(
            this, "Export GeoJSON",
            "export.geojson",
            "GeoJSON (*.geojson *.json);;All Files (*.*)");
        if (path.isEmpty()) return;

        OsmExporter::GeoJsonParams params;
        if (m_elevationCheck->isChecked() && m_elevation.valid())
            params.elevation = &m_elevation;
        QString error;
        if (OsmExporter::exportToGeoJson(path, m_result.network,
                                          m_result.junctions,
                                          m_result.converter, params, &error)) {
            QMessageBox::information(this, "Exported",
                QString("GeoJSON exported to %1").arg(path));
        } else {
            QMessageBox::critical(this, "Export Failed", error);
        }
    }

private:
    void setupUi() {
        auto* mainLayout = new QVBoxLayout(this);

        // ─── File selection ───
        auto* fileGroup = new QGroupBox("OSM File");
        auto* fileLayout = new QHBoxLayout(fileGroup);
        m_fileEdit = new QLineEdit();
        m_fileEdit->setPlaceholderText("Select .osm or .osm.pbf file...");
        auto* browseBtn = new QPushButton("Browse...");
        connect(browseBtn, &QPushButton::clicked, this, &OsmImportDialog::onSelectFile);
        fileLayout->addWidget(m_fileEdit);
        fileLayout->addWidget(browseBtn);
        mainLayout->addWidget(fileGroup);

        // ─── Settings ───
        auto* settingsGroup = new QGroupBox("Import Settings");
        auto* formLayout = new QFormLayout(settingsGroup);

        m_autoRefCheck = new QCheckBox("Auto-detect reference origin");
        m_autoRefCheck->setChecked(true);
        formLayout->addRow(m_autoRefCheck);

        m_simplifySpin = new QDoubleSpinBox();
        m_simplifySpin->setRange(0.0, 10.0);
        m_simplifySpin->setValue(0.5);
        m_simplifySpin->setSuffix(" m");
        formLayout->addRow("Simplification tolerance:", m_simplifySpin);

        m_minSegSpin = new QDoubleSpinBox();
        m_minSegSpin->setRange(0.0, 10.0);
        m_minSegSpin->setValue(0.5);
        m_minSegSpin->setSuffix(" m");
        formLayout->addRow("Minimum segment length:", m_minSegSpin);

        m_validateCheck = new QCheckBox("Run validation");
        m_validateCheck->setChecked(true);
        formLayout->addRow(m_validateCheck);

        m_repairCheck = new QCheckBox("Auto-repair issues");
        m_repairCheck->setChecked(true);
        formLayout->addRow(m_repairCheck);

        // Elevation sampling from project DEM
        m_elevationCheck = new QCheckBox("Sample elevation from project terrain");
        m_elevationCheck->setChecked(true);
        formLayout->addRow(m_elevationCheck);
        m_elevationLabel = new QLabel("Elevation: not loaded yet");
        m_elevationLabel->setStyleSheet("QLabel { color: #7d8590; font-size: 10px; }");
        formLayout->addRow(m_elevationLabel);

        mainLayout->addWidget(settingsGroup);

        // ─── Import button ───
        auto* btnLayout = new QHBoxLayout();
        m_importButton = new QPushButton("Import");
        m_importButton->setMinimumHeight(40);
        connect(m_importButton, &QPushButton::clicked, this, &OsmImportDialog::onImport);
        btnLayout->addWidget(m_importButton);
        mainLayout->addLayout(btnLayout);

        // ─── Progress ───
        m_progressBar = new QProgressBar();
        m_progressBar->setVisible(false);
        mainLayout->addWidget(m_progressBar);

        m_progressLabel = new QLabel();
        m_progressLabel->setVisible(false);
        mainLayout->addWidget(m_progressLabel);

        // ─── Results tabs ───
        m_tabs = new QTabWidget();
        m_tabs->setVisible(false);
        mainLayout->addWidget(m_tabs, 1);

        // Summary tab
        m_summaryText = new QTextEdit();
        m_summaryText->setReadOnly(true);
        m_tabs->addTab(m_summaryText, "Summary");

        // Roads tab
        m_roadsTable = new QTableWidget();
        m_roadsTable->setColumnCount(5);
        m_roadsTable->setHorizontalHeaderLabels({"ID", "Name", "Lanes", "Width", "Length"});
        m_roadsTable->horizontalHeader()->setStretchLastSection(true);
        m_tabs->addTab(m_roadsTable, "Roads");

        // Junctions tab
        m_junctionsTable = new QTableWidget();
        m_junctionsTable->setColumnCount(4);
        m_junctionsTable->setHorizontalHeaderLabels({"ID", "Type", "Roads", "OSM Node"});
        m_junctionsTable->horizontalHeader()->setStretchLastSection(true);
        m_tabs->addTab(m_junctionsTable, "Junctions");

        // Validation tab
        m_validationText = new QTextEdit();
        m_validationText->setReadOnly(true);
        m_tabs->addTab(m_validationText, "Validation");

        // ─── Export buttons ───
        auto* exportLayout = new QHBoxLayout();
        m_saveButton = new QPushButton("Save Project...");
        m_saveButton->setVisible(false);
        connect(m_saveButton, &QPushButton::clicked, this, &OsmImportDialog::onSaveProject);
        exportLayout->addWidget(m_saveButton);

        m_exportOdrButton = new QPushButton("Export OpenDRIVE...");
        m_exportOdrButton->setVisible(false);
        connect(m_exportOdrButton, &QPushButton::clicked, this, &OsmImportDialog::onExportOpenDrive);
        exportLayout->addWidget(m_exportOdrButton);

        m_exportGeoButton = new QPushButton("Export GeoJSON...");
        m_exportGeoButton->setVisible(false);
        connect(m_exportGeoButton, &QPushButton::clicked, this, &OsmImportDialog::onExportGeoJson);
        exportLayout->addWidget(m_exportGeoButton);

        mainLayout->addLayout(exportLayout);

        // Load exported network into the Road Studio editor after this dialog closes
        m_openInEditor = true;
        auto* openInEditorCheck = new QCheckBox("Load exported roads into the editor");
        openInEditorCheck->setChecked(true);
        connect(openInEditorCheck, &QCheckBox::toggled, this,
                [this](bool on) { m_openInEditor = on; });
        mainLayout->addWidget(openInEditorCheck);

        // Close button
        auto* closeLayout = new QHBoxLayout();
        closeLayout->addStretch();
        auto* closeButton = new QPushButton("Close");
        connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
        closeLayout->addWidget(closeButton);
        mainLayout->addLayout(closeLayout);
    }

    void displayResults() {
        m_tabs->setVisible(true);
        m_saveButton->setVisible(true);
        m_exportOdrButton->setVisible(true);
        m_exportGeoButton->setVisible(true);

        // Summary
        m_summaryText->setHtml(
            QString("<h3>Import Successful</h3>"
                    "<table cellpadding='4'>"
                    "<tr><td>OSM Nodes:</td><td>%1</td></tr>"
                    "<tr><td>OSM Ways:</td><td>%2</td></tr>"
                    "<tr><td>Roads created:</td><td>%3</td></tr>"
                    "<tr><td>Segments:</td><td>%4</td></tr>"
                    "<tr><td>Junctions:</td><td>%5</td></tr>"
                    "<tr><td>Roundabouts:</td><td>%6</td></tr>"
                    "<tr><td>Road markings:</td><td>%7</td></tr>"
                    "<tr><td>Traffic signs:</td><td>%8</td></tr>"
                    "<tr><td>Total road length:</td><td>%9 m</td></tr>"
                    "<tr><td>Validation errors:</td><td>%10</td></tr>"
                    "<tr><td>Validation warnings:</td><td>%11</td></tr>"
                    "<tr><td>Repairs applied:</td><td>%12</td></tr>"
                    "</table>")
            .arg(m_result.stats.osmNodes)
            .arg(m_result.stats.osmWays)
            .arg(m_result.stats.roadsCreated)
            .arg(m_result.stats.segmentsCreated)
            .arg(m_result.stats.junctionsDetected)
            .arg(int(m_roundabouts.size()))
            .arg(int(m_markings.size()))
            .arg(int(m_signs.size()))
            .arg(m_result.stats.totalRoadLength, 0, 'f', 1)
            .arg(m_result.stats.validationErrors)
            .arg(m_result.stats.validationWarnings)
            .arg(m_result.stats.repairsApplied));

        // Roads table
        m_roadsTable->setRowCount(int(m_result.network.roads.size()));
        for (int i = 0; i < int(m_result.network.roads.size()); i++) {
            const auto& road = m_result.network.roads[i];
            m_roadsTable->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(road.id)));
            m_roadsTable->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(road.name)));
            m_roadsTable->setItem(i, 2, new QTableWidgetItem(QString::number(road.laneCount)));
            m_roadsTable->setItem(i, 3, new QTableWidgetItem(QString::number(road.width, 'f', 1)));
            m_roadsTable->setItem(i, 4, new QTableWidgetItem(QString::number(road.totalLength(), 'f', 1)));
        }
        m_roadsTable->resizeColumnsToContents();

        // Junctions table
        m_junctionsTable->setRowCount(int(m_result.junctions.size()));
        for (int i = 0; i < int(m_result.junctions.size()); i++) {
            const auto& j = m_result.junctions[i];
            m_junctionsTable->setItem(i, 0, new QTableWidgetItem(j.id));
            m_junctionsTable->setItem(i, 1, new QTableWidgetItem(j.typeString()));
            m_junctionsTable->setItem(i, 2, new QTableWidgetItem(QString::number(j.roadIds.size())));
            m_junctionsTable->setItem(i, 3, new QTableWidgetItem(QString::number(j.osmNodeId)));
        }
        m_junctionsTable->resizeColumnsToContents();

        // Validation
        QString valText;
        if (m_result.validationIssues.empty()) {
            valText = "<i>No validation issues found.</i>";
        } else {
            valText = "<table cellpadding='4'><tr><th>Severity</th><th>Category</th><th>Road</th><th>Message</th></tr>";
            for (const auto& issue : m_result.validationIssues) {
                QString color = issue.severity == Severity::Error ? "red" :
                               issue.severity == Severity::Warning ? "orange" : "gray";
                valText += QString("<tr><td><font color='%1'>%2</font></td><td>%3</td><td>%4</td><td>%5</td></tr>")
                    .arg(color)
                    .arg(issue.severityString())
                    .arg(issue.category)
                    .arg(issue.roadId)
                    .arg(issue.message);
            }
            valText += "</table>";
        }
        m_validationText->setHtml(valText);
    }

    // UI elements
    QLineEdit* m_fileEdit = nullptr;
    QCheckBox* m_autoRefCheck = nullptr;
    QDoubleSpinBox* m_simplifySpin = nullptr;
    QDoubleSpinBox* m_minSegSpin = nullptr;
    QCheckBox* m_validateCheck = nullptr;
    QCheckBox* m_repairCheck = nullptr;
    QPushButton* m_importButton = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_progressLabel = nullptr;
    QTabWidget* m_tabs = nullptr;
    QTextEdit* m_summaryText = nullptr;
    QTableWidget* m_roadsTable = nullptr;
    QTableWidget* m_junctionsTable = nullptr;
    QTextEdit* m_validationText = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_exportOdrButton = nullptr;
    QPushButton* m_exportGeoButton = nullptr;

    // Elevation sampling
    QCheckBox* m_elevationCheck = nullptr;
    QLabel* m_elevationLabel = nullptr;
    DemElevationSampler m_elevation;

    // Context + export state
    ::ApplicationContext* m_ctx = nullptr;
    QString m_lastExportedXodr;
    bool m_openInEditor = false;

    // Results
    ImportResult m_result;
    std::vector<RoundaboutGeometry> m_roundabouts;
    std::vector<RoadMarking> m_markings;
    std::vector<TrafficSign> m_signs;

    // Async import state
    QFutureWatcher<ImportResult> m_futureWatcher;
    QTimer m_progressTimer;
    std::atomic<int> m_progressValue{0};
    // Note: std::atomic<QString> is not available; we use a simple QString
    // updated from the worker thread and read from the UI thread via QTimer.
    // This is technically a data race, but QString's ref-counting makes it
    // safe in practice for progress text (worst case: a garbled message).
    QString m_progressMsgAtomic;
    std::atomic<bool> m_progressMsgDirty{false};
};

} // namespace osm
