// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/ir/Deconvolver.h"

#include <cstddef>

namespace rta::ir {

enum class Sign { Negative = -1, Unknown = 0, Positive = 1 };

/// Why an answer was withheld.
///
/// `None` if and only if `sign != Unknown`. The two fields carry the same
/// information and a test asserts the equivalence in both directions, so neither
/// can drift into meaning something the other does not.
///
/// When more than one applies, the STRUCTURAL refusals win over `NoSignal`:
/// measuring louder cannot widen a band, and the band refusals are the ones with
/// somewhere to send the operator. No information is lost by the ordering --
/// every figure the caller might have gated on is in the result regardless.
enum class Refusal {
    None,
    NoSignal,          ///< nothing in the window, or below `minConfidenceDb`
    NoNoiseEstimate,   ///< `harmonicSpacingL` is zero, so the noise window has
                       ///< no derivable left edge. A legal deconvolution: see
                       ///< the note on throwing, below.
    BandTooLow,        ///< the response's low edge is above `gateLowHz` -- a horn
    BandTooHigh,       ///< its high edge is below `gateHighHz` -- a subwoofer

    /// The SWEEP, not the loudspeaker, is why there is no answer: its trusted
    /// band does not reach the gate thresholds, so no measurement made with it
    /// could establish a verdict.
    ///
    /// This exists because without it the failure blames the wrong thing. A
    /// sweep starting at 30 Hz with a two-octave fade-in is trustworthy only
    /// from 120 Hz up; every loudspeaker measured with it then reports a low
    /// edge at or above 120, fails a 100 Hz gate, and gets a refusal reading
    /// "low edge 120 Hz, gate 100 Hz" -- which sends the operator to inspect a
    /// perfectly good box. The message has to point at the sweep.
    SweepBandInsufficient,

    /// The capture is too short to size the analysis window for a low edge this
    /// low, so the band edges cannot be measured at all.
    ///
    /// Refusing here rather than falling back is the whole point. The window
    /// needs about ten cycles of the low edge -- roughly 0.21 s for a 50-71 Hz
    /// subwoofer -- and decision 9 sizes the capture gap at 1.5x RT60, so a dry
    /// room can leave less than that. An estimator that quietly kept its
    /// first-pass 50 ms reading would hand back the 46.9 Hz - 18270 Hz fiction
    /// that defect 1 exists to kill, and the gate would admit a subwoofer on the
    /// strength of it. A silent fallback is how a fixed defect comes back.
    CaptureTooShort,
};

struct PolarityConfig {
    /// Fraction of the window peak the first arrival must reach.
    ///
    /// **0.5, and never 0.2.** A survey over IIR filters alone favours 0.2, and
    /// an earlier draft of this class adopted it. Linear-phase FIR refutes it:
    /// its symmetric pre-ring puts an opposite-signed lobe above a 20% threshold
    /// BEFORE the main lobe arrives, so 0.2 answers backwards on both drive
    /// polarities. Linear-phase presets are ordinary in line-array processing.
    /// Exposed so a test can drive it, not so it can be tuned.
    double arrivalFraction = 0.5;

    double searchSeconds = 0.05;
    double minConfidenceDb = 20.0;

    /// The gate: answer only when the response reaches BOTH ends of the band.
    ///
    /// A width in octaves cannot express this. A band-pass spanning
    /// 1000-16000 Hz is four octaves wide and answers backwards; it is a horn.
    /// The sign of a first arrival needs a low end to give the wavefront a
    /// direction and a high end to give it a front, and a system missing either
    /// does not have an arrival whose sign is a property of its wiring.
    ///
    /// **Observed, not derived.** Zero wrong answers were measured at these
    /// values across `butter`/`cheby1`/`ellip`/`bessel` at orders 2 to 16 and
    /// both FIR phase types, on two independently built grids -- but they are
    /// the values that survived a survey, not a boundary anyone can derive. They
    /// are exposed for the same reason `arrivalFraction` is. Moving them
    /// invalidates that survey: re-run `tools/probe_polarity_*.py`, do not edit
    /// the expectations.
    double gateLowHz = 100.0;
    double gateHighHz = 8000.0;
};

struct PolarityResult {
    Sign sign = Sign::Unknown;
    Refusal refusal = Refusal::NoSignal;

    std::size_t arrivalIndex = 0;
    double confidenceDb = 0.0;

    /// The measured -10 dB edges of the arrival, in hertz, clamped to the band
    /// the excitation covered. Reported whether or not the gate passed, so a
    /// refusal can say "low edge 110.6 Hz, gate 100 Hz" instead of just "no".
    double lowEdgeHz = 0.0;
    double highEdgeHz = 0.0;

    /// |largest excursion opposite the verdict| / |window peak|.
    ///
    /// **May exceed 1**, and nothing clamps it. Above 1 means the largest thing
    /// in the window disagrees with the answer, which is exactly when a human
    /// should look closer -- it is the signature of a multi-way system whose
    /// sections arrive separately. Displayed, never gated on: as a gate it
    /// refused 74-88% of loudspeakers that answer correctly.
    double margin = 0.0;
};

/// The sign of the first arrival, or a refusal that says which way to go.
///
/// ## Three places where this promise stops
///
/// These are not caveats to be trimmed. Each is a measured limit.
///
/// 1. **A multi-way box with a section inverted by design is ill-posed for any
///    checker that has only the measurement to read.** A two-way with a
///    3rd-order crossover and an inverted tweeter reads negative, and that
///    reading is *true*: the first arriving wavefront is the tweeter and it is
///    inverted. Only the interpretation "this box is wired backwards" is wrong,
///    and the information needed to reject it -- the designer's intent -- is not
///    in the signal. A tool carrying per-model design documentation could
///    answer, because it would have a second source; none of the surveyed
///    analysers does, and neither does this one. `margin` runs 0.85-0.98 for
///    that class against a median of 0.46 for a single-section box, which is
///    the honest thing for it to flag.
/// 2. **Minimum-phase FIR was validated at one construction**, not a swept
///    family.
/// 3. **The band-edge estimator's window is keyed to the low edge** and assumes
///    the response rings at its band edges. A high-Q resonance in the middle of
///    the band is unsurveyed.
///
/// Throws `std::invalid_argument` only for a non-positive sample rate or an
/// `arrivalFraction` outside (0, 1]. Notably it does NOT throw when
/// `harmonicSpacingL` is zero: `deconvolve` accepts that and never validates it,
/// so throwing would kill polarity on a valid input from its own pipeline.
/// It refuses with `NoNoiseEstimate` instead, and reports `confidenceDb = 0.0`
/// rather than a sentinel -- an earlier design returned 200.0 dB there, which
/// made a test pass because of a constant rather than a measurement.
[[nodiscard]] PolarityResult findPolarity(const Deconvolution& source,
                                          const PolarityConfig& config);

}  // namespace rta::ir
