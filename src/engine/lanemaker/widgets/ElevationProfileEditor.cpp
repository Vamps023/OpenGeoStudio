#include "ElevationProfileEditor.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

ElevationProfileEditor::ElevationProfileEditor(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    // Dedicated graph band — the profile curve is painted here. Without
    // this spacer the (opaque) table would cover the painted graph.
    auto* graphHost = new QWidget(this);
    graphHost->setFixedHeight(90);
    layout->addWidget(graphHost);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({tr("s (m)"), tr("z (m)")});
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setMaximumHeight(110);
    layout->addWidget(m_table);

    auto* entryRow = new QHBoxLayout;
    m_sSpin = new QDoubleSpinBox(this);
    m_sSpin->setDecimals(1);
    m_sSpin->setSingleStep(5.0);
    m_sSpin->setRange(0, 100000);
    m_sSpin->setPrefix("s: ");
    m_sSpin->setSuffix(" m");
    m_zSpin = new QDoubleSpinBox(this);
    m_zSpin->setDecimals(2);
    m_zSpin->setSingleStep(0.5);
    m_zSpin->setRange(-500, 500);
    m_zSpin->setPrefix("z: ");
    m_zSpin->setSuffix(" m");
    m_addBtn = new QPushButton(tr("Add"), this);
    m_updateBtn = new QPushButton(tr("Update"), this);
    m_removeBtn = new QPushButton(tr("Remove"), this);
    entryRow->addWidget(m_sSpin, 1);
    entryRow->addWidget(m_zSpin, 1);
    layout->addLayout(entryRow);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_updateBtn);
    btnRow->addWidget(m_removeBtn);
    layout->addLayout(btnRow);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet(QStringLiteral("QLabel { color: gray; font-size: 11px; }"));
    layout->addWidget(m_infoLabel);

    setMinimumHeight(300);

    connect(m_addBtn, &QPushButton::clicked, this, &ElevationProfileEditor::onAdd);
    connect(m_updateBtn, &QPushButton::clicked, this, &ElevationProfileEditor::onUpdate);
    connect(m_removeBtn, &QPushButton::clicked, this, &ElevationProfileEditor::onRemove);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ElevationProfileEditor::onRowSelected);
}

void ElevationProfileEditor::setPoints(std::map<double, double> points, double roadLength)
{
    m_points = std::move(points);
    m_roadLength = roadLength;
    m_sSpin->setRange(0, std::max(1.0, m_roadLength));
    rebuildTable();
}

void ElevationProfileEditor::clear()
{
    m_points.clear();
    m_roadLength = 0;
    m_sSpin->setRange(0, 1);
    rebuildTable();
}

void ElevationProfileEditor::rebuildTable(int selectS10)
{
    QSignalBlocker block(m_table);
    m_table->setRowCount(static_cast<int>(m_points.size()));
    int row = 0;
    for (const auto& [s, z] : m_points)
    {
        auto* sItem = new QTableWidgetItem(QString::number(s, 'f', 1));
        sItem->setData(Qt::UserRole, sKey(s));
        auto* zItem = new QTableWidgetItem(QString::number(z, 'f', 2));
        m_table->setItem(row, 0, sItem);
        m_table->setItem(row, 1, zItem);
        if (sKey(s) == selectS10)
            m_table->selectRow(row);
        ++row;
    }
    updateInfoLabel();
    update();
}
void ElevationProfileEditor::onAdd()
{
    if (m_roadLength <= 0) return;
    const double s = m_sSpin->value();
    const double z = m_zSpin->value();
    m_points[s] = z;
    rebuildTable(sKey(s));
    emitProfile();
}

void ElevationProfileEditor::onUpdate()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* sItem = m_table->item(row, 0);
    if (!sItem) return;
    const double sOld = sItem->data(Qt::UserRole).toDouble() / 10.0;
    auto it = m_points.find(sOld);
    if (it == m_points.end()) return;

    const double sNew = m_sSpin->value();
    const double zNew = m_zSpin->value();
    if (std::abs(sNew - sOld) > 0.05)
    {
        m_points.erase(it);
        m_points[sNew] = zNew;
    }
    else
    {
        it->second = zNew;
    }
    rebuildTable(sKey(sNew));
    emitProfile();
}

