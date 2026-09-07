// SPDX-License-Identifier: AGPL-3.0-or-later
// Test-only helper: excess-phase fixtures for test_delay_policy.cpp's B6/B7
// rows (docs/plans/2026-09-07-L7-delay-impl-plan.md, record
// docs/dsp/2026-09-06-l7-auto-delay.md sec.7). Split out of
// test_delay_policy.cpp to keep that file under the project's 400-line cap
// (CLAUDE.md) -- this header does one job, generating filtered test signals
// from rta::dsp::Biquad, and is not itself a TEST_CASE file.
//
// The allpass and lowpass/highpass coefficient forms are the RBJ Audio EQ
// Cookbook's Q-parameterised biquads (the same source core/include/rta/eq/
// BiquadDesign.h cites) -- FilterSpec.h deliberately excludes an all-pass
// type from the SHIPPED EQ vocabulary (a placement solve has no use for a
// filter that cannot move magnitude), so these are re-derived here as plain
// rta::dsp::Biquad::Coeffs rather than added to that file's type.
#pragma once

#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"

#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace rta::test {

[[nodiscard]] inline rta::dsp::Biquad::Coeffs rbjAllpass(double fcHz, double q, double fs) {
    const double w0 = 2.0 * std::numbers::pi * fcHz / fs;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    return {(1.0 - alpha) / a0, -2.0 * cw / a0, (1.0 + alpha) / a0, -2.0 * cw / a0,
            (1.0 - alpha) / a0};
}

[[nodiscard]] inline rta::dsp::Biquad::Coeffs rbjLowpass(double fcHz, double q, double fs) {
    const double w0 = 2.0 * std::numbers::pi * fcHz / fs;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    const double b1 = 1.0 - cw;
    return {b1 / (2.0 * a0), b1 / a0, b1 / (2.0 * a0), -2.0 * cw / a0, (1.0 - alpha) / a0};
}

[[nodiscard]] inline rta::dsp::Biquad::Coeffs rbjHighpass(double fcHz, double q, double fs) {
    const double w0 = 2.0 * std::numbers::pi * fcHz / fs;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double cw = std::cos(w0);
    const double a0 = 1.0 + alpha;
    const double b1 = 1.0 + cw;
    return {b1 / (2.0 * a0), -b1 / a0, b1 / (2.0 * a0), -2.0 * cw / a0, (1.0 - alpha) / a0};
}

/// One RBJ section run twice (Butterworth Q = 1/sqrt(2)): a 4th-order
/// Linkwitz-Riley low/high pair. Summing the two outputs at the SAME
/// crossover reproduces the classic LR4 identity -- unity magnitude, but
/// with the crossover's own excess phase, which is exactly what B7 needs
/// (record sec.7: filter SHAPE does not move the verdict, its own in-band
/// group delay does, honestly).
[[nodiscard]] inline std::vector<float> lr4CrossoverSum(std::span<const float> in, double fcHz,
                                                          double fs) {
    constexpr double kButterworthQ = 0.7071067811865476;  // 1/sqrt(2)
    const auto lpCoeffs = rbjLowpass(fcHz, kButterworthQ, fs);
    const auto hpCoeffs = rbjHighpass(fcHz, kButterworthQ, fs);

    std::vector<float> out(in.size());
    rta::dsp::Biquad::State lp1{}, lp2{}, hp1{}, hp2{};
    for (std::size_t n = 0; n < in.size(); ++n) {
        double lp = rta::dsp::Biquad::processSample(lpCoeffs, lp1, in[n]);
        lp = rta::dsp::Biquad::processSample(lpCoeffs, lp2, lp);
        double hp = rta::dsp::Biquad::processSample(hpCoeffs, hp1, in[n]);
        hp = rta::dsp::Biquad::processSample(hpCoeffs, hp2, hp);
        out[n] = static_cast<float>(lp + hp);
    }
    return out;
}

[[nodiscard]] inline std::vector<float> applyBiquad(const rta::dsp::Biquad::Coeffs& coeffs,
                                                      std::span<const float> in) {
    std::vector<float> out(in.size());
    rta::dsp::Biquad::State state{};
    for (std::size_t n = 0; n < in.size(); ++n) {
        out[n] = static_cast<float>(rta::dsp::Biquad::processSample(coeffs, state, in[n]));
    }
    return out;
}

[[nodiscard]] inline std::vector<float> applyButterworthBandPass(std::span<const float> in,
                                                                   double lowerHz, double upperHz,
                                                                   double fs, int sections) {
    const auto design = rta::dsp::ButterworthDesign::bandPass(lowerHz, upperHz, fs, sections);
    rta::dsp::BiquadCascade cascade(design.sections);
    std::vector<float> out(in.size());
    cascade.process(in, out);
    return out;
}

}  // namespace rta::test
