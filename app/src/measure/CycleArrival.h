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
/// three times nearer than the runner-up (d <= s/4 against >= 3s/4).
///
/// With ONE candidate there is no gap to measure, but there is a bound on it
/// (H5 closeout D6): the fit searched tau in [-R, R], R = `searchRangeSeconds`,
/// and found a single local maximum at c. Its neighbours c + s and c - s were
/// not found, so both lie outside the range: c + s > R and c - s < -R, i.e.
/// s > R + |c|. The guard then uses that LOWER bound as the spacing, which is
/// never looser than the real quarter-spacing rule (d <= (R + |c|) / 4 <= s / 4).
/// Before this, one candidate accepted any integer, so an IR integer 50 ms from
/// the fit's only candidate was "chosen". A non-positive range gives no bound:
/// nullopt (ask). nullopt also: no candidates, or the integer sits between them.
[[nodiscard]] std::optional<double> candidateWithinGuard(const std::vector<double>& candidatesSeconds,
                                                         double targetSeconds,
                                                         double searchRangeSeconds);

/// The widest disagreement between the operator's SEED corner and the measured
/// acoustic crossing for which the IR integer is still trusted (H5 closeout
/// D9): 1/3 octave. The topology peak lag is evaluated at the seed, but the
/// arrivals were produced at the true corner. A low-pass lag is k / fc, with
/// k = 0.94 for LR8, the highest-lag shipped topology (`test_cycle_arrival.cpp`),
/// so a seed at f_s against a true corner f_t mis-corrects the integer by
/// (k / f_t) * |f_t / f_s - 1|. The guard accepts an error up to a quarter of
/// the candidate spacing, about 1 / (4 f_t) (spacing about 1 / f_t for the
/// shipped +-1 octave window; 10.8 ms at 100 Hz). Equating: |f_t / f_s - 1| <=
/// 1 / (4 k) = 0.27 for LR8 (0.54 for LR4), and 2^(1/3) - 1 = 0.26 is the widest
/// octave fraction inside the LR8 figure. Past that, one notch of cycle is a
/// wrong integer the guard cannot see: a verifier found SEED 60 on a 200 Hz LR4
/// corner passing at 5.4 ms and SEED 80 / 100 / 60 on 150 / 200 / 120 Hz LR8.
inline constexpr double kSeedCrossingToleranceOctaves = 1.0 / 3.0;

/// True when the seed and the crossing are within `kSeedCrossingToleranceOctaves`.
/// A non-positive frequency on either side is "do not know": reported as agreeing,
/// because there is nothing to compare (no seed means the lag uses the crossing).
[[nodiscard]] bool seedAgreesWithCrossing(double seedHz, double crossingHz) noexcept;

}  // namespace rta::measure
