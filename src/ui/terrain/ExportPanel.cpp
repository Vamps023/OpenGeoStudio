// ExportPanel — Export settings UI implementation

#include "ExportPanel.hpp"
#include "ExportEngine.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QStandardPaths>
#include <QDir>

// ============================================================
// Dark theme stylesheet (GitHub dark inspired, matching Electron)
// ============================================================
static const char* kDarkTheme = R"(
    QWidget { background: #0d1117; color: #e6edf3; font-size: 12px; }
    QScrollArea { background: #0d1117; border: none; }
    QGroupBox {
        background: #161b22; border: 1px solid #30363d; border-radius: 6px;
        margin-top: 12px; padding-top: 8px; font-size: 11px;
        font-weight: bold; color: #7d8590; letter-spacing: 1px;
    }
    QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
    QComboBox {
        background: #21262d; border: 1px solid #30363d; border-radius: 4px;
        padding: 4px 8px; color: #e6edf3; min-height: 20px;
    }
    QComboBox::drop-down { border: none; width: 20px; }
    QComboBox::down-arrow { image: none; width: 0; height: 0; }
    QComboBox QAbstractItemView {
        background: #1c2128; border: 1px solid #30363d; selection-background-color: #1f6feb;
        color: #e6edf3; outline: none;
    }
    QSpinBox, QDoubleSpinBox {
        background: #21262d; border: 1px solid #30363d; border-radius: 4px;
        padding: 4px 8px; color: #e6edf3; min-height: 20px;
    }
    QLineEdit {
        background: #21262d; border: 1px solid #30363d; border-radius: 4px;
        padding: 4px 8px; color: #e6edf3;
    }
    QLineEdit:focus { border-color: #1f6feb; }
    QCheckBox { color: #e6edf3; spacing: 6px; }
    QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px; }
    QCheckBox::indicator:unchecked { background: #21262d; border: 1px solid #30363d; }
    QCheckBox::indicator:checked { background: #1f6feb; border: 1px solid #1f6feb; }
    QPushButton {
        background: #21262d; border: 1px solid #30363d; border-radius: 4px;
        padding: 6px 12px; color: #e6edf3; font-weight: 500;
    }
    QPushButton:hover { background: #30363d; border-color: #8b949e; }
    QPushButton:pressed { background: #1c2128; }
    QPushButton:disabled { color: #484f58; background: #161b22; }
    QToolButton {
        background: transparent; border: none; color: #7d8590;
        font-size: 10px; font-weight: bold; text-transform: uppercase;
        letter-spacing: 1px; padding: 6px 10px;
    }
    QToolButton:hover { color: #e6edf3; }
    QProgressBar {
        background: #21262d; border: 1px solid #30363d; border-radius: 4px;
        height: 6px; text-align: center;
    }
    QProgressBar::chunk { background: #1f6feb; border-radius: 3px; }
    QLabel { color: #e6edf3; }
)";

ExportPanel::ExportPanel(TerrainStore* store, ApplicationContext* ctx, QWidget* parent)
    : QWidget(parent), m_store(store), m_ctx(ctx) {
    m_engine = new ExportEngine(store, this);
    setupUi();
    applyDarkTheme();

    connect(m_engine, &ExportEngine::progress, this, &ExportPanel::onExportProgress);
    connect(m_engine, &ExportEngine::finished, this, &ExportPanel::onExportFinished);
    connect(m_store, &TerrainStore::tileSelectionChanged, this, [this]() {
        int count = m_store->selectedTiles().size();
        m_tileCountLabel->setText(QString("%1 tiles selected").arg(count));
        if (m_tileBadge) m_tileBadge->setText(QString::number(count));
        // Update export button text dynamically
        if (count > 0) {
            m_exportBtn->setText(QString("Export %1 tile%2").arg(count).arg(count > 1 ? "s" : ""));
        } else {
            m_exportBtn->setText("Export");
        }
    });
}

void ExportPanel::applyDarkTheme() {
    setStyleSheet(kDarkTheme);
}

void ExportPanel::setupUi() {
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget();
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(6);

    // --- Header ---
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(8);
    auto* headerLabel = new QLabel("EXPORT");
    headerLabel->setStyleSheet(
        "QLabel { font-size: 14px; font-weight: bold; color: #e6edf3; letter-spacing: 2px; }");
    headerLayout->addWidget(headerLabel);

    auto* tileBadge = new QLabel("0");
    tileBadge->setStyleSheet(
        "QLabel { background: rgba(31,111,235,0.2); color: #58a6ff; border-radius: 10px;"
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

    // --- Basic Settings (always visible) ---
    auto* basicGroup = new QGroupBox("OUTPUT");
    auto* basicLayout = new QFormLayout(basicGroup);
    basicLayout->setSpacing(6);
    basicLayout->setContentsMargins(10, 16, 10, 10);
    // Set label alignment
    basicLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    basicLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);

    // Heightmap format
    m_heightmapFormatCombo = new QComboBox();
    m_heightmapFormatCombo->addItems({
        "None (Albedo only)", "PNG 16-bit", "R16 Raw",
        "GeoTIFF Int16 (DEM)", "GeoTIFF UInt16 (normalized)", "GeoTIFF Float32 (full precision)"
    });
    connect(m_heightmapFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setHeightmapFormat(static_cast<terrain::HeightmapFormat>(idx));
    });
    basicLayout->addRow("Heightmap:", m_heightmapFormatCombo);

    // Albedo format
    m_albedoFormatCombo = new QComboBox();
    m_albedoFormatCombo->addItems({"PNG (RGB)", "GeoTIFF (RGB)"});
    connect(m_albedoFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setAlbedoFormat(static_cast<terrain::AlbedoFormat>(idx));
    });
    basicLayout->addRow("Albedo:", m_albedoFormatCombo);

    // DEM Source
    m_demSourceCombo = new QComboBox();
    m_demSourceCombo->addItems({
        "AWS Terrarium (~30m, free)",
        "Mapzen Terrarium (~30m, free)",
        "Mapbox Terrain-RGB (HD 0.1m, token)",
        "NASA EarthData Copernicus GLO-30 (~30m, free)",
        "OpenTopo Copernicus GLO-30 (~30m, best)",
        "OpenTopo NASADEM (~30m, reprocessed)",
        "OpenTopo SRTM GL1 (~30m, global)",
        "OpenTopo SRTM GL3 (~90m, global)",
        "OpenTopo ALOS AW3D30 (~30m, global)",
        "OpenTopo USGS 3DEP (~10m, USA only)",
        "GPXZ LiDAR (5m, API key)",
        "GLAD SRTM (~30m, free, UMD)",
        "Import GeoTIFF DEM from file..."
    });
    connect(m_demSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setDemSource(static_cast<terrain::DemSource>(idx));
        onDemSourceChanged();
    });
    basicLayout->addRow("DEM Source:", m_demSourceCombo);

    // Imagery source
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
    basicLayout->addRow("Imagery:", m_imagerySourceCombo);

    mainLayout->addWidget(basicGroup);

    // --- Advanced Settings Toggle (collapsible, matching Electron) ---
    m_advancedToggle = new QToolButton();
    m_advancedToggle->setText("Settings  +");
    m_advancedToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_advancedToggle->setStyleSheet(
        "QToolButton { color: #7d8590; font-size: 10px; font-weight: bold;"
        "text-transform: uppercase; letter-spacing: 1px; padding: 8px 0;"
        "border-top: 1px solid #30363d; border-bottom: 1px solid #30363d;"
        "background: transparent; }"
        "QToolButton:hover { color: #e6edf3; }");
    connect(m_advancedToggle, &QToolButton::clicked, this, &ExportPanel::onToggleAdvancedSettings);
    mainLayout->addWidget(m_advancedToggle);

    // --- Advanced Settings Container (hidden by default) ---
    m_advancedContainer = new QWidget();
    auto* advLayout = new QVBoxLayout(m_advancedContainer);
    advLayout->setContentsMargins(0, 0, 0, 0);
    advLayout->setSpacing(6);

    // Settings group
    m_settingsGroup = new QGroupBox("ADVANCED");
    auto* formLayout = new QFormLayout(m_settingsGroup);
    formLayout->setSpacing(6);
    formLayout->setContentsMargins(10, 16, 10, 10);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // GLAD ARD interval
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

    // CRS
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

    // Heightmap resolution
    m_heightmapResCombo = new QComboBox();
    m_heightmapResCombo->addItems({
        "512 × 512 (~150m/pixel)",
        "1024 × 1024 (~75m/pixel)",
        "2048 × 2048 (~37m/pixel)",
        "4096 × 4096 (~18m/pixel)"
    });
    const int hResValues[] = {512, 1024, 2048, 4096};
    connect(m_heightmapResCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, hResValues](int idx) {
        terrain::ExportSettings s = m_store->exportSettings();
        s.heightmapResolution = hResValues[idx];
        m_store->setExportSettings(s);
    });
    formLayout->addRow("Heightmap Res:", m_heightmapResCombo);

    // Albedo resolution
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

    // Imagery zoom
    m_imageryZoomCombo = new QComboBox();
    m_imageryZoomCombo->addItems({
        "Auto (recommended)", "10 — Low", "12 — Medium", "14 — Good",
        "16 — High", "18 — Very High", "19 — Ultra",
        "20 — Extreme (ArcGIS/Mapbox)", "21 — Max Detail", "22 — Micro"
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

    advLayout->addWidget(m_settingsGroup);

    // Local file import group
    m_localGroup = new QGroupBox("LOCAL FILE IMPORT");
    auto* localLayout = new QFormLayout(m_localGroup);
    localLayout->setSpacing(6);
    localLayout->setContentsMargins(10, 16, 10, 10);
    localLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

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

    m_localGroup->setVisible(false);
    advLayout->addWidget(m_localGroup);

    // API Keys group
    m_keysGroup = new QGroupBox("API KEYS");
    auto* keysLayout = new QFormLayout(m_keysGroup);
    keysLayout->setSpacing(6);
    keysLayout->setContentsMargins(10, 16, 10, 10);
    keysLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

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

    advLayout->addWidget(m_keysGroup);
    mainLayout->addWidget(m_advancedContainer);
    m_advancedContainer->setVisible(false);  // Hidden by default

    // --- API Key Warning ---
    m_apiKeyWarning = new QLabel("");
    m_apiKeyWarning->setStyleSheet(
        "QLabel { color: #d29922; font-size: 11px; padding: 8px 10px;"
        "background: rgba(210,153,34,0.1); border: 1px solid rgba(210,153,34,0.3);"
        "border-radius: 6px; }");
    m_apiKeyWarning->setWordWrap(true);
    m_apiKeyWarning->setVisible(false);
    mainLayout->addWidget(m_apiKeyWarning);

    // --- Tile Selection ---
    auto* tileGroup = new QGroupBox("TILES");
    auto* tileLayout = new QVBoxLayout(tileGroup);
    tileLayout->setSpacing(6);
    tileLayout->setContentsMargins(10, 16, 10, 10);

    m_tileCountLabel = new QLabel("0 tiles selected");
    m_tileCountLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
    tileLayout->addWidget(m_tileCountLabel);

    auto* tileBtnLayout = new QHBoxLayout();
    tileBtnLayout->setSpacing(6);
    m_selectAllBtn = new QPushButton("Select All");
    connect(m_selectAllBtn, &QPushButton::clicked, m_store, &TerrainStore::selectAllTiles);
    m_clearTilesBtn = new QPushButton("Clear");
    connect(m_clearTilesBtn, &QPushButton::clicked, m_store, &TerrainStore::clearTileSelection);
    tileBtnLayout->addWidget(m_selectAllBtn);
    tileBtnLayout->addWidget(m_clearTilesBtn);
    tileLayout->addLayout(tileBtnLayout);

    mainLayout->addWidget(tileGroup);

    // --- Export Button ---
    m_exportBtn = new QPushButton("Export");
    m_exportBtn->setStyleSheet(
        "QPushButton { background-color: #238636; color: #ffffff; padding: 10px;"
        "font-weight: bold; border: none; border-radius: 6px; font-size: 13px; }"
        "QPushButton:hover { background-color: #2ea043; }"
        "QPushButton:disabled { background-color: #21262d; color: #484f58; }");
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportPanel::onExportClicked);
    mainLayout->addWidget(m_exportBtn);

    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();

    scrollArea->setWidget(content);
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);
}

void ExportPanel::onToggleAdvancedSettings() {
    bool visible = !m_advancedContainer->isVisible();
    m_advancedContainer->setVisible(visible);
    m_advancedToggle->setText(visible ? "Settings  −" : "Settings  +");
}

void ExportPanel::onDemSourceChanged() {
    int idx = m_demSourceCombo->currentIndex();
    bool isLocal = (idx == static_cast<int>(terrain::DemSource::Local_File));
    m_localGroup->setVisible(isLocal);
    updateApiKeyWarnings();
}

void ExportPanel::onImagerySourceChanged() {
    int idx = m_imagerySourceCombo->currentIndex();
    bool isGladArd = (idx == static_cast<int>(terrain::ImagerySource::GLAD_ARD_Landsat));
    bool isLocal = (idx == static_cast<int>(terrain::ImagerySource::Local_File));

    m_gladArdContainer->setVisible(isGladArd);

    // Show local file group if either DEM or imagery is local
    bool demLocal = (m_demSourceCombo->currentIndex() == static_cast<int>(terrain::DemSource::Local_File));
    m_localGroup->setVisible(isLocal || demLocal);

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

    // Export directly to the project's Terrain folder — no file dialog
    QString dir;
    if (m_ctx && m_ctx->projects().hasProject()) {
        dir = m_ctx->projects().current().basePath + "/Terrain";
        QDir().mkpath(dir);
    } else {
        // No project open — fall back to Documents/OpenGeoStudio/Exports
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/OpenGeoStudio/Exports";
        QDir().mkpath(dir);
    }

    m_exportBtn->setEnabled(false);
    m_exportBtn->setText("Exporting...");
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText("Exporting to: " + dir);

    m_engine->exportToDirectory(dir);
}

void ExportPanel::onExportProgress(int percent, const QString& stage) {
    m_progressBar->setValue(percent);
    m_statusLabel->setText(stage);
}

void ExportPanel::onExportFinished(bool success, const QString& message) {
    m_exportBtn->setEnabled(true);
    int count = m_store->selectedTiles().size();
    m_exportBtn->setText(count > 0 ? QString("Export %1 tile%2").arg(count).arg(count > 1 ? "s" : "") : "Export");
    m_progressBar->setVisible(false);
    m_statusLabel->setText(message);
    if (success) {
        QMessageBox::information(this, "Export Complete", message);
    } else {
        QMessageBox::warning(this, "Export Failed", message);
    }
}
