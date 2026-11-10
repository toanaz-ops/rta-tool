// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared fixture for test_ir_polarity.cpp and test_ir_polarity_measured.cpp: the sweep
// configuration every figure in decision 6b was measured with, and the
// sweep -> Butterworth band-pass -> deconvolve chain. Moved out of the test file whole
// (no line changed) so a second file can use it under the 400-line cap.
#pragma once

#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/gen/Sweep.h"
#include "rta/ir/Deconvolver.h"
#include "rta/ir/Polarity.h"

#include <cstddef>
#include <vector>

namespace polarity_fixture {

using rta::gen::Sweep;

inline constexpr double kSampleRate = 48000.0;

/// 20 Hz to 20 kHz over two seconds: the configuration every figure in decision
/// 6b was measured with. Changing it invalidates the gate constants, which are
/// observed rather than derived -- so if this fixture must change, the survey in
/// tools/probe_polarity_*.py has to be re-run, not the expectations edited.
inline Sweep::Config testConfig() {
    Sweep::Config cfg;
    cfg.sampleRate  = kSampleRate;
    cfg.startHz     = 20.0;
    cfg.endHz       = 20000.0;
    cfg.durationSec = 2.0;
    return cfg;
}

/// Drive the sweep through a Butterworth band-pass and deconvolve.
///
/// `sections` is the SOS-section count, which is the same number SciPy calls the
/// order -- so `sections = 8` here is `scipy.signal.butter(8, ...)` there, and
/// the survey tables transfer directly.
inline rta::ir::Deconvolution measureThrough(double lowHz, double highHz, int sections,
                                      float drive) {
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);

    const auto design =
        rta::dsp::ButterworthDesign::bandPass(lowHz, highHz, kSampleRate, sections);
    rta::dsp::BiquadCascade cascade(design.sections);

    std::vector<float> driven(excitation.size());
    for (std::size_t i = 0; i < excitation.size(); ++i)
        driven[i] = static_cast<float>(
            cascade.processSample(static_cast<double>(drive) * excitation[i]));

    rta::ir::DeconvolverConfig cfg;
    cfg.sampleRate       = kSampleRate;
    cfg.harmonicSpacingL = sweep.lengthConstantL();
    // The clamp is the RAW excited band. Clamping to the trusted band instead
    // makes any system below it unreadable -- measured, a 50-71 Hz subwoofer
    // then reports a low edge of 82 Hz, describing a different loudspeaker.
    // The trusted band is carried separately, and is used only to decide
    // whether a sweep like this one can answer at all.
    cfg.excitationLowHz  = testConfig().startHz;
    cfg.excitationHighHz = testConfig().endHz;
    cfg.trustedLowHz     = sweep.validBandLowHz();
    cfg.trustedHighHz    = sweep.validBandHighHz();

    return rta::ir::deconvolve(driven, sweep.buildInverseFilter(), cfg);
}

inline rta::ir::PolarityResult readPolarity(double lowHz, double highHz, int sections,
                                     float drive) {
    return rta::ir::findPolarity(measureThrough(lowHz, highHz, sections, drive), {});
}

}  // namespace polarity_fixture
