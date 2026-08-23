#pragma once

// ============================================================
// RailOsmImportDialog — OSM import UI for Train Studio
// ============================================================
//
// Mirrors OsmImportDialog but for railway data import.
// Provides a dialog for importing OSM railway data:
//   - File selection (.osm)
//   - Import settings (simplification, filters)
//   - Progress display
//   - Results summary (tracks, switches, validation)
//

#include "../../core/osm/RailImportPipeline.hpp"
#include "../../core/osm/OsmExporter.hpp"
#include "../../core/osm/RailNetworkDefinitionExporter.hpp"

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
#include <QTextEdit>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QCoreApplication>
#include <QDir>

namespace osm {

class RailOsmImportDialog : public QDialog {
    Q_OBJECT

public:
    explicit RailOsmImportDialog(QWidget* parent = nullptr)
        : QDialog(parent)
    {
        setWindowTitle("Import OSM Railway Data");
        setMinimumSize(600, 500);
        setupUi();
    }

    const RailImportResult& result() const { return m_result; }

private slots:
    void onSelectTexture() {
        const QString path = QFileDialog::getOpenFileName(
            this, "Select Trackbed Texture", m_textureEdit->text(),
            "DDS Textures (*.dds);;All Files (*.*)");
        if (!path.isEmpty()) m_textureEdit->setText(path);
    }

    void onExportWorldBuilderXml() {
        if (!m_result.success) return;
        const QString output = QFileDialog::getExistingDirectory(
            this, "Select XML Output Directory");
        if (output.isEmpty()) return;

        const auto exportResult = RailNetworkDefinitionExporter::exportProject(
            output, m_result, m_textureEdit->text().trimmed());
        if (!exportResult.success) {
            QMessageBox::critical(this, "XML Export Failed", exportResult.errorMessage);
            return;
        }
        QMessageBox::information(this, "Rail XML Exported",
            QString("Exported WorldBuilder rail XML:\n\n%1\n%2\n%3\n\nTracks: %4\nConnections: %5")
                .arg(exportResult.baseNetworkPath)
                .arg(exportResult.helperPath)
                .arg(exportResult.trackNamesPath)
                .arg(exportResult.segmentCount)
                .arg(exportResult.connectionCount));
    }
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

        m_importButton->setEnabled(false);
        m_progressBar->setVisible(true);
        m_progressLabel->setVisible(true);
        QApplication::processEvents();

        RailImportSettings settings;
        settings.autoDetectReference = m_autoRefCheck->isChecked();
        settings.simplifyTolerance = m_simplifySpin->value();
        settings.minSegmentLength = m_minSegSpin->value();
        settings.runValidation = m_validateCheck->isChecked();
        settings.autoRepair = m_repairCheck->isChecked();
        settings.progressCallback = [this](double progress, const QString& msg) {
            m_progressBar->setValue(int(progress * 100));
            m_progressLabel->setText(msg);
            QApplication::processEvents();
        };

        m_result = RailImportPipeline::importFromFile(path, settings);

        m_progressBar->setVisible(false);
        m_progressLabel->setVisible(false);
        m_importButton->setEnabled(true);

        if (!m_result.success) {
            QMessageBox::critical(this, "Import Failed", m_result.errorMessage);
            return;
        }

        displayResults();
    }

    void onExportOpenDrive() {
        QString path = QFileDialog::getSaveFileName(
            this, "Export OpenDRIVE",
            "rail_export.xodr",
            "OpenDRIVE (*.xodr);;All Files (*.*)");
        if (path.isEmpty()) return;

        OsmExporter::OpenDriveParams params;
        QString error;
        if (OsmExporter::exportToOpenDrive(path, m_result.network,
                                            {}, m_result.converter, params, &error)) {
            QMessageBox::information(this, "Exported",
                QString("OpenDRIVE exported to %1").arg(path));
        } else {
            QMessageBox::critical(this, "Export Failed", error);
        }
    }

