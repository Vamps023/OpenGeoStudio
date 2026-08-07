#pragma once

// ═══════════════════════════════════════════════════════════
// Intersection Engine — Edge-based junction generation
// ═══════════════════════════════════════════════════════════
//
// Algorithm:
// 1. Find centerline intersection point
// 2. Compute road edge lines (left + right offsets)
// 3. Compute angle between roads → trim distance = R × tan(θ/2)
// 4. Split and trim roads at tangent points
// 5. Find edge-edge intersections → true corner points
// 6. Generate circular fillet arcs at each corner
// 7. Build polygon from edge segments + arcs
// 8. Generate lane connections, stop lines, crosswalks

#include "geometry.hpp"
#include "road.hpp"
#include "arc.hpp"

namespace geo {

// ─── Compass direction ─────────────────────────────────────
inline std::string compassDirection(const Point2D& from, const Point2D& to) {
    double dx = to.x - from.x;
    double dy = to.y - from.y;
    double angle = std::atan2(dy, dx) * RAD_TO_DEG;
    if (angle >= -45 && angle < 45) return "east";
    if (angle >= 45 && angle < 135) return "north";
    if (angle >= 135 || angle < -135) return "west";
    return "south";
}

// ─── Find centerline intersection ──────────────────────────
inline Point2D findCenterlineIntersection(const std::vector<Point2D>& cl1,
                                           const std::vector<Point2D>& cl2) {
    for (size_t i = 0; i < cl1.size() - 1; i++) {
        for (size_t j = 0; j < cl2.size() - 1; j++) {
            Point2D hit = segmentIntersection(cl1[i], cl1[i + 1],
                                               cl2[j], cl2[j + 1]);
            if (isValid(hit)) return hit;
        }
    }
    return {std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::quiet_NaN()};
}

// ─── Build approach centerline ─────────────────────────────
// Builds a trimmed centerline from one side of the intersection.
// startSide=true: from road start to intersection (reversed)
// startSide=false: from intersection to road end
// The approach ends exactly at trimDist from the center.
inline std::vector<Point2D> buildApproachCenterline(
    const std::vector<Point2D>& samples,
    const Point2D& center,
    size_t closestIdx,
    double trimDist,
    bool startSide
) {
    std::vector<Point2D> approach;

    if (startSide) {
        // Collect samples from start up to closestIdx that are beyond trimDist
        for (size_t i = 0; i <= closestIdx; i++) {
            double d = samples[i].distanceTo(center);
            if (d > trimDist) {
                approach.push_back(samples[i]);
            }
        }
        // Add the exact trim point along the line from last sample to center
        if (approach.empty()) {
            // All samples are within trimDist — create a point at exactly trimDist
            // along the direction from center to the closest sample
            Vec2 dir = (samples[closestIdx] - center).normalized();
            approach.push_back(center + dir * trimDist);
        } else {
            auto& last = approach.back();
            double d = last.distanceTo(center);
            if (d > trimDist) {
                Vec2 dir = (center - last).normalized();
                approach.push_back(last + dir * (d - trimDist));
            }
        }
    } else {
        // Collect samples from closestIdx to end that are beyond trimDist
        for (size_t i = closestIdx; i < samples.size(); i++) {
            double d = samples[i].distanceTo(center);
            if (d > trimDist) {
                approach.push_back(samples[i]);
            }
        }
        // Prepend the exact trim point along the line from center to first sample
        if (approach.empty()) {
            Vec2 dir = (samples[closestIdx] - center).normalized();
            approach.push_back(center + dir * trimDist);
        } else {
            auto& first = approach.front();
            double d = first.distanceTo(center);
            if (d > trimDist) {
                Vec2 dir = (first - center).normalized();
                approach.insert(approach.begin(), center + dir * trimDist);
            }
        }
    }
    return approach;
}

// ─── Road boundary lines ───────────────────────────────────
// Each approach has a left and right boundary line (infinite line).
struct BoundaryLine {
    Point2D point;    // a point on the line (at trim distance)
    Vec2 direction;   // line direction (toward center)
    bool isLeft;      // left or right boundary
    int approachIdx;  // which approach this belongs to
};

// ─── Approach edge data ────────────────────────────────────
struct ApproachEdgeData {
    int approachIdx;
    double angle;           // approach angle for sorting
    Point2D trimCenter;     // centerline point at trim line
    Point2D leftAtTrim;     // left edge point at trim line
    Point2D rightAtTrim;    // right edge point at trim line
    Vec2 tangent;           // direction from trim toward center
    Vec2 normal;            // left normal (perpendicular to tangent)
    double halfWidth;
    BoundaryLine leftLine;  // left boundary as infinite line
    BoundaryLine rightLine; // right boundary as infinite line
};

// ─── Generate edge-based polygon (true boundary-based) ─────
// Builds the junction polygon from road boundary intersections.
//
// Algorithm:
// 1. Compute left/right boundary lines for each approach
// 2. Sort approaches by angle (CCW around center)
// 3. For each pair of adjacent approaches (i, i+1) in CCW order:
//    a. Find boundary intersection = LEFT line of i ∩ RIGHT line of i+1
//       (this is the outer corner between the two approaches)
//    b. Compute tangent points on each line at distance R from corner
//    c. Compute fillet arc center and radius
//    d. Generate arc points
// 4. Build polygon (CCW outer boundary):
//    leftTrim(i) → tangentIn → arc → tangentOut → rightTrim(i+1)
//    → [next iteration starts with leftTrim(i+1), crossing the road]
// 5. Clean and validate
//
// Key insight: for a CCW outer boundary, the corner between approach i
// and approach i+1 is formed by the LEFT edge of i and RIGHT edge of i+1
// (NOT right of i and left of i+1, which would cross through the center).
inline std::vector<Point2D> generateEdgeBasedPolygon(
    const std::vector<ApproachRoad>& approaches,
    const Point2D& center,
    double cornerRadius,
    std::vector<FilletCorner>& outCorners,
    std::vector<TrimLine>& outTrimLines,
    std::vector<Point2D>& outBoundaryIntersections
) {
    if (approaches.size() < 2) return {};

    // Step 1: Compute edge data for each approach
    std::vector<ApproachEdgeData> edgeData;
    for (size_t i = 0; i < approaches.size(); i++) {
        const auto& app = approaches[i];
        if (app.centerline.size() < 2) continue;

        Point2D trimPt = app.centerline[0];
        // Direction from trim toward center
        Vec2 tangent = (app.centerline[1] - app.centerline[0]).normalized();
        Vec2 normal = tangent.perp();  // left normal (90° CCW from tangent)
        double halfW = app.width / 2.0;

        // Left edge = centerline + normal * halfWidth (left side when facing center)
        // Right edge = centerline - normal * halfWidth (right side when facing center)
        Point2D leftAtTrim = {trimPt.x + normal.x * halfW, trimPt.y + normal.y * halfW};
        Point2D rightAtTrim = {trimPt.x - normal.x * halfW, trimPt.y - normal.y * halfW};

        BoundaryLine leftLine = {leftAtTrim, tangent, true, (int)i};
        BoundaryLine rightLine = {rightAtTrim, tangent, false, (int)i};

        edgeData.push_back({
            (int)i,
            std::atan2(tangent.y, tangent.x),
            trimPt, leftAtTrim, rightAtTrim,
            tangent, normal, halfW,
            leftLine, rightLine
        });

        // Record trim line
        outTrimLines.push_back({leftAtTrim, rightAtTrim, trimPt, (int)i});
    }

    if (edgeData.size() < 2) return {};

    // Step 2: Sort by angle (CCW around center)
    std::sort(edgeData.begin(), edgeData.end(),
              [](const ApproachEdgeData& a, const ApproachEdgeData& b) {
                  return a.angle < b.angle;
              });

    // Step 3: Compute corners (boundary intersections) and fillets
    int filletSegments = 12;

    for (size_t i = 0; i < edgeData.size(); i++) {
        const auto& current = edgeData[i];
        const auto& next = edgeData[(i + 1) % edgeData.size()];

        // ─── KEY FIX: Corner = LEFT line of current ∩ RIGHT line of next ───
        // This gives the OUTER corner of the intersection polygon.
        // (The old code used right∩left which crosses through the center,
        //  creating a figure-8 "star" pattern.)
        Point2D corner = lineIntersection(current.leftAtTrim, current.tangent,
                                           next.rightAtTrim, next.tangent);

        if (!isValid(corner)) {
            // Parallel edges — skip fillet
            outCorners.push_back({
                corner, corner, corner, corner, 0, {}, (int)i, (int)((i + 1) % edgeData.size())
            });
            continue;
        }

        outBoundaryIntersections.push_back(corner);

        // Compute tangent points on each edge line at distance R from corner
        // Tangent point on current LEFT line: go from corner toward trim point
        Vec2 dirToCurrentTrim = (current.leftAtTrim - corner).normalized();
        Point2D tangentIn = corner + dirToCurrentTrim * cornerRadius;

        // Tangent point on next RIGHT line: go from corner toward trim point
        Vec2 dirToNextTrim = (next.rightAtTrim - corner).normalized();
        Point2D tangentOut = corner + dirToNextTrim * cornerRadius;

        // Compute fillet arc center
        // The arc center is at the intersection of perpendiculars at tangent points
        Vec2 perpCurrent = dirToCurrentTrim.perp();
        Vec2 perpNext = dirToNextTrim.perp();

        // The center is on the inside of the corner (toward intersection center)
        Vec2 dirToCenter = (center - corner).normalized();
        Vec2 normalCurrent = perpCurrent.dot(dirToCenter) > 0 ? perpCurrent : perpCurrent * -1;
        Vec2 normalNext = perpNext.dot(dirToCenter) > 0 ? perpNext : perpNext * -1;

        Point2D arcCenter = lineIntersection(tangentIn, normalCurrent,
                                              tangentOut, normalNext);

        if (!isValid(arcCenter)) {
            arcCenter = {(tangentIn.x + tangentOut.x) / 2, (tangentIn.y + tangentOut.y) / 2};
        }

        // Generate arc points from tangentIn to tangentOut
        std::vector<Point2D> arcPoints;
        Vec2 vIn = tangentIn - arcCenter;
        Vec2 vOut = tangentOut - arcCenter;
        double angleIn = std::atan2(vIn.y, vIn.x);
        double angleOut = std::atan2(vOut.y, vOut.x);
        double sweep = angleOut - angleIn;

        // Normalize sweep to shortest path
        constexpr double PI = 3.14159265358979323846;
        while (sweep > PI) sweep -= 2 * PI;
        while (sweep < -PI) sweep += 2 * PI;

        double actualRadius = tangentIn.distanceTo(arcCenter);

        for (int j = 0; j <= filletSegments; j++) {
            double t = (double)j / filletSegments;
            double a = angleIn + sweep * t;
            arcPoints.push_back({
                arcCenter.x + actualRadius * std::cos(a),
                arcCenter.y + actualRadius * std::sin(a)
            });
        }

        FilletCorner fc;
        fc.boundaryIntersection = corner;
        fc.tangentIn = tangentIn;
        fc.tangentOut = tangentOut;
        fc.arcCenter = arcCenter;
        fc.radius = actualRadius;
        fc.arcPoints = arcPoints;
        fc.approachInIdx = (int)i;
        fc.approachOutIdx = (int)((i + 1) % edgeData.size());
        outCorners.push_back(fc);
    }

    // Step 4: Build polygon (CCW outer boundary)
    //
    // Polygon structure for N approaches:
    //   leftTrim(0) → [fillet 0→1] → rightTrim(1)
    //   → leftTrim(1) → [fillet 1→2] → rightTrim(2)
    //   → leftTrim(2) → [fillet 2→3] → rightTrim(3)
    //   → ...
    //   → leftTrim(N-1) → [fillet N-1→0] → rightTrim(0)
    //   → [closes back to leftTrim(0)] (crossing road 0)
    //
    // Between rightTrim(i+1) and leftTrim(i+1): straight line across road i+1
    // Between leftTrim(i) and rightTrim(i+1): fillet arc at corner
    std::vector<Point2D> polygon;

    for (size_t i = 0; i < edgeData.size(); i++) {
        const auto& current = edgeData[i];
        const auto& next = edgeData[(i + 1) % edgeData.size()];
        const auto& fc = outCorners[i];

        // 1. Left edge of current approach at trim line
        polygon.push_back(current.leftAtTrim);

        // 2. Fillet arc: tangentIn → arc points → tangentOut
        if (isValid(fc.boundaryIntersection) && fc.radius > 0.1) {
            polygon.push_back(fc.tangentIn);
            for (size_t j = 1; j < fc.arcPoints.size() - 1; j++) {
                polygon.push_back(fc.arcPoints[j]);
            }
            polygon.push_back(fc.tangentOut);
        } else if (isValid(fc.boundaryIntersection)) {
            // Sharp corner (no fillet)
            polygon.push_back(fc.boundaryIntersection);
        }

        // 3. Right edge of next approach at trim line
        // (The crossing from rightTrim(i+1) to leftTrim(i+1) happens
        //  at the start of the next iteration when we push leftTrim(i+1))
        polygon.push_back(next.rightAtTrim);
    }

    // Clean: remove duplicate consecutive points
    std::vector<Point2D> cleaned;
    for (const auto& p : polygon) {
        if (cleaned.empty() || p.distanceTo(cleaned.back()) > EPSILON) {
            cleaned.push_back(p);
        }
    }
    // Check first/last duplicate
    if (cleaned.size() > 1 && cleaned.front().distanceTo(cleaned.back()) < EPSILON) {
        cleaned.pop_back();
    }

    return cleaned;
}

// ─── Generate lane connections ─────────────────────────────
inline std::vector<LaneConnection> generateLaneConnections(
    const std::vector<ApproachRoad>& approaches,
    const Point2D& center
) {
    std::vector<LaneConnection> connections;

    for (size_t i = 0; i < approaches.size(); i++) {
        for (size_t j = 0; j < approaches.size(); j++) {
            if (i == j) continue;
            const auto& from = approaches[i];
            const auto& to = approaches[j];

            if (from.centerline.size() < 2 || to.centerline.size() < 2) continue;

            // Determine turn type
            Vec2 fromDir = (from.centerline[1] - from.centerline[0]).normalized();
            Vec2 toDir = (to.centerline[1] - to.centerline[0]).normalized();
            double cross = fromDir.cross(toDir);
            double dot = fromDir.dot(toDir);

            std::string type;
            if (dot > 0.5) type = "straight";
            else if (cross > 0) type = "left";
            else type = "right";

            // Path: from approach start → center → to approach start
            std::vector<Point2D> path = {
                from.centerline[0],
                center,
                to.centerline[0]
            };

            connections.push_back({
                from.direction, to.direction, type, path
            });
        }
    }
    return connections;
}

// ─── Main: Generate intersection ───────────────────────────
inline GeneratedIntersection generateIntersection(
    const Road& road1,
    const Road& road2,
    double refLat,
    double refLon
) {
    GeneratedIntersection result;

    // Sample centerlines
    auto s1 = road1.sampleCenterline(32);
    auto s2 = road2.sampleCenterline(32);

    if (s1.size() < 2 || s2.size() < 2) return result;

    // Step 1: Find centerline intersection
    Point2D center = findCenterlineIntersection(s1, s2);

    if (!isValid(center)) {
        // Fallback: closest point between the two roads
        double minDist = std::numeric_limits<double>::max();
        Point2D bestP = {0, 0};
        for (const auto& a : s1) {
            for (const auto& b : s2) {
                double d = a.distanceTo(b);
                if (d < minDist) { minDist = d; bestP = {(a.x + b.x) / 2, (a.y + b.y) / 2}; }
            }
        }
        if (minDist > 50) return result; // too far apart
        center = bestP;
    }

    result.center = center;

    // Step 2: Find closest sample indices
    size_t idx1 = 0, idx2 = 0;
    double minD1 = std::numeric_limits<double>::max();
    double minD2 = std::numeric_limits<double>::max();
    for (size_t i = 0; i < s1.size(); i++) {
        double d = s1[i].distanceTo(center);
        if (d < minD1) { minD1 = d; idx1 = i; }
    }
    for (size_t i = 0; i < s2.size(); i++) {
        double d = s2[i].distanceTo(center);
        if (d < minD2) { minD2 = d; idx2 = i; }
    }

    // Step 3: Compute angle between roads
    Vec2 t1 = tangentAt(s1, idx1);
    Vec2 t2 = tangentAt(s2, idx2);
    double dot = t1.dot(t2);
    double angleBetweenVal = std::acos(clamp(std::abs(dot), -1.0, 1.0));
    double halfAngle = angleBetweenVal / 2.0;

    // Step 4: Trim distances
    // The trim distance is how far from the center each road is cut.
    // It must be large enough that the road edges clear the intersection polygon.
    // Formula: trimDist = halfWidth_other / sin(angle) + cornerRadius / tan(halfAngle)
    // Simplified: trimDist = halfWidth_other + cornerRadius * tan(halfAngle)
    // But we also need to account for the road's own halfWidth.
    double halfW1 = road1.width / 2.0;
    double halfW2 = road2.width / 2.0;
    double maxHalfW = std::max(halfW1, halfW2);
    // Corner radius: use the smaller halfWidth, clamped to [3, 15]
    double cornerRadius = std::clamp(std::min(halfW1, halfW2), 3.0, 15.0);
    // For perpendicular roads (90°), tan(45°) = 1, so trimDist = halfW + R
    // For acute angles, tan(halfAngle) is larger, so trimDist is larger
    double tanHalf = std::tan(halfAngle);
    if (tanHalf > 10.0) tanHalf = 10.0;  // clamp for very acute angles
    double trimDist1 = halfW2 + cornerRadius * tanHalf;
    double trimDist2 = halfW1 + cornerRadius * tanHalf;
    // Ensure minimum trim distance
    double minTrim = maxHalfW + cornerRadius;
    trimDist1 = std::max(trimDist1, minTrim);
    trimDist2 = std::max(trimDist2, minTrim);

    // Step 5: Build approaches
    auto r1Start = buildApproachCenterline(s1, center, idx1, trimDist1, true);
    auto r1End = buildApproachCenterline(s1, center, idx1, trimDist1, false);
    auto r2Start = buildApproachCenterline(s2, center, idx2, trimDist2, true);
    auto r2End = buildApproachCenterline(s2, center, idx2, trimDist2, false);

    double zAvg = 0; // TODO: interpolate z

    if (r1Start.size() >= 2) {
        std::reverse(r1Start.begin(), r1Start.end());
        result.approaches.push_back({
            road1.id, compassDirection(center, r1Start.back()),
            r1Start, road1.width, road1.laneCount, zAvg
        });
    }
    if (r1End.size() >= 2) {
        result.approaches.push_back({
            road1.id, compassDirection(center, r1End.front()),
            r1End, road1.width, road1.laneCount, zAvg
        });
    }
    if (r2Start.size() >= 2) {
        std::reverse(r2Start.begin(), r2Start.end());
        result.approaches.push_back({
            road2.id, compassDirection(center, r2Start.back()),
            r2Start, road2.width, road2.laneCount, zAvg
        });
    }
    if (r2End.size() >= 2) {
        result.approaches.push_back({
            road2.id, compassDirection(center, r2End.front()),
            r2End, road2.width, road2.laneCount, zAvg
        });
    }

    if (result.approaches.size() < 2) return result;

    // Step 6: Generate polygon (with construction debug data)
    result.cornerRadius = cornerRadius;
    result.trimDistance1 = trimDist1;
    result.trimDistance2 = trimDist2;
    result.intersectionAngle = angleBetweenVal;
    result.polygon = generateEdgeBasedPolygon(
        result.approaches, center, cornerRadius,
        result.corners, result.trimLines, result.boundaryIntersections
    );

    // Step 6b: Validate and clean polygon
    auto validation = validatePolygon(result.polygon);
    if (validation.hasDuplicateVertices || validation.hasZeroLengthEdges) {
        result.polygon = cleanPolygon(result.polygon);
        // Re-validate after cleaning
        validation = validatePolygon(result.polygon);
    }
    if (!validation.isValid) {
        printf("[Intersection] WARNING: Polygon validation failed: %s (verts=%zu, area=%.2f, intersections=%d)\n",
               validation.errorMessage.c_str(), result.polygon.size(),
               validation.signedArea, validation.intersectionCount);
    }

    // Step 7: Lane connections
    result.laneConnections = generateLaneConnections(result.approaches, center);

    // Step 8: Stop lines and crosswalks
    for (const auto& approach : result.approaches) {
        if (approach.centerline.size() < 2) continue;

        Point2D edgePt = approach.centerline[0];
        Vec2 tangent = (approach.centerline[1] - approach.centerline[0]).normalized();
        Vec2 normal = tangent.perp();
        double halfW = approach.width / 2.0;

        result.stopLines.push_back({
            approach.direction,
            {edgePt.x + normal.x * halfW, edgePt.y + normal.y * halfW},
            {edgePt.x - normal.x * halfW, edgePt.y - normal.y * halfW}
        });

        // Crosswalk 3m before stop line
        double cwOffset = 3.0;
        Point2D cwCenter = {edgePt.x - tangent.x * cwOffset, edgePt.y - tangent.y * cwOffset};
        double cwDepth = 2.0;
        result.crosswalks.push_back({
            approach.direction,
            {
                {cwCenter.x + normal.x * halfW, cwCenter.y + normal.y * halfW},
                {cwCenter.x - normal.x * halfW, cwCenter.y - normal.y * halfW},
                {cwCenter.x - tangent.x * cwDepth - normal.x * halfW,
                 cwCenter.y - tangent.y * cwDepth - normal.y * halfW},
                {cwCenter.x - tangent.x * cwDepth + normal.x * halfW,
                 cwCenter.y - tangent.y * cwDepth + normal.y * halfW}
            }
        });
    }

    return result;
}

} // namespace geo
