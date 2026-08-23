#pragma once

// ============================================================
// TerrainPipelinePanel — UI for the QGIS-like terrain pipeline
// ============================================================

#include "../../core/terrain/TerrainManager.hpp"
#include "../../core/terrain/ValidationManager.hpp"
#include "../../core/terrain/TerrainPipelineTypes.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QSplitter>

#include <cstring>
#include <algorithm>

namespace terrain_pipeline {

class TerrainPipelinePanel : public QWidget {
    Q_OBJECT

public:
    explicit TerrainPipelinePanel(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        m_manager = new TerrainManager(this);
        connect(m_manager, &TerrainManager::progress,
                this, &TerrainPipelinePanel::onProgress);
        connect(m_manager, &TerrainManager::finished,
                this, &TerrainPipelinePanel::onFinished);
        connect(m_manager, &TerrainManager::stageResult,
                this, &TerrainPipelinePanel::onStageResult);

        setupUI();
    }

    PipelineConfig currentConfig() const {
        PipelineConfig config;
        config.minLat = m_minLatSpin->value();
        config.maxLat = m_maxLatSpin->value();
        config.minLon = m_minLonSpin->value();
        config.maxLon = m_maxLonSpin->value();
        config.heightmapResolution = m_heightmapResSpin->value();
        config.albedoResolution = m_albedoResSpin->value();
        config.tileSize = m_tileSizeSpin->value();
        config.tileRows = m_tileRowsSpin->value();
        config.tileCols = m_tileColsSpin->value();
        config.enableDEM = m_enableDemCheck->isChecked();
        config.enableImagery = m_enableImageryCheck->isChecked();
        config.enableLandCover = m_enableLandCoverCheck->isChecked();
        config.enableWater = m_enableWaterCheck->isChecked();
        config.enableRoads = m_enableRoadsCheck->isChecked();
        config.enableBuildings = m_enableBuildingsCheck->isChecked();
        config.demSource = static_cast<terrain::DemSource>(m_demSourceCombo->currentIndex());
        config.imagerySource = static_cast<terrain::ImagerySource>(m_imagerySourceCombo->currentIndex());
        config.openTopoApiKey = m_openTopoKeyEdit->text();
        config.gpxzApiKey = m_gpxzKeyEdit->text();
        config.mapboxToken = m_mapboxTokenEdit->text();
        config.exportDir = m_exportDirEdit->text();
        config.heightmapFormat = static_cast<terrain::HeightmapFormat>(m_heightmapFormatCombo->currentIndex());
        config.exportPackedMask = m_packedMaskCheck->isChecked();
        config.masks = collectMaskDefinitions();
        // Propagate project CRS if set
        if (m_projectCrsEpsg > 0) {
            config.targetCrs.epsg = m_projectCrsEpsg;
            config.targetCrs.description = m_projectCrsAuthId;
        }
        return config;
    }

    // Set the project CRS for terrain pipeline output
    void setProjectCrs(const QString& authId, int epsg) {
        m_projectCrsAuthId = authId;
        m_projectCrsEpsg = epsg;
    }

