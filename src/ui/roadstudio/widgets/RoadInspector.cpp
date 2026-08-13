// RoadInspector — Property panel implementation

#include "RoadInspector.hpp"
#include "GeoConvert.hpp"
#include "editor/RoadCommands.hpp"

#include <QColorDialog>
#include <QPushButton>
#include <QHBoxLayout>
#include <cmath>

RoadInspector::RoadInspector(RoadStudioStore* store, QWidget* parent)
    : QWidget(parent), m_store(store) {
    setupUi();

    connect(m_store, &RoadStudioStore::selectionChanged,
            this, &RoadInspector::onSelectionChanged);
    connect(m_store, &RoadStudioStore::roadsChanged,
            this, &RoadInspector::onRoadsChanged);

    updateFromSelection();
}

void RoadInspector::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Info label
    m_infoLabel = new QLabel("No selection");
    m_infoLabel->setStyleSheet("color: #7d8590; font-style: italic;");
    mainLayout->addWidget(m_infoLabel);

    // --- Road properties group ---
    m_roadGroup = new QGroupBox("Road Properties");
    auto* roadLayout = new QFormLayout(m_roadGroup);

    m_nameEdit = new QLineEdit();
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &RoadInspector::onNameChanged);
    roadLayout->addRow("Name:", m_nameEdit);

    m_widthSpin = new QDoubleSpinBox();
    m_widthSpin->setRange(2.0, 30.0);
    m_widthSpin->setSuffix(" m");
    m_widthSpin->setSingleStep(0.5);
    connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &RoadInspector::onWidthChanged);
    roadLayout->addRow("Width:", m_widthSpin);

    m_laneCountSpin = new QSpinBox();
    m_laneCountSpin->setRange(1, 8);
    connect(m_laneCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &RoadInspector::onLaneCountChanged);
    roadLayout->addRow("Lanes:", m_laneCountSpin);

    m_colorBtn = new QPushButton();
    m_colorBtn->setFixedSize(60, 24);
    connect(m_colorBtn, &QPushButton::clicked, this, &RoadInspector::onColorChanged);
    roadLayout->addRow("Color:", m_colorBtn);

    m_profileCombo = new QComboBox();
    m_profileCombo->addItems({"city_2x1", "city_2x2", "country_2x1", "highway_2x3", "custom"});
    connect(m_profileCombo, &QComboBox::currentTextChanged,
            this, &RoadInspector::onProfileChanged);
    roadLayout->addRow("Profile:", m_profileCombo);

    // Road length (read-only, computed)
    m_lengthLabel = new QLabel("—");
    m_lengthLabel->setStyleSheet("color: #7d8590;");
    roadLayout->addRow("Length:", m_lengthLabel);

    // Control point count (read-only)
    m_cpCountLabel = new QLabel("—");
    m_cpCountLabel->setStyleSheet("color: #7d8590;");
    roadLayout->addRow("Control Points:", m_cpCountLabel);

    // Surface texture (read-only display)
    m_surfaceLabel = new QLabel("—");
    m_surfaceLabel->setStyleSheet("color: #7d8590;");
    roadLayout->addRow("Surface:", m_surfaceLabel);

    // Sidewalk/Curb toggles
    m_sidewalkCheck = new QCheckBox("Sidewalk");
    m_sidewalkCheck->setStyleSheet("QCheckBox { color: #e6edf3; }");
    roadLayout->addRow("", m_sidewalkCheck);

    m_curbCheck = new QCheckBox("Curb");
    m_curbCheck->setStyleSheet("QCheckBox { color: #e6edf3; }");
    roadLayout->addRow("", m_curbCheck);

    m_roadGroup->setVisible(false);
    mainLayout->addWidget(m_roadGroup);

    // --- Control point properties group ---
    m_pointGroup = new QGroupBox("Control Point");
    auto* pointLayout = new QFormLayout(m_pointGroup);

    m_latSpin = new QDoubleSpinBox();
    m_latSpin->setRange(-90.0, 90.0);
    m_latSpin->setDecimals(8);
    m_latSpin->setSingleStep(0.00001);
    connect(m_latSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &RoadInspector::onLatChanged);
    pointLayout->addRow("Latitude:", m_latSpin);

    m_lonSpin = new QDoubleSpinBox();
    m_lonSpin->setRange(-180.0, 180.0);
    m_lonSpin->setDecimals(8);
    m_lonSpin->setSingleStep(0.00001);
    connect(m_lonSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &RoadInspector::onLonChanged);
    pointLayout->addRow("Longitude:", m_lonSpin);

    m_zSpin = new QDoubleSpinBox();
    m_zSpin->setRange(-1000.0, 10000.0);
    m_zSpin->setDecimals(3);
    m_zSpin->setSuffix(" m");
    connect(m_zSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &RoadInspector::onZChanged);
    pointLayout->addRow("Elevation:", m_zSpin);

    m_pointTypeCombo = new QComboBox();
    m_pointTypeCombo->addItems({"smooth", "corner"});
    connect(m_pointTypeCombo, &QComboBox::currentTextChanged,
            this, &RoadInspector::onPointTypeChanged);
    pointLayout->addRow("Type:", m_pointTypeCombo);

    m_pointGroup->setVisible(false);
    mainLayout->addWidget(m_pointGroup);

    mainLayout->addStretch();
}

