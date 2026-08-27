// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
//
// Station B: Oscillator, DualSine, RampedGain.
//
// Case numbering (6-11) matches the shared numbering in
// docs/plans/2026-08-27-generator-impl-plan.md section 4.2, which continues
// from Station A's Prng cases (1-5) so the whole generator suite reads as one
// sequence across the split files.

#include "rta/gen/Oscillator.h"

#include "rta/dsp/SpectrumEngine.h"
#include "rta/dsp/Window.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::dsp::Averaging;
using rta::dsp::SpectrumEngine;
using rta::dsp::WindowType;
using rta::gen::DualSine;
using rta::gen::Oscillator;
using rta::gen::RampedGain;

namespace {

/// Runs `engine` over enough repeats of one block of `osc`-generated samples
/// to accumulate several averaged frames, then returns it ready to read.
/// `totalSamples` must be >= one full fftSize so at least one frame lands.
void feedEngine(SpectrumEngine& engine, Oscillator& osc, std::size_t totalSamples) {
    std::vector<float> block(totalSamples);
    osc.process(block);
    engine.process(block);
}

void feedEngine(SpectrumEngine& engine, DualSine& gen, std::size_t totalSamples) {
    std::vector<float> block(totalSamples);
    gen.process(block);
    engine.process(block);
}

/// argmax of spectrum() over bins [1, numBins) -- skips DC, which carries no
/// tone information and would win a naive argmax on a badly normalised engine.
std::size_t peakBin(const SpectrumEngine& engine) {
    auto spectrum = engine.spectrum();
    std::size_t best = 1;
    for (std::size_t k = 1; k < spectrum.size(); ++k) {
        if (spectrum[k] > spectrum[best]) best = k;
    }
    return best;
}

/// Recovers a coherent tone's peak amplitude, in dBFS, from a mean-square bin
/// value: spectrum()[k] is mean square (see SpectrumEngine.h), so
/// amplitude = sqrt(2 * meanSquare) for a sine sitting on that bin.
double binAmplitudeDbFs(const SpectrumEngine& engine, std::size_t bin) {
    double meanSquare = engine.spectrum()[bin];
    double amplitude = std::sqrt(2.0 * meanSquare);
    return 20.0 * std::log10(amplitude);
}

}  // namespace

TEST_CASE("The phase accumulator returns to zero after an integer number of cycles") {
    // 1000 Hz at 48000 Hz sample rate is exactly 48 samples/cycle, so both
    // 48000 samples (1000 cycles) and 4800 samples (100 cycles) land the
    // accumulator back on an exact integer number of turns -- this is a
    // closed-form identity, not a value the implementation produced.
    constexpr double fs = 48000.0;
    constexpr double f = 1000.0;

    Oscillator osc(fs, f, -20.0);
    for (int i = 0; i < 48000; ++i) osc.nextSample();
    CHECK_THAT(osc.phaseTurns(), WithinAbs(0.0, 1e-12));

    Oscillator osc2(fs, f, -20.0);
    for (int i = 0; i < 4800; ++i) osc2.nextSample();
    CHECK_THAT(osc2.phaseTurns(), WithinAbs(0.0, 1e-12));
}

TEST_CASE("A 1 kHz sine at -20 dBFS reads back at 1000 Hz and -20.0 dBFS") {
    // fs = 64000, fftSize = 32768 -> bin width 1.953125 Hz, and
    // 1000 / 1.953125 = 512 exactly: 1 kHz lands exactly on bin 512, so there
    // is zero scalloping loss and this case is purely about the generator's
    // frequency and level, not about spectral interpolation. Binding
    // acceptance row per docs/plans/2026-08-27-generator-impl-plan.md 4.2.
    constexpr double fs = 64000.0;
    constexpr std::size_t fftSize = 32768;

    SpectrumEngine::Config cfg;
    cfg.fftSize = fftSize;
    cfg.hopSize = fftSize / 2;
    cfg.sampleRate = fs;
    cfg.window = WindowType::Hann;
    cfg.averaging = Averaging::Linear;
    SpectrumEngine engine(cfg);

    Oscillator osc(fs, 1000.0, -20.0);
    feedEngine(engine, osc, fftSize * 6);

    REQUIRE(engine.frameCount() > 0);
    CHECK(peakBin(engine) == 512);
    CHECK_THAT(engine.binFrequency(512), WithinAbs(1000.0, 0.5));
    CHECK_THAT(binAmplitudeDbFs(engine, 512), WithinAbs(-20.0, 0.1));
}

