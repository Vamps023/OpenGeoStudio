#pragma once

// ============================================================
// TerrainStudioWidget — Terrain workspace widget
// ============================================================

#include "../../core/ApplicationContext.hpp"
#include "TerrainStore.hpp"
#include "TerrainViewport.hpp"
#include "ExportPanel.hpp"
#include "LayerStack.hpp"
#include "SearchBar.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QAction>
#include <QHBoxLayout>
#include <QToolButton>
#include <QFrame>

class TerrainStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit TerrainStudioWidget(ApplicationContext* ctx, QWidget* parent = nullptr)
        : QWidget(parent), m_ctx(ctx)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_store = &m_ctx->terrain();  // Use shared TerrainStore from context

        // Toolbar
        m_toolbar = new QToolBar("Terrain", this);
        m_toolbar->setMovable(false);
        setupToolbar();
        layout->addWidget(m_toolbar);

        // Search bar row
        auto* searchContainer = new QWidget();
        searchContainer->setStyleSheet("background: #0d1117; border-bottom: 1px solid #30363d;");
        auto* searchLayout = new QHBoxLayout(searchContainer);
        searchLayout->setContentsMargins(8, 4, 8, 4);
        m_searchBar = new SearchBar();
        searchLayout->addWidget(m_searchBar);
        layout->addWidget(searchContainer);

        // Main content: LayerStack | viewport | export panel
        auto* contentWidget = new QWidget();
        auto* contentLayout = new QHBoxLayout(contentWidget);
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(0);

        // Left panel: LayerStack
        m_layerStack = new LayerStack(m_store);
        m_layerStack->setMaximumWidth(240);
        m_layerStack->setStyleSheet("QWidget { background: #0d1117; }");
        contentLayout->addWidget(m_layerStack);

        // Separator
        auto* leftSep = new QFrame();
        leftSep->setFrameShape(QFrame::VLine);
        leftSep->setStyleSheet("color: #30363d;");
        contentLayout->addWidget(leftSep);

        m_viewport = new TerrainViewport(ctx, m_store, this);
        contentLayout->addWidget(m_viewport, 3);

        // Separator
        auto* rightSep = new QFrame();
        rightSep->setFrameShape(QFrame::VLine);
        rightSep->setStyleSheet("color: #30363d;");
        contentLayout->addWidget(rightSep);

        m_exportPanel = new ExportPanel(m_store, ctx, this);
        m_exportPanel->setMinimumWidth(340);
        m_exportPanel->setMaximumWidth(400);
        contentLayout->addWidget(m_exportPanel, 1);

        // Connect search bar to map — fly to location on select
        connect(m_searchBar, &SearchBar::locationSelected, this, [this](double lat, double lon, int zoom) {
            if (m_viewport && m_viewport->mapWidget()) {
                m_viewport->mapWidget()->setCenter(lat, lon);
                m_viewport->mapWidget()->setZoom(zoom);
                if (m_statusLabel) {
                    m_statusLabel->setText(QString("Flew to %1, %2 (zoom %3)").arg(lat, 0, 'f', 4).arg(lon, 0, 'f', 4).arg(zoom));
                }
            }
        });

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
        m_toolbar->setStyleSheet(
            "QToolBar { background: #0d1117; border-bottom: 1px solid #30363d; spacing: 4px; padding: 4px; }"
            "QToolBar QToolButton { color: #e6edf3; padding: 4px 10px; border-radius: 4px; }"
            "QToolBar QToolButton:hover { background: #21262d; }"
            "QToolBar QToolButton:checked { background: #1f6feb; color: #ffffff; }"
            "QToolBar QLabel { color: #7d8590; font-size: 11px; padding: 0 6px; }"
            "QToolBar QDoubleSpinBox { background: #21262d; border: 1px solid #30363d; border-radius: 4px; padding: 2px 6px; color: #e6edf3; }");

        // Tile size buttons (discrete, matching Electron)
        auto* tileLabel = new QLabel("Tile:");
        m_toolbar->addWidget(tileLabel);

        for (int size : {1, 2, 4, 8, 16}) {
            auto* btn = new QToolButton();
            btn->setText(QString("%1km").arg(size));
            btn->setCheckable(true);
            btn->setChecked(size == 2);
            connect(btn, &QToolButton::clicked, this, [this, size, btn]() {
                m_store->setTileSizeKm(size);
                // Uncheck siblings
                for (auto* child : m_toolbar->findChildren<QToolButton*>()) {
                    if (child != btn && child->text().endsWith("km"))
                        child->setChecked(false);
                }
            });
            m_toolbar->addWidget(btn);
        }

        m_toolbar->addSeparator();

        // Select All / Clear
        auto* selectAllBtn = new QToolButton();
        selectAllBtn->setText("Select All");
        connect(selectAllBtn, &QToolButton::clicked, m_store, &TerrainStore::selectAllTiles);
        m_toolbar->addWidget(selectAllBtn);

        auto* clearBtn = new QToolButton();
        clearBtn->setText("Clear");
        connect(clearBtn, &QToolButton::clicked, m_store, &TerrainStore::clearTileSelection);
        m_toolbar->addWidget(clearBtn);

        // Tile count display
        auto* tileCountLabel = new QLabel("0/0");
        tileCountLabel->setStyleSheet("color: #58a6ff; font-weight: bold; padding: 0 8px;");
        m_toolbar->addWidget(tileCountLabel);
        connect(m_store, &TerrainStore::tileSelectionChanged, this, [tileCountLabel, this]() {
            int selected = m_store->selectedTiles().size();
            int total = m_store->tileGrid().tiles.size();
            tileCountLabel->setText(QString("%1/%2").arg(selected).arg(total));
        });

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

        // Zoom controls
        auto* zoomInBtn = new QToolButton();
        zoomInBtn->setText("➕");
        zoomInBtn->setToolTip("Zoom in");
        zoomInBtn->setShortcut(QKeySequence::ZoomIn);
        connect(zoomInBtn, &QToolButton::clicked, this, [this]() {
            if (m_viewport && m_viewport->mapWidget() && m_viewport->mapWidget()->map()) {
                auto* map = m_viewport->mapWidget()->map();
                map->setZoom(std::min(20.0, map->zoom() + 1.0));
            }
        });
        m_toolbar->addWidget(zoomInBtn);

        auto* zoomOutBtn = new QToolButton();
        zoomOutBtn->setText("➖");
        zoomOutBtn->setToolTip("Zoom out");
        zoomOutBtn->setShortcut(QKeySequence::ZoomOut);
        connect(zoomOutBtn, &QToolButton::clicked, this, [this]() {
            if (m_viewport && m_viewport->mapWidget() && m_viewport->mapWidget()->map()) {
                auto* map = m_viewport->mapWidget()->map();
                map->setZoom(std::max(0.0, map->zoom() - 1.0));
            }
        });
        m_toolbar->addWidget(zoomOutBtn);

        auto* fitBtn = new QToolButton();
        fitBtn->setText("Fit");
        fitBtn->setToolTip("Fit to selection");
        connect(fitBtn, &QToolButton::clicked, this, [this]() {
            if (m_viewport && m_viewport->mapWidget()) {
                const auto& b = m_store->selectedBounds();
                if (b.isValid()) {
                    m_viewport->mapWidget()->fitBounds(b.south, b.west, b.north, b.east);
                }
            }
        });
        m_toolbar->addWidget(fitBtn);

        m_toolbar->addSeparator();

        auto* hintLabel = new QLabel("Shift+drag to select | Click tiles to toggle | Scroll to zoom");
        hintLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
        m_toolbar->addWidget(hintLabel);
    }

    ApplicationContext* m_ctx;
    TerrainStore* m_store = nullptr;
    QToolBar* m_toolbar = nullptr;
    SearchBar* m_searchBar = nullptr;
    LayerStack* m_layerStack = nullptr;
    TerrainViewport* m_viewport = nullptr;
    ExportPanel* m_exportPanel = nullptr;
    QLabel* m_statusLabel = nullptr;
};
