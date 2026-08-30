// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L4a: frequency response of a deconvolved impulse response.
// Decisions 7 and 9 of docs/dsp/2026-08-30-sweep-ir-l4a.md.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/gen/Sweep.h"
#include "rta/ir/Deconvolver.h"
#include "rta/ir/IrSpectrum.h"

#include <cmath>
#include <complex>
#include <cstddef>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rta::gen::Sweep;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kStartHz    = 100.0;
constexpr double kEndHz      = 10000.0;
constexpr double kDuration   = 2.0;
constexpr double kPi         = 3.14159265358979323846;

Sweep::Config baseConfig() {
    Sweep::Config cfg;
    cfg.sampleRate  = kSampleRate;
    cfg.startHz     = kStartHz;
    cfg.endHz       = kEndHz;
    cfg.durationSec = kDuration;
    return cfg;
}

/// Read from the Sweep rather than recomputed. Novak synchronisation rounds
/// `f1*L` to a whole number, so `T/ln(f2/f1)` is the REQUESTED L and not the one
/// the sweep uses -- a helper that recomputes it disagrees with the object it is
/// testing, silently, by about 1%.
double lengthConstant() { return Sweep(baseConfig()).lengthConstantL(); }

Sweep::Config wideFadeConfig() {
    auto cfg = baseConfig();
    cfg.fadeInSec   = 2.0 * std::log(2.0) * lengthConstant();   // two octaves
    return cfg;
}

/// f_lo = f1 * exp(fadeInSec/L). With the fade two octaves wide this collapses
/// to f1 * 2^2 = 400 Hz -- the lower band edge IS the fade width.
double validBandLowHz(const Sweep::Config& cfg) {
    const double fadeSec = static_cast<double>(Sweep(cfg).fadeInSamples()) / cfg.sampleRate;
    return cfg.startHz * std::exp(fadeSec / lengthConstant());
}

std::vector<float> renderSweep(const Sweep::Config& cfg) {
    Sweep sweep(cfg);
    std::vector<float> out(sweep.lengthSamples());
    sweep.process(out);
    return out;
}

rta::ir::DeconvolverConfig plainConfig() {
    return { kSampleRate, lengthConstant(), 1.0 };
}

/// |H(e^jw)| of a cascade, evaluated from the coefficients themselves, so the
/// expectation is the filter's own closed form rather than a number a previous
/// run printed.
double cascadeMagnitude(std::span<const rta::dsp::Biquad::Coeffs> sections, double hz) {
    const double omega = 2.0 * kPi * hz / kSampleRate;
    const std::complex<double> z1 = std::polar(1.0, -omega);
    const std::complex<double> z2 = z1 * z1;
    double magnitude = 1.0;
    for (const auto& c : sections) {
        const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
        const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;
        magnitude *= std::abs(num / den);
    }
    return magnitude;
}

}  // namespace

TEST_CASE("The window leads t=0 by two cycles of the low band edge", "[ir][spectrum]") {
    const auto cfg = wideFadeConfig();
    const auto excitation = renderSweep(cfg);
    const auto inverse = Sweep(cfg).buildInverseFilter();
    const auto reference = rta::ir::deconvolve(excitation, inverse, plainConfig());

    const double lowHz = validBandLowHz(cfg);
    CHECK_THAT(lowHz, WithinRel(400.0, 1.0e-3));

    // 2 / 400 Hz = 5.00 ms = 240 samples at 48 kHz. Derived from the Config the
    // caller already supplied, not a chosen number of milliseconds.
    CHECK(rta::ir::leadInSamplesFor(kSampleRate, lowHz) == 240u);

    const auto spectrum = rta::ir::analyseSpectrum(reference, { lowHz });
    CHECK(spectrum.leadInSamples == 240u);
    CHECK(spectrum.bins.size() > 1u);
    CHECK_THAT(spectrum.binHz * static_cast<double>((spectrum.bins.size() - 1) * 2),
               WithinRel(kSampleRate, 1.0e-9));
}

TEST_CASE("Phase is referenced to t=0, not to the window start", "[ir][spectrum]") {
    const auto cfg = wideFadeConfig();
    const auto excitation = renderSweep(cfg);
    const auto inverse = Sweep(cfg).buildInverseFilter();

    constexpr std::size_t kDelay = 96;      // 2 ms at 48 kHz
    std::vector<float> response(excitation.size() + kDelay, 0.0f);
    for (std::size_t i = 0; i < excitation.size(); ++i)
        response[i + kDelay] = excitation[i];

    const auto measured = rta::ir::deconvolve(response, inverse, plainConfig());
    const auto spectrum = rta::ir::analyseSpectrum(measured, { validBandLowHz(cfg) });

    // A pure delay of D samples has phase -2*pi*f*D/fs. If the lead-in's linear
    // phase were left in, every reading would carry a further 240 samples of
    // group delay -- a constant offset nobody could later trace to a window.
    for (double hz : { 500.0, 1000.0, 2000.0, 4000.0 }) {
        const auto k = static_cast<std::size_t>(std::llround(hz / spectrum.binHz));
        REQUIRE(k < spectrum.bins.size());
        const double expected = -2.0 * kPi * hz * static_cast<double>(kDelay) / kSampleRate;
        const double got = std::arg(spectrum.bins[k]);
        CAPTURE(hz, got, expected);
        CHECK_THAT(std::remainder(got - expected, 2.0 * kPi), WithinAbs(0.0, 0.05));
    }
}