    void setConfig(const PipelineConfig& config) {
        m_minLatSpin->setValue(config.minLat);
        m_maxLatSpin->setValue(config.maxLat);
        m_minLonSpin->setValue(config.minLon);
        m_maxLonSpin->setValue(config.maxLon);
        m_heightmapResSpin->setValue(config.heightmapResolution);
        m_albedoResSpin->setValue(config.albedoResolution);
        m_tileSizeSpin->setValue(config.tileSize);
        m_tileRowsSpin->setValue(config.tileRows);
        m_tileColsSpin->setValue(config.tileCols);
        m_enableDemCheck->setChecked(config.enableDEM);
        m_enableImageryCheck->setChecked(config.enableImagery);
        m_enableLandCoverCheck->setChecked(config.enableLandCover);
        m_enableWaterCheck->setChecked(config.enableWater);
        m_enableRoadsCheck->setChecked(config.enableRoads);
        m_enableBuildingsCheck->setChecked(config.enableBuildings);
        m_demSourceCombo->setCurrentIndex(static_cast<int>(config.demSource));
        m_imagerySourceCombo->setCurrentIndex(static_cast<int>(config.imagerySource));
        m_openTopoKeyEdit->setText(config.openTopoApiKey);
        m_gpxzKeyEdit->setText(config.gpxzApiKey);
        m_mapboxTokenEdit->setText(config.mapboxToken);
        m_exportDirEdit->setText(config.exportDir);
        m_heightmapFormatCombo->setCurrentIndex(static_cast<int>(config.heightmapFormat));
        m_packedMaskCheck->setChecked(config.exportPackedMask);
    }

private:
    TerrainManager* m_manager = nullptr;
    QString m_projectCrsAuthId;  // Project CRS authority ID (e.g. "EPSG:32643")
    int m_projectCrsEpsg = 0;    // Project CRS EPSG code

    // Area inputs
    QDoubleSpinBox* m_minLatSpin = nullptr;
    QDoubleSpinBox* m_maxLatSpin = nullptr;
    QDoubleSpinBox* m_minLonSpin = nullptr;
    QDoubleSpinBox* m_maxLonSpin = nullptr;

    // Resolution
    QSpinBox* m_heightmapResSpin = nullptr;
    QSpinBox* m_albedoResSpin = nullptr;
    QSpinBox* m_tileSizeSpin = nullptr;
    QSpinBox* m_tileRowsSpin = nullptr;
    QSpinBox* m_tileColsSpin = nullptr;

    // Dataset toggles
    QCheckBox* m_enableDemCheck = nullptr;
    QCheckBox* m_enableImageryCheck = nullptr;
    QCheckBox* m_enableLandCoverCheck = nullptr;
    QCheckBox* m_enableWaterCheck = nullptr;
    QCheckBox* m_enableRoadsCheck = nullptr;
    QCheckBox* m_enableBuildingsCheck = nullptr;

    // Source selection
    QComboBox* m_demSourceCombo = nullptr;
    QComboBox* m_imagerySourceCombo = nullptr;
    QComboBox* m_heightmapFormatCombo = nullptr;

    // API keys
    QLineEdit* m_openTopoKeyEdit = nullptr;
    QLineEdit* m_gpxzKeyEdit = nullptr;
    QLineEdit* m_mapboxTokenEdit = nullptr;

    // Export
    QLineEdit* m_exportDirEdit = nullptr;
    QPushButton* m_browseExportBtn = nullptr;

    // Masks
    QCheckBox* m_packedMaskCheck = nullptr;
    QMap<QString, QCheckBox*> m_maskCheckboxes;

    // Progress
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_stageLabel = nullptr;
    QTextEdit* m_logEdit = nullptr;

    // Results table
    QTableWidget* m_resultsTable = nullptr;

    // Run button
    QPushButton* m_runBtn = nullptr;
    QPushButton* m_validateBtn = nullptr;