TEST_CASE("The same tone off-bin at 48 kHz reads back through a flat-top window") {
    // fs = 48000, fftSize = 65536 -> bin width 0.732421875 Hz;
    // 1000 / 0.732421875 = 1365.33..., so the true tone sits 0.366 Hz from
    // the nearest bin (1365) -- inside +/-0.5 Hz even with zero interpolation.
    // FlatTop's scalloping loss is under 0.01 dB by construction, so the
    // amplitude reads correctly even though the tone is off-bin. Corroborates
    // the previous case at a real device sample rate.
    constexpr double fs = 48000.0;
    constexpr std::size_t fftSize = 65536;
    constexpr std::size_t expectedBin = 1365;

    SpectrumEngine::Config cfg;
    cfg.fftSize = fftSize;
    cfg.hopSize = fftSize / 2;
    cfg.sampleRate = fs;
    cfg.window = WindowType::FlatTop;
    cfg.averaging = Averaging::Linear;
    SpectrumEngine engine(cfg);

    Oscillator osc(fs, 1000.0, -20.0);
    feedEngine(engine, osc, fftSize * 6);

    REQUIRE(engine.frameCount() > 0);
    CHECK(peakBin(engine) == expectedBin);
    CHECK_THAT(engine.binFrequency(expectedBin), WithinAbs(1000.0, 0.5));
    CHECK_THAT(binAmplitudeDbFs(engine, expectedBin), WithinAbs(-20.0, 0.1));
}

TEST_CASE("Dual sine places exactly two components and manufactures no third") {
    // fs = 64000, fftSize = 32768 (RealFft requires a power of two, so
    // fftSize itself cannot be picked freely -- see below). Bin width is
    // 64000/32768 = 1.953125 Hz = 125/64 Hz, so bin(f) = f*64/125, an integer
    // exactly when f is a multiple of 125 Hz (since gcd(64,125) = 1).
    //
    // f1 = 1000 Hz = 8*125 -> bin 512 exactly (same tone as case 7).
    // f2 = 1200 Hz is NOT a multiple of 125 (1200/125 = 9.6), so it cannot
    // land on-bin for ANY power-of-two fftSize at fs = 64000 -- 1200's prime
    // factorisation carries only 5^2, one short of the 5^3 in 64000's, and no
    // power of two can supply the missing factor of 5. This is a deviation
    // from the plan's suggested (1000, 1200) pair, forced by that arithmetic
    // fact, not a choice of convenience. f2 = 1250 Hz = 10*125 -> bin 640
    // exactly is the nearest multiple of 125 and stands in for it here.
    constexpr double fs = 64000.0;
    constexpr std::size_t fftSize = 32768;
    constexpr std::size_t bin1 = 512;
    constexpr std::size_t bin2 = 640;

    SpectrumEngine::Config cfg;
    cfg.fftSize = fftSize;
    cfg.hopSize = fftSize / 2;
    cfg.sampleRate = fs;
    cfg.window = WindowType::Hann;
    cfg.averaging = Averaging::Linear;
    SpectrumEngine engine(cfg);

    DualSine gen(fs, 1000.0, 1250.0, -20.0);
    feedEngine(engine, gen, fftSize * 6);

    REQUIRE(engine.frameCount() > 0);
    // Each tone carries amplitude/2 of the requested peak, so its own bin
    // reads 6 dB below the pair's peak-referenced level.
    CHECK_THAT(binAmplitudeDbFs(engine, bin1), WithinAbs(-26.0206, 0.1));
    CHECK_THAT(binAmplitudeDbFs(engine, bin2), WithinAbs(-26.0206, 0.1));

    auto spectrum = engine.spectrum();
    for (std::size_t k = 1; k < spectrum.size(); ++k) {
        bool isTone = (k >= bin1 - 1 && k <= bin1 + 1) || (k >= bin2 - 1 && k <= bin2 + 1);
        if (isTone) continue;
        double dbFs = binAmplitudeDbFs(engine, k);
        CHECK(dbFs < -100.0);
    }
}

