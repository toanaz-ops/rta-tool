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

#include <atomic>
#include <cstddef>
#include <span>
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

/// One fit core's `decayTimes` made, restated so the pane can draw it. Core fits a
/// least-squares line to the curve between the first sample at or below `upperDb`
/// and the first at or below `lowerDb`; the line's slope is -60 / T exactly (that
/// is how T is read off it), so the line is rebuilt from T and the span's mean
/// point -- a least-squares line passes through (mean x, mean y) -- with no second
/// regression to disagree with core's.
struct Rt60FitSpan {
    bool valid = false;           ///< false when the figure was refused
    double upperDb = 0.0;         ///< 0 (EDT) or -5 (T20, T30): where the fit starts
    double lowerDb = 0.0;         ///< -10, -25 or -35: where it ends
    double startSec = 0.0;        ///< seconds after the arrival
    double endSec = 0.0;
    double slopeDbPerSec = 0.0;   ///< -60 / T
    double interceptDb = 0.0;     ///< the line's value at t = 0
    /// Largest |curve - line| inside the span. A straight Schroeder curve reads
    /// near zero; a comb's staircase reads half its step.
    double maxResidualDb = 0.0;
};

/// The Schroeder curve the figures were fitted to (the Lundeby-corrected
/// energy decay curve, dB re its start), decimated for drawing. Present only when
/// the curve was accepted.
struct Rt60Edc {
    std::vector<float> db;        ///< 0 dB at the arrival, falling
    double stepSeconds = 0.0;     ///< time between two entries
    Rt60FitSpan t30, t20, edt;

    [[nodiscard]] bool valid() const noexcept { return !db.empty(); }
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

    /// For the EDC plot (plan T10).
    Rt60Edc edc;
};

struct Rt60Result {
    Rt60BandSet bandSet = Rt60BandSet::Octave;
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

/// Analyses a SWEEP IR into the bands of `rt60Bands(set)` (octave by default).
/// `cancel`, when given and set, stops the analysis between bands: the result is
/// then incomplete and the caller (the worker) discards it.
///
/// The steps, per IR:
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
[[nodiscard]] Rt60Result analyseRt60(const SweepIr* ir, Rt60BandSet set = Rt60BandSet::Octave,
                                     const std::atomic<bool>* cancel = nullptr);

/// The same, on caller-chosen bands (a test's 40 Hz third, which no table row has).
/// `set` is only what the result reports itself as.
[[nodiscard]] Rt60Result analyseRt60Bands(const SweepIr* ir, std::span<const Rt60BandEdges> bands,
                                          Rt60BandSet set = Rt60BandSet::Octave,
                                          const std::atomic<bool>* cancel = nullptr);

/// One figure across several captures: core's `DecaySpread`, restated.
struct Rt60Spread {
    double seconds = 0.0;  ///< the median across the captures that produced a figure
    Rt60Refusal refusal = Rt60Refusal::NoDecayFound;
    /// Inter-quartile spread, percent of the median. Zero unless meaningful.
    double spreadPercent = 0.0;
    std::size_t captures = 0;  ///< how many captures contributed (refused ones are skipped)

    [[nodiscard]] bool has() const noexcept { return refusal == Rt60Refusal::None; }
    /// Three captures, not two: with two points the IQR is just the gap between
    /// them and reads as a precision figure it is not (core's `DecaySpread`).
    [[nodiscard]] bool spreadIsMeaningful() const noexcept { return captures >= 3; }
};

struct Rt60EnsembleBand {
    int nominalHz = 0;
    double lowHz = 0.0;
    double highHz = 0.0;
    /// `OutsideTrustedBand` when no capture was analysed in this band; else None.
    Rt60Refusal refusal = Rt60Refusal::None;
    Rt60Spread t30, t20, edt;
    /// B * T of the first capture whose band refused on it, else of the first
    /// capture: the number beside a BandwidthTimeTooSmall note.
    double bandwidthTime = 0.0;
};

struct Rt60EnsembleResult {
    Rt60BandSet bandSet = Rt60BandSet::Octave;
    Rt60Refusal refusal = Rt60Refusal::None;
    std::string message;
    std::size_t captureCount = 0;  ///< IRs handed in
    double sampleRate = 0.0;
    double tailSeconds = 0.0;      ///< the SHORTEST tail any capture held
    double trustedLowHz = 0.0;     ///< the tightest trusted band across the IRs
    double trustedHighHz = 0.0;
    double wallMs = 0.0;
    std::vector<Rt60EnsembleBand> bands;

    [[nodiscard]] bool analysed() const noexcept { return refusal == Rt60Refusal::None; }
};

/// Each IR through `analyseRt60`'s crop and band filters, then core's
/// `decayTimesAcross` per band: median across captures, IQR % at three or more.
/// Every IR must be its OWN capture -- the same IR three times reports a spread of
/// zero (core's contract; the caller dedups). Captures of different sample rates
/// are refused. Never throws.
[[nodiscard]] Rt60EnsembleResult analyseRt60Ensemble(std::span<const SweepIr* const> irs,
                                                     Rt60BandSet set = Rt60BandSet::Octave,
                                                     const std::atomic<bool>* cancel = nullptr);

}  // namespace rta::measure
