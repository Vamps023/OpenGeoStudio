// ExportPanel — Export settings UI implementation

#include "ExportPanel.hpp"
#include "ExportEngine.hpp"
#include "../../theme/Theme.hpp"


#include "gis/ui/CrsSelectorDialog.hpp"
#include "gis/crs/CRSManager.hpp"
#include "gis/crs/CRSSearch.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QStandardPaths>
#include <QDir>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QUrl>

// ============================================================
// Dark theme stylesheet (GitHub dark inspired, matching Electron)
// ============================================================
static QString darkThemeSheet() {
    return ogs::theme::resolveTokens(QStringLiteral(R"(
    QWidget { background: %BgBase%; color: %Text%; font-size: 12px; }
    QScrollArea { background: %BgBase%; border: none; }
    QGroupBox {
        background: %BgSurface%; border: 1px solid %Border%; border-radius: 6px;
        margin-top: 12px; padding-top: 8px; font-size: 11px;
        font-weight: bold; color: %TextMuted%; letter-spacing: 1px;
    }
    QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
    QComboBox {
        background: %BgOverlay%; border: 1px solid %Border%; border-radius: 4px;
        padding: 4px 8px; color: %Text%; min-height: 20px;
    }
    QComboBox::drop-down { border: none; width: 20px; }
    QComboBox::down-arrow { image: none; width: 0; height: 0; }
    QComboBox QAbstractItemView {
        background: %BgActive%; border: 1px solid %Border%; selection-background-color: %Accent%;
        color: %Text%; outline: none;
    }
    QSpinBox, QDoubleSpinBox {
        background: %BgOverlay%; border: 1px solid %Border%; border-radius: 4px;
        padding: 4px 8px; color: %Text%; min-height: 20px;
    }
    QLineEdit {
        background: %BgOverlay%; border: 1px solid %Border%; border-radius: 4px;
        padding: 4px 8px; color: %Text%;
    }
    QLineEdit:focus { border-color: %Accent%; }
    QCheckBox { color: %Text%; spacing: 6px; }
    QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px; }
    QCheckBox::indicator:unchecked { background: %BgOverlay%; border: 1px solid %Border%; }
    QCheckBox::indicator:checked { background: %Accent%; border: 1px solid %Accent%; }
    QPushButton {
        background: %BgOverlay%; border: 1px solid %Border%; border-radius: 4px;
        padding: 6px 12px; color: %Text%; font-weight: 500;
    }
    QPushButton:hover { background: %Border%; border-color: %TextSoft%; }
    QPushButton:pressed { background: %BgActive%; }
    QPushButton:disabled { color: %TextFaint%; background: %BgSurface%; }
    QToolButton {
        background: transparent; border: none; color: %TextMuted%;
        font-size: 10px; font-weight: bold; text-transform: uppercase;
        letter-spacing: 1px; padding: 6px 10px;
    }
    QToolButton:hover { color: %Text%; }
    QProgressBar {
        background: %BgOverlay%; border: 1px solid %Border%; border-radius: 4px;
        height: 14px; text-align: center; font-size: 10px; color: %Text%;
    }
    QProgressBar::chunk { background: %Accent%; border-radius: 3px; }
    QLabel { color: %Text%; }
)"));
}


ExportPanel::ExportPanel(TerrainStore* store, ApplicationContext* ctx, QWidget* parent)
    : QWidget(parent), m_store(store), m_ctx(ctx) {
    m_engine = new ExportEngine(store, this);
    setupUi();
    applyDarkTheme();

    connect(m_engine, &ExportEngine::progress, this, &ExportPanel::onExportProgress);
    connect(m_engine, &ExportEngine::finished, this, &ExportPanel::onExportFinished);
    connect(m_store, &TerrainStore::tileSelectionChanged, this, [this]() {
        const int count = m_store->selectedTiles().size();
        if (m_tileBadge) m_tileBadge->setText(QString::number(count));
        // Update export button text dynamically
        m_exportBtn->setText(count > 0
            ? QString("Export %1 tile%2").arg(count).arg(count > 1 ? "s" : "")
            : "Export");
    });
    connect(&m_elapsedTick, &QTimer::timeout, this, &ExportPanel::updateElapsedLabel);
}

