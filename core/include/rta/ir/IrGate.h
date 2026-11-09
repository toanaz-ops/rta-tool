// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/ir/Deconvolver.h"
#include "rta/ir/IrSpectrum.h"

#include <cstddef>
#include <span>
#include <string>

namespace rta::ir {

/// A time gate on an impulse response, in milliseconds relative to the
/// ARRIVAL of the direct sound (record docs/dsp/2026-11-09-l4c-ir-gate.md
/// sec. 5). Negative `startMs` is before the arrival.
///
/// ## The window, sample by sample
///
/// With `fs` the sample rate, every length is rounded to the nearest sample
/// (`llround`, ties away from zero):
///
///   S  = round(startMs*fs/1000)        E  = round(endMs*fs/1000)
///   N  = E - S                         the gate covers IR index
///                                      `arrival + S + n`, n = 0 .. N-1
///   Fi = round(fadeInMs*fs/1000)       Fo = round(fadeOutFraction*N)
///
///   w[n] = 0.5*(1 - cos(pi*n/Fi))            0 <= n < Fi        (rises 0 -> 1)
///   w[n] = 1                                 Fi <= n < N-Fo
///   w[n] = 0.5*(1 + cos(pi*(n-(N-Fo))/Fo))   N-Fo <= n < N      (falls 1 -> 0)
///
/// Both ramps are HALF Hann windows: the flat top is the point of a gate, and a
/// full Hann over the whole length would taper the direct sound itself. Index
/// N would be exactly 0, so the last kept sample is a hair above it.
struct GateSpec {
    double startMs = -1.0;
    double endMs = 10.0;
    double fadeInMs = 0.5;
    /// Share of the gate length spent on the closing ramp.
    double fadeOutFraction = 0.25;
    /// Where the arrival sits, in samples after the source's `originIndex`.
    /// Signed: an arrival before t = 0 is legal (`SweepIr::arrivalSamples`).
    long arrivalSamples = 0;
    /// Transform size of the returned spectrum, a power of two. It sets the bin
    /// spacing of whatever trace is built from the result: a gate of 11 ms is
    /// 528 samples, and transforming just those would leave 47 Hz bins.
    std::size_t transformSize = 16384;
};

enum class GateRefusal {
    None,
    EmptyIr,               ///< value: the sample count (0)
    BadSampleRate,         ///< value: the sample rate
    StartNotBeforeEnd,     ///< value: the gate length in ms (<= 0)
    GateShorterThanFades,  ///< value: the gate length in ms
    StartBeforeIr,         ///< value: the requested startMs
    EndPastIr,             ///< value: the requested endMs
};

struct GateResult {
    GateRefusal refusal = GateRefusal::None;
    /// The offending value, as tabulated on `GateRefusal`.
    double refusalValue = 0.0;
    /// Reader-facing sentence carrying the value and the limit it broke.
    std::string message;

    /// Phase referenced to the ARRIVAL (t = 0 of the gated IR is the direct
    /// sound), through `analyseSpectrum`'s own lead-in rotation.
    Spectrum spectrum;

    /// Lowest frequency the gate can speak for: `1 / (N - Fo/2)` seconds. The
    /// closing ramp only half-counts because a sample inside it carries less
    /// than full weight; the opening ramp ends before the direct sound, so it
    /// shortens nothing. Below this the response is gate-limited, not measured.
    double fMinHz = 0.0;

    /// The realised gate, in samples (after rounding), for readouts and tests.
    long startSamples = 0;
    long endSamples = 0;
    std::size_t fadeInSamples = 0;
    std::size_t fadeOutSamples = 0;

    [[nodiscard]] bool ok() const noexcept { return refusal == GateRefusal::None; }
};

/// Gates `samples` (index `originIndex` is t = 0, `spec.arrivalSamples` after it
/// is the direct sound) and returns the gated frequency response.
///
/// Refuses -- never clamps, never moves a marker for the caller -- when the gate
/// is empty or inverted, shorter than its own ramps, starts before the first
/// sample of the IR or ends after the last. This overload takes a span so a
/// caller holding a multi-megabyte IR does not copy it per drag.
[[nodiscard]] GateResult gateSpectrum(std::span<const float> samples, std::size_t originIndex,
                                      double sampleRate, const GateSpec& spec);

[[nodiscard]] GateResult gateSpectrum(const Deconvolution& source, const GateSpec& spec);

}  // namespace rta::ir
