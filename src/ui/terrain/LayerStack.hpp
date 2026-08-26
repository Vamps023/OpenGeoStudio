#pragma once

// ============================================================
// LayerStack — Terrain layer management and mask configuration
// ============================================================
//
// Replaces modules/terrain/client/LayerStack/LayerStack.tsx.
// Shows DEM, satellite imagery, and mask layers with toggle controls.
// Mask layers have additional configuration (road width, cliff threshold).
//

#include "TerrainStore.hpp"

#include "../../theme/Theme.hpp"


#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QSlider>
#include <QFrame>
#include <QScrollArea>

class LayerStack : public QWidget {
    Q_OBJECT

public:
    explicit LayerStack(TerrainStore* store, QWidget* parent = nullptr)
        : QWidget(parent), m_store(store)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Header
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(12, 10, 12, 10);
        headerLayout->setSpacing(8);
        auto* title = new QLabel("LAYERS");
        title->setStyleSheet(
            QStringLiteral("QLabel { font-size: %1px; font-weight: bold; color: %2;"
            "letter-spacing: 2px; }")
                .arg(ogs::theme::FontSmall).arg(ogs::theme::c::TextMuted));
        headerLayout->addWidget(title);
        headerLayout->addStretch();

        m_tileInfoLabel = new QLabel("");
        m_tileInfoLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: %2px;")
                .arg(ogs::theme::c::TextMuted).arg(ogs::theme::FontSmall));
        headerLayout->addWidget(m_tileInfoLabel);
        layout->addLayout(headerLayout);

        // Separator
        auto* sep = new QFrame();
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet(QStringLiteral("color: %1;").arg(ogs::theme::c::Border));
        layout->addWidget(sep);

        // Scrollable layer list
        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet(
            QStringLiteral("QScrollArea { background: %1; border: none; }")
                .arg(ogs::theme::c::BgBase));

        auto* content = new QWidget();
        content->setStyleSheet(
            QStringLiteral("background: %1;").arg(ogs::theme::c::BgBase));
        m_listLayout = new QVBoxLayout(content);
        m_listLayout->setContentsMargins(0, 0, 0, 0);
        m_listLayout->setSpacing(0);

        buildLayerList();
        scroll->setWidget(content);
        layout->addWidget(scroll, 1);

        // Connect store signals
        connect(m_store, &TerrainStore::tileGridChanged, this, &LayerStack::updateTileInfo);
        connect(m_store, &TerrainStore::tileSelectionChanged, this, &LayerStack::updateTileInfo);

        updateTileInfo();
    }

private:
    void buildLayerList() {
        // DEM layer
        addLayerRow("DEM (Heightmap)", ogs::theme::c::Warning, [this]() {
            m_store->setDemVisible(!m_store->demVisible());
        }, [this]() { return m_store->demVisible(); });

        // Satellite imagery layer
        addLayerRow("Satellite Imagery", ogs::theme::c::Success, [this]() {
            m_store->setSatelliteVisible(!m_store->satelliteVisible());
        }, [this]() { return m_store->satelliteVisible(); });

        // Mask layers (Road/Water/Vegetation/Building/Cliff) are hidden until
        // mask generation is wired into the export pipeline.

        m_listLayout->addStretch();
    }

    void addLayerRow(const QString& label, const QString& color,
                     std::function<void()> onToggle,
                     std::function<bool()> isActive) {
        auto* row = new QWidget();
        row->setStyleSheet(
            QStringLiteral("QWidget { background: %1; border-bottom: 1px solid %2; }"
            "QWidget:hover { background: %3; }")
                .arg(ogs::theme::c::BgBase, ogs::theme::c::BorderSub, ogs::theme::c::BgSurface));
        row->setAutoFillBackground(true);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(8);

        auto* dot = new QLabel();
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(color));
        rowLayout->addWidget(dot);

        auto* name = new QLabel(label);
        name->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px;").arg(ogs::theme::c::Text));
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        rowLayout->addWidget(name);

        auto* visBtn = new QToolButton();
        visBtn->setCheckable(true);
        visBtn->setChecked(isActive());
        visBtn->setText(isActive() ? "On" : "Off");
        visBtn->setStyleSheet(
            QStringLiteral("QToolButton { color: %1; font-size: %2px; padding: 2px 8px; border-radius: 3px; }"
            "QToolButton:checked { color: %3; }")
                .arg(ogs::theme::c::TextMuted).arg(ogs::theme::FontSmall)
                .arg(ogs::theme::c::Success));
        connect(visBtn, &QToolButton::clicked, this, [onToggle, visBtn, isActive]() {
            onToggle();
            visBtn->setChecked(isActive());
            visBtn->setText(isActive() ? "On" : "Off");
        });
        rowLayout->addWidget(visBtn);

        m_listLayout->addWidget(row);
    }

private slots:
    void updateTileInfo() {
        int total = m_store->tileGrid().tiles.size();
        int selected = m_store->selectedTiles().size();
        if (total > 0) {
            m_tileInfoLabel->setText(QString("%1 tiles").arg(total));
        } else {
            m_tileInfoLabel->setText("No tiles");
        }
    }

private:
    TerrainStore* m_store;
    QVBoxLayout* m_listLayout = nullptr;
    QLabel* m_tileInfoLabel = nullptr;
};