void ExportPanel::applyDarkTheme() {
    setStyleSheet(darkThemeSheet());
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
    headerLabel->setStyleSheet(ogs::theme::resolveTokens(
        "QLabel { font-size: 14px; font-weight: bold; color: %Text%; letter-spacing: 2px; }"));
    headerLayout->addWidget(headerLabel);

    auto* tileBadge = new QLabel("0");
    tileBadge->setStyleSheet(ogs::theme::resolveTokens(
        "QLabel { background: %AccentEdge%; color: %Accent%; border-radius: 10px;"
        "padding: 2px 10px; font-size: 11px; font-weight: bold; }"));

    tileBadge->setAlignment(Qt::AlignCenter);
    tileBadge->setMinimumWidth(30);
    headerLayout->addWidget(tileBadge);
    headerLayout->addStretch();

    auto* engineBadge = new QLabel("C++ Native · Float32 GeoTIFF + PNG");
    engineBadge->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("QLabel { color: %Success%; font-size: 11px; }")));
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

    // Heightmap format is fixed: only 32-bit Float GeoTIFF is supported for
    // Unigine / QGIS interoperability.
    auto* hmCaption = new QLabel("GeoTIFF Float32 (full precision)");
    hmCaption->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%;")));
    basicLayout->addRow("Heightmap:", hmCaption);

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
    m_advancedToggle->setStyleSheet(ogs::theme::resolveTokens(
        "QToolButton { color: %TextMuted%; font-size: 10px; font-weight: bold;"
        "text-transform: uppercase; letter-spacing: 1px; padding: 8px 0;"
        "border-top: 1px solid %Border%; border-bottom: 1px solid %Border%;"
        "background: transparent; }"
        "QToolButton:hover { color: %Text%; }"));
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
    gladHint->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));
    gladLayout->addWidget(gladHint);
    formLayout->addRow("GLAD Interval:", m_gladArdContainer);
    m_gladArdContainer->setVisible(false);

    // CRS — dynamic combo with "Select CRS..." dialog option
    m_crsCombo = new QComboBox();
    m_crsCombo->addItem("Use Project CRS (if set)", static_cast<int>(terrain::CrsSource::Project_CRS));
    m_crsCombo->addItem("Auto (UTM from bounds centroid)", static_cast<int>(terrain::CrsSource::Auto_UTM));
    m_crsCombo->addItem("EPSG:4326 — WGS84 (lat/lon)", static_cast<int>(terrain::CrsSource::EPSG_4326));
    m_crsCombo->addItem("EPSG:3857 — Web Mercator", static_cast<int>(terrain::CrsSource::EPSG_3857));
    m_crsCombo->addItem("Select CRS...", -1);  // Sentinel: opens CRS selector dialog

    connect(m_crsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int idx) {
        int crsValue = m_crsCombo->itemData(idx).toInt();
        if (crsValue == -1) {
            // "Select CRS..." — open the PROJ-backed CRS selector dialog
            gis::CrsSelectorDialog dialog(this);
            if (dialog.exec() == QDialog::Accepted) {
                auto crs = dialog.selectedCRS();
                if (crs.isValid()) {
                    // Update export settings with the selected EPSG code
                    terrain::ExportSettings s = m_store->exportSettings();
                    s.crsSource = terrain::CrsSource::Custom_EPSG;
                    s.customEpsg = crs.code;
                    s.customCrsName = QString::fromStdString(crs.name);
                    m_store->setExportSettings(s);
                    // Update combo label to show the selected CRS
                    // (replace "Select CRS..." entry with the actual CRS name)
                    QString label = QString("EPSG:%1 — %2")
                        .arg(crs.code)
                        .arg(QString::fromStdString(crs.name));
                    m_crsCombo->setItemText(idx, label);
                    m_crsCombo->setItemData(idx, static_cast<int>(terrain::CrsSource::Custom_EPSG));
                    return;
                }
            }
            // User cancelled — revert to previous selection
            m_crsCombo->blockSignals(true);
            // Find and select the entry matching the current store setting
            auto currentSrc = m_store->exportSettings().crsSource;
            for (int i = 0; i < m_crsCombo->count(); ++i) {
                if (m_crsCombo->itemData(i).toInt() == static_cast<int>(currentSrc)) {
                    m_crsCombo->setCurrentIndex(i);
                    break;
                }
            }
            m_crsCombo->blockSignals(false);
        } else {
            auto src = static_cast<terrain::CrsSource>(crsValue);
            m_store->setCrsSource(src);
        }
    });
    // Default to Project CRS — falls back to EPSG:4326 if no project CRS set
    m_crsCombo->setCurrentIndex(0);
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
    m_imageryZoomCombo->addItem("Auto (recommended)", 0);
    m_imageryZoomCombo->addItem("10 — Low", 10);
    m_imageryZoomCombo->addItem("12 — Medium", 12);
    m_imageryZoomCombo->addItem("14 — Good", 14);
    m_imageryZoomCombo->addItem("16 — High", 16);
    m_imageryZoomCombo->addItem("18 — Very High", 18);
    m_imageryZoomCombo->addItem("19 — Ultra", 19);
    connect(m_imageryZoomCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_store->setImageryZoomLevel(m_imageryZoomCombo->itemData(idx).toInt());
    });
    auto syncImageryZoom = [this]() {
        const QSignalBlocker blocker(m_imageryZoomCombo);
        const int idx = m_imageryZoomCombo->findData(m_store->exportSettings().imageryZoomLevel);
        m_imageryZoomCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    connect(m_store, &TerrainStore::exportSettingsChanged, this, syncImageryZoom);
    syncImageryZoom();
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
    m_localDemLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));
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
    m_localImageryLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));
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
    m_apiKeyWarning->setStyleSheet(ogs::theme::resolveTokens(
        "QLabel { color: %Warning%; font-size: 11px; padding: 8px 10px;"
        "background: rgba(210,153,34,0.1); border: 1px solid rgba(210,153,34,0.3);"
        "border-radius: 6px; }"));
    m_apiKeyWarning->setWordWrap(true);
    m_apiKeyWarning->setVisible(false);
    mainLayout->addWidget(m_apiKeyWarning);

    // --- Export Button ---
    m_exportBtn = new QPushButton("Export");
    m_exportBtn->setStyleSheet(ogs::theme::resolveTokens(
        "QPushButton { background-color: %Accent%; color: %OnAccent%; padding: 10px;"
        "font-weight: bold; border: none; border-radius: 6px; font-size: 13px; }"
        "QPushButton:hover { background-color: %AccentBright%; }"
        "QPushButton:disabled { background-color: %BgOverlay%; color: %TextFaint%; }"));
    connect(m_exportBtn, &QPushButton::clicked, this, &ExportPanel::onExportClicked);
    mainLayout->addWidget(m_exportBtn);

    // --- Progress row: bar + cancel + elapsed time ---
    auto* progRow = new QHBoxLayout();
    progRow->setSpacing(6);
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    progRow->addWidget(m_progressBar, 1);
    m_elapsedLabel = new QLabel();
    m_elapsedLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));
    m_elapsedLabel->setFixedWidth(40);
    m_elapsedLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_elapsedLabel->setVisible(false);
    progRow->addWidget(m_elapsedLabel);
    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &ExportPanel::onCancelClicked);
    progRow->addWidget(m_cancelBtn);
    mainLayout->addLayout(progRow);

    // --- Status row: message + inline Open Folder ---
    auto* statusRow = new QHBoxLayout();
    statusRow->setSpacing(6);
    m_statusLabel = new QLabel("");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_statusLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));
    statusRow->addWidget(m_statusLabel, 1);

    m_openFolderBtn = new QPushButton("Open Folder");
    m_openFolderBtn->setVisible(false);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &ExportPanel::onOpenFolderClicked);
    statusRow->addWidget(m_openFolderBtn);
    mainLayout->addLayout(statusRow);

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
        // No project open — fall back to C:/OpenGeoStudio/Exports
        dir = "C:/OpenGeoStudio/Exports";
        QDir().mkpath(dir);
    }

    m_exportBtn->setEnabled(false);
    m_exportBtn->setText("Exporting...");
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_cancelBtn->setVisible(true);
    m_openFolderBtn->setVisible(false);
    m_elapsedLabel->setVisible(true);
    m_elapsed.start();
    m_elapsedTick.start(1000);
    updateElapsedLabel();
    m_statusLabel->setText("Exporting to: " + dir);
    m_statusLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %TextMuted%; font-size: 11px;")));

    m_lastExportDir = dir;
    m_engine->exportToDirectory(dir);
}

