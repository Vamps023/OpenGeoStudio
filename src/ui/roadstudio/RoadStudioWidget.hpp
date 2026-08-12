#pragma once

// ============================================================
// RoadStudioWidget — Combined Road Studio workspace widget
// ============================================================
//
// Contains the 2D viewport, toolbar, and inspector panel.
//

#include "../../core/ApplicationContext.hpp"
#include "RoadStudioStore.hpp"
#include "RoadViewport2D.hpp"
#include "widgets/RoadStudioToolbar.hpp"

#include <QWidget>
#include <QVBoxLayout>

class RoadStudioWidget : public QWidget {
    Q_OBJECT

public:
    explicit RoadStudioWidget(ApplicationContext* ctx, QWidget* parent = nullptr)
        : QWidget(parent), m_ctx(ctx)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Create the store (owned by this widget)
        m_store = new RoadStudioStore(&m_ctx->events(), this);

        // Toolbar
        m_toolbar = new RoadStudioToolbar(m_store, this);
        layout->addWidget(m_toolbar);

        // 2D viewport
        m_viewport = new RoadViewport2D(ctx, this);
        layout->addWidget(m_viewport, 1);

        // Connect store changes to viewport
        connect(m_store, &RoadStudioStore::roadsChanged,
                m_viewport, qOverload<>(&QWidget::update));
        connect(m_store, &RoadStudioStore::selectionChanged,
                m_viewport, qOverload<>(&QWidget::update));
        connect(m_store, &RoadStudioStore::toolChanged,
                m_viewport, qOverload<>(&QWidget::update));
        connect(m_store, &RoadStudioStore::lmRoadStateChanged,
                m_viewport, qOverload<>(&QWidget::update));
    }

    RoadStudioStore* store() { return m_store; }
    RoadViewport2D* viewport() { return m_viewport; }
    MapViewportWidget* mapWidget() { return m_viewport ? m_viewport->mapWidget() : nullptr; }

private:
    ApplicationContext* m_ctx;
    RoadStudioStore* m_store = nullptr;
    RoadStudioToolbar* m_toolbar = nullptr;
    RoadViewport2D* m_viewport = nullptr;
};
