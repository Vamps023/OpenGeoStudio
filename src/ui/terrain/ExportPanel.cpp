// ExportPanel — Export settings UI implementation

#include "ExportPanel.hpp"
#include "ExportEngine.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>

ExportPanel::ExportPanel(TerrainStore* store, QWidget* parent)
    : QWidget(parent), m_store(store) {
    m_engine = new ExportEngine(store, this);
    setupUi();

    connect(m_engine, &ExportEngine::progress, this, &ExportPanel::onExportProgress);
    connect(m_engine, &ExportEngine::finished, this, &ExportPanel::onExportFinished);
    connect(m_store, &TerrainStore::tileSelectionChanged, this, [this]() {
        int count = m_store->selectedTiles().size();
        m_tileCountLabel->setText(QString("%1 tiles selected").arg(count));
        if (m_tileBadge) m_tileBadge->setText(QString::number(count));
    });
}

void ExportPanel::setupUi() {
    // Wrap everything in a scroll area since there are many options now
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // --- Header ---
    auto* headerLayout = new QHBoxLayout();
    auto* headerLabel = new QLabel("EXPORT");
    headerLabel->setStyleSheet(
        "QLabel { font-size: 14px; font-weight: bold; color: #e6edf3; letter-spacing: 2px; }");
    headerLayout->addWidget(headerLabel);

    auto* tileBadge = new QLabel("0");
    tileBadge->setStyleSheet(
        "QLabel { background: rgba(6,182,212,0.2); color: #06b6d4; border-radius: 10px;"
        "padding: 2px 10px; font-size: 11px; font-weight: bold; }");
    tileBadge->setAlignment(Qt::AlignCenter);
    tileBadge->setMinimumWidth(30);
    headerLayout->addWidget(tileBadge);
    headerLayout->addStretch();

    auto* engineBadge = new QLabel("C++ Native · Float32 GeoTIFF + PNG");
    engineBadge->setStyleSheet("QLabel { color: #3fb950; font-size: 11px; }");
    headerLayout->addWidget(engineBadge);

    mainLayout->addLayout(headerLayout);
    m_tileBadge = tileBadge;

    // --- Export Settings ---
    auto* settingsGroup = new QGroupBox("Export Settings");
    auto* formLayout = new QFormLayout(settingsGroup);

    // Heightmap format — now includes "None" for albedo-only
    m_heightmapFormatCombo = new QComboBox();
    m_heightmapFormatCombo->addItems({
        "None (Albedo only)", "PNG 16-bit", "R16 Raw",
        "GeoTIFF Int16 (DEM)", "GeoTIFF UInt16 (normalized)", "GeoTIFF Float32 (full precision)"
    });
    connect(m_heightmapFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setHeightmapFormat(static_cast<terrain::HeightmapFormat>(idx));
    });
    formLayout->addRow("Heightmap:", m_heightmapFormatCombo);

    // Albedo format
    m_albedoFormatCombo = new QComboBox();
    m_albedoFormatCombo->addItems({"PNG (RGB)", "GeoTIFF (RGB)"});
    connect(m_albedoFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setAlbedoFormat(static_cast<terrain::AlbedoFormat>(idx));
    });
    formLayout->addRow("Albedo:", m_albedoFormatCombo);

    // DEM Source — full list matching Electron version
    m_demSourceCombo = new QComboBox();
    m_demSourceCombo->addItems({
        // Tiled (no API key)
        "AWS Terrarium (~30m, free)",
        "Mapzen Terrarium (~30m, free)",
        "Mapbox Terrain-RGB (HD 0.1m, token)",
        // Copernicus (free, no key)
        "NASA EarthData Copernicus GLO-30 (~30m, free)",
        // OpenTopography (free API key)
        "OpenTopo Copernicus GLO-30 (~30m, best)",
        "OpenTopo NASADEM (~30m, reprocessed)",
        "OpenTopo SRTM GL1 (~30m, global)",
        "OpenTopo SRTM GL3 (~90m, global)",
        "OpenTopo ALOS AW3D30 (~30m, global)",
        "OpenTopo USGS 3DEP (~10m, USA only)",
        // GPXZ
        "GPXZ LiDAR (5m, API key)",
        // GLAD
        "GLAD SRTM (~30m, free, UMD)",
        // Local file
        "Import GeoTIFF DEM from file..."
    });
    connect(m_demSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setDemSource(static_cast<terrain::DemSource>(idx));
        onDemSourceChanged();
    });
    formLayout->addRow("DEM Source:", m_demSourceCombo);

    // Imagery source — full list
    m_imagerySourceCombo = new QComboBox();
    m_imagerySourceCombo->addItems({
        "Google Satellite (free, up-to-date)",
        "ArcGIS World Imagery (free)",
        "Mapbox Satellite (token req)",
        "MapTiler Satellite (token req)",
        "GLAD ARD Landsat (free, 30m, UMD)",
        "Import imagery from file..."
    });
    connect(m_imagerySourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setImagerySource(static_cast<terrain::ImagerySource>(idx));
        onImagerySourceChanged();
    });
    formLayout->addRow("Imagery:", m_imagerySourceCombo);

    // GLAD ARD interval (shown only when GLAD ARD is selected)
    m_gladArdContainer = new QWidget();
    auto* gladLayout = new QHBoxLayout(m_gladArdContainer);
    gladLayout->setContentsMargins(0, 0, 0, 0);
    m_gladArdIntervalSpin = new QSpinBox();
    m_gladArdIntervalSpin->setRange(1, 1000);
    m_gladArdIntervalSpin->setValue(920);
    m_gladArdIntervalSpin->setToolTip("Interval ~920 ≈ mid-2022");
    connect(m_gladArdIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), m_store, &TerrainStore::setGladArdInterval);
    gladLayout->addWidget(m_gladArdIntervalSpin);
    auto* gladHint = new QLabel("≈ mid-2022");
    gladHint->setStyleSheet("color: #7d8590; font-size: 11px;");
    gladLayout->addWidget(gladHint);
    formLayout->addRow("GLAD Interval:", m_gladArdContainer);
    m_gladArdContainer->setVisible(false);

    // CRS — full list with specific UTM zones
    m_crsCombo = new QComboBox();
    m_crsCombo->addItems({
        "Auto (UTM from bounds centroid)",
        "EPSG:4326 — WGS84 (lat/lon)",
        "EPSG:3857 — Web Mercator",
        "EPSG:32633 — UTM Zone 33N",
        "EPSG:32634 — UTM Zone 34N",
        "EPSG:32635 — UTM Zone 35N",
        "EPSG:25832 — ETRS89 UTM Zone 32N",
        "EPSG:25833 — ETRS89 UTM Zone 33N"
    });
    connect(m_crsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setCrsSource(static_cast<terrain::CrsSource>(idx));
    });
    formLayout->addRow("CRS:", m_crsCombo);

    // Heightmap resolution — presets with labels
    m_heightmapResCombo = new QComboBox();
    m_heightmapResCombo->addItems({
        "512 × 512 (~150m/pixel)",
        "1024 × 1024 (~75m/pixel)",
        "2048 × 2048 (~37m/pixel)",
        "4096 × 4096 (~18m/pixel)"
    });
    // Map combo index to actual resolution
    const int hResValues[] = {512, 1024, 2048, 4096};
    connect(m_heightmapResCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, hResValues](int idx) {
        auto& settings = const_cast<terrain::ExportSettings&>(m_store->exportSettings());
        const_cast<TerrainStore*>(m_store)->setExportSettings(settings);
        // Use the store's setExportSettings to update
        terrain::ExportSettings s = m_store->exportSettings();
        s.heightmapResolution = hResValues[idx];
        m_store->setExportSettings(s);
    });
    formLayout->addRow("Heightmap Res:", m_heightmapResCombo);

    // Albedo resolution — presets with labels
    m_albedoResCombo = new QComboBox();
    m_albedoResCombo->addItems({
        "1024 × 1024",
        "2048 × 2048",
        "4096 × 4096",
        "8192 × 8192 (Ultra)"
    });
    const int aResValues[] = {1024, 2048, 4096, 8192};
    connect(m_albedoResCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, aResValues](int idx) {
        terrain::ExportSettings s = m_store->exportSettings();
        s.albedoResolution = aResValues[idx];
        m_store->setExportSettings(s);
    });
    formLayout->addRow("Albedo Res:", m_albedoResCombo);

    // Imagery zoom level — manual override
    m_imageryZoomCombo = new QComboBox();
    m_imageryZoomCombo->addItems({
        "Auto (recommended)",
        "10 — Low",
        "12 — Medium",
        "14 — Good",
        "16 — High",
        "18 — Very High",
        "19 — Ultra",
        "20 — Extreme (ArcGIS/Mapbox)",
        "21 — Max Detail (limited areas)",
        "22 — Micro (city blocks only)"
    });
    const int zoomValues[] = {0, 10, 12, 14, 16, 18, 19, 20, 21, 22};
    connect(m_imageryZoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, zoomValues](int idx) {
        m_store->setImageryZoomLevel(zoomValues[idx]);
    });
    formLayout->addRow("Imagery Zoom:", m_imageryZoomCombo);

    // Compress
    m_compressCheck = new QCheckBox("Deflate compression");
    connect(m_compressCheck, &QCheckBox::toggled, [this](bool checked) {
        terrain::ExportSettings s = m_store->exportSettings();
        s.compressDeflate = checked;
        m_store->setExportSettings(s);
    });
    formLayout->addRow("Compress:", m_compressCheck);

    mainLayout->addWidget(settingsGroup);

    // --- Local file import (shown when DEM/imagery source is local) ---
    auto* localGroup = new QGroupBox("Local File Import");
    auto* localLayout = new QFormLayout(localGroup);

    m_localDemBtn = new QPushButton("Browse...");
    m_localDemLabel = new QLabel("No DEM file selected");
    m_localDemLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
    connect(m_localDemBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Select DEM GeoTIFF", "", "GeoTIFF (*.tif *.tiff);;All Files (*)");
        if (!path.isEmpty()) {
            m_localDemLabel->setText(path);
            m_store->setLocalDemFilePath(path);
        }
    });
    localLayout->addRow("DEM file:", m_localDemBtn);
    localLayout->addRow("", m_localDemLabel);

    m_localImageryBtn = new QPushButton("Browse...");
    m_localImageryLabel = new QLabel("No imagery file selected");
    m_localImageryLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
    connect(m_localImageryBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(this, "Select Imagery", "", "Images (*.png *.jpg *.tif *.tiff);;All Files (*)");
        if (!path.isEmpty()) {
            m_localImageryLabel->setText(path);
            m_store->setLocalImageryFilePath(path);
        }
    });
    localLayout->addRow("Imagery file:", m_localImageryBtn);
    localLayout->addRow("", m_localImageryLabel);

    localGroup->setVisible(false);
    mainLayout->addWidget(localGroup);
    // Store references for visibility toggling
    m_localDemBtn->setProperty("group", "localFile");

    // --- API Keys ---
    auto* keysGroup = new QGroupBox("API Keys");
    auto* keysLayout = new QFormLayout(keysGroup);

    m_openTopoKeyEdit = new QLineEdit();
    m_openTopoKeyEdit->setPlaceholderText("OpenTopography API key");
    m_openTopoKeyEdit->setEchoMode(QLineEdit::Password);
    connect(m_openTopoKeyEdit, &QLineEdit::textChanged, m_store, &TerrainStore::setOpenTopoApiKey);
    keysLayout->addRow("OpenTopo:", m_openTopoKeyEdit);

    m_mapboxTokenEdit = new QLineEdit();
    m_mapboxTokenEdit->setPlaceholderText("Mapbox access token");
    m_mapboxTokenEdit->setEchoMode(QLineEdit::Password);
    connect(m_mapboxTokenEdit, &QLineEdit::textChanged, m_store, &TerrainStore::setMapboxToken);
    keysLayout->addRow("Mapbox:", m_mapboxTokenEdit);

    m_maptilerTokenEdit = new QLineEdit();
    m_maptilerTokenEdit->setPlaceholderText("MapTiler API key");
    m_maptilerTokenEdit->setEchoMode(QLineEdit::Password);
    connect(m_maptilerTokenEdit, &QLineEdit::textChanged, m_store, &TerrainStore::setMaptilerToken);
    keysLayout->addRow("MapTiler:", m_maptilerTokenEdit);

    m_gpxzKeyEdit = new QLineEdit();
    m_gpxzKeyEdit->setPlaceholderText("GPXZ API key");
    m_gpxzKeyEdit->setEchoMode(QLineEdit::Password);
    connect(m_gpxzKeyEdit, &QLineEdit::textChanged, m_store, &TerrainStore::setGpxzApiKey);
    keysLayout->addRow("GPXZ:", m_gpxzKeyEdit);

    m_stadiaKeyEdit = new QLineEdit();
    m_stadiaKeyEdit->setPlaceholderText("Stadia Maps API key");
    m_stadiaKeyEdit->setEchoMode(QLineEdit::Password);
    connect(m_stadiaKeyEdit, &QLineEdit::textChanged, m_store, &TerrainStore::setStadiaApiKey);
    keysLayout->addRow("Stadia:", m_stadiaKeyEdit);

    mainLayout->addWidget(keysGroup);

    // --- API Key Warning ---
    m_apiKeyWarning = new QLabel("");
    m_apiKeyWarning->setStyleSheet(
        "QLabel { color: #d29922; font-size: 11px; padding: 6px; "
        "background: rgba(210,153,34,0.1); border-radius: 4px; }");
    m_apiKeyWarning->setWordWrap(true);
    m_apiKeyWarning->setVisible(false);
    mainLayout->addWidget(m_apiKeyWarning);

    // --- Tile Selection ---
    auto* tileGroup = new QGroupBox("Tiles");
    auto* tileLayout = new QVBoxLayout(tileGroup);

    m_tileCountLabel = new QLabel("0 tiles selected");
    tileLayout->addWidget(m_tileCountLabel);

    auto* tileBtnLayout = new QHBoxLayout();
    m_selectAllBtn = new QPushButton("Select All");
    connect(m_selectAllBtn, &QPushButton::clicked, m_store, &TerrainStore::selectAllTiles);
    m_clearTilesBtn = new QPushButton("Clear");
    connect(m_clearTilesBtn, &QPushButton::clicked, m_store, &TerrainStore::clearTileSelection);
    tileBtnLayout->addWidget(m_selectAllBtn);
    tileBtnLayout->addWidget(m_clearTilesBtn);
    tileLayout->addLayout(tileBtnLayout);

    mainLayout->addWidget(tileGroup);

    // --- Export ---
    m_exportBtn = new QPushButton("Export");
    m_exportBtn->setStyleSheet(
        "QPushButton { background-color: #06b6d4; color: #0d1117; padding: 10px; font-weight: bold; border: none; border-radius: 6px; }"
        "QPushButton:hover { background-color: #22d3ee; }"
        "QPushButton:disabled { background-color: #21262d; color: #484f58; }");
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportPanel::onExportClicked);
    mainLayout->addWidget(m_exportBtn);

    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("");
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();

    scrollArea->setWidget(content);
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

