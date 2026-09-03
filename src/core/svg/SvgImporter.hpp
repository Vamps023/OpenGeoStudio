#pragma once

// ============================================================
// SvgImporter — Import SVG paths as road centerlines
// ============================================================
//
// RoadBuilder-inspired "SVG file import" feature:
//   - Parses SVG <path> elements (M, L, C, Q, A, Z commands)
//   - Converts SVG path data into road geometry segments
//     (LineSegment and ArcSegment)
//   - Supports coordinate transform (SVG user units → meters)
//   - Handles <line>, <polyline>, and <polygon> elements too
//   - Returns a list of polylines, each of which can become a
//     RoadV2 with addSegment<LineSegment>(...)
//
// SVG coordinate system: Y increases downward (screen coords).
// We flip Y by default so that the result matches geographic
// conventions (Y increases upward).
//

#include "../../engine/road/geometry.hpp"
#include "../../engine/road/road_v2.hpp"
#include "../../engine/road/geometry_segment.hpp"

#include <QFile>
#include <QXmlStreamReader>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QChar>
#include <QPointF>

#include "../logger/Logger.hpp"
#include <cmath>
#include <vector>
#include <string>
#include <charconv>
#include <sstream>

namespace svg {

// ─── SVG import result ──────────────────────────────────────
struct SvgPolyline {
    std::vector<geo::Point2D> points;
    QString id;       // SVG element id (if any)
    QString style;    // SVG style attribute (for stroke color, etc.)
};

struct SvgImportResult {
    std::vector<SvgPolyline> polylines;
    double width = 0;     // SVG viewBox width
    double height = 0;    // SVG viewBox height
    bool success = false;
    QString errorMessage;

    // Convert polylines to RoadV2 objects
    std::vector<geo::RoadV2> toRoads(double scale = 1.0,
                                      double laneWidth = 3.5) const
    {
        std::vector<geo::RoadV2> roads;
        for (size_t i = 0; i < polylines.size(); ++i) {
            const auto& pl = polylines[i];
            if (pl.points.size() < 2) continue;
            geo::RoadV2 road;
            road.id = "svg_road_" + std::to_string(i + 1);
            road.name = pl.id.isEmpty() ? road.id : pl.id.toStdString();
            road.laneCount = 1;
            road.width = laneWidth;
            for (size_t j = 1; j < pl.points.size(); ++j) {
                const auto& p0 = pl.points[j - 1];
                const auto& p1 = pl.points[j];
                const double dx = (p1.x - p0.x) * scale;
                const double dy = (p1.y - p0.y) * scale;
                if (std::hypot(dx, dy) < 0.01) continue;
                road.addSegment<geo::LineSegment>(
                    geo::Point2D{p0.x * scale, p0.y * scale},
                    geo::Point2D{p1.x * scale, p1.y * scale});
            }
            if (road.numSegments() == 0) continue;
            // Add a single driving lane
            geo::Lane lane;
            lane.id = 1;
            lane.type = geo::LaneType::Driving;
            lane.width = geo::Polynomial3(laneWidth);
            geo::LaneSection section;
            section.addLane(lane);
            road.addLaneSection(section);
            roads.push_back(std::move(road));
        }
        return roads;
    }
};

// ─── SvgImporter ────────────────────────────────────────────
class SvgImporter {
public:
    // Import an SVG file and extract all polylines/paths
    static SvgImportResult import(const QString& path, bool flipY = true)
    {
        SvgImportResult result;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            result.errorMessage = QString("Cannot open SVG file: %1").arg(path);
            return result;
        }
        QXmlStreamReader xml(&file);
        double flipSign = flipY ? -1.0 : 1.0;

        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            const auto name = xml.name();
            if (name == QLatin1String("svg")) {
                // Parse viewBox or width/height
                const auto vb = xml.attributes().value("viewBox").toString();
                if (!vb.isEmpty()) {
                    const auto parts = vb.split(' ', Qt::SkipEmptyParts);
                    if (parts.size() >= 4) {
                        result.width = parts[2].toDouble();
                        result.height = parts[3].toDouble();
                    }
                }
                if (result.width == 0)
                    result.width = xml.attributes().value("width").toDouble();
                if (result.height == 0)
                    result.height = xml.attributes().value("height").toDouble();
            } else if (name == QLatin1String("path")) {
                const QString d = xml.attributes().value("d").toString();
                const QString id = xml.attributes().value("id").toString();
                const QString style = xml.attributes().value("style").toString();
                auto polylines = parsePathData(d, flipSign);
                for (auto& pl : polylines) {
                    pl.id = id;
                    pl.style = style;
                    result.polylines.push_back(std::move(pl));
                }
            } else if (name == QLatin1String("line")) {
                SvgPolyline pl;
                pl.points.push_back({
                    xml.attributes().value("x1").toDouble(),
                    flipSign * xml.attributes().value("y1").toDouble()});
                pl.points.push_back({
                    xml.attributes().value("x2").toDouble(),
                    flipSign * xml.attributes().value("y2").toDouble()});
                pl.id = xml.attributes().value("id").toString();
                result.polylines.push_back(std::move(pl));
            } else if (name == QLatin1String("polyline") ||
                       name == QLatin1String("polygon")) {
                const QString pointsStr = xml.attributes().value("points").toString();
                SvgPolyline pl;
                pl.points = parsePointsList(pointsStr, flipSign);
                pl.id = xml.attributes().value("id").toString();
                if (name == QLatin1String("polygon") && pl.points.size() >= 3)
                    pl.points.push_back(pl.points.front());
                result.polylines.push_back(std::move(pl));
            }
        }
        if (xml.hasError()) {
            result.errorMessage = xml.errorString();
            return result;
        }
        result.success = true;
        appLog().info("[SvgImporter] Imported", result.polylines.size(),
                      "polylines from", path);
        return result;
    }

