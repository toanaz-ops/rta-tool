// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/ir/Deconvolver.h"

#include <complex>
#include <cstddef>
#include <vector>

namespace rta::ir {

struct IrSpectrumConfig {
    /// The bottom of the sweep's valid band, `f_lo = f1 * exp(fadeInSec/L)`.
    /// The transform's lead-in is two cycles of it -- see `analyseSpectrum`.
    double lowBandEdgeHz = 0.0;
};

struct Spectrum {
    /// `size/2 + 1` complex bins. Phase is in **radians**, referenced to
    /// `t = 0`, not to the start of the analysis window.
    std::vector<std::complex<float>> bins;

    /// Hertz per bin: `sampleRate / transformSize`.
    double binHz = 0.0;

    /// How many samples before `originIndex` the window began. Reported so a
    /// caller can check the linear-phase term was removed rather than assume it.
    std::size_t leadInSamples = 0;
};

/// Frequency response of the causal part of a deconvolution.
///
/// ## The window starts BEFORE t = 0, and that is not a detail
///
/// The analysis pulse `sweep (x) inverseFilter` is symmetric about its peak --
/// measured to 3.0e-5 of peak amplitude over +-2 ms. So the deconvolved signal
/// `h (x) p` carries real content on **both** sides of `originIndex`, and a
/// transform beginning exactly there slices the pulse down the middle. Measured
/// against two known filters over 100 Hz - 10 kHz, the error from that slice is
/// up to **+23.5 dB** on a steep low-frequency skirt -- while a pure-delay round
/// trip through the same code stays exact to the sample and to four decimal
/// places of gain. A test that inspects only the peak cannot see this.
///
/// The lead-in is **two cycles of `lowBandEdgeHz`**, not a chosen number of
/// milliseconds: the pulse's width is set by the bottom of the band the sweep
/// can speak for. Measured, the error is already 0.00-0.01 dB at one cycle for
/// two different fade widths, so two is one doubling past where it vanishes.
/// It is the same reasoning that gives `Sweep` its `2/startHz` fade floor --
/// both ask how long the slowest thing in the signal takes.
///
/// ## Radians, and only one place converts
///
/// Phase stays in radians here. `app/` converts to degrees exactly once, in
/// `Analyser::pushPair`. A second conversion point in `core/` is how a factor
/// of 57.3 gets applied twice.
///
/// Throws `std::invalid_argument` if the deconvolution is empty, if its sample
/// rate is not positive, if `lowBandEdgeHz` is not positive, or if the
/// deconvolution holds less pre-arrival room than the lead-in needs.
[[nodiscard]] Spectrum analyseSpectrum(const Deconvolution& source,
                                       const IrSpectrumConfig& config);

/// Lead-in the given band edge implies, in samples: `round(2*fs/lowBandEdgeHz)`.
/// Exposed because a window nobody can measure is a window nobody can check.
[[nodiscard]] std::size_t leadInSamplesFor(double sampleRate, double lowBandEdgeHz);

}  // namespace rta::ir
