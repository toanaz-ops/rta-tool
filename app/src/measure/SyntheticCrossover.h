// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
//
// ALIGN-UI lane (docs/plans/2026-10-20-align-ui-lane-plan.md, T0/T6): the two
// output paths of the SYNTHETIC loopback are a crossover pair. A 4th-order
// Linkwitz-Riley low- or high-pass is an RBJ Butterworth section (Q = 1/sqrt 2)
// run twice; the pair then differs by exactly N*90 = 360 degrees of phase at
// every frequency (record l7-alignment-wizard sec.3), so the only thing left
// between two such paths is whatever delay the fixture gives them -- which is
// what the wizard has to find.
#pragma once

#include "rta/dsp/Biquad.h"

#include <cmath>
#include <numbers>
#include <vector>

namespace rta::measure {

namespace detail {

inline rta::dsp::Biquad::Coeffs rbjSection(double fcHz, double fs, bool highPass) {
    constexpr double kButterworthQ = 0.7071067811865476;  // 1/sqrt(2)
    const double w0 = 2.0 * std::numbers::pi * fcHz / fs;
    const double alpha = std::sin(w0) / (2.0 * kButterworthQ);
    const double cw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    const double b1 = highPass ? 1.0 + cw : 1.0 - cw;
    return {b1 / (2.0 * a0), (highPass ? -b1 : b1) / a0, b1 / (2.0 * a0), -2.0 * cw / a0,
            (1.0 - alpha) / a0};
}

}  // namespace detail

/// Two identical sections: LR4 low-pass at `fcHz`.
[[nodiscard]] inline std::vector<rta::dsp::Biquad::Coeffs> lr4LowPass(double fcHz, double fs) {
    const auto s = detail::rbjSection(fcHz, fs, false);
    return {s, s};
}

}  // namespace rta::measure
