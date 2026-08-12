#pragma once

// ============================================================
// RoadElevationEditor — SCANeR-style s vs z elevation profile
// ============================================================
//
// Replaces modules/road-studio/client/RoadElevationEditor.tsx.
// Collapsible bottom panel showing elevation profile along road.
// Tools: Flat, Slope Up, Slope Down, Bridge, Roller.
//

#include "RoadStudioStore.hpp"

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>

class RoadElevationCanvas : public QWidget {
    Q_OBJECT
public:
    explicit RoadElevationCanvas(RoadStudioStore* store, QWidget* parent = nullptr)
        : QWidget(parent), m_store(store)
    {
        setMinimumHeight(120);
        setMouseTracking(true);
    }

    void setActiveRoad(const QString& roadId) {
        m_roadId = roadId;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // Background
        p.fillRect(rect(), QColor(0x0d, 0x11, 0x17));

        // Grid
        p.setPen(QPen(QColor(0x21, 0x26, 0x2d), 1));
        for (int x = 0; x < width(); x += 40) p.drawLine(x, 0, x, height());
        for (int y = 0; y < height(); y += 30) p.drawLine(0, y, width(), y);

        // Axes
        p.setPen(QPen(QColor(0x30, 0x36, 0x3d), 2));
        p.drawLine(40, 0, 40, height());           // Y axis
        p.drawLine(0, height() - 20, width(), height() - 20); // X axis

        // Labels
        p.setPen(QColor(0x7d, 0x85, 0x90));
        p.setFont(QFont("Segoe UI", 8));
        p.drawText(5, 15, "z (m)");
        p.drawText(width() - 30, height() - 5, "s (m)");

        if (m_roadId.isEmpty()) {
            p.setPen(QColor(0x48, 0x4f, 0x58));
            p.drawText(rect(), Qt::AlignCenter, "Select a road to edit elevation");
            return;
        }

        // Find road
        const auto& roads = m_store->roads();
        const roads::Road* road = nullptr;
        for (const auto& r : roads) {
            if (r.id == m_roadId) { road = &r; break; }
        }
        if (!road || road->points.size() < 2) return;

        // Compute s (distance along road) and z (elevation)
        const double refLat = m_store->refLat();
        const double refLon = m_store->refLon();
        auto geoToLocal = [](double lat, double lon, double refLat, double refLon,
                             double& lx, double& ly) {
            const double R = 6378137.0;
            lx = (lon - refLon) * M_PI / 180.0 * R * cos(refLat * M_PI / 180.0);
            ly = (lat - refLat) * M_PI / 180.0 * R;
        };

        std::vector<double> svals, zvals;
        double totalS = 0;
        double prevLx = 0, prevLy = 0;
        for (int i = 0; i < road->points.size(); ++i) {
            double lx, ly;
            geoToLocal(road->points[i].lat, road->points[i].lon, refLat, refLon, lx, ly);
            if (i > 0) {
                totalS += std::hypot(lx - prevLx, ly - prevLy);
            }
            svals.push_back(totalS);
            zvals.push_back(road->points[i].z);
            prevLx = lx;
            prevLy = ly;
        }

        if (totalS < 1e-6) return;

        // Find z range
        double zMin = zvals[0], zMax = zvals[0];
        for (double z : zvals) {
            zMin = std::min(zMin, z);
            zMax = std::max(zMax, z);
        }
        if (zMax - zMin < 1.0) { zMin -= 0.5; zMax += 0.5; }
        double zRange = zMax - zMin;
        if (zRange < 1e-9) zRange = 1.0;  // Prevent division by zero

        // Draw elevation profile
        const int margin = 40;
        const int bottomMargin = 20;
        const int topMargin = 10;
        const int plotW = width() - margin - 10;
        const int plotH = height() - bottomMargin - topMargin;

        auto toScreen = [&](double s, double z) -> QPointF {
            double x = margin + (s / totalS) * plotW;
            double y = topMargin + (1.0 - (z - zMin) / zRange) * plotH;
            return QPointF(x, y);
        };

        // Filled area under curve
        QPainterPath fillPath;
        fillPath.moveTo(toScreen(0, zMin));
        for (size_t i = 0; i < svals.size(); ++i) {
            fillPath.lineTo(toScreen(svals[i], zvals[i]));
        }
        fillPath.lineTo(toScreen(totalS, zMin));
        fillPath.closeSubpath();
        p.fillPath(fillPath, QColor(6, 182, 212, 40));

        // Profile line (cyan)
        p.setPen(QPen(QColor(0x06, 0xb6, 0xd4), 2));
        QPainterPath linePath;
        for (size_t i = 0; i < svals.size(); ++i) {
            QPointF pt = toScreen(svals[i], zvals[i]);
            if (i == 0) linePath.moveTo(pt);
            else linePath.lineTo(pt);
        }
        p.drawPath(linePath);

        // Control points (dots)
        for (size_t i = 0; i < svals.size(); ++i) {
            QPointF pt = toScreen(svals[i], zvals[i]);
            p.setBrush(QColor(0xe6, 0xed, 0xf3));
            p.setPen(QPen(QColor(0x06, 0xb6, 0xd4), 2));
            p.drawEllipse(pt, 4, 4);

            // Z label
            p.setPen(QColor(0x7d, 0x85, 0x90));
            p.setFont(QFont("Segoe UI", 7));
            p.drawText(pt + QPointF(6, -6), QString::number(zvals[i], 'f', 1));
        }

        // S axis labels
        p.setPen(QColor(0x7d, 0x85, 0x90));
        p.setFont(QFont("Segoe UI", 8));
        for (int i = 0; i <= 4; ++i) {
            double s = totalS * i / 4.0;
            int x = margin + (s / totalS) * plotW;
            p.drawLine(x, height() - bottomMargin, x, height() - bottomMargin + 4);
            p.drawText(x - 20, height() - 5, QString::number(s, 'f', 0) + "m");
        }

        // Z axis labels
        for (int i = 0; i <= 3; ++i) {
            double z = zMin + zRange * (3 - i) / 3.0;
            int y = topMargin + (i / 3.0) * plotH;
            p.drawLine(margin - 4, y, margin, y);
            p.drawText(5, y + 4, QString::number(z, 'f', 1));
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (m_roadId.isEmpty()) return;
        // Click to add/edit elevation point — simple implementation
        emit elevationPointClicked(event->position());
    }

signals:
    void elevationPointClicked(QPointF pos);

private:
    RoadStudioStore* m_store;
    QString m_roadId;
};

class RoadElevationEditor : public QWidget {
    Q_OBJECT
public:
    explicit RoadElevationEditor(RoadStudioStore* store, QWidget* parent = nullptr)
        : QWidget(parent), m_store(store)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        // Header bar with tools
        auto* headerLayout = new QHBoxLayout();
        headerLayout->setContentsMargins(8, 4, 8, 4);
        headerLayout->setSpacing(4);