void RoadInspector::updateFromSelection() {
    const auto& sel = m_store->selection();

    if (sel.isEmpty()) {
        m_infoLabel->setText("No selection");
        m_roadGroup->setVisible(false);
        m_pointGroup->setVisible(false);
        return;
    }

    auto* road = m_store->getRoad(sel.roadId);
    if (!road) {
        m_infoLabel->setText("Road not found");
        m_roadGroup->setVisible(false);
        m_pointGroup->setVisible(false);
        return;
    }

    m_currentRoadId = sel.roadId;

    // Show road properties
    m_infoLabel->setText(QString("Road: %1 (%2 points)")
        .arg(road->name).arg(road->points.size()));
    m_roadGroup->setVisible(true);

    // Block signals while updating
    m_nameEdit->blockSignals(true);
    m_widthSpin->blockSignals(true);
    m_laneCountSpin->blockSignals(true);
    m_profileCombo->blockSignals(true);

    m_nameEdit->setText(road->name);
    m_widthSpin->setValue(road->width);
    m_laneCountSpin->setValue(road->laneCount);
    m_profileCombo->setCurrentText(road->profile.type);
    m_colorBtn->setStyleSheet(
        QString("background-color: %1; border: 1px solid #30363d;").arg(road->color));

    // Compute road length
    double totalLen = 0;
    const double refLat = m_store->refLat();
    const double refLon = m_store->refLon();
    for (int i = 1; i < road->points.size(); ++i) {
        double dlat = (road->points[i].lat - road->points[i-1].lat) * 111320.0;
        double dlon = (road->points[i].lon - road->points[i-1].lon) * 111320.0 * cos(refLat * M_PI / 180.0);
        totalLen += std::hypot(dlat, dlon);
    }
    m_lengthLabel->setText(QString::number(totalLen, 'f', 1) + " m");
    m_cpCountLabel->setText(QString::number(road->points.size()));

    // Surface texture from profile
    QString surface = "asphalt";
    if (road->profile.type.contains("country")) surface = "gravel";
    else if (road->profile.type.contains("highway")) surface = "asphalt";
    m_surfaceLabel->setText(surface);

    // Sidewalk/curb from profile type
    m_sidewalkCheck->blockSignals(true);
    m_curbCheck->blockSignals(true);
    m_sidewalkCheck->setChecked(road->profile.type.contains("city"));
    m_curbCheck->setChecked(road->profile.type.contains("city_2x2"));
    m_sidewalkCheck->blockSignals(false);
    m_curbCheck->blockSignals(false);

    m_nameEdit->blockSignals(false);
    m_widthSpin->blockSignals(false);
    m_laneCountSpin->blockSignals(false);
    m_profileCombo->blockSignals(false);

    // Show control point properties if a point is selected
    if (!sel.pointIndices.isEmpty()) {
        int idx = sel.pointIndices.first();
        if (idx >= 0 && idx < road->points.size()) {
            m_currentPointIdx = idx;
            const auto& cp = road->points[idx];

            m_pointGroup->setVisible(true);
            m_pointGroup->setTitle(QString("Control Point %1").arg(idx));

            m_latSpin->blockSignals(true);
            m_lonSpin->blockSignals(true);
            m_zSpin->blockSignals(true);
            m_pointTypeCombo->blockSignals(true);

            m_latSpin->setValue(cp.lat);
            m_lonSpin->setValue(cp.lon);
            m_zSpin->setValue(cp.z);
            m_pointTypeCombo->setCurrentText(cp.typeStr());

            m_latSpin->blockSignals(false);
            m_lonSpin->blockSignals(false);
            m_zSpin->blockSignals(false);
            m_pointTypeCombo->blockSignals(false);
        } else {
            m_pointGroup->setVisible(false);
            m_currentPointIdx = -1;
        }
    } else {
        m_pointGroup->setVisible(false);
        m_currentPointIdx = -1;
    }
}

