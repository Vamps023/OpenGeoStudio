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
            "QLabel { font-size: 11px; font-weight: bold; color: #7d8590;"
            "letter-spacing: 2px; }");
        headerLayout->addWidget(title);
        headerLayout->addStretch();

        m_tileInfoLabel = new QLabel("");
        m_tileInfoLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
        headerLayout->addWidget(m_tileInfoLabel);
        layout->addLayout(headerLayout);

        // Separator
        auto* sep = new QFrame();
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #30363d;");
        layout->addWidget(sep);

        // Scrollable layer list
        auto* scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet("QScrollArea { background: #0d1117; border: none; }");

        auto* content = new QWidget();
        content->setStyleSheet("background: #0d1117;");
        m_listLayout = new QVBoxLayout(content);
        m_listLayout->setContentsMargins(0, 0, 0, 0);
        m_listLayout->setSpacing(0);

        buildLayerList();
        scroll->setWidget(content);
        layout->addWidget(scroll, 1);

        // Connect store signals
        connect(m_store, &TerrainStore::tileGridChanged, this, &LayerStack::updateTileInfo);
        connect(m_store, &TerrainStore::maskSettingsChanged, this, &LayerStack::onMaskSettingsChanged);
        connect(m_store, &TerrainStore::visibilityChanged, this, &LayerStack::onVisibilityChanged);
        connect(m_store, &TerrainStore::tileSelectionChanged, this, &LayerStack::updateTileInfo);

        updateTileInfo();
    }