        auto* title = new QLabel("Elevation Profile");
        title->setStyleSheet("font-weight: bold; color: #e6edf3; font-size: 12px; padding: 0 8px;");
        headerLayout->addWidget(title);

        headerLayout->addStretch();

        // Elevation tools
        auto* flatBtn = new QPushButton("Flat");
        flatBtn->setStyleSheet("QPushButton { padding: 3px 10px; font-size: 11px; }");
        headerLayout->addWidget(flatBtn);

        auto* slopeUpBtn = new QPushButton("Slope Up");
        slopeUpBtn->setStyleSheet("QPushButton { padding: 3px 10px; font-size: 11px; }");
        headerLayout->addWidget(slopeUpBtn);

        auto* slopeDownBtn = new QPushButton("Slope Down");
        slopeDownBtn->setStyleSheet("QPushButton { padding: 3px 10px; font-size: 11px; }");
        headerLayout->addWidget(slopeDownBtn);

        auto* bridgeBtn = new QPushButton("Bridge");
        bridgeBtn->setStyleSheet("QPushButton { padding: 3px 10px; font-size: 11px; }");
        headerLayout->addWidget(bridgeBtn);

        auto* rollerBtn = new QPushButton("Roller");
        rollerBtn->setStyleSheet("QPushButton { padding: 3px 10px; font-size: 11px; }");
        headerLayout->addWidget(rollerBtn);

        layout->addLayout(headerLayout);

        // Canvas
        m_canvas = new RoadElevationCanvas(store, this);
        layout->addWidget(m_canvas, 1);

        setFixedHeight(160);
        setVisible(false); // Hidden by default, shown when road selected
    }

    void setActiveRoad(const QString& roadId) {
        m_canvas->setActiveRoad(roadId);
        setVisible(!roadId.isEmpty());
    }

private:
    RoadStudioStore* m_store;
    RoadElevationCanvas* m_canvas;
};
