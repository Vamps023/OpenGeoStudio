#pragma once

// ============================================================
// MapRectangle — Geographic extent/bounds (inspired by QgsRectangle)
// ============================================================

#include <cmath>
#include <string>
#include <algorithm>

namespace map {

class MapRectangle {
public:
    double xMin = 0, yMin = 0, xMax = 0, yMax = 0;

    MapRectangle() = default;
    MapRectangle(double xMin_, double yMin_, double xMax_, double yMax_)
        : xMin(xMin_), yMin(yMin_), xMax(xMax_), yMax(yMax_) {}

    // Normalize so min < max
    void normalize() {
        if (xMin > xMax) std::swap(xMin, xMax);
        if (yMin > yMax) std::swap(yMin, yMax);
    }

    double width()  const { return xMax - xMin; }
    double height() const { return yMax - yMin; }
    double centerX() const { return (xMin + xMax) * 0.5; }
    double centerY() const { return (yMin + yMax) * 0.5; }

    bool isNull() const { return xMin == 0 && yMin == 0 && xMax == 0 && yMax == 0; }
    bool isEmpty() const { return width() == 0 || height() == 0; }
    bool isValid() const { return xMax >= xMin && yMax >= yMin && !isNull(); }

    bool contains(double x, double y) const {
        return x >= xMin && x <= xMax && y >= yMin && y <= yMax;
    }

    bool intersects(const MapRectangle& other) const {
        return !(xMax < other.xMin || xMin > other.xMax ||
                 yMax < other.yMin || yMin > other.yMax);
    }

    // Scale around center (or given point), factor > 1 = zoom out
    void scale(double factor, double cx = 0, double cy = 0, bool useCenter = true) {
        if (useCenter) { cx = centerX(); cy = centerY(); }
        double w = width() * factor * 0.5;
        double h = height() * factor * 0.5;
        xMin = cx - w; xMax = cx + w;
        yMin = cy - h; yMax = cy + h;
    }

    void grow(double delta) {
        xMin -= delta; xMax += delta;
        yMin -= delta; yMax += delta;
    }

    void combineWith(const MapRectangle& other) {
        if (isNull()) { *this = other; return; }
        xMin = std::min(xMin, other.xMin);
        yMin = std::min(yMin, other.yMin);
        xMax = std::max(xMax, other.xMax);
        yMax = std::max(yMax, other.yMax);
    }

    bool operator==(const MapRectangle& other) const {
        return xMin == other.xMin && yMin == other.yMin &&
               xMax == other.xMax && yMax == other.yMax;
    }
};

} // namespace map