TEST_CASE("RampedGain follows the raised-cosine closed form") {
    // g(p) = 0.5*(1 - cos(pi*p)), p = n/L -- half a raised-cosine period, the
    // shape whose value AND slope are both zero at p=0 and both flat at p=1
    // (g=1, dg/dp=0), which is why no click appears at either end.
    constexpr double fs = 48000.0;
    constexpr double rampSeconds = 0.010;
    constexpr int rampLen = 480;  // fs * rampSeconds, exact

    RampedGain ramp(fs, rampSeconds);
    ramp.requestOn();

    std::vector<double> rising(rampLen + 1);
    for (int n = 0; n <= rampLen; ++n) {
        float g = ramp.nextGain();
        double p = double(n) / double(rampLen);
        double expected = 0.5 * (1.0 - std::cos(std::numbers::pi * p));
        CHECK_THAT(double(g), WithinAbs(expected, 1e-6));
        rising[n] = g;
    }
    CHECK_THAT(rising[0], WithinAbs(0.0, 1e-6));
    // rising[rampLen] is the first Running-state sample (p=1 -> g=1), one
    // full ramp length after requestOn(); check it lands at unity.
    CHECK_THAT(rising.back(), WithinAbs(1.0, 1e-6));

    RampedGain fall(fs, rampSeconds);
    fall.requestOn();
    for (int n = 0; n < rampLen; ++n) fall.nextGain();  // drive to Running
    fall.requestOff();
    for (int n = 0; n <= rampLen; ++n) {
        float g = fall.nextGain();
        double p = double(n) / double(rampLen);
        double expected = 0.5 * (1.0 + std::cos(std::numbers::pi * p));  // mirror: g(1-p)
        CHECK_THAT(double(g), WithinAbs(expected, 1e-6));
    }
}

TEST_CASE("A ramp interrupted mid-rise falls from where it was") {
    // The naive "restart the falling ramp from gain=1.0" bug produces one
    // large, single-sample jump exactly at the reversal point. The correct
    // implementation reverses the direction of one continuous position, so
    // gain is a continuous function of that position and no step anywhere
    // exceeds the uninterrupted ramp's own natural per-sample slope.
    constexpr double fs = 48000.0;
    constexpr double rampSeconds = 0.010;
    constexpr int rampLen = 480;

    // Reference: the largest per-sample step of an uninterrupted rising ramp.
    // The raised-cosine's slope is greatest at its midpoint (p=0.5).
    double maxNaturalStep = 0.0;
    {
        RampedGain reference(fs, rampSeconds);
        reference.requestOn();
        float prev = reference.nextGain();
        for (int n = 1; n <= rampLen; ++n) {
            float g = reference.nextGain();
            maxNaturalStep = std::max(maxNaturalStep, double(std::abs(g - prev)));
            prev = g;
        }
    }

    RampedGain ramp(fs, rampSeconds);
    ramp.requestOn();

    std::vector<float> trace;
    trace.reserve(rampLen * 2);
    for (int n = 0; n < rampLen / 2; ++n) trace.push_back(ramp.nextGain());
    ramp.requestOff();
    // Drive well past a full ramp length to be sure the fall completes.
    for (int n = 0; n < rampLen * 2; ++n) trace.push_back(ramp.nextGain());

    double maxStep = 0.0;
    for (std::size_t n = 1; n < trace.size(); ++n) {
        maxStep = std::max(maxStep, double(std::abs(trace[n] - trace[n - 1])));
    }
    CHECK(maxStep <= maxNaturalStep + 1e-6);
    CHECK(ramp.state() == RampedGain::State::Idle);
}
