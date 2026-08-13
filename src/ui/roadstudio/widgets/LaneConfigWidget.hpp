#pragma once

// ============================================================
// LaneConfigWidget — Visual lane cross-section editor
// ============================================================
//
// Replaces the LaneMaker LaneConfigWidget.
// Shows a cross-section of the road with lane configuration.
// Allows adjusting left/right lane counts interactively.
//

#include "RoadStudioStore.hpp"

#include <QWidget>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <cmath>

// Forward declaration
class CrossSectionVisual;

class LaneConfigWidget : public QWidget {
    Q_OBJECT

public:
    explicit LaneConfigWidget(RoadStudioStore* store, QWidget* parent = nullptr);

private:
    RoadStudioStore* m_store;
    QToolButton* m_leftMinus = nullptr;
    QToolButton* m_leftPlus = nullptr;
    QToolButton* m_rightMinus = nullptr;
    QToolButton* m_rightPlus = nullptr;
    CrossSectionVisual* m_visual = nullptr;
};

// Cross-section visual widget
class CrossSectionVisual : public QWidget {
    Q_OBJECT

public:
    CrossSectionVisual(RoadStudioStore* store, QWidget* parent)
        : QWidget(parent), m_store(store) {
        setMinimumHeight(60);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int w = width();
        const int h = height();
        const int cx = w / 2;
        const int cy = h / 2;

        const auto& config = m_store->laneConfig();
        int leftLanes = config.left.laneCount;
        int rightLanes = config.right.laneCount;
        int totalLanes = leftLanes + rightLanes;
        if (totalLanes == 0) totalLanes = 1;

        const double laneWidth = 3.5; // meters
        const double totalWidth = totalLanes * laneWidth;
        const double scale = std::min(w / (totalWidth + 4), h / 6.0);

        // Draw road surface
        double roadLeft = cx - totalWidth * scale / 2.0;
        double roadRight = cx + totalWidth * scale / 2.0;
        p.fillRect(QRectF(roadLeft, cy - 15, roadRight - roadLeft, 30),
                   QColor(60, 60, 65));

        // Draw lane dividers
        p.setPen(QPen(QColor(255, 255, 255, 180), 1, Qt::DashLine));
        for (int i = 1; i < totalLanes; ++i) {
            double x = roadLeft + i * laneWidth * scale;
            p.drawLine(QPointF(x, cy - 15), QPointF(x, cy + 15));
        }

        // Draw center line (yellow)
        p.setPen(QPen(QColor(255, 200, 0), 2));
        p.drawLine(QPointF(cx, cy - 15), QPointF(cx, cy + 15));

        // Draw edge lines (white)
        p.setPen(QPen(QColor(255, 255, 255), 2));
        p.drawLine(QPointF(roadLeft, cy - 15), QPointF(roadLeft, cy + 15));
        p.drawLine(QPointF(roadRight, cy - 15), QPointF(roadRight, cy + 15));

        // Draw direction arrows
        p.setPen(QPen(QColor(100, 200, 255), 1));
        p.setBrush(QColor(100, 200, 255));
        QFont font = p.font();
        font.setPointSize(8);
        p.setFont(font);

        // Left lanes (traffic going left)
        for (int i = 0; i < leftLanes; ++i) {
            double x = cx - (i + 0.5) * laneWidth * scale;
            p.drawText(QRectF(x - 15, cy - 8, 30, 16), Qt::AlignCenter, "<--");
        }

        // Right lanes (traffic going right)
        for (int i = 0; i < rightLanes; ++i) {
            double x = cx + (i + 0.5) * laneWidth * scale;
            p.drawText(QRectF(x - 15, cy - 8, 30, 16), Qt::AlignCenter, "-->");
        }

        // Labels
        p.setPen(QColor(200, 200, 200));
        font.setPointSize(7);
        p.setFont(font);
        p.drawText(QRectF(0, h - 14, w, 14), Qt::AlignCenter,
                   QString("L:%1  R:%2  W:%3m").arg(leftLanes).arg(rightLanes)
                       .arg(totalWidth, 0, 'f', 1));
    }

private:
    RoadStudioStore* m_store;
};

inline LaneConfigWidget::LaneConfigWidget(RoadStudioStore* store, QWidget* parent)
    : QWidget(parent), m_store(store) {
    setMinimumSize(200, 80);
    setMaximumHeight(100);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    m_leftMinus = new QToolButton(this);
    m_leftMinus->setText("-");
    m_leftMinus->setFixedSize(24, 24);
    connect(m_leftMinus, &QToolButton::clicked, this, [this]() {
        m_store->setLeftLaneCount(m_store->laneConfig().left.laneCount - 1);
        update();
    });
    layout->addWidget(m_leftMinus);

    m_visual = new CrossSectionVisual(m_store, this);
    layout->addWidget(m_visual, 1);

    m_rightPlus = new QToolButton(this);
    m_rightPlus->setText("+");
    m_rightPlus->setFixedSize(24, 24);
    connect(m_rightPlus, &QToolButton::clicked, this, [this]() {
        m_store->setRightLaneCount(m_store->laneConfig().right.laneCount + 1);
        update();
    });
    layout->addWidget(m_rightPlus);

    m_rightMinus = new QToolButton(this);
    m_rightMinus->setText("-");
    m_rightMinus->setFixedSize(24, 24);
    connect(m_rightMinus, &QToolButton::clicked, this, [this]() {
        m_store->setRightLaneCount(m_store->laneConfig().right.laneCount - 1);
        update();
    });
    layout->addWidget(m_rightMinus);

    m_leftPlus = new QToolButton(this);
    m_leftPlus->setText("+");
    m_leftPlus->setFixedSize(24, 24);
    connect(m_leftPlus, &QToolButton::clicked, this, [this]() {
        m_store->setLeftLaneCount(m_store->laneConfig().left.laneCount + 1);
        update();
    });
    layout->addWidget(m_leftPlus);

    connect(m_store, &RoadStudioStore::configChanged, this, [this]() { update(); });
}