TEST_CASE("A steep skirt comes back right, and the lead-in is why", "[ir][spectrum]") {
    // ButterworthDesign offers bandPass only, so the skirt under test is a
    // band-pass whose UPPER edge is far above the comparison band: across
    // 500 Hz to 8 kHz it is a 300 Hz high-pass in everything but name. The
    // Nyquist clamp sits at 0.995*24000 = 23880 Hz, above 23000, so the design
    // is not clamped and its own response is the expectation.
    const auto design = rta::dsp::ButterworthDesign::bandPass(300.0, 23000.0, kSampleRate, 2);

    const auto cfg = wideFadeConfig();
    const auto excitation = renderSweep(cfg);
    const auto inverse = Sweep(cfg).buildInverseFilter();

    rta::dsp::BiquadCascade cascade(design.sections);
    std::vector<float> driven(excitation.size());
    for (std::size_t i = 0; i < excitation.size(); ++i)
        driven[i] = static_cast<float>(cascade.processSample(static_cast<double>(excitation[i])));

    const auto reference = rta::ir::deconvolve(excitation, inverse, plainConfig());
    const auto measured  = rta::ir::deconvolve(driven, inverse, plainConfig());

    const double lowHz = validBandLowHz(cfg);
    const auto refSpec = rta::ir::analyseSpectrum(reference, { lowHz });
    const auto gotSpec = rta::ir::analyseSpectrum(measured, { lowHz });
    REQUIRE(refSpec.bins.size() == gotSpec.bins.size());

    // Dividing by the reference spectrum removes the analysis pulse's own
    // shaping -- same excitation, same inverse filter -- so what remains is the
    // filter and nothing else.
    double worstDb = 0.0;
    for (double hz : { 500.0, 800.0, 1500.0, 3000.0, 6000.0, 8000.0 }) {
        const auto k = static_cast<std::size_t>(std::llround(hz / gotSpec.binHz));
        REQUIRE(k < gotSpec.bins.size());
        const double ratio = std::abs(gotSpec.bins[k]) / std::abs(refSpec.bins[k]);
        const double want = cascadeMagnitude(design.sections, hz);
        const double errDb = 20.0 * std::log10(ratio / want);
        CAPTURE(hz, ratio, want, errDb);
        CHECK_THAT(errDb, WithinAbs(0.0, 0.5));
        worstDb = std::max(worstDb, std::abs(errDb));
    }

    // THE FALSIFIER, reached through the public API rather than by editing the
    // source: claim a 20 kHz band edge and the lead-in collapses to 5 samples,
    // slicing the analysis pulse almost exactly at its peak. If the lead-in
    // were decorative this would agree with the case above; it does not.
    const auto starved = rta::ir::analyseSpectrum(measured, { 20000.0 });
    const auto starvedRef = rta::ir::analyseSpectrum(reference, { 20000.0 });
    REQUIRE(starved.leadInSamples == 5u);
    double starvedWorstDb = 0.0;
    for (double hz : { 500.0, 800.0, 1500.0, 3000.0, 6000.0, 8000.0 }) {
        const auto k = static_cast<std::size_t>(std::llround(hz / starved.binHz));
        const double ratio = std::abs(starved.bins[k]) / std::abs(starvedRef.bins[k]);
        starvedWorstDb = std::max(starvedWorstDb,
                                  std::abs(20.0 * std::log10(ratio / cascadeMagnitude(design.sections, hz))));
    }
    // Measured: 0.0073 dB with the derived lead-in, 1.10 dB with five samples
    // of it -- 150x. The bound is 10x, well clear of both.
    CAPTURE(worstDb, starvedWorstDb);
    CHECK(starvedWorstDb > 10.0 * worstDb);
}