    void setupUI() {
        auto* mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(8, 8, 8, 8);
        mainLayout->setSpacing(8);

        auto* splitter = new QSplitter(Qt::Vertical);

        // Config area (scrollable)
        auto* configScroll = new QScrollArea();
        configScroll->setWidgetResizable(true);
        auto* configWidget = new QWidget();
        auto* configLayout = new QVBoxLayout(configWidget);
        configLayout->setSpacing(8);

        // Area Selection Group
        auto* areaGroup = new QGroupBox("Area Selection");
        auto* areaLayout = new QGridLayout(areaGroup);
        areaLayout->addWidget(new QLabel("Min Lat:"), 0, 0);
        m_minLatSpin = new QDoubleSpinBox();
        m_minLatSpin->setRange(-90, 90);
        m_minLatSpin->setDecimals(6);
        m_minLatSpin->setValue(29.65);
        areaLayout->addWidget(m_minLatSpin, 0, 1);
        areaLayout->addWidget(new QLabel("Max Lat:"), 0, 2);
        m_maxLatSpin = new QDoubleSpinBox();
        m_maxLatSpin->setRange(-90, 90);
        m_maxLatSpin->setDecimals(6);
        m_maxLatSpin->setValue(29.69);
        areaLayout->addWidget(m_maxLatSpin, 0, 3);
        areaLayout->addWidget(new QLabel("Min Lon:"), 1, 0);
        m_minLonSpin = new QDoubleSpinBox();
        m_minLonSpin->setRange(-180, 180);
        m_minLonSpin->setDecimals(6);
        m_minLonSpin->setValue(-95.42);
        areaLayout->addWidget(m_minLonSpin, 1, 1);
        areaLayout->addWidget(new QLabel("Max Lon:"), 1, 2);
        m_maxLonSpin = new QDoubleSpinBox();
        m_maxLonSpin->setRange(-180, 180);
        m_maxLonSpin->setDecimals(6);
        m_maxLonSpin->setValue(-95.38);
        areaLayout->addWidget(m_maxLonSpin, 1, 3);
        configLayout->addWidget(areaGroup);

        // Datasets Group
        auto* datasetsGroup = new QGroupBox("Datasets");
        auto* datasetsLayout = new QVBoxLayout(datasetsGroup);
        m_enableDemCheck = new QCheckBox("Elevation (DEM)");
        m_enableDemCheck->setChecked(true);
        datasetsLayout->addWidget(m_enableDemCheck);
        m_enableImageryCheck = new QCheckBox("Satellite Imagery");
        m_enableImageryCheck->setChecked(true);
        datasetsLayout->addWidget(m_enableImageryCheck);
        m_enableLandCoverCheck = new QCheckBox("Land Cover");
        datasetsLayout->addWidget(m_enableLandCoverCheck);
        m_enableWaterCheck = new QCheckBox("Water Bodies");
        datasetsLayout->addWidget(m_enableWaterCheck);
        m_enableRoadsCheck = new QCheckBox("Roads");
        datasetsLayout->addWidget(m_enableRoadsCheck);
        m_enableBuildingsCheck = new QCheckBox("Buildings");
        datasetsLayout->addWidget(m_enableBuildingsCheck);
        configLayout->addWidget(datasetsGroup);

        // Source Selection Group
        auto* sourceGroup = new QGroupBox("Data Sources");
        auto* sourceLayout = new QGridLayout(sourceGroup);
        sourceLayout->addWidget(new QLabel("DEM Source:"), 0, 0);
        m_demSourceCombo = new QComboBox();
        m_demSourceCombo->addItem("AWS Terrarium");
        m_demSourceCombo->addItem("Mapzen Terrarium");
        m_demSourceCombo->addItem("Mapbox Terrain RGB");
        m_demSourceCombo->addItem("Copernicus GLO30");
        m_demSourceCombo->addItem("OpenTopo Copernicus");
        m_demSourceCombo->addItem("OpenTopo NASADEM");
        m_demSourceCombo->addItem("OpenTopo SRTM GL1");
        m_demSourceCombo->addItem("OpenTopo SRTM GL3");
        m_demSourceCombo->addItem("OpenTopo ALOS AW3D30");
        m_demSourceCombo->addItem("OpenTopo USGS 3DEP");
        m_demSourceCombo->addItem("GPXZ LiDAR");
        m_demSourceCombo->addItem("GLAD SRTM");
        m_demSourceCombo->addItem("Local File");
        sourceLayout->addWidget(m_demSourceCombo, 0, 1);
        sourceLayout->addWidget(new QLabel("Imagery Source:"), 1, 0);
        m_imagerySourceCombo = new QComboBox();
        m_imagerySourceCombo->addItem("Google Satellite");
        m_imagerySourceCombo->addItem("ArcGIS World Imagery");
        m_imagerySourceCombo->addItem("Mapbox Satellite");
        m_imagerySourceCombo->addItem("MapTiler Satellite");
        m_imagerySourceCombo->addItem("GLAD ARD Landsat");
        m_imagerySourceCombo->addItem("Local File");
        sourceLayout->addWidget(m_imagerySourceCombo, 1, 1);
        sourceLayout->addWidget(new QLabel("Heightmap Format:"), 2, 0);
        m_heightmapFormatCombo = new QComboBox();
        m_heightmapFormatCombo->addItem("None");
        m_heightmapFormatCombo->addItem("PNG 16-bit");
        m_heightmapFormatCombo->addItem("R16 Raw");
        m_heightmapFormatCombo->addItem("GeoTIFF Int16");
        m_heightmapFormatCombo->addItem("GeoTIFF UInt16");
        m_heightmapFormatCombo->addItem("GeoTIFF Float32");
        m_heightmapFormatCombo->setCurrentIndex(5);
        sourceLayout->addWidget(m_heightmapFormatCombo, 2, 1);
        configLayout->addWidget(sourceGroup);

        // API Keys Group
        auto* keysGroup = new QGroupBox("API Keys");
        auto* keysLayout = new QGridLayout(keysGroup);
        keysLayout->addWidget(new QLabel("OpenTopography:"), 0, 0);
        m_openTopoKeyEdit = new QLineEdit();
        m_openTopoKeyEdit->setEchoMode(QLineEdit::Password);
        keysLayout->addWidget(m_openTopoKeyEdit, 0, 1);
        keysLayout->addWidget(new QLabel("GPXZ:"), 1, 0);
        m_gpxzKeyEdit = new QLineEdit();
        m_gpxzKeyEdit->setEchoMode(QLineEdit::Password);
        m_gpxzKeyEdit->setText("ak_NgEXLGho_z5TBKb44GCFKIirC");
        keysLayout->addWidget(m_gpxzKeyEdit, 1, 1);
        keysLayout->addWidget(new QLabel("Mapbox:"), 2, 0);
        m_mapboxTokenEdit = new QLineEdit();
        m_mapboxTokenEdit->setEchoMode(QLineEdit::Password);
        keysLayout->addWidget(m_mapboxTokenEdit, 2, 1);
        configLayout->addWidget(keysGroup);

        // Resolution Group
        auto* resGroup = new QGroupBox("Resolution & Tiles");
        auto* resLayout = new QGridLayout(resGroup);
        resLayout->addWidget(new QLabel("Heightmap Res:"), 0, 0);
        m_heightmapResSpin = new QSpinBox();
        m_heightmapResSpin->setRange(64, 8192);
        m_heightmapResSpin->setValue(1024);
        m_heightmapResSpin->setSingleStep(256);
        resLayout->addWidget(m_heightmapResSpin, 0, 1);
        resLayout->addWidget(new QLabel("Albedo Res:"), 0, 2);
        m_albedoResSpin = new QSpinBox();
        m_albedoResSpin->setRange(64, 8192);
        m_albedoResSpin->setValue(1024);
        m_albedoResSpin->setSingleStep(256);
        resLayout->addWidget(m_albedoResSpin, 0, 3);
        resLayout->addWidget(new QLabel("Tile Size (px):"), 1, 0);
        m_tileSizeSpin = new QSpinBox();
        m_tileSizeSpin->setRange(64, 4096);
        m_tileSizeSpin->setValue(512);
        resLayout->addWidget(m_tileSizeSpin, 1, 1);
        resLayout->addWidget(new QLabel("Tile Rows:"), 1, 2);
        m_tileRowsSpin = new QSpinBox();
        m_tileRowsSpin->setRange(1, 16);
        m_tileRowsSpin->setValue(2);
        resLayout->addWidget(m_tileRowsSpin, 1, 3);
        resLayout->addWidget(new QLabel("Tile Cols:"), 2, 0);
        m_tileColsSpin = new QSpinBox();
        m_tileColsSpin->setRange(1, 16);
        m_tileColsSpin->setValue(2);
        resLayout->addWidget(m_tileColsSpin, 2, 1);
        configLayout->addWidget(resGroup);

        // Masks Group
        auto* masksGroup = new QGroupBox("Masks");
        auto* masksLayout = new QVBoxLayout(masksGroup);
        auto* masksGrid = new QGridLayout();
        masksGrid->setSpacing(4);
        auto maskDefs = PipelineConfig::defaultMasks();
        for (int i = 0; i < maskDefs.size(); i++) {
            auto* cb = new QCheckBox(maskDefs[i].name);
            cb->setChecked(false);
            cb->setProperty("maskId", maskDefs[i].id);
            m_maskCheckboxes[maskDefs[i].id] = cb;
            masksGrid->addWidget(cb, i / 3, i % 3);
        }
        masksLayout->addLayout(masksGrid);
        m_packedMaskCheck = new QCheckBox("Export packed RGBA mask (R=Vegetation, G=Water, B=Urban, A=Road)");
        masksLayout->addWidget(m_packedMaskCheck);
        configLayout->addWidget(masksGroup);

        // Export Group
        auto* exportGroup = new QGroupBox("Export");
        auto* exportLayout = new QHBoxLayout(exportGroup);
        m_exportDirEdit = new QLineEdit();
        m_exportDirEdit->setPlaceholderText("Select export directory...");
        exportLayout->addWidget(m_exportDirEdit);
        m_browseExportBtn = new QPushButton("Browse...");
        connect(m_browseExportBtn, &QPushButton::clicked, this, [this]() {
            QString dir = QFileDialog::getExistingDirectory(this, "Select Export Directory");
            if (!dir.isEmpty()) m_exportDirEdit->setText(dir);
        });
        exportLayout->addWidget(m_browseExportBtn);
        configLayout->addWidget(exportGroup);

        configLayout->addStretch();
        configScroll->setWidget(configWidget);
        splitter->addWidget(configScroll);

        // Bottom: progress + log + results
        auto* bottomWidget = new QWidget();
        auto* bottomLayout = new QVBoxLayout(bottomWidget);
        bottomLayout->setContentsMargins(0, 0, 0, 0);
        bottomLayout->setSpacing(4);

        // Buttons
        auto* btnLayout = new QHBoxLayout();
        m_runBtn = new QPushButton("Run Pipeline");
        m_runBtn->setStyleSheet(
            "QPushButton { background: #238636; color: white; padding: 8px 24px; "
            "font-weight: bold; border-radius: 4px; }"
            "QPushButton:hover { background: #2ea043; }"
            "QPushButton:disabled { background: #21262d; color: #7d8590; }");
        connect(m_runBtn, &QPushButton::clicked, this, &TerrainPipelinePanel::runPipeline);
        btnLayout->addWidget(m_runBtn);

        m_validateBtn = new QPushButton("Run Validation Tests");
        m_validateBtn->setStyleSheet(
            "QPushButton { background: #1f6feb; color: white; padding: 8px 24px; "
            "font-weight: bold; border-radius: 4px; }"
            "QPushButton:hover { background: #388bfd; }");
        connect(m_validateBtn, &QPushButton::clicked, this, &TerrainPipelinePanel::runValidation);
        btnLayout->addWidget(m_validateBtn);

        btnLayout->addStretch();
        bottomLayout->addLayout(btnLayout);

        // Progress
        m_progressBar = new QProgressBar();
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(0);
        bottomLayout->addWidget(m_progressBar);
        m_stageLabel = new QLabel("Ready");
        bottomLayout->addWidget(m_stageLabel);

        // Log
        m_logEdit = new QTextEdit();
        m_logEdit->setMaximumHeight(150);
        m_logEdit->setReadOnly(true);
        m_logEdit->setStyleSheet("QTextEdit { background: #0d1117; color: #e6edf3; font-family: monospace; font-size: 11px; }");
        bottomLayout->addWidget(m_logEdit);

        // Results table
        m_resultsTable = new QTableWidget();
        m_resultsTable->setColumnCount(3);
        m_resultsTable->setHorizontalHeaderLabels({"Test", "Result", "Message"});
        m_resultsTable->horizontalHeader()->setStretchLastSection(true);
        m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        bottomLayout->addWidget(m_resultsTable);

        splitter->addWidget(bottomWidget);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);

