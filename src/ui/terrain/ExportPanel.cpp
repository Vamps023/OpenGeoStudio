// ExportPanel — Export settings UI implementation

#include "ExportPanel.hpp"
#include "ExportEngine.hpp"

#include <QFileDialog>
#include <QMessageBox>

ExportPanel::ExportPanel(TerrainStore* store, QWidget* parent)
    : QWidget(parent), m_store(store) {
    m_engine = new ExportEngine(store, this);
    setupUi();

    connect(m_engine, &ExportEngine::progress, this, &ExportPanel::onExportProgress);
    connect(m_engine, &ExportEngine::finished, this, &ExportPanel::onExportFinished);
    connect(m_store, &TerrainStore::tileSelectionChanged, this, [this]() {
        int count = m_store->selectedTiles().size();
        m_tileCountLabel->setText(QString("%1 tiles selected").arg(count));
    });
}

void ExportPanel::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // --- Export Settings ---
    auto* settingsGroup = new QGroupBox("Export Settings");
    auto* formLayout = new QFormLayout(settingsGroup);

    m_heightmapFormatCombo = new QComboBox();
    m_heightmapFormatCombo->addItems({"PNG 16-bit", "R16 Raw", "GeoTIFF Int16", "GeoTIFF UInt16", "GeoTIFF Float32"});
    connect(m_heightmapFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setHeightmapFormat(static_cast<terrain::HeightmapFormat>(idx));
    });
    formLayout->addRow("Heightmap:", m_heightmapFormatCombo);

    m_albedoFormatCombo = new QComboBox();
    m_albedoFormatCombo->addItems({"PNG", "GeoTIFF RGB"});
    connect(m_albedoFormatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setAlbedoFormat(static_cast<terrain::AlbedoFormat>(idx));
    });
    formLayout->addRow("Albedo:", m_albedoFormatCombo);

    m_demSourceCombo = new QComboBox();
    m_demSourceCombo->addItems({"OpenTopo SRTM GL1", "OpenTopo SRTM GL3", "OpenTopo ALOS AW3D30",
                                 "OpenTopo Copernicus GLO-30", "OpenTopo NASADEM", "GLAD SRTM"});
    connect(m_demSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setDemSource(static_cast<terrain::DemSource>(idx));
    });
    formLayout->addRow("DEM Source:", m_demSourceCombo);

    m_imagerySourceCombo = new QComboBox();
    m_imagerySourceCombo->addItems({"ArcGIS World Imagery", "Google Satellite", "Mapbox Satellite"});
    connect(m_imagerySourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setImagerySource(static_cast<terrain::ImagerySource>(idx));
    });
    formLayout->addRow("Imagery:", m_imagerySourceCombo);

    m_heightmapResSpin = new QSpinBox();
    m_heightmapResSpin->setRange(256, 8192);
    m_heightmapResSpin->setValue(1024);
    m_heightmapResSpin->setSuffix(" px");
    formLayout->addRow("Heightmap Res:", m_heightmapResSpin);

    m_albedoResSpin = new QSpinBox();
    m_albedoResSpin->setRange(256, 8192);
    m_albedoResSpin->setValue(1024);
    m_albedoResSpin->setSuffix(" px");
    formLayout->addRow("Albedo Res:", m_albedoResSpin);

    m_compressCheck = new QCheckBox("Deflate compression");
    formLayout->addRow("Compress:", m_compressCheck);

    mainLayout->addWidget(settingsGroup);

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

    mainLayout->addWidget(keysGroup);

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
    m_exportBtn->setStyleSheet("QPushButton { background-color: #0078d4; color: white; padding: 8px; font-weight: bold; }");
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportPanel::onExportClicked);
    mainLayout->addWidget(m_exportBtn);

    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("");
    m_statusLabel->setWordWrap(true);
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();
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
