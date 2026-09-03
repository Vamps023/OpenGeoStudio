#pragma once

// ═══════════════════════════════════════════════════════════
// Curvature sampling along an odr::RefLine.
// Used by furniture placement (skip repeated props on sharp
// curves) and testable headlessly — no GL dependency.
// ═══════════════════════════════════════════════════════════

#include "RefLine.h"
#include <cmath>
#include <limits>

namespace LM
{

// Signed curvature (1/m) at station s via central difference of
// the reference-line heading. Near-zero on straights.
inline double curvatureAt(const odr::RefLine& ref, double s, double probe = 1.0)
{
    if (ref.length <= 0.0) return 0.0;
    const double s0 = std::max(0.0, s - probe);
    const double s1 = std::min(ref.length, s + probe);
    if (s1 - s0 < 1e-6) return 0.0;

    double dHdg = ref.get_hdg(s1) - ref.get_hdg(s0);
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    while (dHdg > kPi) dHdg -= kTwoPi;   // wrap heading delta to [-π, π]
    while (dHdg < -kPi) dHdg += kTwoPi;
    return dHdg / (s1 - s0);
}

// Radius of the local turn in meters; infinity on a straight.
inline double turnRadiusAt(const odr::RefLine& ref, double s, double probe = 1.0)
{
    const double k = std::abs(curvatureAt(ref, s, probe));
    return k < 1e-6 ? std::numeric_limits<double>::infinity() : 1.0 / k;
}

} // namespace LM