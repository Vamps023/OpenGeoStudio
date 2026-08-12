#pragma once

// ═══════════════════════════════════════════════════════════
// Clothoid (Euler Spiral) — Continuous curvature transition
// ═══════════════════════════════════════════════════════════
//
// A clothoid (also called Euler spiral or Cornu spiral) is a curve
// whose curvature changes linearly with arc length:
//
//   κ(s) = κ₀ + s / A²
//
// where:
//   κ(s) = curvature at arc length s
//   κ₀   = initial curvature
//   A    = clothoid parameter (determines how fast curvature changes)
//   s    = arc length from start
//
// The position is computed via Fresnel integrals:
//   x(s) = ∫₀ˢ cos(κ₀·t + t²/(2A²)) dt
//   y(s) = ∫₀ˢ sin(κ₀·t + t²/(2A²)) dt
//
// Applications:
// - Highway ramp design (smooth curvature transition)
// - Railway transition curves (prevent sudden jerk)
// - Racing track design (constant steering rate)
// - Connecting straight roads to circular arcs (G2 continuity)

#include "geometry.hpp"
#include <cmath>

namespace geo {

// ─── Fresnel integrals (numerical computation) ─────────────
// C(t) = ∫₀ᵗ cos(πu²/2) du
// S(t) = ∫₀ᵗ sin(πu²/2) du
//
// Uses shared simpsonIntegrate2D from geometry.hpp.

struct FresnelResult {
    double C;  // C(t) = ∫ cos(πu²/2) du
    double S;  // S(t) = ∫ sin(πu²/2) du
};

inline FresnelResult fresnel(double t) {
    auto [c, s] = simpsonIntegrate2D(
        [](double u) -> std::pair<double, double> {
            double arg = PI * u * u / 2.0;
            return {std::cos(arg), std::sin(arg)};
        },
        t, 1000
    );
    return {c, s};
}

// ─── Clothoid parameters ───────────────────────────────────
struct ClothoidParams {
    double A;           // clothoid parameter
    double L;           // total length
    double kappa0;      // initial curvature (0 = straight, 1/R = arc)
    double kappa1;      // final curvature
    Point2D startPoint;
    Vec2 startDirection;  // normalized
    bool isLeftTurn;     // true = CCW, false = CW
};

// ─── Clothoid result ───────────────────────────────────────
struct ClothoidResult {
    ClothoidParams params;
    std::vector<Point2D> points;     // sampled points
    Vec2 tangentIn;                   // direction at start
    Vec2 tangentOut;                  // direction at end
    double totalAngle;                // total angle change (radians)
};

// ─── Compute clothoid from parameters ──────────────────────
inline ClothoidResult computeClothoid(const ClothoidParams& params, int segments = 64) {
    ClothoidResult result;
    result.params = params;
    result.tangentIn = params.startDirection;

    double A = params.A;
    double L = params.L;
    double k0 = params.kappa0;
    double k1 = params.kappa1;

    if (A < EPSILON || L < EPSILON) {
        result.points.push_back(params.startPoint);
        result.tangentOut = params.startDirection;
        result.totalAngle = 0;
        return result;
    }

    // Total angle change
    double totalAngle = k0 * L + L * L / (2.0 * A * A);
    result.totalAngle = totalAngle;

    // Sample the clothoid
    result.points.clear();
    result.points.reserve(segments + 1);

    double dirAngle = std::atan2(params.startDirection.y, params.startDirection.x);
    double turnSign = params.isLeftTurn ? 1.0 : -1.0;

    for (int i = 0; i <= segments; i++) {
        double s = (static_cast<double>(i) / segments) * L;

        // Position via shared Simpson's rule integration:
        // x(s) = ∫₀ˢ cos(k0·t + t²/(2A²)) dt
        // y(s) = ∫₀ˢ sin(k0·t + t²/(2A²)) dt
        auto [x, y] = simpsonIntegrate2D(
            [k0, A](double t) -> std::pair<double, double> {
                double angle = k0 * t + t * t / (2.0 * A * A);
                return {std::cos(angle), std::sin(angle)};
            },
            s, 50
        );

        // Apply turn direction and rotate to start direction
        double rotAngle = dirAngle;  // base direction
        Point2D localPt = {x, turnSign * y};
        Point2D worldPt = localPt.rotated(rotAngle);

        result.points.push_back(params.startPoint + worldPt);
    }

    // Compute tangent at end
    double endAngle = dirAngle + turnSign * totalAngle;
    result.tangentOut = {std::cos(endAngle), std::sin(endAngle)};

    return result;
}

// ─── Fit clothoid between two points with given tangents ────
// This finds a clothoid that connects startPoint (with startDirection)
// to endPoint (with endDirection), achieving G2 continuity.
//
// Uses an iterative approach to find the clothoid parameter A
// and length L that best fit the boundary conditions.
inline ClothoidResult fitClothoid(
    const Point2D& startPoint,
    const Vec2& startDirection,
    const Point2D& endPoint,
    const Vec2& endDirection,
    double initialA = 50.0,
    int segments = 64
) {
    // Estimate length from distance
    double dist = startPoint.distanceTo(endPoint);

    // Determine turn direction
    Vec2 toEnd = (endPoint - startPoint).normalized();
    double cross = startDirection.cross(toEnd);
    bool isLeftTurn = cross > 0;

    // Angle change needed
    double startAngle = std::atan2(startDirection.y, startDirection.x);
    double endAngle = std::atan2(endDirection.y, endDirection.x);
    double angleChange = normalizeAnglePi(endAngle - startAngle);
    if (!isLeftTurn) angleChange = -std::abs(angleChange);
    else angleChange = std::abs(angleChange);

    // For a clothoid from straight (k0=0) to curvature k1:
    // totalAngle = L² / (2A²)
    // So A = L / sqrt(2 * totalAngle / L) = L / sqrt(2 * totalAngle) * sqrt(L)
    // Simplification: A = sqrt(L / (2 * |totalAngle| / L)) = sqrt(L² / (2 * |totalAngle|))
    // A = L / sqrt(2 * |totalAngle|)

    double L = dist * 1.1;  // slightly longer than straight line
    double absAngle = std::abs(angleChange);
    double A = (absAngle > EPSILON) ? L / std::sqrt(2.0 * absAngle) : initialA;

    // Iterative refinement: adjust L until the clothoid endpoint matches
    for (int iter = 0; iter < 10; iter++) {
        ClothoidParams params;
        params.A = A;
        params.L = L;
        params.kappa0 = 0;  // start straight
        params.kappa1 = L / (A * A);  // end curvature
        params.startPoint = startPoint;
        params.startDirection = startDirection;
        params.isLeftTurn = isLeftTurn;

        auto result = computeClothoid(params, segments);

        // Check endpoint error
        Point2D computedEnd = result.points.back();
        double error = computedEnd.distanceTo(endPoint);

        if (error < 0.5) {
            return result;  // good enough
        }

        // Adjust L based on error
        double scale = dist / computedEnd.distanceTo(startPoint);
        L *= scale;
        A = (absAngle > EPSILON) ? L / std::sqrt(2.0 * absAngle) : initialA;
    }

    // Final computation with best-fit parameters
    ClothoidParams params;
    params.A = A;
    params.L = L;
    params.kappa0 = 0;
    params.kappa1 = L / (A * A);
    params.startPoint = startPoint;
    params.startDirection = startDirection;
    params.isLeftTurn = isLeftTurn;

    return computeClothoid(params, segments);
}

} // namespace geo
