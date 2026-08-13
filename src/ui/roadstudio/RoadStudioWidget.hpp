#pragma once

// ============================================================
// RoadStudioWidget — Combined Road Studio workspace widget
// ============================================================
//
// Contains the 2D/3D viewport, toolbar, and inspector panel.
// Switches between 2D (MapLibre + road overlay) and 3D (OpenGL mesh)
// based on the store's viewMode.
//

#include "../../core/ApplicationContext.hpp"
#include "RoadStudioStore.hpp"
#include "RoadEngineService.hpp"
#include "RoadViewport2D.hpp"
#include "RoadViewport3D.hpp"
#include "widgets/RoadStudioToolbar.hpp"
#include "widgets/RoadElevationEditor.hpp"
#include "widgets/LaneConfigWidget.hpp"

#include <QWidget>
#include <QVBoxLayout>
#include <QStackedWidget>

class RoadStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit RoadStudioWidget(ApplicationContext* ctx, QWidget* parent = nullptr)
        : QWidget(parent), m_ctx(ctx)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Create the store and engine service (owned by this widget)
        m_store = new RoadStudioStore(&m_ctx->events(), this);
        m_engine = new RoadEngineService(this);

        // Toolbar
        m_toolbar = new RoadStudioToolbar(m_store, this);
        layout->addWidget(m_toolbar);

        // Stacked widget for 2D/3D view switching
        m_viewStack = new QStackedWidget(this);

        // Page 0: 2D viewport (MapLibre + road overlay)
        m_viewport2d = new RoadViewport2D(ctx, m_store, m_engine, this);
        m_viewStack->addWidget(m_viewport2d);

        // Page 1: 3D viewport (OpenGL mesh)
        m_viewport3d = new RoadViewport3D(m_store, m_engine, this);
        m_viewStack->addWidget(m_viewport3d);

        layout->addWidget(m_viewStack, 1);

        // Elevation editor (bottom panel, collapsible)
        m_elevationEditor = new RoadElevationEditor(m_store, this);
        layout->addWidget(m_elevationEditor);

        // Lane config widget (shown in road/lane modes)
        m_laneConfig = new LaneConfigWidget(m_store, this);
        layout->addWidget(m_laneConfig);

        // Connect store changes
        connect(m_store, &RoadStudioStore::roadsChanged, this, &RoadStudioWidget::onRoadsChanged);
        connect(m_store, &RoadStudioStore::viewModeChanged, this, &RoadStudioWidget::onViewModeChanged);
        connect(m_store, &RoadStudioStore::selectionChanged,
                m_viewport2d, qOverload<>(&QWidget::update));
        connect(m_store, &RoadStudioStore::selectionChanged, this, [this]() {
            const auto& sel = m_store->selection();
            if (!sel.roadId.isEmpty()) {
                m_elevationEditor->setActiveRoad(sel.roadId);
            } else {
                m_elevationEditor->setActiveRoad("");
            }
        });
        connect(m_store, &RoadStudioStore::toolChanged,
                m_viewport2d, qOverload<>(&QWidget::update));
        connect(m_store, &RoadStudioStore::lmRoadStateChanged,
                m_viewport2d, qOverload<>(&QWidget::update));
        connect(m_store, &RoadStudioStore::debugModeChanged,
                m_viewport2d, qOverload<>(&QWidget::update));
    }

    RoadStudioStore* store() { return m_store; }
    RoadEngineService* engine() { return m_engine; }
    RoadViewport2D* viewport2d() { return m_viewport2d; }
    RoadViewport3D* viewport3d() { return m_viewport3d; }
    MapViewportWidget* mapWidget() { return m_viewport2d ? m_viewport2d->mapWidget() : nullptr; }

private slots:
    void onRoadsChanged() {
        if (m_viewport2d) m_viewport2d->update();
        if (m_viewStack && m_viewStack->currentIndex() == 1 && m_viewport3d) {
            m_viewport3d->refreshMeshes();
        }
    }

    void onViewModeChanged(roads::ViewMode mode) {
        if (mode == roads::ViewMode::Top) {
            m_viewStack->setCurrentIndex(0);
            if (m_viewport2d) m_viewport2d->update();
        } else {
            m_viewStack->setCurrentIndex(1);
            if (m_viewport3d) m_viewport3d->refreshMeshes();
        }
    }

private:
    ApplicationContext* m_ctx;
    RoadStudioStore* m_store = nullptr;
    RoadEngineService* m_engine = nullptr;
    RoadStudioToolbar* m_toolbar = nullptr;
    QStackedWidget* m_viewStack = nullptr;
    RoadViewport2D* m_viewport2d = nullptr;
    RoadViewport3D* m_viewport3d = nullptr;
    RoadElevationEditor* m_elevationEditor = nullptr;
    LaneConfigWidget* m_laneConfig = nullptr;
};
