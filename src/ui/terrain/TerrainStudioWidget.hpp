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

#include "../theme/Theme.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QToolBar>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QAction>
#include <QHBoxLayout>
#include <QToolButton>
#include <QFrame>
#include <QSplitter>
#include <QSettings>

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

        // Main content: LayerStack | viewport | export panel — resizable
        m_splitter = new QSplitter(Qt::Horizontal, this);
        m_splitter->setChildrenCollapsible(false);

        // Left panel: LayerStack
        m_layerStack = new LayerStack(m_store);
        m_layerStack->setMinimumWidth(160);
        m_layerStack->setStyleSheet(
            QStringLiteral("QWidget { background: %1; }").arg(ogs::theme::c::BgBase));
        m_splitter->addWidget(m_layerStack);

        auto* canvasFrame = new QFrame();
        canvasFrame->setObjectName(QStringLiteral("terrainCanvasFrame"));
        canvasFrame->setStyleSheet(
            QStringLiteral("QFrame#terrainCanvasFrame { background: %1; border: 1px solid %2;"
            "border-radius: %3px; }")
                .arg(ogs::theme::c::BgSurface, ogs::theme::c::Border)
                .arg(ogs::theme::RadiusM));
        auto* canvasLayout = new QVBoxLayout(canvasFrame);
        canvasLayout->setContentsMargins(8, 8, 8, 8);
        canvasLayout->setSpacing(0);

        m_viewport = new TerrainViewport(ctx, m_store, canvasFrame);
        m_viewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        canvasLayout->addWidget(m_viewport, 1);
        m_splitter->addWidget(canvasFrame);

        m_exportPanel = new ExportPanel(m_store, ctx);
        m_exportPanel->setMinimumWidth(300);
        m_splitter->addWidget(m_exportPanel);

        // Map gets the extra space; remember pane sizes across runs
        m_splitter->setStretchFactor(0, 0);
        m_splitter->setStretchFactor(1, 1);
        m_splitter->setStretchFactor(2, 0);
        m_splitter->setSizes({220, 600, 360});
        {
            QSettings s;
            const QByteArray st = s.value("terrain/splitter").toByteArray();
            if (!st.isEmpty()) m_splitter->restoreState(st);
        }
        connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
            QSettings s;
            s.setValue("terrain/splitter", m_splitter->saveState());
        });

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

        layout->addWidget(m_splitter, 1);

        // Status bar — GitHub dark theme
        m_statusLabel = new QLabel("Shift+drag on map to select area");
        m_statusLabel->setStyleSheet(
            QStringLiteral("QLabel { background: %1; color: %2; padding: 4px 12px;"
            "border-top: 1px solid %3; font-size: 12px; }")
                .arg(ogs::theme::c::BgBase, ogs::theme::c::TextMuted, ogs::theme::c::Border));
        layout->addWidget(m_statusLabel);
    }

    TerrainStore* store() { return m_store; }
    TerrainViewport* viewport() { return m_viewport; }
    MapViewportWidget* mapWidget() { return m_viewport ? m_viewport->mapWidget() : nullptr; }

private:
    void setupToolbar() {
        m_toolbar->setStyleSheet(
            ogs::theme::resolveTokens(QStringLiteral("QToolBar { background: %1; border-bottom: 2px solid %2; spacing: 4px; padding: 4px; }"
            "QToolBar QToolButton { color: %3; padding: 4px 10px; border-radius: 4px; }"
            "QToolBar QToolButton:hover { background: %4; }"
            "QToolBar QToolButton:checked { background: %5; color: %OnAccent%; }"
            "QToolBar QLabel { color: %6; font-size: 11px; padding: 0 6px; }"
            "QToolBar QDoubleSpinBox { background: %4; border: 1px solid %2; border-radius: 4px; padding: 2px 6px; color: %3; }")
                .arg(ogs::theme::c::BgBase, ogs::theme::c::Border, ogs::theme::c::Text,
                     ogs::theme::c::BgOverlay, ogs::theme::c::Accent, ogs::theme::c::TextMuted)));


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
        tileCountLabel->setStyleSheet(ogs::theme::resolveTokens(
            QStringLiteral("color: %Accent%; font-weight: bold; padding: 0 8px;")));
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
        hintLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: %2px;")
                .arg(ogs::theme::c::TextMuted).arg(ogs::theme::FontSmall));
        m_toolbar->addWidget(hintLabel);

        // Location search takes the remaining toolbar width
        auto* searchHolder = new QWidget();
        searchHolder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto* searchLayout = new QHBoxLayout(searchHolder);
        searchLayout->setContentsMargins(8, 0, 4, 0);
        m_searchBar = new SearchBar();
        m_searchBar->setMaximumWidth(360);
        searchLayout->addWidget(m_searchBar);
        searchLayout->addStretch();
        m_toolbar->addWidget(searchHolder);
    }

    ApplicationContext* m_ctx;
    TerrainStore* m_store = nullptr;
    QToolBar* m_toolbar = nullptr;
    QSplitter* m_splitter = nullptr;
    SearchBar* m_searchBar = nullptr;
    LayerStack* m_layerStack = nullptr;
    TerrainViewport* m_viewport = nullptr;
    ExportPanel* m_exportPanel = nullptr;
    QLabel* m_statusLabel = nullptr;
};
