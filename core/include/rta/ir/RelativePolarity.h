// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/ir/Deconvolver.h"
#include "rta/ir/Polarity.h"

#include <cstddef>

namespace rta::ir {

/// WHY THIS IS IN rta::ir AND NOT rta::dsp (ALIGN-R3). The record files it
/// under core/ (rta::dsp). Its inputs are two rta::ir::Deconvolution and its
/// window is L4a's arrival window, so placing it in rta::dsp would make dsp
/// include rta/ir/Deconvolver.h and invert a dependency direction that holds
/// everywhere else in this codebase. It lives beside Polarity.h and reuses
/// that header's Sign and Refusal rather than inventing a second spelling.
struct RelativePolarityConfig {
    /// The SAME arrival window L4a uses. `searchSeconds` lives on
    /// PolarityConfig (Polarity.h:68), not on Deconvolution -- ALIGN-R4 --
    /// so the default is cited from there rather than re-chosen here.
    double searchSeconds = 0.05;
};

/// rho = |r(l*)| / sqrt(E_a E_b), in [0, 1] by Cauchy-Schwarz, and the sign of
/// r(l*).
///
///     r(l) = sum_{n in W} a[n] b[n+l],  E_a = sum_W a^2,  E_b = sum_W b^2
///     l* = argmax_l |r(l)|
///
/// Both signals are taken as ZERO outside their window, which is what makes
/// the Cauchy-Schwarz bound exact at every lag and not only at zero: a shifted
/// window can lose energy, never gain it.
///
/// ## NO VERDICT, NO THRESHOLD
///
/// Record Sec.8 forbids shipping a number until two independent grids agree
/// (task F), and memory/a-threshold-read-off-a-grid-is-that-grids-floor.md is
/// the reason: three thresholds in lane L4a, three sessions, each the floor of
/// its own grid. rho is bounded -- that is the one property those three
/// lacked -- but a bound is not a boundary. Until the two surveys agree, rho
/// is a figure the UI shows and never a gate it applies.
///
/// ## What this may and may not answer (record Sec.7's table)
///
/// It answers "same system, before vs after" and "two units of one model".
/// It does NOT answer "LP side vs HP side across a crossover": across a BW2 or
/// LR2 the two IRs are 180 degrees apart by design, so this reads NEGATIVE on
/// correct wiring with a high rho, confidently wrong. Across an odd order the
/// aligned-lag correlation is cos 90 = 0 and the sign is whichever side the
/// peak fell on. Both failures are locked as fixtures in
/// core/tests/test_relative_polarity.cpp, so a session that "fixes" either one
/// goes red and reads why.
///
/// ## Cost
///
/// O(W^2) in the window length, computed directly rather than through an FFT:
/// an arrival window is a few thousand samples and a direct sum is obviously
/// correct, which matters more here than speed. Do not hand it a ten-second
/// capture as the window.
struct RelativePolarity {
    double rho = 0.0;
    Sign sign = Sign::Unknown;

    /// The lag of the peak, in samples, relative to the two origins. Leaving
    /// zero on a pair that should be aligned is the observable that says the
    /// peak has stopped tracking the thing the caller thinks it measures
    /// (probe 2026-09-15 Sec.5).
    std::ptrdiff_t lag = 0;

    /// L4a's enum, reused. `None` if and only if `sign != Unknown`.
    Refusal refusal = Refusal::NoSignal;
};

/// Throws std::invalid_argument for a non-positive or mismatched sample rate,
/// or a non-positive searchSeconds -- there is no window to take, and a
/// silent fallback to some default window is how a fixed defect comes back.
[[nodiscard]] RelativePolarity relativePolarity(const Deconvolution& a, const Deconvolution& b,
                                                const RelativePolarityConfig& config);

}  // namespace rta::ir