void ElevationProfileEditor::onRemove()
{
    // Keep at least the two endpoint anchors — the spline needs them
    if (m_points.size() <= 2) return;
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* sItem = m_table->item(row, 0);
    if (!sItem) return;
    const double s = sItem->data(Qt::UserRole).toDouble() / 10.0;
    if (s <= 0.05 || s >= m_roadLength - 0.05) return;  // endpoints locked
    m_points.erase(s);
    rebuildTable();
    emitProfile();
}

void ElevationProfileEditor::onRowSelected()
{
    int row = m_table->currentRow();
    if (row < 0) return;
    auto* sItem = m_table->item(row, 0);
    auto* zItem = m_table->item(row, 1);
    if (!sItem || !zItem) return;
    QSignalBlocker blockS(m_sSpin);
    QSignalBlocker blockZ(m_zSpin);
    m_sSpin->setValue(sItem->text().toDouble());
    m_zSpin->setValue(zItem->text().toDouble());
    updateInfoLabel();
}

void ElevationProfileEditor::updateInfoLabel()
{
    if (m_points.empty())
    {
        m_infoLabel->setText(tr("No elevation control points"));
        return;
    }

    int row = m_table->currentRow();
    if (row >= 0)
    {
        auto* sItem = m_table->item(row, 0);
        if (sItem)
        {
            const double s = sItem->text().toDouble();
            auto it = m_points.find(s);
            if (it != m_points.end())
            {
                auto next = std::next(it);
                if (next != m_points.end())
                {
                    const double grade = (next->second - it->second) / std::max(0.1, next->first - it->first) * 100.0;
                    m_infoLabel->setText(tr("Grade at s=%1m: %2%").arg(s, 0, 'f', 1).arg(grade, 0, 'f', 2));
                    return;
                }
                if (it != m_points.begin())
                {
                    auto prev = std::prev(it);
                    const double grade = (it->second - prev->second) / std::max(0.1, it->first - prev->first) * 100.0;
                    m_infoLabel->setText(tr("Grade at s=%1m: %2%").arg(s, 0, 'f', 1).arg(grade, 0, 'f', 2));
                    return;
                }
            }
        }
    }
    m_infoLabel->setText(tr("%1 control points — select a row to edit").arg(m_points.size()));
}

void ElevationProfileEditor::emitProfile()
{
    emit profileEdited(m_points);
}

void ElevationProfileEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Graph occupies the top band reserved by the spacer in the ctor
    QRect area(4, 4, rect().width() - 8, 82);
    const auto bg = palette().color(QPalette::Window);
    const auto grid = palette().color(QPalette::Mid);
    const auto curve = palette().color(QPalette::WindowText);
    const auto marker = palette().color(QPalette::Link);

    p.fillRect(area, bg);
    p.setPen(grid);
    p.drawRect(area);

    if (m_points.size() < 2 || m_roadLength <= 0)
    {
        p.drawText(area, Qt::AlignCenter, tr("Elevation profile"));
        return;
    }

    double zMin = m_points.begin()->second;
    double zMax = zMin;
    for (const auto& [s, z] : m_points) { zMin = std::min(zMin, z); zMax = std::max(zMax, z); }
    if (zMax - zMin < 1.0) { zMin -= 0.5; zMax += 0.5; }
    const double zPad = (zMax - zMin) * 0.1;

    auto toScreen = [&](double s, double z) -> QPointF {
        const double x = area.left() + (s / m_roadLength) * area.width();
        const double zLo = zMin - zPad, zHi = zMax + zPad;
        const double y = area.bottom() - ((z - zLo) / (zHi - zLo)) * area.height();
        return QPointF(x, y);
    };

    // Zero-elevation reference line when in view
    if (zMin <= 0 && zMax >= 0)
    {
        QPen dash(grid);
        dash.setStyle(Qt::DashLine);
        p.setPen(dash);
        const double y0 = toScreen(0, 0).y();
        p.drawLine(QPointF(area.left(), y0), QPointF(area.right(), y0));
    }

    // Piecewise-linear guide through control points (the true spline
    // renders in the 3D view — this is the editing guide)
    QPainterPath path;
    bool first = true;
    for (const auto& [s, z] : m_points)
    {
        const QPointF pt = toScreen(s, z);
        if (first) { path.moveTo(pt); first = false; }
        else path.lineTo(pt);
    }
    p.setPen(QPen(curve, 1.6));
    p.drawPath(path);

    // Control point markers
    p.setPen(QPen(marker, 1.2));
    p.setBrush(marker);
    for (const auto& [s, z] : m_points)
    {
        p.drawEllipse(toScreen(s, z), 3.0, 3.0);
    }
}

