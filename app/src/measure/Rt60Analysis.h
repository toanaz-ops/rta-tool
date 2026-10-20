// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// RT60-UI lane (docs/plans/2026-10-20-rt60-ui-lane-plan.md) task T1, with the
// record `docs/dsp/2026-08-30-ir-decay-l4b.md`. This is the app half of what
// core's decay code (`rta::ir::bandFilterZeroPhase`, `energyDecayCurve`,
// `decayTimes`) cannot know on its own: where in a SWEEP IR the room is.
//
// This header deliberately does NOT include `rta/ir/*`: the message thread
// names the result (the pane, the controller), and the decay code's filters
// and Lundeby iterations belong behind `.cpp` (see SweepIrOutcome.h for the
// same line drawn for the deconvolver). `Rt60Refusal` therefore MIRRORS
// `rta::ir::DecayRefusal` enumerator for enumerator, and Rt60Analysis.cpp
// pins the correspondence with static_asserts rather than a runtime table.
//
// Message-thread cost, measured by test_rt60_analysis.cpp (it prints the
// figure): one IR, seven bands, about 150k samples.
#pragma once

#include "measure/Rt60Bands.h"
#include "measure/SweepIrOutcome.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rta::measure {

/// Why a figure, a band or the whole analysis was withheld.
///
/// The first seven are `rta::ir::DecayRefusal`, same names and same order
/// (static_assert'ed in the .cpp). The last three are the app's own: a band
/// the sweep never established, no IR to analyse, and an IR that does not say
/// how long its capture ran.
enum class Rt60Refusal {
    None,
    BandwidthTimeTooSmall,
    NoDecayFound,
    RangeTooSmall,
    NoNoiseEstimate,
    NoLeadIn,
    OutsideTrustedBand,
    NoIr,
    NoTail,
};

struct Rt60Figure {
    double seconds = 0.0;
    Rt60Refusal refusal = Rt60Refusal::NoDecayFound;

    [[nodiscard]] bool has() const noexcept { return refusal == Rt60Refusal::None; }
};

struct Rt60Band {
    int nominalHz = 0;
    double lowHz = 0.0;
    double highHz = 0.0;

    /// `None` when the band was analysed and the curve was accepted. Otherwise
    /// the reason the whole band was refused: `OutsideTrustedBand`, or the
    /// curve's own (`NoNoiseEstimate`, `BandwidthTimeTooSmall`, ...), which every
    /// figure below then repeats.
    Rt60Refusal refusal = Rt60Refusal::NoDecayFound;

    Rt60Figure t30;  ///< -5 to -35 dB
    Rt60Figure t20;  ///< -5 to -25 dB
    Rt60Figure edt;  ///< 0 to -10 dB. No validity check: record section 9.

    /// How far the Lundeby-corrected curve fell: -db.back(), the exact quantity
    /// core's T30/T20 gates read. Present only when the curve was accepted
    /// (core clears the curve when it refuses the band on B*T).
    bool hasRange = false;
    double rangeDb = 0.0;

    /// B * T where T is the decay the gate read (T30, else T20), so the number
    /// beside a BandwidthTimeTooSmall refusal is the number core compared with
    /// its bar. Zero when no decay was fitted.
    double bandwidthTime = 0.0;

    /// Where Lundeby judged the decay met the noise floor, seconds after the
    /// arrival. Zero when it did not.
    double crossingSec = 0.0;
};

struct Rt60Result {
    /// `None` when the IR was analysed (individual bands may still refuse);
    /// otherwise the whole analysis was withheld and `bands` is empty.
    Rt60Refusal refusal = Rt60Refusal::None;
    /// An exception's text, for the one case no refusal names (a DSP failure).
    std::string message;

    double sampleRate = 0.0;
    double tailSeconds = 0.0;
    double trustedLowHz = 0.0;
    double trustedHighHz = 0.0;
    long arrivalSamples = 0;
    /// The lead-in kept before the arrival, and the samples handed to the band
    /// filters: [arrival - leadIn, origin + tail).
    std::size_t leadInSamples = 0;
    std::size_t analysedSamples = 0;
    /// Wall time of the whole analysis, for the pane and the PR body.
    double wallMs = 0.0;

    std::vector<Rt60Band> bands;

    [[nodiscard]] bool analysed() const noexcept { return refusal == Rt60Refusal::None; }
};

/// The lead-in a zero-phase filter needs before the arrival, in samples:
/// `min(200 ms, half the distance from t = 0 to the 2nd harmonic packet)`.
///
/// The packet of harmonic order k sits at -L*ln(k) after t = 0 (record L4a
/// decision 3), the nearest being k = 2. The lead-in exists so the band
/// filters have somewhere to put their pre-ring (record L4b section 5a); it
/// must stop short of that packet or the filters ring it into the decay.
/// Half the distance leaves the packet's own skirt outside the window. 200 ms
/// is far longer than any octave band here needs and is the cap. Rounded DOWN:
/// the bound is a "no further than", so rounding up could cross it.
///
/// `harmonicSpacingL` <= 0 means the IR does not state it; the cap applies.
[[nodiscard]] std::size_t rt60LeadInSamples(double sampleRate, double harmonicSpacingL);

/// Analyses a SWEEP IR into the octave bands of `rt60OctaveBands()`.
///
/// 1. Crop (plan F1). The window is [arrival - leadIn, origin + tail): past
///    `origin + tailSamples` the linear deconvolution overlaps the inverse filter
///    less and less, and what remains is a decaying low-pass artefact with an
///    apparent RT60 near 10 s for the shipped sweep, not a room.
/// 2. Per band: refuse outside the sweep's trusted band (plan F3), else
///    zero-phase octave filter, Schroeder integral with Lundeby truncation, and
///    EDT / T20 / T30 with the B*T gate -- all core's, unchanged.
///
/// A null IR, an empty one or one with no sample rate is `NoIr`; one that does
/// not say how long its capture ran is `NoTail`; an arrival closer to the start
/// than the lead-in is `NoLeadIn`. Never throws.
[[nodiscard]] Rt60Result analyseRt60(const SweepIr* ir);

}  // namespace rta::measure
