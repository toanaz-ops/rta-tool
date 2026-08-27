// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SyntheticPink and SyntheticSine are deterministic TEST feeds (see
// rta/gen/Synthetic.h for why they are not the real-time product generator).
// Every expectation below is closed-form:
//
//   * the pink slope is asserted from ONE rendered block's own spectrum, not a
//     Welch-averaged estimate over many blocks -- the generator places
//     |X(k)| = k^-1/2 directly, so the -3.0103 dB/octave slope is exact by
//     construction and the only error left is float round-trip through the
//     FFT, which is why the tolerance is tight (+/-0.05 dB);
//   * the sine's level is read at a frequency chosen to land exactly on an
//     analysis bin (SpectrumEngine::binFrequency), so there is no spectral
//     leakage to account for and the dBFS convention from plan §1.4 applies
//     directly: 10*log10(power) + 3.0103;
//   * determinism is a property of the seeded std::mt19937, asserted bitwise.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/RealFft.h"
#include "rta/dsp/SpectrumEngine.h"
#include "rta/gen/Synthetic.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::gen;

namespace {

// Ordinary least squares slope of y against x -- used to read the pink
// spectrum's dB-per-octave trend from a handful of octave-spaced bins.
double leastSquaresSlope(const std::vector<double>& x, const std::vector<double>& y) {
    const double n = double(x.size());
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        sx += x[i];
        sy += y[i];
        sxx += x[i] * x[i];
        sxy += x[i] * y[i];
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

}  // namespace

TEST_CASE("Pink noise falls at exactly -3.0103 dB per octave, by construction",
          "[synthetic][pink]") {
    constexpr std::size_t blockSize = 65536;
    SyntheticPink pink(blockSize, /*levelDbFs=*/-10.0, /*seed=*/0x5EEDu);
    REQUIRE(pink.blockSize() == blockSize);

    std::vector<float> block(blockSize);
    pink.render(block);

    rta::dsp::RealFft fft(blockSize);
    std::vector<std::complex<float>> spectrum(fft.numBins());
    fft.forward(block, spectrum);

    // Octaves 3..14 (k = 8 .. 16384), well clear of DC and of the Nyquist bin
    // (k = 32768), whose phase-dependent folding is not part of what this test
    // checks.
    const std::vector<std::size_t> octaveBins = { 8,   16,  32,   64,   128,  256,
                                                    512, 1024, 2048, 4096, 8192, 16384 };
    std::vector<double> logK, powerDb;
    for (const std::size_t k : octaveBins) {
        const double power = double(std::norm(spectrum[k]));
        logK.push_back(std::log2(double(k)));
        powerDb.push_back(10.0 * std::log10(power));
    }

    const double slope = leastSquaresSlope(logK, powerDb);
    CHECK_THAT(slope, WithinAbs(-10.0 * std::log10(2.0), 0.05));
}

TEST_CASE("A sine reads its exact dBFS level at its own bin through SpectrumEngine",
          "[synthetic][sine]") {
    rta::dsp::SpectrumEngine::Config config;
    config.fftSize = 4096;
    config.hopSize = 1024;
    config.sampleRate = 48000.0;
    config.window = rta::dsp::WindowType::Hann;
    config.averaging = rta::dsp::Averaging::Linear;

    rta::dsp::SpectrumEngine engine(config);

    // An interior bin, well clear of DC and Nyquist; its exact frequency (not
    // a round number like 1000 Hz) is what makes the tone land with zero
    // leakage into its neighbours.
    constexpr std::size_t bin = 137;
    const double frequencyHz = engine.binFrequency(bin);
    constexpr double levelDbFs = -20.0;

    SyntheticSine sine(config.sampleRate, frequencyHz, levelDbFs);
    std::vector<float> samples(config.fftSize + 6 * config.hopSize);
    sine.render(samples);
    engine.process(samples);
    REQUIRE(engine.frameCount() > 0);

    std::size_t argmax = 0;
    for (std::size_t k = 1; k < engine.numBins(); ++k) {
        if (engine.spectrum()[k] > engine.spectrum()[argmax]) argmax = k;
    }
    REQUIRE(argmax == bin);

    // Plan §1.4: full-scale sine = 0.0 dB, i.e. 10*log10(power) + 3.0103.
    constexpr double kFullScaleSineOffsetDb = 3.0102999566398120;
    const double readDbFs =
        10.0 * std::log10(double(engine.spectrum()[bin])) + kFullScaleSineOffsetDb;
    CHECK_THAT(readDbFs, WithinAbs(levelDbFs, 0.1));
}

TEST_CASE("SyntheticPink is bit-identical for a given seed, and differs for another",
          "[synthetic][pink][determinism]") {
    constexpr std::size_t blockSize = 4096;
    constexpr double levelDbFs = -10.0;

    SyntheticPink a(blockSize, levelDbFs, 0x5EEDu);
    SyntheticPink b(blockSize, levelDbFs, 0x5EEDu);
    SyntheticPink c(blockSize, levelDbFs, 0x5EEEu);

    std::vector<float> outA(blockSize), outB(blockSize), outC(blockSize);
    a.render(outA);
    b.render(outB);
    c.render(outC);

    CHECK(outA == outB);  // same seed -> bit-identical
    CHECK(outA != outC);  // a different seed must move at least one sample
}

TEST_CASE("SyntheticSine's phase is continuous across render calls, not reset per block",
          "[synthetic][sine]") {
    SyntheticSine oneCall(48000.0, 997.0, -6.0);
    SyntheticSine twoCalls(48000.0, 997.0, -6.0);

    std::vector<float> whole(1000);
    oneCall.render(whole);

    std::vector<float> first(500), second(500);
    twoCalls.render(first);
    twoCalls.render(second);

    for (std::size_t i = 0; i < 500; ++i) CHECK(first[i] == whole[i]);
    for (std::size_t i = 0; i < 500; ++i) CHECK(second[i] == whole[500 + i]);
}