        mainLayout->addWidget(splitter);
    }

    QList<MaskDefinition> collectMaskDefinitions() const {
        QList<MaskDefinition> masks;
        auto defaults = PipelineConfig::defaultMasks();
        for (const auto& def : defaults) {
            auto it = m_maskCheckboxes.find(def.id);
            if (it != m_maskCheckboxes.end() && it.value()->isChecked()) {
                MaskDefinition m = def;
                m.enabled = true;
                masks.append(m);
            }
        }
        return masks;
    }

private slots:
    void runPipeline() {
        if (m_exportDirEdit->text().isEmpty()) {
            QMessageBox::warning(this, "Export Directory", "Please select an export directory.");
            return;
        }
        if (m_manager->isAsyncBusy()) {
            QMessageBox::information(this, "Pipeline Busy",
                "A pipeline is already running. Please wait for it to finish.");
            return;
        }
        m_logEdit->clear();
        m_progressBar->setValue(0);
        m_runBtn->setEnabled(false);
        m_validateBtn->setEnabled(false);

        PipelineConfig config = currentConfig();
        // Use async pipeline to avoid blocking the UI thread
        m_manager->runPipelineAsync(config);
    }

    void runValidation() {
        m_logEdit->append("=== Running Validation Tests ===");

        // For validation, we use the current config and check what exists
        PipelineConfig config = currentConfig();

        // Check if export directory has data
        QMap<QString, ByteRaster> masks;
        QList<TileInfo> tiles;
        RasterGrid dem;

        // Load manifest if it exists
        QString manifestPath = config.exportDir + "/manifest.json";
        QFile f(manifestPath);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            if (doc.isObject()) {
                auto obj = doc.object();
                QJsonArray tilesArr = obj["tiles"].toArray();
                for (const auto& v : tilesArr) {
                    auto tObj = v.toObject();
                    TileInfo t;
                    t.row = tObj["row"].toInt();
                    t.col = tObj["col"].toInt();
                    t.west = tObj["west"].toDouble();
                    t.east = tObj["east"].toDouble();
                    t.north = tObj["north"].toDouble();
                    t.south = tObj["south"].toDouble();
                    t.width = tObj["width"].toInt();
                    t.height = tObj["height"].toInt();
                    t.crs = CrsSpec::fromJson(tObj["crs"].toObject());
                    tiles.append(t);
                }
            }
        }

