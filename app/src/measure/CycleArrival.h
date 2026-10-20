// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
//
// ALIGN-UI wave B, PR #72 verifier round 1: two facts that stand between "the
// IRs' arrivals" and "the cycle integer".
//
// 1. An ARRIVAL is the peak of |h|, and |h| of a band-limited path peaks LATE.
//    An LR4 low-pass at 100 Hz peaks ~4.6 ms (221 samples at 48 kHz) after the
//    wavefront entered it; a high-pass peaks ~0. So `arrival(low) - arrival(high)`
//    is the pure delay PLUS the low side's peak lag, and handing that to the
//    candidate choice picks a cycle that is wrong whenever the lag is more than
//    half the candidate spacing (an LR8 low-pass at 100 Hz lags 9.4 ms; the
//    candidates sit ~10.8 ms apart). `topologyPeakLagSamples` is that lag for the
//    topology the operator NAMED, computed from the same kind of biquad design
//    the topology is built from, never a typed 0.46/fc.
// 2. The correction can only remove the lag of the filter that was named. A box
//    with another filter in the path (a subsonic high-pass on the sub) leaves a
//    residual. `candidateWithinGuard` refuses to choose when the integer is not
//    clearly nearest one candidate, so a residual surfaces as a question to the
//    operator and not as a silently wrong cycle.
#pragma once

#include "measure/CrossoverTopology.h"

#include "rta/dsp/Biquad.h"

#include <optional>
#include <vector>

namespace rta::measure {

/// The low-pass (or high-pass) cascade the topology is built from, at
/// `crossoverHz`: an order-N Butterworth, or the order-N/2 Butterworth twice for
/// Linkwitz-Riley. Empty for an order this does not build. Public so a test
/// fixture can put the SAME filters on a SYNTHETIC output and let the loopback
/// measure the lag instead of trusting the formula.
[[nodiscard]] std::vector<rta::dsp::Biquad::Coeffs> topologyCascade(Topology topology, bool highPass,
                                                                    double crossoverHz, double sampleRate);

/// Samples from t = 0 to the peak of |h| of one side of `topology` at
/// `crossoverHz`: argmax|h| of the impulse response of the low-pass (or the
/// high-pass) Butterworth cascade the topology is built from. Linkwitz-Riley
/// order N is the order-N/2 Butterworth twice (Linkwitz 1976). Returns 0 for a
/// non-positive frequency or rate, or an order outside 1..16 (no correction).
[[nodiscard]] double topologyPeakLagSamples(Topology topology, bool highPass, double crossoverHz,
                                            double sampleRate);

/// The candidate nearest `targetSeconds`, but only when it is clearly the
/// nearest: its distance is at most a QUARTER of the candidate spacing (the
/// smallest gap between neighbouring candidates). Why a quarter: the nearest
/// candidate is right only while the integer's error stays under HALF the
/// spacing, where the next candidate becomes equally near. A guard at half
/// would accept a margin of zero; at a quarter the chosen candidate is at least
/// three times nearer than the runner-up (d <= s/4 against >= 3s/4). With one
/// candidate there is no spacing to measure and nothing to confuse it with, so
/// it is returned. nullopt: no candidates, or the integer sits between them.
[[nodiscard]] std::optional<double> candidateWithinGuard(const std::vector<double>& candidatesSeconds,
                                                         double targetSeconds);

}  // namespace rta::measure
