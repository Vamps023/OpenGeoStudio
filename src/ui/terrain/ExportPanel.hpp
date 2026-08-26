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
#include <QTimer>
#include <QElapsedTimer>

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
    void onCancelClicked();
    void onOpenFolderClicked();

private:
    void setupUi();
    void updateApiKeyWarnings();
    void applyDarkTheme();
    void updateElapsedLabel();

    TerrainStore* m_store;
    ExportEngine* m_engine;
    ApplicationContext* m_ctx;

    // Format settings
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
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QPushButton* m_openFolderBtn = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_elapsedLabel = nullptr;
    QString m_lastExportDir;
    QElapsedTimer m_elapsed;
    QTimer m_elapsedTick;
    QLabel* m_tileBadge = nullptr;
};
