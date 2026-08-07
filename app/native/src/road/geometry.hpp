#pragma once

// ═══════════════════════════════════════════════════════════
// Geometry Kernel — Core math types and operations
// ═══════════════════════════════════════════════════════════
//
// This is the foundation of the entire C++ road geometry engine.
// Every other module (road, intersection, arc, mesh, etc.) depends
// on these types.
//
// Design principles:
// - Header-only (no .cpp files needed for core types)
// - constexpr where possible
// - No heap allocations for small types
// - No external dependencies (no Boost, no CGAL)
// - Modern C++20

#include <cmath>
#include <vector>
#include <array>
#include <limits>
#include <algorithm>
#include <string>

namespace geo {

// ─── Constants ─────────────────────────────────────────────
constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;
constexpr double HALF_PI = PI / 2.0;
constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;
constexpr double EPSILON = 1e-10;
constexpr double EARTH_RADIUS_M = 6378137.0; // WGS84 equatorial radius

// ─── Point2D ───────────────────────────────────────────────
struct Point2D {
    double x = 0.0;
    double y = 0.0;

    constexpr Point2D() = default;
    constexpr Point2D(double x_, double y_) : x(x_), y(y_) {}

    // Arithmetic operators
    constexpr Point2D operator+(const Point2D& o) const { return {x + o.x, y + o.y}; }
    constexpr Point2D operator-(const Point2D& o) const { return {x - o.x, y - o.y}; }
    constexpr Point2D operator*(double s) const { return {x * s, y * s}; }
    constexpr Point2D operator/(double s) const { return {x / s, y / s}; }
    constexpr Point2D operator-() const { return {-x, -y}; }

    Point2D& operator+=(const Point2D& o) { x += o.x; y += o.y; return *this; }
    Point2D& operator-=(const Point2D& o) { x -= o.x; y -= o.y; return *this; }
    Point2D& operator*=(double s) { x *= s; y *= s; return *this; }
    Point2D& operator/=(double s) { x /= s; y /= s; return *this; }

    bool operator==(const Point2D& o) const {
        return std::abs(x - o.x) < EPSILON && std::abs(y - o.y) < EPSILON;
    }
    bool operator!=(const Point2D& o) const { return !(*this == o); }

    // Distance to another point
    double distanceTo(const Point2D& o) const {
        return std::hypot(x - o.x, y - o.y);
    }

    // Squared distance (faster, no sqrt)
    double distanceSquaredTo(const Point2D& o) const {
        double dx = x - o.x, dy = y - o.y;
        return dx * dx + dy * dy;
    }

    // Norm (distance from origin)
    double norm() const { return std::hypot(x, y); }
    double normSquared() const { return x * x + y * y; }

    // Normalize to unit vector
    Point2D normalized() const {
        double n = norm();
        if (n < EPSILON) return {0, 0};
        return {x / n, y / n};
    }

    // Dot product
    double dot(const Point2D& o) const { return x * o.x + y * o.y; }

    // Cross product (z-component of 3D cross)
    double cross(const Point2D& o) const { return x * o.y - y * o.x; }

    // Angle from origin to this point
    double angle() const { return std::atan2(y, x); }

    // Perpendicular vector (rotated 90° CCW)
    Point2D perp() const { return {-y, x}; }

    // Rotate by angle (radians)
    Point2D rotated(double angle) const {
        double c = std::cos(angle), s = std::sin(angle);
        return {x * c - y * s, x * s + y * c};
    }

