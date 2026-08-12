#pragma once

// ============================================================
// RoadInspector — Property panel for selected road/control point
// ============================================================
//
// Replaces the RoadInspector panel in the reference app.
// Shows road properties (name, width, lane count, color, profile)
// and control point properties (lat, lon, z, type, handles).
//

#include "RoadStudioStore.hpp"

#include <QWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>

class RoadInspector : public QWidget {
    Q_OBJECT

public:
    explicit RoadInspector(RoadStudioStore* store, QWidget* parent = nullptr);

private slots:
    void onSelectionChanged(const roads::Selection& sel);
    void onRoadsChanged();

    // Road property editors
    void onNameChanged();
    void onWidthChanged(double w);
    void onLaneCountChanged(int n);
    void onColorChanged();
    void onProfileChanged(const QString& profile);

    // Control point editors
    void onLatChanged(double v);
    void onLonChanged(double v);
    void onZChanged(double v);
    void onPointTypeChanged(const QString& type);

private:
    void setupUi();
    void updateFromSelection();

    RoadStudioStore* m_store;

    // Road properties
    QGroupBox* m_roadGroup = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QDoubleSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_laneCountSpin = nullptr;
    QPushButton* m_colorBtn = nullptr;
    QComboBox* m_profileCombo = nullptr;
    QLabel* m_lengthLabel = nullptr;
    QLabel* m_cpCountLabel = nullptr;
    QLabel* m_surfaceLabel = nullptr;
    QCheckBox* m_sidewalkCheck = nullptr;
    QCheckBox* m_curbCheck = nullptr;

    // Control point properties
    QGroupBox* m_pointGroup = nullptr;
    QDoubleSpinBox* m_latSpin = nullptr;
    QDoubleSpinBox* m_lonSpin = nullptr;
    QDoubleSpinBox* m_zSpin = nullptr;
    QComboBox* m_pointTypeCombo = nullptr;

    // Info
    QLabel* m_infoLabel = nullptr;

    QString m_currentRoadId;
    int m_currentPointIdx = -1;
};
