#pragma once

// ============================================================
// MapToPixel — Screen ↔ World coordinate conversion
// Inspired by QgsMapToPixel
// ============================================================

#include "MapRectangle.hpp"
#include <QTransform>
#include <QPointF>

namespace map {

class MapToPixel {
public:
    MapToPixel() = default;

    MapToPixel(double mapUnitsPerPixel, double centerX, double centerY,
               int widthPixels, int heightPixels, double rotation = 0.0)
        : m_mupp(mapUnitsPerPixel), m_centerX(centerX), m_centerY(centerY)
        , m_width(widthPixels), m_height(heightPixels), m_rotation(rotation)
    {
        updateMatrix();
    }

    // Set from extent and output size
    static MapToPixel fromExtent(const MapRectangle& extent, int width, int height) {
        double muppX = extent.width() / width;
        double muppY = extent.height() / height;
        double mupp = std::max(muppX, muppY);
        return MapToPixel(mupp, extent.centerX(), extent.centerY(), width, height);
    }

    // World → Screen
    QPointF transform(double x, double y) const {
        return m_matrix.map(QPointF(x, y));
    }

    QPointF transform(const MapPoint& p) const {
        return m_matrix.map(QPointF(p.x, p.y));
    }

    // Screen → World
    MapPoint toMapCoordinates(double screenX, double screenY) const {
        QPointF wp = m_matrix.inverted().map(QPointF(screenX, screenY));
        return MapPoint(wp.x(), wp.y());
    }

    MapPoint toMapCoordinates(const QPointF& screen) const {
        return toMapCoordinates(screen.x(), screen.y());
    }

    // Parameters
    double mapUnitsPerPixel() const { return m_mupp; }
    int width() const { return m_width; }
    int height() const { return m_height; }
    double rotation() const { return m_rotation; }

    void setMapUnitsPerPixel(double mupp) { m_mupp = mupp; updateMatrix(); }
    void setCenter(double cx, double cy) { m_centerX = cx; m_centerY = cy; updateMatrix(); }
    void setOutputSize(int w, int h) { m_width = w; m_height = h; updateMatrix(); }

    // Get the extent covered by this transform
    MapRectangle extent() const {
        double halfW = m_width * m_mupp * 0.5;
        double halfH = m_height * m_mupp * 0.5;
        return MapRectangle(m_centerX - halfW, m_centerY - halfH,
                            m_centerX + halfW, m_centerY + halfH);
    }

private:
    void updateMatrix() {
        // World → Screen: translate center to origin, scale by 1/mupp, flip Y, translate to screen center
        m_matrix = QTransform();
        m_matrix.translate(m_width * 0.5, m_height * 0.5);
        m_matrix.scale(1.0 / m_mupp, -1.0 / m_mupp);  // flip Y
        m_matrix.translate(-m_centerX, -m_centerY);

        if (m_rotation != 0.0) {
            // Apply rotation around center
            QTransform rot;
            rot.rotate(m_rotation);
            m_matrix = rot * m_matrix;
        }
    }

    double m_mupp = 1.0;
    double m_centerX = 0, m_centerY = 0;
    int m_width = 0, m_height = 0;
    double m_rotation = 0.0;
    QTransform m_matrix;
};

} // namespace map
