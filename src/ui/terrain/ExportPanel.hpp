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

class ExportEngine;

class ExportPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExportPanel(TerrainStore* store, QWidget* parent = nullptr);

private slots:
    void onExportClicked();
    void onExportProgress(int percent, const QString& stage);
    void onExportFinished(bool success, const QString& message);

private:
    void setupUi();

    TerrainStore* m_store;
    ExportEngine* m_engine;

    // Format settings
    QComboBox* m_heightmapFormatCombo = nullptr;
    QComboBox* m_albedoFormatCombo = nullptr;
    QComboBox* m_demSourceCombo = nullptr;
    QComboBox* m_imagerySourceCombo = nullptr;
    QSpinBox* m_heightmapResSpin = nullptr;
    QSpinBox* m_albedoResSpin = nullptr;
    QCheckBox* m_compressCheck = nullptr;

    // API keys
    QLineEdit* m_openTopoKeyEdit = nullptr;
    QLineEdit* m_mapboxTokenEdit = nullptr;

    // Export controls
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_selectAllBtn = nullptr;
    QPushButton* m_clearTilesBtn = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_tileCountLabel = nullptr;
    QLabel* m_tileBadge = nullptr;
};
