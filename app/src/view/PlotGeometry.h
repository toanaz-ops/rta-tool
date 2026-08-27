// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE, no Qt, no audio-device API:
// this file is compiled directly into rtatool_analysis_tests, which builds
// on CI even when RTA_BUILD_APP is OFF. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §1.3, §3.5.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace rta::view {

/// Pure log-frequency / dB axis math for the RTA plot -- a plain struct of
/// floats and doubles, no JUCE `Rectangle` or `Graphics`, so it can be
/// tested with no component, no peer, no screen.
///
/// The x axis is logarithmic over `[fLowHz, fHighHz]`: equal PIXEL distance
/// per equal RATIO of frequency, which is what makes a decade's geometric
/// mean sit at its pixel midpoint (`test_plot_geometry.cpp` asserts exactly
/// that -- the one property a linear axis would fail while still passing
/// every single-point sample check). The y axis maps `[dbBottom, dbTop]`
/// linearly, top-down (`dbTop` at `top`, `dbBottom` at `bottom`) and clamps
/// outside that range, because an unclamped floor draws bars off the
/// bottom of the component and into the panel below.
struct PlotGeometry {
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;

    double fLowHz = 20.0;
    double fHighHz = 20000.0;
    double dbTop = 0.0;
    double dbBottom = -90.0;

    /// Pixel x for a frequency. `hz <= 0` is not a meaningful frequency and
    /// is not guarded against: callers only ever pass a band's own centre/
    /// edge frequency, which is always positive by construction.
    [[nodiscard]] float xForHz(double hz) const noexcept {
        const double t = std::log(hz / fLowHz) / std::log(fHighHz / fLowHz);
        return left + static_cast<float>(t) * (right - left);
    }

    /// The inverse of `xForHz` -- not clamped to `[left, right]`, so a
    /// caller can ask "what frequency is just past the edge" without the
    /// answer silently folding back onto the axis.
    [[nodiscard]] double hzForX(float x) const noexcept {
        const double t = (right != left) ? (double) (x - left) / (double) (right - left) : 0.0;
        return fLowHz * std::pow(fHighHz / fLowHz, t);
    }

    /// Pixel y for a dB value, clamped to `[dbBottom, dbTop]` before
    /// mapping -- see the class comment for why an unclamped floor is a
    /// drawing bug, not a display nicety.
    [[nodiscard]] float yForDb(double db) const noexcept {
        const double clamped = std::clamp(db, dbBottom, dbTop);
        const double t = (dbTop != dbBottom) ? (clamped - dbTop) / (dbBottom - dbTop) : 0.0;
        return top + static_cast<float>(t) * (bottom - top);
    }
};

/// The 1-2-5 decade tick sequence within `[lo, hi]` inclusive -- e.g.
/// `decadeTicks(20, 20000)` yields `20 50 100 200 500 1000 2000 5000 10000
/// 20000`, the frequency labels plan §1.4 fixes: whole hertz, no `k`
/// abbreviation.
///
/// Decades are built by repeated multiplication by 10 from 1.0, not
/// `std::pow(10.0, n)`: for the small integer exponents this range ever
/// needs, that keeps every tick an EXACT double (10, 100, 1000, ... are all
/// exactly representable, and so are their x1/x2/x5 multiples), which is
/// what lets the test compare the result against a literal list with `==`
/// instead of a tolerance.
[[nodiscard]] inline std::vector<double> decadeTicks(double lo, double hi) {
    std::vector<double> ticks;
    if (lo <= 0.0 || hi <= lo) return ticks;

    const int startExp = static_cast<int>(std::floor(std::log10(lo) + 1e-9));
    const int endExp = static_cast<int>(std::ceil(std::log10(hi) - 1e-9));

    for (int e = startExp; e <= endExp; ++e) {
        double decade = 1.0;
        if (e >= 0) {
            for (int i = 0; i < e; ++i) decade *= 10.0;
        } else {
            for (int i = 0; i < -e; ++i) decade /= 10.0;
        }
        for (const double mult : { 1.0, 2.0, 5.0 }) {
            const double v = mult * decade;
            if (v >= lo - 1e-6 && v <= hi + 1e-6) ticks.push_back(v);
        }
    }
    return ticks;
}

}  // namespace rta::view
