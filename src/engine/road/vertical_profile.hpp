#pragma once

// ═══════════════════════════════════════════════════════════
// VerticalProfile / Superelevation — station-based vertical design
// ═══════════════════════════════════════════════════════════
//
// First-class vertical alignment for RoadV2 (all units metric):
//   - ElevationPoint(s, z): station [m], elevation [m]
//   - VerticalProfile: monotone piecewise-cubic (Fritsch–Carlson)
//     interpolation through elevation control points. Monotone method
//     prevents the overshoot a natural cubic spline produces on road
//     grades (no sag dipping below both knots or crest bulging above).
//   - Superelevation: cross slope [% as fraction] vs station,
//     piecewise-linear.
//   - validateGrade(): warning-level grade-limit checking.
//
// Queries outside the defined station range clamp to the end values
// (standard linear-referencing behaviour); range checks live in
// validation, not in the interpolator.

#include <algorithm>
#include <cmath>
#include <vector>

namespace geo {

struct ElevationPoint {
    double s = 0.0; // station along road [m]
    double z = 0.0; // elevation [m]
};

class VerticalProfile {
public:
    // Add a control point. Duplicate stations are ignored (first wins);
    // points are kept sorted by s.
    void addPoint(double s, double z) {
        auto it = std::lower_bound(points_.begin(), points_.end(), s,
            [](const ElevationPoint& p, double v) { return p.s < v; });
        if (it != points_.end() && it->s == s) return;
        points_.insert(it, ElevationPoint{s, z});
        derivsDirty_ = true;
    }

    void clear() { points_.clear(); derivsDirty_ = true; }
    bool empty() const { return points_.empty(); }
    size_t size() const { return points_.size(); }
    const std::vector<ElevationPoint>& points() const { return points_; }

    double startS() const { return points_.empty() ? 0.0 : points_.front().s; }
    double endS()   const { return points_.empty() ? 0.0 : points_.back().s; }

    // Elevation at station s. Empty profile → 0 (flat). Outside range →
    // clamped to the nearest end elevation.
    double elevationAt(double s) const {
        if (points_.empty()) return 0.0;
        if (s <= points_.front().s) return points_.front().z;
        if (s >= points_.back().s)  return points_.back().z;
        ensureDerivatives();
        const size_t i = segmentIndexFor(s);
        return hermiteEval(i, s);
    }

    // Grade dz/ds at station s [m/m]. Same boundary behaviour as above.
    double gradeAt(double s) const {
        if (points_.size() < 2) return 0.0;
        if (s <= points_.front().s) return m_.empty() ? slopeOf(0) : m_[0];
        if (s >= points_.back().s)  return m_.empty() ? slopeOf(size()-2) : m_[size()-2];
        ensureDerivatives();
        const size_t i = segmentIndexFor(s);
        return hermiteDeriv(i, s);
    }

private:
    size_t segmentIndexFor(double s) const {
        // points_ sorted; s strictly inside (front().s, back().s)
        size_t i = 0;
        while (i + 2 < points_.size() && points_[i + 1].s <= s) ++i;
        return i;
    }

    double slopeOf(size_t i) const {
        return (points_[i+1].z - points_[i].z) / (points_[i+1].s - points_[i].s);
    }

    // Fritsch–Carlson monotone tangents at each control point.
    void ensureDerivatives() const {
        if (!derivsDirty_ && m_.size() == points_.size()) return;
        const size_t n = points_.size();
        m_.assign(n, 0.0);
        if (n < 2) { derivsDirty_ = false; return; }

        std::vector<double> delta(n - 1);
        for (size_t i = 0; i + 1 < n; ++i) delta[i] = slopeOf(i);

        // Endpoints: one-sided three-point estimates, clamped per FC rules.
        m_[0] = delta[0];
        m_[n-1] = delta[n-2];
        if (n > 2) {
            const double h01 = points_[1].s - points_[0].s;
            const double h12 = points_[2].s - points_[1].s;
            const double d = ((2.0*h01 + h12) * delta[0] - h01 * delta[1]) / (h01 + h12);
            m_[0] = (d * delta[0] <= 0.0) ? 0.0
                  : (std::abs(d) > 3.0*std::abs(delta[0])) ? 3.0*delta[0] : d;
            const size_t k = n - 1;
            const double hk1 = points_[k-1].s - points_[k-2].s;
            const double hk = points_[k].s - points_[k-1].s;
            const double dl = ((2.0*hk + hk1) * delta[k-1] - hk * delta[k-2]) / (hk + hk1);
            m_[k] = (dl * delta[k-1] <= 0.0) ? 0.0
                  : (std::abs(dl) > 3.0*std::abs(delta[k-1])) ? 3.0*delta[k-1] : dl;
        }

        // Interior: zero tangent at local extrema, else weighted harmonic mean.
        for (size_t i = 1; i + 1 < n; ++i) {
            if (delta[i-1] * delta[i] <= 0.0) { m_[i] = 0.0; continue; }
            const double hPrev = points_[i].s - points_[i-1].s;
            const double hNext = points_[i+1].s - points_[i].s;
            const double w1 = 2.0*hNext + hPrev, w2 = hNext + 2.0*hPrev;
            m_[i] = (w1 + w2) / (w1/delta[i-1] + w2/delta[i]);
        }
        derivsDirty_ = false;
    }