private:
    // ─── Parse SVG points attribute ──────────────────────────
    static std::vector<geo::Point2D> parsePointsList(const QString& str, double flipSign)
    {
        std::vector<geo::Point2D> points;
        const auto parts = str.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
        for (int i = 0; i + 1 < parts.size(); i += 2)
            points.push_back({parts[i].toDouble(), flipSign * parts[i + 1].toDouble()});
        return points;
    }

    // ─── Parse SVG path data (d attribute) ───────────────────
    // Supports: M, L, H, V, C, Q, A, Z (absolute and relative)
    static std::vector<SvgPolyline> parsePathData(const QString& d, double flipSign)
    {
        std::vector<SvgPolyline> polylines;
        SvgPolyline current;
        geo::Point2D cursor{0, 0};
        geo::Point2D start{0, 0};

        // Tokenize the path data
        int pos = 0;
        const int len = d.length();
        auto skipSpaces = [&]() {
            while (pos < len && d[pos].isSpace()) pos++;
        };
        auto readNumber = [&]() -> double {
            skipSpaces();
            int start = pos;
            // Optional sign
            if (pos < len && (d[pos] == '+' || d[pos] == '-')) pos++;
            // Digits
            while (pos < len && (d[pos].isDigit() || d[pos] == '.')) pos++;
            // Exponent
            if (pos < len && (d[pos] == 'e' || d[pos] == 'E')) {
                pos++;
                if (pos < len && (d[pos] == '+' || d[pos] == '-')) pos++;
                while (pos < len && d[pos].isDigit()) pos++;
            }
            return d.mid(start, pos - start).toDouble();
        };
        auto readPoint = [&](bool relative) -> geo::Point2D {
            double x = readNumber();
            skipSpaces();
            if (pos < len && d[pos] == ',') pos++;
            double y = readNumber();
            if (relative) { x += cursor.x; y += cursor.y; }
            return {x, flipSign * y};
        };

        char cmd = 0;
        skipSpaces();
        while (pos < len) {
            if (d[pos].isLetter()) {
                cmd = d[pos].toLatin1();
                pos++;
            }
            bool relative = std::islower(cmd);
            char absCmd = char(std::toupper(cmd));

            switch (absCmd) {
            case 'M': {
                // Move to (starts a new subpath)
                if (!current.points.empty()) {
                    polylines.push_back(std::move(current));
                    current = SvgPolyline{};
                }
                cursor = readPoint(relative);
                start = cursor;
                current.points.push_back(cursor);
                // Subsequent pairs are treated as L
                cmd = relative ? 'l' : 'L';
                break;
            }
            case 'L': {
                cursor = readPoint(relative);
                current.points.push_back(cursor);
                break;
            }
            case 'H': {
                double x = readNumber();
                if (relative) x += cursor.x;
                cursor.x = x;
                current.points.push_back(cursor);
                break;
            }
            case 'V': {
                double y = readNumber();
                if (relative) y += cursor.y * flipSign;
                cursor.y = flipSign * y;
                current.points.push_back(cursor);
                break;
            }
            case 'C': {
                // Cubic Bezier: C x1 y1, x2 y2, x y
                // Flatten into line segments
                auto p1 = readPoint(relative);
                auto p2 = readPoint(relative);
                auto p3 = readPoint(relative);
                flattenCubicBezier(cursor, p1, p2, p3, current.points);
                cursor = p3;
                break;
            }
            case 'Q': {
                // Quadratic Bezier: Q x1 y1, x y
                auto p1 = readPoint(relative);
                auto p2 = readPoint(relative);
                flattenQuadraticBezier(cursor, p1, p2, current.points);
                cursor = p2;
                break;
            }
            case 'A': {
                // Arc: A rx ry x-axis-rotation large-arc-flag sweep-flag x y
                double rx = readNumber();
                double ry = readNumber();
                double rot = readNumber();
                double largeArc = readNumber();
                double sweep = readNumber();
                auto endpoint = readPoint(relative);
                flattenArc(cursor, rx, ry, rot, largeArc > 0.5,
                           sweep > 0.5, endpoint, current.points);
                cursor = endpoint;
                break;
            }
            case 'Z': {
                if (!current.points.empty()) {
                    current.points.push_back(start);
                    polylines.push_back(std::move(current));
                    current = SvgPolyline{};
                }
                cursor = start;
                break;
            }
            default:
                // Unknown command — skip
                pos++;
                break;
            }
            skipSpaces();
        }
        if (!current.points.empty())
            polylines.push_back(std::move(current));
        return polylines;
    }

    // ─── Flatten cubic Bezier into line segments ─────────────
    static void flattenCubicBezier(const geo::Point2D& p0,
                                    const geo::Point2D& p1,
                                    const geo::Point2D& p2,
                                    const geo::Point2D& p3,
                                    std::vector<geo::Point2D>& out,
                                    int segments = 16)
    {
        for (int i = 1; i <= segments; ++i) {
            const double t = double(i) / segments;
            const double u = 1.0 - t;
            const double x = u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x;
            const double y = u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y;
            out.push_back({x, y});
        }
    }

    // ─── Flatten quadratic Bezier into line segments ─────────
    static void flattenQuadraticBezier(const geo::Point2D& p0,
                                        const geo::Point2D& p1,
                                        const geo::Point2D& p2,
                                        std::vector<geo::Point2D>& out,
                                        int segments = 12)
    {
        for (int i = 1; i <= segments; ++i) {
            const double t = double(i) / segments;
            const double u = 1.0 - t;
            const double x = u*u*p0.x + 2*u*t*p1.x + t*t*p2.x;
            const double y = u*u*p0.y + 2*u*t*p1.y + t*t*p2.y;
            out.push_back({x, y});
        }
    }

    // ─── Flatten SVG arc into line segments ──────────────────
    static void flattenArc(const geo::Point2D& start,
                           double rx, double ry, double rotation,
                           bool largeArc, bool sweep,
                           const geo::Point2D& end,
                           std::vector<geo::Point2D>& out,
                           int segments = 16)
    {
        if (rx < 0.01 || ry < 0.01) {
            out.push_back(end);
            return;
        }
        // Convert SVG arc to center parameterization
        const double rotRad = rotation * geo::PI / 180.0;
        const double cosR = std::cos(rotRad);
        const double sinR = std::sin(rotRad);

        // Compute center
        const double dx = start.x - end.x;
        const double dy = start.y - end.y;
        const double x1p = (cosR * dx + sinR * dy) / 2.0;
        const double y1p = (-sinR * dx + cosR * dy) / 2.0;

        const double rx2 = rx * rx;
        const double ry2 = ry * ry;
        const double x1p2 = x1p * x1p;
        const double y1p2 = y1p * y1p;

        double lambda = x1p2 / rx2 + y1p2 / ry2;
        if (lambda > 1.0) {
            const double s = std::sqrt(lambda);
            rx *= s; ry *= s;
        }

        const double rx2n = rx * rx;
        const double ry2n = ry * ry;
        const double denom = rx2n * y1p2 + ry2n * x1p2;
        const double num = rx2n * ry2n - denom;
        const double factor = std::sqrt(std::max(0.0, num / denom));
        const double sign = (largeArc != sweep) ? 1.0 : -1.0;
        const double cxp = sign * factor * (rx * y1p) / ry;
        const double cyp = sign * factor * -(ry * x1p) / rx;

        const double cx = cosR * cxp - sinR * cyp + (start.x + end.x) / 2.0;
        const double cy = sinR * cxp + cosR * cyp + (start.y + end.y) / 2.0;

        // Compute start and end angles
        const double startVecX = (x1p - cxp) / rx;
        const double startVecY = (y1p - cyp) / ry;
        const double endVecX = (-x1p - cxp) / rx;
        const double endVecY = (-y1p - cyp) / ry;

        double startAngle = std::atan2(startVecY, startVecX);
        double endAngle = std::atan2(endVecY, endVecX);

        // Adjust angle sweep
        double deltaAngle = endAngle - startAngle;
        if (!sweep && deltaAngle > 0) deltaAngle -= 2 * geo::PI;
        if (sweep && deltaAngle < 0) deltaAngle += 2 * geo::PI;
        if (largeArc && std::abs(deltaAngle) < geo::PI) {
            if (sweep) deltaAngle += 2 * geo::PI;
            else deltaAngle -= 2 * geo::PI;
        }

        for (int i = 1; i <= segments; ++i) {
            const double t = double(i) / segments;
            const double angle = startAngle + deltaAngle * t;
            const double x = cosR * rx * std::cos(angle) - sinR * ry * std::sin(angle) + cx;
            const double y = sinR * rx * std::cos(angle) + cosR * ry * std::sin(angle) + cy;
            out.push_back({x, y});
        }
    }
};

} // namespace svg
