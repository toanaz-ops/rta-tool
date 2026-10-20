// SPDX-License-Identifier: AGPL-3.0-or-later
// Fixture-only: the feedback-comb gain that gives SYNTHETIC's test room a chosen
// RT60 (RT60-UI lane decision 10). Production never builds a room -- the comb
// is reachable only through `restartSyntheticForTest` -- so this lives with the
// tests and the snapshot tool rather than in app/src (a function no production
// caller reaches is an orphan, tools/orphan_check.py).
#pragma once

#include <cmath>

namespace rta::measure {

/// The gain g for which y[n] = x[n] + g*y[n-D] has a 60 dB decay time of exactly
/// `t60Seconds`, in every band. Each trip round the loop costs 20*log10(1/g) dB
/// per D samples, so T = 3*D / (fs*log10(1/g)) (see FeedbackComb), hence
/// g = 10^(-3*D / (fs*T)). D = 480 at 48 kHz and T = 0.80 s give 0.91728, a
/// loss of 0.75 dB per trip: 40 steps across the T30 span.
[[nodiscard]] inline double roomCombGainFor(double t60Seconds, int delaySamples, double sampleRate) {
    return std::pow(10.0, -3.0 * static_cast<double>(delaySamples) / (sampleRate * t60Seconds));
}

}  // namespace rta::measure
