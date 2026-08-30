#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <map>

// ============================================================
// ElevationProfileEditor — SCANeR-style vertical profile editor
// ============================================================
// Shows the road's z(s) profile as a graph, lets the user add /
// move / remove elevation control points, and emits the edited
// control-point map so the caller can rebuild the road's
// elevation spline and regenerate graphics.
//
// Pure Qt — no LaneMaker dependencies. The caller (MainWidget)
// owns the translation to/from odr::CubicSpline.
// ============================================================

class ElevationProfileEditor : public QWidget
{
    Q_OBJECT

public:
    explicit ElevationProfileEditor(QWidget* parent = nullptr);

    // Load control points (s → z) and the road length. The caller
    // is responsible for ensuring s=0 and s=roadLength anchors exist.
    void setPoints(std::map<double, double> points, double roadLength);

    // Nothing selected — empty out the editor
    void clear();

signals:
    void profileEdited(std::map<double, double> points);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onAdd();
    void onUpdate();
    void onRemove();
    void onRowSelected();

private:
    void rebuildTable(int selectS10 = -1);
    void emitProfile();
    void updateInfoLabel();

    // s scaled by 10 for table row identity (avoids double equality)
    int sKey(double s) const { return static_cast<int>(s * 10.0 + 0.5); }

    std::map<double, double> m_points;
    double m_roadLength = 0;

    QTableWidget* m_table;
    QDoubleSpinBox* m_sSpin;
    QDoubleSpinBox* m_zSpin;
    QPushButton* m_addBtn;
    QPushButton* m_updateBtn;
    QPushButton* m_removeBtn;
    QLabel* m_infoLabel;
};