        // Load actual mask files from the export directory.
        // Masks are stored as PNG rasters at <exportDir>/Masks/<name>/tile_<id>.png
        // (see TerrainManager::exportTiles). We merge all tile PNGs for each mask
        // name into a single representative raster so the validation suite can
        // inspect real mask content instead of placeholder data.
        QString masksDir = config.exportDir + "/Masks";
        QDir masksDirObj(masksDir);
        if (masksDirObj.exists()) {
            for (const auto& subdir : masksDirObj.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                QDir maskSubdir(masksDir + "/" + subdir);
                QStringList pngFilters;
                pngFilters << "tile_*.png";
                const auto pngFiles = maskSubdir.entryInfoList(pngFilters, QDir::Files);
                if (pngFiles.isEmpty()) continue;

                // Load the first tile to determine dimensions, then accumulate
                // pixel data across all tiles into a single mosaic raster.
                QImage firstImg(pngFiles.first().absoluteFilePath());
                if (firstImg.isNull()) continue;
                firstImg = firstImg.convertToFormat(QImage::Format_Grayscale8);

                ByteRaster mask;
                mask.width = firstImg.width();
                mask.height = firstImg.height();
                mask.data.resize(mask.width * mask.height, 0);
                mask.valid = true;

                // Copy first tile's pixels
                const uchar* src = firstImg.constBits();
                std::memcpy(mask.data.data(), src, mask.width * mask.height);

                // For additional tiles, take the maximum per-pixel value so
                // overlapping mask regions are preserved.
                for (int i = 1; i < pngFiles.size(); ++i) {
                    QImage img(pngFiles[i].absoluteFilePath());
                    if (img.isNull()) continue;
                    img = img.convertToFormat(QImage::Format_Grayscale8);
                    const int w = std::min(img.width(), mask.width);
                    const int h = std::min(img.height(), mask.height);
                    const uchar* s = img.constBits();
                    for (int y = 0; y < h; ++y) {
                        for (int x = 0; x < w; ++x) {
                            const int idx = y * mask.width + x;
                            mask.data[idx] = std::max(mask.data[idx],
                                                      static_cast<uint8_t>(s[y * img.width() + x]));
                        }
                    }
                }

                masks[subdir.toLower()] = mask;
            }
        }