    void onExportGeoJson() {
        QString path = QFileDialog::getSaveFileName(
            this, "Export GeoJSON",
            "rail_export.geojson",
            "GeoJSON (*.geojson *.json);;All Files (*.*)");
        if (path.isEmpty()) return;

        OsmExporter::GeoJsonParams params;
        QString error;
        if (OsmExporter::exportToGeoJson(path, m_result.network,
                                          {}, m_result.converter, params, &error)) {
            QMessageBox::information(this, "Exported",
                QString("GeoJSON exported to %1").arg(path));
        } else {
            QMessageBox::critical(this, "Export Failed", error);
        }
    }

private:
    static QString defaultTexturePath() {
        // Try application-relative paths first (portable), then dev-tree path.
        const QString deployedPath = QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("assets/trackbed_alb.dds"));
        if (QFileInfo::exists(deployedPath)) return deployedPath;
        const QString devPath = QDir(QCoreApplication::applicationDirPath())
            .absoluteFilePath(QStringLiteral("../assets/trackbed_alb.dds"));
        if (QFileInfo::exists(devPath)) return devPath;
        const QString sourcePath = QStringLiteral(
            "D:/git/OpenGeoStudio-Qt/assets/trackbed_alb.dds");
        if (QFileInfo::exists(sourcePath)) return sourcePath;
        return deployedPath;
    }

    void setupUi() {
        auto* mainLayout = new QVBoxLayout(this);

        // File selection
        auto* fileGroup = new QGroupBox("OSM File");
        auto* fileLayout = new QHBoxLayout(fileGroup);
        m_fileEdit = new QLineEdit();
        m_fileEdit->setPlaceholderText("Select .osm file with railway data...");
        auto* browseBtn = new QPushButton("Browse...");
        connect(browseBtn, &QPushButton::clicked, this, &RailOsmImportDialog::onSelectFile);
        fileLayout->addWidget(m_fileEdit);
        fileLayout->addWidget(browseBtn);
        mainLayout->addWidget(fileGroup);

        // Rail material
        auto* materialGroup = new QGroupBox("Rail Material");
        auto* materialLayout = new QHBoxLayout(materialGroup);
        m_textureEdit = new QLineEdit(defaultTexturePath());
        m_textureEdit->setToolTip("Trackbed ballast albedo texture used by the rail XML export");
        auto* textureBrowseBtn = new QPushButton("Browse...");
        connect(textureBrowseBtn, &QPushButton::clicked,
                this, &RailOsmImportDialog::onSelectTexture);
        materialLayout->addWidget(m_textureEdit);
        materialLayout->addWidget(textureBrowseBtn);
        mainLayout->addWidget(materialGroup);

        // Settings
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

        mainLayout->addWidget(settingsGroup);

        // Import button
        auto* btnLayout = new QHBoxLayout();
        m_importButton = new QPushButton("Import");
        m_importButton->setMinimumHeight(40);
        connect(m_importButton, &QPushButton::clicked, this, &RailOsmImportDialog::onImport);
        btnLayout->addWidget(m_importButton);
        mainLayout->addLayout(btnLayout);

        // Progress
        m_progressBar = new QProgressBar();
        m_progressBar->setVisible(false);
        mainLayout->addWidget(m_progressBar);

        m_progressLabel = new QLabel();
        m_progressLabel->setVisible(false);
        mainLayout->addWidget(m_progressLabel);

        // Results tabs
        m_tabs = new QTabWidget();
        m_tabs->setVisible(false);
        mainLayout->addWidget(m_tabs, 1);

        m_summaryText = new QTextEdit();
        m_summaryText->setReadOnly(true);
        m_tabs->addTab(m_summaryText, "Summary");

        m_tracksTable = new QTableWidget();
        m_tracksTable->setColumnCount(4);
        m_tracksTable->setHorizontalHeaderLabels({"ID", "Name", "Width", "Length"});
        m_tracksTable->horizontalHeader()->setStretchLastSection(true);
        m_tabs->addTab(m_tracksTable, "Tracks");

        m_validationText = new QTextEdit();
        m_validationText->setReadOnly(true);
        m_tabs->addTab(m_validationText, "Validation");

        // Export buttons
        auto* exportLayout = new QHBoxLayout();
        m_exportOdrButton = new QPushButton("Export OpenDRIVE...");
        m_exportOdrButton->setVisible(false);
        connect(m_exportOdrButton, &QPushButton::clicked, this, &RailOsmImportDialog::onExportOpenDrive);
        exportLayout->addWidget(m_exportOdrButton);

        m_exportGeoButton = new QPushButton("Export GeoJSON...");
        m_exportGeoButton->setVisible(false);
        connect(m_exportGeoButton, &QPushButton::clicked, this, &RailOsmImportDialog::onExportGeoJson);
        exportLayout->addWidget(m_exportGeoButton);

        m_exportXmlButton = new QPushButton("Export WorldBuilder XML...");
        m_exportXmlButton->setVisible(false);
        m_exportXmlButton->setToolTip(
            "Export base_network.xml, helper.xml, and track_names.xml");
        connect(m_exportXmlButton, &QPushButton::clicked,
                this, &RailOsmImportDialog::onExportWorldBuilderXml);
        exportLayout->addWidget(m_exportXmlButton);

        mainLayout->addLayout(exportLayout);

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
        m_exportOdrButton->setVisible(true);
        m_exportGeoButton->setVisible(true);
        m_exportXmlButton->setVisible(true);

        m_summaryText->setHtml(
            QString("<h3>Rail Import Successful</h3>"
                    "<table cellpadding='4'>"
                    "<tr><td>OSM Nodes:</td><td>%1</td></tr>"
                    "<tr><td>OSM Ways:</td><td>%2</td></tr>"
                    "<tr><td>Tracks created:</td><td>%3</td></tr>"
                    "<tr><td>Segments:</td><td>%4</td></tr>"
                    "<tr><td>Switches detected:</td><td>%5</td></tr>"
                    "<tr><td>End points:</td><td>%6</td></tr>"
                    "<tr><td>Total track length:</td><td>%7 m</td></tr>"
                    "<tr><td>Validation errors:</td><td>%8</td></tr>"
                    "<tr><td>Validation warnings:</td><td>%9</td></tr>"
                    "<tr><td>Repairs applied:</td><td>%10</td></tr>"
                    "</table>")
            .arg(m_result.stats.osmNodes)
            .arg(m_result.stats.osmWays)
            .arg(m_result.stats.tracksCreated)
            .arg(m_result.stats.segmentsCreated)
            .arg(m_result.stats.switchesDetected)
            .arg(m_result.stats.endPointsDetected)
            .arg(m_result.stats.totalTrackLength, 0, 'f', 1)
            .arg(m_result.stats.validationErrors)
            .arg(m_result.stats.validationWarnings)
            .arg(m_result.stats.repairsApplied));

        m_tracksTable->setRowCount(int(m_result.network.roads.size()));
        for (int i = 0; i < int(m_result.network.roads.size()); i++) {
            const auto& track = m_result.network.roads[i];
            m_tracksTable->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(track.id)));
            m_tracksTable->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(track.name)));
            m_tracksTable->setItem(i, 2, new QTableWidgetItem(QString::number(track.width, 'f', 1)));
            m_tracksTable->setItem(i, 3, new QTableWidgetItem(QString::number(track.totalLength(), 'f', 1)));
        }
        m_tracksTable->resizeColumnsToContents();

        QString valText;
        if (m_result.validationIssues.empty()) {
            valText = "<i>No validation issues found.</i>";
        } else {
            valText = "<table cellpadding='4'><tr><th>Severity</th><th>Category</th><th>Track</th><th>Message</th></tr>";
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
    QTableWidget* m_tracksTable = nullptr;
    QTextEdit* m_validationText = nullptr;
    QPushButton* m_exportOdrButton = nullptr;
    QPushButton* m_exportGeoButton = nullptr;
    QPushButton* m_exportXmlButton = nullptr;
    QLineEdit* m_textureEdit = nullptr;

    RailImportResult m_result;
};

} // namespace osm
