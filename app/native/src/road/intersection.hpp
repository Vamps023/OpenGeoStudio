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

// ─── Generate edge-based polygon ───────────────────────────
inline std::vector<Point2D> generateEdgeBasedPolygon(
    const std::vector<ApproachRoad>& approaches,
    const Point2D& center,
    double cornerRadius
) {
    if (approaches.size() < 2) return {};

    struct EdgeInfo {
        const ApproachRoad* approach;
        double angle;
        Point2D leftEdge;
        Point2D rightEdge;
        Vec2 tangent;
        Vec2 normal;
        double halfWidth;
    };

    std::vector<EdgeInfo> edges;

    for (const auto& approach : approaches) {
        if (approach.centerline.size() < 2) continue;

        Point2D edgePt = approach.centerline[0];
        Vec2 tangent = (approach.centerline[1] - approach.centerline[0]).normalized();
        Vec2 normal = tangent.perp();
        double halfW = approach.width / 2.0;

        edges.push_back({
            &approach,
            std::atan2(tangent.y, tangent.x),
            {edgePt.x + normal.x * halfW, edgePt.y + normal.y * halfW},
            {edgePt.x - normal.x * halfW, edgePt.y - normal.y * halfW},
            tangent, normal, halfW
        });
    }

    if (edges.size() < 2) return {};

    // Sort by angle
    std::sort(edges.begin(), edges.end(),
              [](const EdgeInfo& a, const EdgeInfo& b) { return a.angle < b.angle; });

    // Build polygon
    std::vector<Point2D> polygon;
    int filletSegments = 8;

    for (size_t i = 0; i < edges.size(); i++) {
        const auto& current = edges[i];
        const auto& next = edges[(i + 1) % edges.size()];

        // Corner = intersection of right edge line of current and left edge line of next
        Point2D corner = lineIntersection(current.rightEdge, current.tangent,
                                           next.leftEdge, next.tangent);

        // Add right edge of current approach
        polygon.push_back(current.rightEdge);

        if (isValid(corner)) {
            // Check if corner is reasonable (not too far from center)
            double cornerDist = corner.distanceTo(center);
            double maxCornerDist = 100.0;  // sanity limit

            if (cornerDist < maxCornerDist) {
                Vec2 dirIn = (current.rightEdge - corner).normalized();
                Vec2 dirOut = (next.leftEdge - corner).normalized();
                double lenIn = current.rightEdge.distanceTo(corner);
                double lenOut = next.leftEdge.distanceTo(corner);
                double r = std::min({cornerRadius, lenIn * 0.9, lenOut * 0.9});

                if (r > 0.1) {
                    auto arc = filletArc(corner, dirIn, dirOut, r, filletSegments);
                    for (size_t j = 1; j < arc.size() - 1; j++) {
                        polygon.push_back(arc[j]);
                    }
                }
            }
            // If corner is invalid/too far, just connect directly (sharp corner)
        }

        // Add left edge of next approach
        polygon.push_back(next.leftEdge);
    }

    // Validate polygon: remove duplicate consecutive points
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
    double halfW1 = road1.width / 2.0;
    double halfW2 = road2.width / 2.0;
    double maxHalfW = std::max(halfW1, halfW2);
    double cornerRadius = std::min(maxHalfW, 5.0);
    double trimDist1 = halfW2 + cornerRadius * std::tan(halfAngle);
    double trimDist2 = halfW1 + cornerRadius * std::tan(halfAngle);

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

    // Step 6: Generate polygon
    result.polygon = generateEdgeBasedPolygon(result.approaches, center, cornerRadius);

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
