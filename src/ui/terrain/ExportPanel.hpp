#pragma once

// ============================================================
// ExportPanel — Export settings and trigger UI
// ============================================================
//
// Replaces modules/export/client/ExportPanel/ExportPanel.tsx.
// Shows export format options, DEM/imagery source selection,
// and an Export button that triggers the export engine.
//

#include "TerrainStore.hpp"
#include "../../core/ApplicationContext.hpp"

#include <QWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QScrollArea>
#include <QToolButton>

class ExportEngine;

class ExportPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExportPanel(TerrainStore* store, ApplicationContext* ctx = nullptr, QWidget* parent = nullptr);

private slots:
    void onExportClicked();
    void onExportProgress(int percent, const QString& stage);
    void onExportFinished(bool success, const QString& message);
    void onDemSourceChanged();
    void onImagerySourceChanged();
    void onToggleAdvancedSettings();

private:
    void setupUi();
    void updateApiKeyWarnings();
    void applyDarkTheme();

    TerrainStore* m_store;
    ExportEngine* m_engine;
    ApplicationContext* m_ctx;

    // Format settings
    QComboBox* m_heightmapFormatCombo = nullptr;
    QComboBox* m_albedoFormatCombo = nullptr;
    QComboBox* m_demSourceCombo = nullptr;
    QComboBox* m_imagerySourceCombo = nullptr;
    QComboBox* m_crsCombo = nullptr;
    QComboBox* m_heightmapResCombo = nullptr;
    QComboBox* m_albedoResCombo = nullptr;
    QComboBox* m_imageryZoomCombo = nullptr;
    QSpinBox* m_gladArdIntervalSpin = nullptr;
    QCheckBox* m_compressCheck = nullptr;

    // API keys
    QLineEdit* m_openTopoKeyEdit = nullptr;
    QLineEdit* m_mapboxTokenEdit = nullptr;
    QLineEdit* m_maptilerTokenEdit = nullptr;
    QLineEdit* m_gpxzKeyEdit = nullptr;
    QLineEdit* m_stadiaKeyEdit = nullptr;

    // Local file import
    QPushButton* m_localDemBtn = nullptr;
    QPushButton* m_localImageryBtn = nullptr;
    QLabel* m_localDemLabel = nullptr;
    QLabel* m_localImageryLabel = nullptr;

    // API key warning
    QLabel* m_apiKeyWarning = nullptr;

    // GLAD ARD interval container (shown/hidden based on imagery source)
    QWidget* m_gladArdContainer = nullptr;

    // Collapsible advanced settings
    QToolButton* m_advancedToggle = nullptr;
    QWidget* m_advancedContainer = nullptr;
    QGroupBox* m_settingsGroup = nullptr;
    QGroupBox* m_keysGroup = nullptr;
    QGroupBox* m_localGroup = nullptr;

    // Export controls
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_selectAllBtn = nullptr;
    QPushButton* m_clearTilesBtn = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_tileCountLabel = nullptr;
    QLabel* m_tileBadge = nullptr;
};