void ExportPanel::onCancelClicked() {
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setText("Cancelling...");
    m_engine->cancel();
}

void ExportPanel::onOpenFolderClicked() {
    if (!m_lastExportDir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastExportDir));
}

void ExportPanel::updateElapsedLabel() {
    const qint64 ms = m_elapsed.elapsed();
    m_elapsedLabel->setText(QString("%1:%2")
        .arg(ms / 60000).arg((ms / 1000) % 60, 2, 10, QChar('0')));
}

void ExportPanel::onExportProgress(int percent, const QString& stage) {
    m_progressBar->setValue(percent);
    m_statusLabel->setText(stage);
    updateElapsedLabel();
}

void ExportPanel::onExportFinished(bool success, const QString& message) {
    m_elapsedTick.stop();
    m_exportBtn->setEnabled(true);
    const int count = m_store->selectedTiles().size();
    m_exportBtn->setText(count > 0 ? QString("Export %1 tile%2").arg(count).arg(count > 1 ? "s" : "") : "Export");
    m_progressBar->setVisible(false);
    m_cancelBtn->setVisible(false);
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setText("Cancel");
    m_elapsedLabel->setVisible(false);

    if (message == QStringLiteral("Export cancelled.")) {
        m_statusLabel->setText("Export cancelled.");
        // keep default muted styling
    } else if (success) {
        m_openFolderBtn->setVisible(true);
        m_statusLabel->setText(QString("✔ %1").arg(message));
        m_statusLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %Success%; font-size: 11px;")));
    } else {
        m_statusLabel->setText(QString("✖ %1").arg(message));
        m_statusLabel->setStyleSheet(ogs::theme::resolveTokens(QStringLiteral("color: %Danger%; font-size: 11px;")));
        QMessageBox::warning(this, "Export Failed", message);
    }
}