    // Linear interpolation
    static Point2D lerp(const Point2D& a, const Point2D& b, double t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
    }
};

// ─── Point3D ───────────────────────────────────────────────
struct Point3D {
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr Point3D() = default;
    constexpr Point3D(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    Point3D operator+(const Point3D& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Point3D operator-(const Point3D& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Point3D operator*(double s) const { return {x * s, y * s, z * s}; }

    double distanceTo(const Point3D& o) const {
        return std::sqrt((x-o.x)*(x-o.x) + (y-o.y)*(y-o.y) + (z-o.z)*(z-o.z));
    }

    Point2D xy() const { return {x, y}; }
};

// ─── Vector2 (alias for Point2D when used as direction) ───
using Vec2 = Point2D;
using Vec3 = Point3D;

// ─── BoundingBox2D ─────────────────────────────────────────
struct BoundingBox2D {
    Point2D min{std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Point2D max{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};

    void expand(const Point2D& p) {
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
    }

    bool contains(const Point2D& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
    }

    bool intersects(const BoundingBox2D& o) const {
        return !(max.x < o.min.x || min.x > o.max.x || max.y < o.min.y || min.y > o.max.y);
    }

    Point2D center() const { return {(min.x + max.x) / 2, (min.y + max.y) / 2}; }
    double width() const { return max.x - min.x; }
    double height() const { return max.y - min.y; }
};

// ─── Line (infinite line: point + direction) ───────────────
struct Line {
    Point2D point;
    Vec2 direction;  // normalized

    Line() = default;
    Line(const Point2D& p, const Vec2& dir) : point(p), direction(dir.normalized()) {}

    // Point on line at parameter t: point + direction * t
    Point2D at(double t) const { return point + direction * t; }
};

// ─── Segment (finite line segment) ─────────────────────────
struct Segment {
    Point2D p1, p2;

    Segment() = default;
    Segment(const Point2D& a, const Point2D& b) : p1(a), p2(b) {}

    double length() const { return p1.distanceTo(p2); }
    Vec2 direction() const { return (p2 - p1).normalized(); }
    Point2D midpoint() const { return Point2D::lerp(p1, p2, 0.5); }

    // Point on segment at parameter t [0,1]
    Point2D at(double t) const { return Point2D::lerp(p1, p2, t); }
};

// ─── Math utility functions ────────────────────────────────

// Clamp value to range
template<typename T>
constexpr T clamp(T val, T lo, T hi) {
    return val < lo ? lo : (val > hi ? hi : val);
}

// Normalize angle to [0, 2π)
inline double normalizeAngle(double a) {
    while (a < 0) a += TWO_PI;
    while (a >= TWO_PI) a -= TWO_PI;
    return a;
}

// Normalize angle to [-π, π)
inline double normalizeAnglePi(double a) {
    while (a > PI) a -= TWO_PI;
    while (a < -PI) a += TWO_PI;
    return a;
}

// Angle between two vectors (0 to π)
inline double angleBetween(const Vec2& a, const Vec2& b) {
    double d = a.dot(b);
    d = clamp(d, -1.0, 1.0);
    return std::acos(d);
}

// ─── Intersection functions ────────────────────────────────

// Intersection of two infinite lines (point + direction)
// Returns intersection point or invalid point if parallel
inline Point2D lineIntersection(const Point2D& p1, const Vec2& dir1,
                                 const Point2D& p2, const Vec2& dir2) {
    double denom = dir1.x * dir2.y - dir1.y * dir2.x;
    if (std::abs(denom) < EPSILON) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }
    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double t = (dx * dir2.y - dy * dir2.x) / denom;
    return {p1.x + dir1.x * t, p1.y + dir1.y * t};
}

// Intersection of two line segments
// Returns intersection point or NaN point if no intersection
inline Point2D segmentIntersection(const Point2D& a1, const Point2D& a2,
                                    const Point2D& b1, const Point2D& b2) {
    double x1 = a1.x, y1 = a1.y, x2 = a2.x, y2 = a2.y;
    double x3 = b1.x, y3 = b1.y, x4 = b2.x, y4 = b2.y;

    double denom = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4);
    if (std::abs(denom) < EPSILON) {
        return {std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::quiet_NaN()};
    }

    double t = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / denom;
    double u = -((x1 - x2) * (y1 - y3) - (y1 - y2) * (x1 - x3)) / denom;

    if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
        return {x1 + t * (x2 - x1), y1 + t * (y2 - y1)};
    }
    return {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
}

// Check if a point is valid (not NaN)
inline bool isValid(const Point2D& p) {
    return !std::isnan(p.x) && !std::isnan(p.y);
}

// ─── Geo ↔ Local coordinate conversion ─────────────────────

// Convert lat/lon to local meters (relative to reference point)
inline Point2D geoToLocal(double lat, double lon, double refLat, double refLon) {
    double latRad = lat * DEG_TO_RAD;
    double refLatRad = refLat * DEG_TO_RAD;
    double dLat = (lat - refLat) * DEG_TO_RAD;
    double dLon = (lon - refLon) * DEG_TO_RAD;
    double x = EARTH_RADIUS_M * dLon * std::cos(refLatRad);
    double y = EARTH_RADIUS_M * dLat;
    return {x, y};
}

// Convert local meters to lat/lon
inline Point2D localToGeo(double x, double y, double refLat, double refLon) {
    double refLatRad = refLat * DEG_TO_RAD;
    double dLat = y / EARTH_RADIUS_M;
    double dLon = x / (EARTH_RADIUS_M * std::cos(refLatRad));
    double lat = refLat + dLat * RAD_TO_DEG;
    double lon = refLon + dLon * RAD_TO_DEG;
    return {lat, lon};
}

// ─── Polyline utilities ────────────────────────────────────

// Compute tangent at a point on a polyline (central difference)
inline Vec2 tangentAt(const std::vector<Point2D>& points, size_t idx) {
    if (points.size() < 2) return {1, 0};
    if (idx == 0) return (points[1] - points[0]).normalized();
    if (idx == points.size() - 1) return (points[idx] - points[idx - 1]).normalized();
    return (points[idx + 1] - points[idx - 1]).normalized();
}

// Compute total length of a polyline
inline double polylineLength(const std::vector<Point2D>& points) {
    double len = 0;
    for (size_t i = 1; i < points.size(); i++) {
        len += points[i].distanceTo(points[i - 1]);
    }
    return len;
}

// Offset a polyline by a given distance (positive = left, negative = right)
// Uses miter joints at interior vertices to maintain consistent offset width
// on curves. This is the correct algorithm for road edge generation.
inline std::vector<Point2D> offsetPolyline(const std::vector<Point2D>& points, double offset) {
    if (points.size() < 2) return points;
    std::vector<Point2D> result;
    result.reserve(points.size());

    for (size_t i = 0; i < points.size(); i++) {
        // Compute incoming and outgoing tangents
        Vec2 tanIn, tanOut;
        if (i == 0) {
            tanOut = (points[1] - points[0]).normalized();
            tanIn = tanOut;
        } else if (i == points.size() - 1) {
            tanIn = (points[i] - points[i - 1]).normalized();
            tanOut = tanIn;
        } else {
            tanIn = (points[i] - points[i - 1]).normalized();
            tanOut = (points[i + 1] - points[i]).normalized();
        }

        // Miter joint: bisect the angle between incoming and outgoing tangents
        // The miter direction is the normalized sum of the left normals
        Vec2 normIn = tanIn.perp();   // left normal of incoming
        Vec2 normOut = tanOut.perp(); // left normal of outgoing

        // Miter vector = (normIn + normOut) normalized
        Vec2 miter = normIn + normOut;
        double miterLen = miter.norm();

        if (miterLen < EPSILON) {
            // Tangents are opposite (180° turn) — use perpendicular
            result.push_back(points[i] + normIn * offset);
        } else {
            // Scale by 1/sin(halfAngle) = |normIn + normOut| / (2 * cos(halfAngle))
            // Simplification: miter length = 1 / dot(normIn, miter_normalized)
            miter = miter / miterLen;
            double dot = normIn.dot(miter);
            if (std::abs(dot) < EPSILON) {
                result.push_back(points[i] + normIn * offset);
            } else {
                // Clamp miter to avoid extreme spikes on sharp angles
                double miterScale = std::min(1.0 / dot, 4.0);
                result.push_back(points[i] + miter * offset * miterScale);
            }
        }
    }
    return result;
}

// ─── Bezier curves ─────────────────────────────────────────

// Quadratic Bezier: B(t) = (1-t)²·P0 + 2(1-t)t·P1 + t²·P2
inline Point2D bezierQuad(const Point2D& p0, const Point2D& p1, const Point2D& p2, double t) {
    double mt = 1 - t;
    return p0 * (mt * mt) + p1 * (2 * mt * t) + p2 * (t * t);
}

// Cubic Bezier: B(t) = (1-t)³·P0 + 3(1-t)²t·P1 + 3(1-t)t²·P2 + t³·P3
inline Point2D bezierCubic(const Point2D& p0, const Point2D& p1, const Point2D& p2, const Point2D& p3, double t) {
    double mt = 1 - t;
    double mt2 = mt * mt, mt3 = mt2 * mt;
    double t2 = t * t, t3 = t2 * t;
    return p0 * mt3 + p1 * (3 * mt2 * t) + p2 * (3 * mt * t2) + p3 * t3;
}

// Sample a cubic Bezier curve
inline std::vector<Point2D> sampleBezierCubic(const Point2D& p0, const Point2D& p1,
                                               const Point2D& p2, const Point2D& p3,
                                               int segments = 24) {
    std::vector<Point2D> points;
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; i++) {
        double t = static_cast<double>(i) / segments;
        points.push_back(bezierCubic(p0, p1, p2, p3, t));
    }
    return points;
}

} // namespace geo
