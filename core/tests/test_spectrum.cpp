// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pinned against scipy.signal.welch via tools/gen_welch.py.
//
// Welch's method has about a dozen places to be subtly wrong -- window
// normalisation, the factor of two on a one-sided spectrum, whether DC and
// Nyquist get that factor, how many segments a given overlap yields, what
// happens to a trailing remainder -- and every one of them produces a spectrum
// that looks entirely plausible. None of them can be caught by inspection.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/SpectrumEngine.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::dsp;

namespace {

std::vector<rta::test::GoldenCase> welchGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/welch.txt");
    return cases;
}

double peakOf(const std::vector<double>& values) {
    double peak = 0.0;
    for (const double v : values) peak = std::max(peak, std::abs(v));
    return peak;
}

SpectrumEngine::Config configFor(const rta::test::GoldenCase& testCase) {
    const auto nperseg = (std::size_t) testCase.row("nperseg").front();
    const auto noverlap = (std::size_t) testCase.row("noverlap").front();

    SpectrumEngine::Config config;
    config.fftSize = nperseg;
    config.hopSize = nperseg - noverlap;
    config.sampleRate = 48000.0;
    config.window = WindowType::Hann;
    config.averaging = Averaging::Linear;
    return config;
}

}  // namespace

TEST_CASE("Welch spectra match scipy", "[spectrum][golden]") {
    int checked = 0;

    for (const auto& testCase : welchGolden()) {
        ++checked;
        CAPTURE(testCase.name);

        const auto input = testCase.floatRow("input");
        const auto& expectedDensity = testCase.row("density");
        const auto& expectedSpectrum = testCase.row("spectrum");
        const auto& expectedFreq = testCase.row("freq");

        SpectrumEngine engine(configFor(testCase));
        REQUIRE(engine.numBins() == expectedDensity.size());
        REQUIRE(expectedSpectrum.size() == expectedDensity.size());

        // Fed in awkward chunks, because at a show the samples arrive in
        // whatever block size the driver chose, never in whole FFT frames.
        const std::size_t chunkSizes[] = { 37, 512, 1, 300 };
        std::size_t offset = 0;
        std::size_t which = 0;
        while (offset < input.size()) {
            const std::size_t n = std::min(chunkSizes[which++ % 4], input.size() - offset);
            engine.process({ input.data() + offset, n });
            offset += n;
        }
        REQUIRE(engine.frameCount() > 0);

        const double densityTol = 1.0e-5 * peakOf(expectedDensity);
        const double spectrumTol = 1.0e-5 * peakOf(expectedSpectrum);

        for (std::size_t k = 0; k < engine.numBins(); ++k) {
            CAPTURE(k);
            CHECK_THAT(engine.binFrequency(k), WithinRel(expectedFreq[k], 1.0e-12));
            CHECK_THAT((double) engine.density()[k], WithinAbs(expectedDensity[k], densityTol));
            CHECK_THAT((double) engine.spectrum()[k], WithinAbs(expectedSpectrum[k], spectrumTol));
        }
    }

    REQUIRE(checked == 4);
}

TEST_CASE("Segment count follows the overlap, and a remainder is discarded",
          "[spectrum]") {
    // scipy yields (len - nperseg) / hop + 1 whole segments and drops the tail.
    // Getting this off by one shifts every averaged value.
    SpectrumEngine::Config config;
    config.fftSize = 256;
    config.hopSize = 128;
    config.sampleRate = 48000.0;

    SpectrumEngine engine(config);
    std::vector<float> samples(4096, 0.1f);
    engine.process(samples);
    CHECK(engine.frameCount() == (4096 - 256) / 128 + 1);

    engine.reset();
    CHECK(engine.frameCount() == 0);

    // 300 samples with a 256-point frame: one frame, 44 samples held back.
    std::vector<float> few(300, 0.1f);
    engine.process(few);
    CHECK(engine.frameCount() == 1);
}

TEST_CASE("Power spectrum and density differ by exactly the window's ENBW",
          "[spectrum]") {
    // The identity that decides whether a band level is right: PS = PSD * ENBW.
    // Summing PS over a band instead of PSD reads high by exactly this factor,
    // which for Hann is 1.5 in power -- 1.76 dB on every broadband source.
    SpectrumEngine::Config config;
    config.fftSize = 1024;
    config.hopSize = 512;
    config.sampleRate = 48000.0;
    config.window = WindowType::Hann;

    SpectrumEngine engine(config);

    std::vector<float> noise(16384);
    unsigned state = 12345u;
    for (auto& sample : noise) {
        state = state * 1664525u + 1013904223u;
        sample = (float) ((double) state / 4294967296.0 * 2.0 - 1.0);
    }
    engine.process(noise);

    CHECK_THAT(engine.equivalentNoiseBandwidthHz(),
               WithinRel(1.5 * 48000.0 / 1024.0, 1.0e-9));

    for (std::size_t k = 1; k + 1 < engine.numBins(); ++k) {
        CAPTURE(k);
        CHECK_THAT((double) engine.spectrum()[k],
                   WithinRel((double) engine.density()[k] * engine.equivalentNoiseBandwidthHz(),
                             1.0e-4));
    }
}

TEST_CASE("Exponential averaging converges on the linear answer", "[spectrum]") {
    // Different weighting of the same frames, so a stationary signal must land
    // in the same place. If it does not, one of the two is mis-normalised.
    std::vector<float> tone(65536);
    for (std::size_t i = 0; i < tone.size(); ++i) {
        tone[i] = 0.25f * std::sin(2.0f * 3.14159265358979f * 300.0f * (float) i / 48000.0f);
    }

    SpectrumEngine::Config linear;
    linear.fftSize = 1024;
    linear.hopSize = 512;
    linear.sampleRate = 48000.0;
    linear.averaging = Averaging::Linear;

    auto exponential = linear;
    exponential.averaging = Averaging::Exponential;
    exponential.timeConstantSeconds = 0.05;

    SpectrumEngine a(linear);
    SpectrumEngine b(exponential);
    a.process(tone);
    b.process(tone);

    for (std::size_t k = 0; k < a.numBins(); ++k) {
        CAPTURE(k);
        CHECK_THAT((double) b.density()[k],
                   WithinAbs((double) a.density()[k], 1.0e-4 * (double) a.density()[6]));
    }
}

TEST_CASE("Invalid spectrum configurations are rejected", "[spectrum]") {
    SpectrumEngine::Config config;
    config.fftSize = 1000;  // not a power of two
    CHECK_THROWS_AS(SpectrumEngine(config), std::invalid_argument);

    config.fftSize = 1024;
    config.hopSize = 0;
    CHECK_THROWS_AS(SpectrumEngine(config), std::invalid_argument);

    config.hopSize = 2048;  // a hop past the frame would skip samples
    CHECK_THROWS_AS(SpectrumEngine(config), std::invalid_argument);
}