    // Cubic Hermite on segment [points_[i], points_[i+1]].
    double hermiteEval(size_t i, double s) const {
        const double h = points_[i+1].s - points_[i].s;
        const double t = (s - points_[i].s) / h;
        const double t2 = t*t, t3 = t2*t;
        return (2*t3 - 3*t2 + 1) * points_[i].z
             + (t3 - 2*t2 + t) * h * m_[i]
             + (-2*t3 + 3*t2) * points_[i+1].z
             + (t3 - t2) * h * m_[i+1];
    }

    double hermiteDeriv(size_t i, double s) const {
        const double h = points_[i+1].s - points_[i].s;
        const double t = (s - points_[i].s) / h;
        const double t2 = t*t;
        return (6*t2 - 6*t)/h * points_[i].z
             + (3*t2 - 4*t + 1) * m_[i]
             + (-6*t2 + 6*t)/h * points_[i+1].z
             + (3*t2 - 2*t) * m_[i+1];
    }

    std::vector<ElevationPoint> points_;
    mutable std::vector<double> m_;      // Fritsch–Carlson tangents
    mutable bool derivsDirty_ = true;
};

// ─── Superelevation ──────────────────────────────────────────
//
// Cross slope vs station, piecewise-linear. Units: fraction of width
// (0.02 = 2%). Sign convention: positive = pavement rises toward the
// LEFT of travel direction.

struct CrossfallPoint {
    double s = 0.0;     // station [m]
    double slope = 0.0; // cross slope fraction (+ = left edge higher)
};

class Superelevation {
public:
    void addPoint(double s, double slope) {
        auto it = std::lower_bound(pts_.begin(), pts_.end(), s,
            [](const CrossfallPoint& p, double v) { return p.s < v; });
        if (it != pts_.end() && it->s == s) { it->slope = slope; return; }
        pts_.insert(it, CrossfallPoint{s, slope});
    }
    void clear() { pts_.clear(); }
    bool empty() const { return pts_.empty(); }
    const std::vector<CrossfallPoint>& points() const { return pts_; }

    // Cross slope at s; outside range clamps to end value. Empty → 0.
    double crossfallAt(double s) const {
        if (pts_.empty()) return 0.0;
        if (s <= pts_.front().s) return pts_.front().slope;
        if (s >= pts_.back().s)  return pts_.back().slope;
        const auto hi = std::upper_bound(pts_.begin(), pts_.end(), s,
            [](double v, const CrossfallPoint& p) { return v < p.s; });
        const auto lo = hi - 1;
        const double t = (s - lo->s) / (hi->s - lo->s);
        return lo->slope + t * (hi->slope - lo->slope);
    }

private:
    std::vector<CrossfallPoint> pts_;
};

// ─── Grade validation ────────────────────────────────────────
//
// Warning-level check (never blocks editing). Samples each segment so
// interior grade peaks of the cubic are caught, not just knot grades.

struct GradeIssue {
    double s = 0.0;     // station where the limit was exceeded
    double grade = 0.0; // offending grade [m/m]
};

inline std::vector<GradeIssue> validateGrade(const VerticalProfile& profile,
                                             double maxAbsGrade)
{
    std::vector<GradeIssue> issues;
    if (profile.size() < 2 || maxAbsGrade <= 0.0) return issues;

    constexpr int kSamplesPerSegment = 8;
    const auto& pts = profile.points();
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const double segLen = pts[i+1].s - pts[i].s;
        for (int k = 0; k <= kSamplesPerSegment; ++k) {
            const double s = pts[i].s + segLen * k / kSamplesPerSegment;
            const double g = profile.gradeAt(s);
            if (std::abs(g) > maxAbsGrade &&
                (issues.empty() || s - issues.back().s > segLen / 2.0)) {
                issues.push_back(GradeIssue{s, g});
            }
        }
    }
    return issues;
}

} // namespace geo
