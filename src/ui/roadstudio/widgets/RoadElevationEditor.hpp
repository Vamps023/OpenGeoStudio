#pragma once

// ============================================================
// RoadElevationEditor — SCANeR-style s vs z elevation profile
// ============================================================
//
// Replaces modules/road-studio/client/RoadElevationEditor.tsx.
// Collapsible bottom panel showing elevation profile along road.
// Tools: Flat, Slope Up, Slope Down, Bridge, Roller.
// Click on the canvas to apply the active tool at that s position.
//

#include "RoadStudioStore.hpp"

#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <cmath>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QButtonGroup>

enum class ElevTool {
    None,
    Flat,
    SlopeUp,
    SlopeDown,
    Bridge,
    Roller
};

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

    void setTool(ElevTool tool) { m_tool = tool; update(); }

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
        p.drawLine(40, 0, 40, height());
        p.drawLine(0, height() - 20, width(), height() - 20);

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
        if (zRange < 1e-9) zRange = 1.0;

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

        // Draw cursor line if hovering
        if (m_hoverX >= 0 && m_tool != ElevTool::None) {
            p.setPen(QPen(QColor(0x06, 0xb6, 0xd4), 1, Qt::DashLine));
            p.drawLine(m_hoverX, topMargin, m_hoverX, height() - bottomMargin);

            // Show tool name
            QString toolName;
            switch (m_tool) {
                case ElevTool::Flat: toolName = "Flat"; break;
                case ElevTool::SlopeUp: toolName = "Slope Up"; break;
                case ElevTool::SlopeDown: toolName = "Slope Down"; break;
                case ElevTool::Bridge: toolName = "Bridge"; break;
                case ElevTool::Roller: toolName = "Roller"; break;
                default: break;
            }
            p.setPen(QColor(0x06, 0xb6, 0xd4));
            p.setFont(QFont("Segoe UI", 8));
            p.drawText(m_hoverX + 4, topMargin + 12, toolName);
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        m_hoverX = event->position().x();
        update();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (m_roadId.isEmpty() || m_tool == ElevTool::None) return;

        // Find road
        auto* road = m_store->getRoad(m_roadId);
        if (!road || road->points.size() < 2) return;

        // Compute s values
        const double refLat = m_store->refLat();
        const double refLon = m_store->refLon();
        auto geoToLocal = [](double lat, double lon, double refLat, double refLon,
                             double& lx, double& ly) {
            const double R = 6378137.0;
            lx = (lon - refLon) * M_PI / 180.0 * R * cos(refLat * M_PI / 180.0);
            ly = (lat - refLat) * M_PI / 180.0 * R;
        };

        std::vector<double> svals;
        double totalS = 0;
        double prevLx = 0, prevLy = 0;
        for (int i = 0; i < road->points.size(); ++i) {
            double lx, ly;
            geoToLocal(road->points[i].lat, road->points[i].lon, refLat, refLon, lx, ly);
            if (i > 0) totalS += std::hypot(lx - prevLx, ly - prevLy);
            svals.push_back(totalS);
            prevLx = lx;
            prevLy = ly;
        }
        if (totalS < 1e-6) return;

        // Convert click x to s position
        const int margin = 40;
        const int plotW = width() - margin - 10;
        double clickS = (event->position().x() - margin) / plotW * totalS;
        clickS = std::max(0.0, std::min(totalS, clickS));

        // Find nearest control point index
        int nearestIdx = 0;
        double minDist = std::abs(svals[0] - clickS);
        for (int i = 1; i < road->points.size(); ++i) {
            double d = std::abs(svals[i] - clickS);
            if (d < minDist) { minDist = d; nearestIdx = i; }
        }

        // Apply tool effect
        double currentZ = road->points[nearestIdx].z;
        double newZ = currentZ;

        switch (m_tool) {
            case ElevTool::Flat:
                // Set this point and neighbors to average z
                {
                    double avgZ = 0;
                    for (const auto& cp : road->points) avgZ += cp.z;
                    avgZ /= road->points.size();
                    newZ = avgZ;
                }
                break;
            case ElevTool::SlopeUp:
                newZ = currentZ + 2.0;  // +2m
                break;
            case ElevTool::SlopeDown:
                newZ = currentZ - 2.0;  // -2m
                break;
            case ElevTool::Bridge:
                // Raise point above surrounding terrain
                newZ = currentZ + 5.0;  // +5m for bridge
                break;
            case ElevTool::Roller:
                // Smooth/average with neighbors
                {
                    double neighborSum = currentZ;
                    int count = 1;
                    if (nearestIdx > 0) { neighborSum += road->points[nearestIdx - 1].z; count++; }
                    if (nearestIdx < road->points.size() - 1) { neighborSum += road->points[nearestIdx + 1].z; count++; }
                    newZ = neighborSum / count;
                }
                break;
            default:
                break;
        }

        // Apply the change
        m_store->updateControlPointElevation(m_roadId, nearestIdx, newZ);
        update();
    }

    void leaveEvent(QEvent*) override {
        m_hoverX = -1;
        update();
    }

private:
    RoadStudioStore* m_store;
    QString m_roadId;
    ElevTool m_tool = ElevTool::None;
    double m_hoverX = -1;
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

        // Elevation tools — checkable, exclusive
        auto* toolGroup = new QButtonGroup(this);
        toolGroup->setExclusive(true);

        auto makeToolBtn = [this, toolGroup](const QString& text, ElevTool tool) {
            auto* btn = new QPushButton(text, this);
            btn->setCheckable(true);
            btn->setStyleSheet(
                "QPushButton { padding: 3px 10px; font-size: 11px; "
                "background: #21262d; border: 1px solid #30363d; border-radius: 4px; color: #7d8590; }"
                "QPushButton:hover { background: #30363d; color: #e6edf3; }"
                "QPushButton:checked { background: rgba(6,182,212,0.2); color: #06b6d4; "
                "border: 1px solid rgba(6,182,212,0.4); }");
            toolGroup->addButton(btn);
            connect(btn, &QPushButton::toggled, this, [this, toolGroup, tool](bool checked) {
                if (checked) {
                    m_canvas->setTool(tool);
                } else if (!toolGroup->checkedButton()) {
                    m_canvas->setTool(ElevTool::None);
                }
            });
            return btn;
        };

        headerLayout->addWidget(makeToolBtn("Flat", ElevTool::Flat));
        headerLayout->addWidget(makeToolBtn("Slope Up", ElevTool::SlopeUp));
        headerLayout->addWidget(makeToolBtn("Slope Down", ElevTool::SlopeDown));
        headerLayout->addWidget(makeToolBtn("Bridge", ElevTool::Bridge));
        headerLayout->addWidget(makeToolBtn("Roller", ElevTool::Roller));

        layout->addLayout(headerLayout);

        // Canvas
        m_canvas = new RoadElevationCanvas(store, this);
        layout->addWidget(m_canvas, 1);

        setFixedHeight(160);
        setVisible(false);
    }

    void setActiveRoad(const QString& roadId) {
        m_canvas->setActiveRoad(roadId);
        setVisible(!roadId.isEmpty());
    }

private:
    RoadStudioStore* m_store;
    RoadElevationCanvas* m_canvas;
};