        // Run tests
        auto results = ValidationManager::runAllTests(config, tiles, dem, masks);

        // Update results table
        m_resultsTable->setRowCount(results.size());
        for (int i = 0; i < results.size(); i++) {
            const auto& r = results[i];
            m_resultsTable->setItem(i, 0, new QTableWidgetItem(r.testName));
            auto* statusItem = new QTableWidgetItem(r.passed ? "PASS" : "FAIL");
            statusItem->setForeground(r.passed ? QColor("#3fb950") : QColor("#f85149"));
            m_resultsTable->setItem(i, 1, statusItem);
            m_resultsTable->setItem(i, 2, new QTableWidgetItem(r.message));
            m_logEdit->append(QString("[%1] %2: %3")
                .arg(r.passed ? "PASS" : "FAIL")
                .arg(r.testName)
                .arg(r.message));
        }

        // Generate report
        QString report = ValidationManager::generateReport(results);
        m_logEdit->append("\n" + report);

        // Save report
        QString reportPath = config.exportDir + "/test_report.txt";
        QFile rf(reportPath);
        if (rf.open(QIODevice::WriteOnly)) {
            rf.write(report.toUtf8());
            rf.close();
        }
    }

    void onProgress(int percent, const QString& stage) {
        m_progressBar->setValue(percent);
        m_stageLabel->setText(stage);
        m_logEdit->append(QString("[%1%] %2").arg(percent).arg(stage));
    }

    void onFinished(bool success, const QString& message) {
        m_runBtn->setEnabled(true);
        m_validateBtn->setEnabled(true);
        m_logEdit->append(QString("\n=== %1: %2 ===")
            .arg(success ? "SUCCESS" : "FAILED")
            .arg(message));
        if (success) {
            m_stageLabel->setText("Pipeline complete");
        } else {
            m_stageLabel->setText("Pipeline failed");
        }
    }

    void onStageResult(const StageResult& result) {
        QString status;
        switch (result.status) {
        case StageStatus::Success: status = "OK"; break;
        case StageStatus::Warning: status = "WARN"; break;
        case StageStatus::Failed: status = "FAIL"; break;
        case StageStatus::Skipped: status = "SKIP"; break;
        }
        m_logEdit->append(QString("  [%1] %2: %3")
            .arg(status)
            .arg(result.stageName)
            .arg(result.message));
    }
};

} // namespace terrain_pipeline