void ExportPanel::onDemSourceChanged() {
    int idx = m_demSourceCombo->currentIndex();
    bool isLocal = (idx == static_cast<int>(terrain::DemSource::Local_File));

    // Show/hide local file import group
    auto* localGroup = qobject_cast<QGroupBox*>(m_localDemBtn->parentWidget()->parentWidget());
    if (localGroup) localGroup->setVisible(isLocal);

    updateApiKeyWarnings();
}

void ExportPanel::onImagerySourceChanged() {
    int idx = m_imagerySourceCombo->currentIndex();
    bool isGladArd = (idx == static_cast<int>(terrain::ImagerySource::GLAD_ARD_Landsat));
    bool isLocal = (idx == static_cast<int>(terrain::ImagerySource::Local_File));

    m_gladArdContainer->setVisible(isGladArd);

    // Show/hide local file import for imagery
    if (m_localImageryBtn) {
        auto* localGroup = qobject_cast<QGroupBox*>(m_localImageryBtn->parentWidget()->parentWidget());
        if (localGroup) localGroup->setVisible(
            isLocal || (m_demSourceCombo->currentIndex() == static_cast<int>(terrain::DemSource::Local_File)));
    }

    updateApiKeyWarnings();
}

void ExportPanel::updateApiKeyWarnings() {
    const auto& s = m_store->exportSettings();
    QStringList warnings;

    if (s.demNeedsApiKey() && s.openTopoApiKey.isEmpty())
        warnings << "OpenTopography API key required for selected DEM source";
    if (s.demNeedsMapboxToken() && s.mapboxToken.isEmpty())
        warnings << "Mapbox token required for Mapbox Terrain-RGB";
    if (s.demNeedsGpxzKey() && s.gpxzApiKey.isEmpty())
        warnings << "GPXZ API key required for GPXZ LiDAR";
    if (s.imageryNeedsMapboxToken() && s.mapboxToken.isEmpty())
        warnings << "Mapbox token required for Mapbox Satellite imagery";
    if (s.imageryNeedsMaptilerToken() && s.maptilerToken.isEmpty())
        warnings << "MapTiler token required for MapTiler Satellite imagery";

    if (warnings.isEmpty()) {
        m_apiKeyWarning->setVisible(false);
    } else {
        m_apiKeyWarning->setText("⚠ " + warnings.join("\n⚠ "));
        m_apiKeyWarning->setVisible(true);
    }
}

void ExportPanel::onExportClicked() {
    if (m_store->selectedTiles().isEmpty()) {
        QMessageBox::warning(this, "Export", "No tiles selected. Please select tiles first.");
        return;
    }
    if (!m_store->selectedBounds().isValid()) {
        QMessageBox::warning(this, "Export", "No area selected. Shift+drag on the map to select an area.");
        return;
    }

    const QString dir = QFileDialog::getExistingDirectory(
        this, "Select Export Directory");
    if (dir.isEmpty()) return;

    m_exportBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText("Starting export...");

    m_engine->exportToDirectory(dir);
}

void ExportPanel::onExportProgress(int percent, const QString& stage) {
    m_progressBar->setValue(percent);
    m_statusLabel->setText(stage);
}

void ExportPanel::onExportFinished(bool success, const QString& message) {
    m_exportBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(message);
    if (success) {
        QMessageBox::information(this, "Export Complete", message);
    } else {
        QMessageBox::warning(this, "Export Failed", message);
    }
}