void RoadInspector::onSelectionChanged(const roads::Selection&) {
    updateFromSelection();
}

void RoadInspector::onRoadsChanged() {
    updateFromSelection();
}

void RoadInspector::onNameChanged() {
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road) return;
    QString newName = m_nameEdit->text();
    if (road->name == newName) return;
    m_store->undoStack().push(new SetRoadPropertyCommand(
        m_store, m_currentRoadId, SetRoadPropertyCommand::Property::Name,
        road->name, newName, "Rename road"));
}

void RoadInspector::onWidthChanged(double w) {
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road) return;
    if (std::abs(road->width - w) < 1e-6) return;
    m_store->undoStack().push(new SetRoadPropertyCommand(
        m_store, m_currentRoadId, SetRoadPropertyCommand::Property::Width,
        road->width, w, "Change road width"));
}

void RoadInspector::onLaneCountChanged(int n) {
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road) return;
    if (road->laneCount == n) return;
    m_store->undoStack().push(new SetRoadPropertyCommand(
        m_store, m_currentRoadId, SetRoadPropertyCommand::Property::LaneCount,
        road->laneCount, n, "Change lane count"));
}

void RoadInspector::onColorChanged() {
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road) return;
    QColor color = QColorDialog::getColor(QColor(road->color), this, "Road Color");
    if (!color.isValid()) return;
    QString newColor = color.name();
    if (road->color == newColor) return;
    m_store->undoStack().push(new SetRoadPropertyCommand(
        m_store, m_currentRoadId, SetRoadPropertyCommand::Property::Color,
        road->color, newColor, "Change road color"));
}

void RoadInspector::onProfileChanged(const QString& profile) {
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road) return;
    if (road->profile.type == profile) return;
    m_store->undoStack().push(new SetRoadPropertyCommand(
        m_store, m_currentRoadId, SetRoadPropertyCommand::Property::ProfileType,
        road->profile.type, profile, "Change road profile"));
}

void RoadInspector::onLatChanged(double v) {
    if (m_currentPointIdx < 0) return;
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road || m_currentPointIdx >= road->points.size()) return;
    if (std::abs(road->points[m_currentPointIdx].lat - v) < 1e-9) return;
    m_store->undoStack().push(new SetControlPointPropertyCommand(
        m_store, m_currentRoadId, m_currentPointIdx,
        SetControlPointPropertyCommand::Property::Latitude,
        road->points[m_currentPointIdx].lat, v, "Change latitude"));
}

void RoadInspector::onLonChanged(double v) {
    if (m_currentPointIdx < 0) return;
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road || m_currentPointIdx >= road->points.size()) return;
    if (std::abs(road->points[m_currentPointIdx].lon - v) < 1e-9) return;
    m_store->undoStack().push(new SetControlPointPropertyCommand(
        m_store, m_currentRoadId, m_currentPointIdx,
        SetControlPointPropertyCommand::Property::Longitude,
        road->points[m_currentPointIdx].lon, v, "Change longitude"));
}

void RoadInspector::onZChanged(double v) {
    if (m_currentPointIdx < 0) return;
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road || m_currentPointIdx >= road->points.size()) return;
    if (std::abs(road->points[m_currentPointIdx].z - v) < 1e-6) return;
    m_store->undoStack().push(new SetControlPointPropertyCommand(
        m_store, m_currentRoadId, m_currentPointIdx,
        SetControlPointPropertyCommand::Property::Elevation,
        road->points[m_currentPointIdx].z, v, "Change elevation"));
}

void RoadInspector::onPointTypeChanged(const QString& type) {
    if (m_currentPointIdx < 0) return;
    auto* road = m_store->getRoad(m_currentRoadId);
    if (!road || m_currentPointIdx >= road->points.size()) return;
    if (road->points[m_currentPointIdx].typeStr() == type) return;
    m_store->undoStack().push(new SetControlPointPropertyCommand(
        m_store, m_currentRoadId, m_currentPointIdx,
        SetControlPointPropertyCommand::Property::Type,
        road->points[m_currentPointIdx].typeStr(), type, "Change point type"));
}