private:
    void buildLayerList() {
        // DEM layer
        addLayerRow("DEM (Heightmap)", "#d29922", [this]() {
            m_store->setDemVisible(!m_store->demVisible());
        }, [this]() { return m_store->demVisible(); });

        // Satellite imagery layer
        addLayerRow("Satellite Imagery", "#3fb950", [this]() {
            m_store->setSatelliteVisible(!m_store->satelliteVisible());
        }, [this]() { return m_store->satelliteVisible(); });

        // Mask layers
        addMaskLayer("Road Mask", "#58a6ff", MaskType::Road);
        addMaskLayer("Water Mask", "#06b6d4", MaskType::Water);
        addMaskLayer("Vegetation Mask", "#3fb950", MaskType::Vegetation);
        addMaskLayer("Building Mask", "#d29922", MaskType::Building);
        addMaskLayer("Cliff Mask", "#f85149", MaskType::Cliff);

        m_listLayout->addStretch();
    }

    enum class MaskType { Road, Water, Vegetation, Building, Cliff };

    void addLayerRow(const QString& label, const QString& color,
                     std::function<void()> onToggle,
                     std::function<bool()> isActive) {
        auto* row = new QWidget();
        row->setStyleSheet(
            "QWidget { background: #0d1117; border-bottom: 1px solid #21262d; }"
            "QWidget:hover { background: #161b22; }");
        row->setAutoFillBackground(true);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(8);

        auto* dot = new QLabel();
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(color));
        rowLayout->addWidget(dot);

        auto* name = new QLabel(label);
        name->setStyleSheet("color: #e6edf3; font-size: 12px;");
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        rowLayout->addWidget(name);

        auto* visBtn = new QToolButton();
        visBtn->setCheckable(true);
        visBtn->setChecked(isActive());
        visBtn->setText(isActive() ? "On" : "Off");
        visBtn->setStyleSheet(
            "QToolButton { color: #7d8590; font-size: 11px; padding: 2px 8px; border-radius: 3px; }"
            "QToolButton:checked { color: #3fb950; }");
        connect(visBtn, &QToolButton::clicked, this, [onToggle, visBtn, isActive]() {
            onToggle();
            visBtn->setChecked(isActive());
            visBtn->setText(isActive() ? "On" : "Off");
        });
        rowLayout->addWidget(visBtn);

        m_listLayout->addWidget(row);
    }

    void addMaskLayer(const QString& label, const QString& color, MaskType type) {
        auto* container = new QWidget();
        container->setStyleSheet(
            "QWidget { background: #0d1117; border-bottom: 1px solid #21262d; }");
        auto* containerLayout = new QVBoxLayout(container);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);

        // Toggle row
        auto* row = new QWidget();
        row->setStyleSheet("QWidget:hover { background: #161b22; }");
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(12, 8, 12, 8);
        rowLayout->setSpacing(8);

        auto* dot = new QLabel();
        dot->setFixedSize(8, 8);
        dot->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(color));
        rowLayout->addWidget(dot);

        auto* name = new QLabel(label);
        name->setStyleSheet("color: #e6edf3; font-size: 12px;");
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        rowLayout->addWidget(name);

        auto* visBtn = new QToolButton();
        visBtn->setCheckable(true);
        bool active = isMaskActive(type);
        visBtn->setChecked(active);
        visBtn->setText(active ? "On" : "Off");
        visBtn->setStyleSheet(
            "QToolButton { color: #7d8590; font-size: 11px; padding: 2px 8px; border-radius: 3px; }"
            "QToolButton:checked { color: #3fb950; }");

        // Slider container (shown when mask is active)
        auto* sliderWidget = new QWidget();
        sliderWidget->setStyleSheet("background: #161b22;");
        auto* sliderLayout = new QVBoxLayout(sliderWidget);
        sliderLayout->setContentsMargins(24, 6, 12, 8);
        sliderLayout->setSpacing(4);

        auto* sliderHeader = new QHBoxLayout();
        auto* sliderLabel = new QLabel();
        auto* sliderValue = new QLabel();
        sliderLabel->setStyleSheet("color: #7d8590; font-size: 11px;");
        sliderValue->setStyleSheet("color: #e6edf3; font-size: 11px; font-weight: bold;");
        sliderHeader->addWidget(sliderLabel);
        sliderHeader->addStretch();
        sliderHeader->addWidget(sliderValue);
        sliderLayout->addLayout(sliderHeader);

        auto* slider = new QSlider(Qt::Horizontal);
        slider->setStyleSheet(
            "QSlider::groove:horizontal { background: #21262d; height: 4px; border-radius: 2px; }"
            "QSlider::handle:horizontal { background: #1f6feb; width: 12px; height: 12px;"
            "margin: -4px 0; border-radius: 6px; }"
            "QSlider::sub-page:horizontal { background: #1f6feb; border-radius: 2px; }");
        sliderLayout->addWidget(slider);

        if (type == MaskType::Road) {
            sliderLabel->setText("Road Width");
            slider->setRange(1, 10);
            slider->setValue(m_store->maskSettings().roadLineWidthPx);
            sliderValue->setText(QString("%1px").arg(m_store->maskSettings().roadLineWidthPx));
            connect(slider, &QSlider::valueChanged, this, [this, sliderValue](int val) {
                auto s = m_store->maskSettings();
                s.roadLineWidthPx = val;
                m_store->setMaskSettings(s);
                sliderValue->setText(QString("%1px").arg(val));
            });
        } else if (type == MaskType::Cliff) {
            sliderLabel->setText("Cliff Threshold");
            slider->setRange(0, 90);
            slider->setValue(m_store->maskSettings().cliffThresholdDegrees);
            sliderValue->setText(QString("%1°").arg(m_store->maskSettings().cliffThresholdDegrees));
            connect(slider, &QSlider::valueChanged, this, [this, sliderValue](int val) {
                auto s = m_store->maskSettings();
                s.cliffThresholdDegrees = val;
                m_store->setMaskSettings(s);
                sliderValue->setText(QString("%1°").arg(val));
            });
        } else {
            // No slider for water/vegetation/building
            sliderWidget->setVisible(false);
        }

        sliderWidget->setVisible(active && (type == MaskType::Road || type == MaskType::Cliff));

        connect(visBtn, &QToolButton::clicked, this, [this, type, visBtn, sliderWidget]() {
            toggleMask(type);
            bool a = isMaskActive(type);
            visBtn->setChecked(a);
            visBtn->setText(a ? "On" : "Off");
            sliderWidget->setVisible(a && (type == MaskType::Road || type == MaskType::Cliff));
        });

        rowLayout->addWidget(visBtn);
        containerLayout->addWidget(row);
        containerLayout->addWidget(sliderWidget);

        m_listLayout->addWidget(container);
    }

    bool isMaskActive(MaskType type) const {
        const auto& m = m_store->maskSettings();
        switch (type) {
        case MaskType::Road: return m.generateRoadMask;
        case MaskType::Water: return m.generateWaterMask;
        case MaskType::Vegetation: return m.generateVegetationMask;
        case MaskType::Building: return m.generateBuildingMask;
        case MaskType::Cliff: return m.generateCliffMask;
        }
        return false;
    }

    void toggleMask(MaskType type) {
        auto m = m_store->maskSettings();
        switch (type) {
        case MaskType::Road: m.generateRoadMask = !m.generateRoadMask; break;
        case MaskType::Water: m.generateWaterMask = !m.generateWaterMask; break;
        case MaskType::Vegetation: m.generateVegetationMask = !m.generateVegetationMask; break;
        case MaskType::Building: m.generateBuildingMask = !m.generateBuildingMask; break;
        case MaskType::Cliff: m.generateCliffMask = !m.generateCliffMask; break;
        }
        m_store->setMaskSettings(m);
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

    void onMaskSettingsChanged(const terrain::MaskSettings&) {
        // Rebuild would be heavy; individual widgets update via their lambdas
    }

    void onVisibilityChanged() {
        // Visibility toggles update via their lambdas
    }

private:
    TerrainStore* m_store;
    QVBoxLayout* m_listLayout = nullptr;
    QLabel* m_tileInfoLabel = nullptr;
};
