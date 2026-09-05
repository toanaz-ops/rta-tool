// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
//
// Aggregating decay figures across SEVERAL captures, split from Decay.h when
// that file passed the project's 400-line cap. The seam is the one that made it
// long: Decay.h answers "what does this one capture say", this file answers
// "what do several captures of the same room agree on, and how much do they
// disagree".
#pragma once

#include "rta/ir/Decay.h"

#include <cstddef>
#include <span>

namespace rta::ir {

/// One figure gathered across SEVERAL independent captures of the same room.
///
/// ## Why this type exists, and why there is no per-reading confidence score
///
/// EDT is noisy in a way no algorithm removes. Measured on a two-slope room,
/// 300 realisations per band, the inter-quartile spread of a SINGLE reading is
/// 24 % at octave 1000 Hz and 118 % at third-octave 63 Hz -- and that is after
/// truncation, at bandwidths where the bias is already gone.
///
/// The obvious answer -- ship a confidence score beside each reading -- was
/// tried and **measured to be worthless**. Two candidates with real precedent:
/// the rms residual of the figure's own fit (REW ships this as "model fit
/// error"), and curvature `100*|T30/T20 - 1|` (a standard measure of exactly
/// the two-rate condition that makes EDT untrustworthy). Correlation with the
/// actual error, across 300 realisations:
///
///     band              |corr| residual    |corr| curvature
///     1/3-oct 63 Hz          0.16               0.15
///     1/3-oct 250 Hz         0.13               0.08
///     octave 125 Hz          0.09               0.10
///     octave 1000 Hz         0.02               0.09
///
/// Both are noise. **A confidence score that does not correlate with the error
/// is worse than none**, because it reassures rather than informs.
///
/// What does work is the thing physics allows: more captures. Averaging N
/// independent captures shrinks the spread as 1/sqrt(N), measured --
/// third-octave 63 Hz goes 118.2 / 86.2 / 62.8 / 41.2 / 27.9 % at
/// N = 1 / 2 / 4 / 8 / 16.
///
/// So the honest confidence figure is not a score computed from one reading. It
/// is **the spread across the captures actually taken**, reported beside the
/// value, with the count. An operator who wants a tighter EDT takes more sweeps;
/// there is no other lever, and pretending otherwise is what a per-reading score
/// would have done.
struct DecaySpread {
    /// Median across captures. Median rather than mean: one capture spoiled by
    /// a door closing should not drag the answer, and the operator is asking
    /// what the room does, not what the room and one accident average to.
    DecayTime value;

    /// Inter-quartile spread across captures, as a percentage of `value`.
    ///
    /// Meaningless below three captures and reported as zero there -- with two
    /// points an IQR is the gap between them, which reads as precision when it
    /// is nothing of the kind.
    double spreadPercent = 0.0;

    std::size_t captures = 0;

    /// True only when there are enough captures for `spreadPercent` to mean
    /// something. A caller showing the spread must check this; a caller showing
    /// only the value need not.
    [[nodiscard]] bool spreadIsMeaningful() const noexcept { return captures >= 3; }
};

struct DecayTimesAcrossCaptures {
    DecaySpread edt, t20, t30;
};

/// EDT, T20 and T30 across several independent captures of the same room.
///
/// Each curve must come from its own capture -- re-running the same recording
/// through the analysis N times reports a spread of zero and would turn this
/// type into the false reassurance it exists to avoid. Nothing here can check
/// that; it is the caller's contract.
///
/// Curves that refused are skipped rather than counted, so `captures` is the
/// number that actually contributed. A figure with fewer than three surviving
/// captures still reports its median, with `spreadIsMeaningful()` false.
[[nodiscard]] DecayTimesAcrossCaptures decayTimesAcross(
    std::span<const EnergyDecayCurve> curves);

}  // namespace rta::ir
