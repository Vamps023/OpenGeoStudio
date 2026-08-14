#pragma once

// ============================================================
// TerrainStudioWidget — Terrain workspace widget
// ============================================================

#include "../../core/ApplicationContext.hpp"
#include "TerrainStore.hpp"
#include "TerrainViewport.hpp"
#include "ExportPanel.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QAction>
#include <QHBoxLayout>

class TerrainStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit TerrainStudioWidget(ApplicationContext* ctx, QWidget* parent = nullptr)
        : QWidget(parent), m_ctx(ctx)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_store = new TerrainStore(&m_ctx->events(), this);

        // Toolbar
        m_toolbar = new QToolBar("Terrain", this);
        m_toolbar->setMovable(false);
        setupToolbar();
        layout->addWidget(m_toolbar);

        // Main content: viewport + export panel side by side
        auto* contentWidget = new QWidget();
        auto* contentLayout = new QHBoxLayout(contentWidget);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(0);

        m_viewport = new TerrainViewport(ctx, m_store, this);
        contentLayout->addWidget(m_viewport, 3);

        m_exportPanel = new ExportPanel(m_store, this);
        m_exportPanel->setMaximumWidth(320);
        contentLayout->addWidget(m_exportPanel, 1);

        layout->addWidget(contentWidget, 1);

        // Status bar — GitHub dark theme
        m_statusLabel = new QLabel("Shift+drag on map to select area");
        m_statusLabel->setStyleSheet(
            "QLabel { background: #0d1117; color: #7d8590; padding: 4px 12px;"
            "border-top: 1px solid #30363d; font-size: 12px; }");
        layout->addWidget(m_statusLabel);
    }

    TerrainStore* store() { return m_store; }
    TerrainViewport* viewport() { return m_viewport; }
    MapViewportWidget* mapWidget() { return m_viewport ? m_viewport->mapWidget() : nullptr; }

private:
    void setupToolbar() {
        auto* tileLabel = new QLabel("Tile size (km):");
        m_toolbar->addWidget(tileLabel);

        auto* tileSpin = new QDoubleSpinBox();
        tileSpin->setRange(1, 16);
        tileSpin->setSingleStep(1);
        tileSpin->setValue(2);
        tileSpin->setSuffix(" km");
        connect(tileSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                m_store, &TerrainStore::setTileSizeKm);
        m_toolbar->addWidget(tileSpin);

        m_toolbar->addSeparator();

        // Visibility toggles
        auto* gridAct = m_toolbar->addAction("Grid");
        gridAct->setCheckable(true);
        gridAct->setChecked(true);
        connect(gridAct, &QAction::triggered, this, [this](bool checked) {
            if (m_viewport && m_viewport->overlay()) m_viewport->overlay()->setShowGrid(checked);
        });

        auto* labelsAct = m_toolbar->addAction("Labels");
        labelsAct->setCheckable(true);
        labelsAct->setChecked(true);
        connect(labelsAct, &QAction::triggered, this, [this](bool checked) {
            if (m_viewport && m_viewport->overlay()) m_viewport->overlay()->setShowLabels(checked);
        });

        auto* selAct = m_toolbar->addAction("Selection");
        selAct->setCheckable(true);
        selAct->setChecked(true);
        connect(selAct, &QAction::triggered, this, [this](bool checked) {
            if (m_viewport && m_viewport->overlay()) m_viewport->overlay()->setShowSelection(checked);
        });

        m_toolbar->addSeparator();

        auto* hintLabel = new QLabel("Shift+drag to select area | Click tiles to toggle");
        hintLabel->setStyleSheet("color: #7d8590; padding: 0 10px; font-size: 12px;");
        m_toolbar->addWidget(hintLabel);
    }

    ApplicationContext* m_ctx;
    TerrainStore* m_store = nullptr;
    QToolBar* m_toolbar = nullptr;
    TerrainViewport* m_viewport = nullptr;
    ExportPanel* m_exportPanel = nullptr;
    QLabel* m_statusLabel = nullptr;
};
