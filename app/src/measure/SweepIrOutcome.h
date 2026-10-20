// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// SWEEP lane (docs/plans/2026-09-29-sweep-ir-lane-plan.md), tasks T3-T5: the
// plain-data result of one sweep analysis. It is its own header so that the
// message thread can name the result (the 2 Hz poll, `IrStore`,
// `traceFromSweepIr`) without pulling in `rta/ir/*` -- the deconvolver and
// its 2^21-point transforms belong to the worker thread, and a message-thread
// file that includes them is one `deconvolve()` call away from a UI stall.
// For that reason `SweepIr` below mirrors `rta::ir::Deconvolution` field for
// field instead of holding one; a later ALIGN caller copies the fields back
// (`AlignmentWizard::supplyImpulseResponse`, H5), which is a struct
// assignment, not a reason to share a header.
#pragma once

#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace rta::measure {

/// The raw capture a sweep is analysed from: the reference (loop) channel and
/// the measurement channel, equal length. Same two vectors as
/// `LocateCapture` (AnalysisThread.h:41), redeclared here because that header
/// includes JUCE and this one must not -- the caller copies two vectors
/// (about 5 MB, once per sweep).
struct SweepCapture {
    std::vector<float> reference;
    std::vector<float> measurement;
};

enum class SweepIrStatus {
    Ok,
    CaptureTooShort,     ///< shorter than the sweep, or the two channels differ in length
    ReferenceNotFound,   ///< no positive, prominent peak in the loop's deconvolution
    MeasurementNotFound, ///< the measurement channel deconvolves to silence
    TailTooShort,        ///< the capture ended before the required decay time
    ArrivalOutOfWindow,  ///< the measurement's arrival does not fit the spectrum window
    Failed,              ///< an exception from the DSP; `message` carries what()
};

/// The whole linear deconvolution of the measurement channel, with t = 0
/// relabelled to the reference (loop) peak. `samples` is untouched by that
/// relabelling -- only `originIndex` moves -- so everything below `originIndex`
/// is still negative time, and the harmonic packets are still there (record
/// `docs/dsp/2026-08-30-sweep-ir-l4a.md` decision 3).
struct SweepIr {
    std::vector<float> samples;
    std::size_t originIndex = 0;
    /// WHERE the direct sound arrived, in samples after t = 0 (`originIndex`):
    /// the same number as `SweepIrOutcome::delaySamples`, carried on the IR
    /// itself because the IR is what travels to ALIGN. `originIndex` is not an
    /// arrival -- two sweeps' origins differ by their pre-roll timing (F2).
    long arrivalSamples = 0;
    /// How long the capture ran after the sweep left the output, counted from
    /// the loop's arrival of the sweep START plus the sweep (H6 RT60-UI, F1).
    /// Past `originIndex + tailSamples` the deconvolution's overlap with the
    /// inverse filter shrinks into an artefact, not a room, so a decay
    /// analysis must stop there. Zero means "not stated" (a hand-built IR).
    std::size_t tailSamples = 0;
    double sampleRate = 0.0;
    /// The scalar already applied to `samples` (the loop's in-band mean).
    double normalisationGain = 1.0;
    double harmonicSpacingL = 0.0;
    double excitationLowHz = 0.0;
    double excitationHighHz = 0.0;
    double trustedLowHz = 0.0;
    double trustedHighHz = 0.0;
};

struct SweepIrOutcome {
    SweepIrStatus status = SweepIrStatus::Failed;
    /// Human-readable reason for every status except Ok, for the readout.
    std::string message;

    SweepIr ir;

    /// Where the sweep START reached the loop, in capture samples: the
    /// pre-roll plus the interface latency. Reported, never subtracted.
    std::size_t referenceArrivalSamples = 0;
    /// The measurement's IR peak relative to the loop's: positive when the
    /// microphone path is longer. This is the delay the readout shows.
    long delaySamples = 0;

    /// `bandFlatness` of the loop over the sweep's valid band: how far the
    /// loop itself departs from its own mean (record decision 4; -0.05..+0.22
    /// dB at the defaults). Shown as "loop a..b dB".
    double loopMinDb = 0.0;
    double loopMaxDb = 0.0;

    /// True when the measurement channel reached full scale. The linear IR
    /// survives a clipped ESS (its harmonics fall at negative time), so this
    /// is a warning, not a refusal (owner decision 9).
    bool measurementClipped = false;

    /// The `fftSize/2 + 1` complex bins of the N-point window, phase referenced
    /// to t = 0 (the loop peak), not to the window start. Complex rather than
    /// dB + radians so that a consumer is free to choose its own floor.
    double sampleRate = 0.0;
    std::size_t fftSize = 0;
    double binHz = 0.0;
    std::vector<std::complex<float>> bins;
};

}  // namespace rta::measure