TEST_CASE("More silence after the sweep converges, and too little does not",
          "[ir][spectrum]") {
    // Record decision 9. The plan's first draft asserted only that a truncated
    // capture DIFFERS in the time domain, which every working convolution
    // satisfies -- no wrong implementation makes it red. The pair below is the
    // claim that has teeth: 0.5x RT60 is materially wrong, 1.5x is not.
    const auto cfg = wideFadeConfig();
    const auto excitation = renderSweep(cfg);
    const auto inverse = Sweep(cfg).buildInverseFilter();

    // A DENSE decaying-noise room, and the density is the point. A first
    // version used a sparse room -- one tap every 97 samples -- to keep a
    // direct convolution cheap, and measured 0.0026 dB of error from truncating
    // at half the RT60. That is not the code being right; it is a tail with
    // about a ninety-seventh of the energy it should have, so there was nothing
    // for the truncation to remove. The test could not have failed.
    //
    // Density is free once the convolution goes through the FFT, and decision 1
    // already says how: deconvolution IS convolution, so deconvolve() with a
    // gain of 1 convolves the excitation with the room. Only its originIndex
    // means something else here, and nothing below reads it.
    constexpr double kRt60 = 0.25;
    const auto irLength = static_cast<std::size_t>(2.0 * kRt60 * kSampleRate);
    std::vector<float> room(irLength, 0.0f);
    std::mt19937 noise(20260830u);              // fixed seed: deterministic
    for (std::size_t i = 0; i < irLength; ++i) {
        const float sign = (noise() & 1u) ? 1.0f : -1.0f;
        room[i] = sign * static_cast<float>(
            std::pow(10.0, -3.0 * static_cast<double>(i) / (kRt60 * kSampleRate)));
    }
    room[0] = 1.0f;                             // direct arrival, same order as
                                                // the reverberant field

    const auto full = rta::ir::deconvolve(excitation, room, plainConfig()).samples;

    const double lowHz = validBandLowHz(cfg);
    const auto whole = rta::ir::deconvolve(full, inverse, plainConfig());
    const auto wholeSpec = rta::ir::analyseSpectrum(whole, { lowHz });

    auto worstErrorForGap = [&](double gapFactor) {
        const auto gap = static_cast<std::size_t>(gapFactor * kRt60 * kSampleRate);
        const std::span<const float> cut(full.data(), excitation.size() + gap);
        const auto spec = rta::ir::analyseSpectrum(
            rta::ir::deconvolve(cut, inverse, plainConfig()), { lowHz });
        // EVERY bin in the band, not a handful of round frequencies. A dense
        // reverberant tail puts nulls roughly 1/decayLength apart -- a couple of
        // hertz -- so a scan at 250 Hz steps walks straight past the bins where
        // truncation does its damage, and reports a reassuring number. A first
        // version did exactly that and read 0.005 dB.
        double worst = 0.0;
        for (std::size_t k = 0; k < spec.bins.size(); ++k) {
            const double hz = static_cast<double>(k) * spec.binHz;
            if (hz < 500.0 || hz > 8000.0) continue;
            const auto kw = static_cast<std::size_t>(std::llround(hz / wholeSpec.binHz));
            if (kw >= wholeSpec.bins.size()) continue;
            const double a = std::abs(wholeSpec.bins[kw]);
            const double b = std::abs(spec.bins[k]);
            if (a > 1.0e-6) worst = std::max(worst, std::abs(20.0 * std::log10(b / a)));
        }
        return worst;
    };

    const double atHalf = worstErrorForGap(0.5);
    const double atOneAndAHalf = worstErrorForGap(1.5);
    CAPTURE(atHalf, atOneAndAHalf);
    // Measured for this fixture: 1.76 dB at half the RT60, 0.0023 dB at one and
    // a half -- a factor of 750. Bounds are set clear of both, and the FIRST is
    // the one that makes this a test rather than a demonstration: without it, an
    // implementation that ignored the capture length entirely would pass.
    CHECK(atHalf > 1.0);                       // truncation is NOT harmless
    CHECK(atOneAndAHalf < 0.5);                // and 1.5x RT60 is enough
    CHECK(atOneAndAHalf * 5.0 < atHalf);       // the rule points the right way
}

TEST_CASE("analyseSpectrum refuses what it cannot window", "[ir][spectrum]") {
    const auto cfg = wideFadeConfig();
    const auto excitation = renderSweep(cfg);
    const auto inverse = Sweep(cfg).buildInverseFilter();
    const auto reference = rta::ir::deconvolve(excitation, inverse, plainConfig());

    CHECK_THROWS_AS(rta::ir::analyseSpectrum(reference, { 0.0 }), std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::analyseSpectrum(reference, { -100.0 }), std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::leadInSamplesFor(0.0, 400.0), std::invalid_argument);

    // A band edge so low that two of its cycles do not fit before t = 0. The
    // window would start before the buffer; refuse rather than clamp silently,
    // because a silently shortened lead-in is the +23.5 dB error again.
    const double impossible = 2.0 * kSampleRate
                            / (static_cast<double>(reference.originIndex) + 1.0);
    CHECK_THROWS_AS(rta::ir::analyseSpectrum(reference, { impossible * 0.5 }),
                    std::invalid_argument);
}
