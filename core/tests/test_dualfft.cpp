// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/AverageCount.h"
#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/SpectrumEngine.h"
#include "rta/gen/Synthetic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace rta::dsp;

namespace {
constexpr std::size_t kN = 1024;

/// One non-repeating record of pink noise, `total` samples long.
std::vector<float> pink(std::size_t total, std::uint32_t seed) {
    rta::gen::SyntheticPink source(total, -12.0, seed);
    std::vector<float> out(total);
    source.render(out);
    return out;
}
}  // namespace

TEST_CASE("the engine rejects configurations it cannot honour", "[dualfft]") {
    DualFftEngine::Config c;
    c.fftSize = kN;
    // Config::hopSize defaults to 2048, which exceeds this test's fftSize of
    // 1024 -- left unset, the "valid" construction below would itself violate
    // the very hopSize<=fftSize rule this test exists to check. Every other
    // TEST_CASE in this file sets hopSize alongside fftSize; this one must too.
    c.hopSize = kN / 2;
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.hopSize = 0;        return DualFftEngine(d); }()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.hopSize = kN + 1;   return DualFftEngine(d); }()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.sampleRate = 0.0;   return DualFftEngine(d); }()),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.fifoDepth = 0;      return DualFftEngine(d); }()),
                      std::invalid_argument);
    // A delay past 1<<20 samples (~21.8 s at 48 kHz) is a misconfiguration,
    // not a measurement -- see skipMagnitude()'s comment in DualFftEngine.cpp.
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.referenceDelaySamples = (1 << 20) + 1;
                            return DualFftEngine(d); }()),
                      std::invalid_argument);
    // Below 1.0 averages coherence is definitionally 1.0 at every bin
    // (record §3) -- Config must not let a caller defeat that floor.
    REQUIRE_THROWS_AS(([&]{ auto d = c; d.minimumEffectiveAverages = 0.0;
                            return DualFftEngine(d); }()),
                      std::invalid_argument);

    DualFftEngine engine(c);
    const std::vector<float> a(64, 0.0f), b(65, 0.0f);
    // Two channels of different length are not the same instant in time.
    REQUIRE_THROWS_AS(engine.process(a, b), std::invalid_argument);
}

TEST_CASE("a channel measured against itself is its own spectrum", "[dualfft]") {
    const auto x = pink(1 << 15, 3);
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.window = WindowType::Hann;
    c.fifoDepth = 4096;  // larger than the frame count: a plain mean
    DualFftEngine engine(c);
    engine.process(x, x);

    REQUIRE(engine.frameCount() == (x.size() - kN) / (kN / 2) + 1);

    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        // Sxy = conj(X)*Y with Y == X is |X|^2: real, non-negative, == Sxx.
        REQUIRE(engine.crossPsd()[k].imag() == Catch::Approx(0.0).margin(1e-18));
        REQUIRE(engine.crossPsd()[k].real()
                == Catch::Approx(engine.referencePsd()[k]).epsilon(1e-12));
        REQUIRE(engine.measurementPsd()[k]
                == Catch::Approx(engine.referencePsd()[k]).epsilon(1e-12));
    }
}

TEST_CASE("the two channels are not interchangeable", "[dualfft]") {
    // Every configuration above feeds (x, x) or a rotation of the same
    // signal, where Syy == Sxx bit-for-bit -- so swapping referencePsd() and
    // measurementPsd()'s returns would still pass all of them. y = 0.5*x
    // makes Syy exactly a quarter of Sxx: only the right accessor sees it.
    const auto x = pink(kN * 4, 109);
    std::vector<float> y(x.size());
    for (std::size_t n = 0; n < x.size(); ++n) y[n] = 0.5f * x[n];

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    DualFftEngine engine(c);
    engine.process(x, y);

    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.measurementPsd()[k]
                == Catch::Approx(0.25 * engine.referencePsd()[k]).epsilon(1e-5));
    }
}

TEST_CASE("Sxx is the same density SpectrumEngine reports", "[dualfft][scaling]") {
    // Two divergent PSD scalings in one codebase surface as a constant dB
    // offset nobody can find. This is the test that stops that happening.
    const auto x = pink(1 << 15, 5);

    SpectrumEngine::Config sc;
    sc.fftSize = kN; sc.hopSize = kN / 2; sc.averaging = Averaging::Linear;
    SpectrumEngine reference(sc);
    reference.process(x);

    DualFftEngine::Config dc;
    dc.fftSize = kN; dc.hopSize = kN / 2; dc.fifoDepth = 4096;
    DualFftEngine engine(dc);
    engine.process(x, x);

    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.referencePsd()[k]
                == Catch::Approx(static_cast<double>(reference.density()[k])).epsilon(1e-5));
    }
}

TEST_CASE("delay compensation happens before the transform", "[dualfft][delay]") {
    // y[n] = x[n-D]. Compensated, the two frames are the SAME samples, so the
    // cross-spectrum must come back real and equal to Sxx -- bit for bit, not
    // approximately. If the compensation were applied to the phase afterwards
    // this would still be complex.
    constexpr int kD = 137;
    const auto x = pink(1 << 15, 7);
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = static_cast<std::size_t>(kD); n < x.size(); ++n) {
        y[n] = x[n - static_cast<std::size_t>(kD)];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    c.referenceDelaySamples = kD;
    DualFftEngine engine(c);
    engine.process(x, y);

    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.crossPsd()[k].imag() == Catch::Approx(0.0).margin(1e-15));
        REQUIRE(engine.crossPsd()[k].real()
                == Catch::Approx(engine.referencePsd()[k]).epsilon(1e-9));
    }

    // The same data with NO compensation must NOT come back real -- otherwise
    // the test above would pass for an engine that ignores the field entirely.
    auto uncompensated = c;
    uncompensated.referenceDelaySamples = 0;
    DualFftEngine naive(uncompensated);
    naive.process(x, y);
    double worst = 0.0;
    for (std::size_t k = 1; k + 1 < naive.numBins(); ++k) {
        worst = std::max(worst, std::abs(naive.crossPsd()[k].imag()) / naive.referencePsd()[k]);
    }
    REQUIRE(worst > 0.1);
}

TEST_CASE("a negative delay compensates the other channel", "[dualfft][delay]") {
    // The reference cable is the long one: x[n] = y[n-D], so D is negative.
    constexpr int kD = 91;
    const auto y = pink(1 << 15, 11);
    std::vector<float> x(y.size(), 0.0f);
    for (std::size_t n = static_cast<std::size_t>(kD); n < y.size(); ++n) {
        x[n] = y[n - static_cast<std::size_t>(kD)];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    c.referenceDelaySamples = -kD;
    DualFftEngine engine(c);
    engine.process(x, y);
    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.crossPsd()[k].imag() == Catch::Approx(0.0).margin(1e-15));
    }
}

TEST_CASE("the cross-spectrum carries the delay's SIGN, not just its size",
          "[dualfft][sign]") {
    // Both delay tests above end with the two compensated frames bit-identical,
    // X == Y. And conj(X)*Y == conj(X*conj(Y)), which for X == Y is real either
    // way -- so an engine that conjugates the WRONG channel passes every
    // `imag() == 0` assertion in this file, and the `abs(imag)` check in the
    // uncompensated half erases the sign too. The convention is load-bearing:
    // findDelayPhat's output is fed straight into referenceDelaySamples, and a
    // flipped sign there compensates in the wrong direction.
    //
    // So: leave the delay UNCOMPENSATED and assert the closed form with its
    // sign, Sxy[k] = Sxx[k] * exp(-2*pi*i*k*D/N). That holds exactly for a
    // kN-periodic signal under a rectangular window -- see the plan's note on
    // windows. y is built as a CIRCULAR rotation so there is no start
    // transient to contaminate the first frame.
    constexpr int kD = 5;
    rta::gen::SyntheticPink source(kN, -12.0, 83);   // block period == kN
    std::vector<float> x(kN * 32);
    source.render(x);
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = 0; n < x.size(); ++n) {
        y[n] = x[(n + x.size() - static_cast<std::size_t>(kD)) % x.size()];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.window = WindowType::Rectangular;
    c.fifoDepth = 4096;
    c.referenceDelaySamples = 0;   // deliberately not compensated
    DualFftEngine engine(c);
    engine.process(x, y);

    // The tolerance needs BOTH terms, and the reason is structural rather than
    // fussy. `RealFft` transforms in std::complex<float>, so the transform's
    // ABSOLUTE rounding error is set by the largest bins -- which for pink
    // noise sit near DC. By the top of the band a bin's own magnitude has
    // decayed by ~500x while that error floor has not moved, so a pure
    // parts-per-million-of-this-bin margin asks for more precision than float32
    // can deliver and fails on a CORRECT engine two bins below Nyquist.
    // (Measured: k=510, sxx=1.9e-07, residual 3.9e-13 against a 1.9e-13
    // margin.) The engine is not the limit here -- it promotes to double at the
    // earliest possible point; the ceiling is one layer down in RealFft.
    const double peak = *std::max_element(engine.referencePsd().begin(),
                                          engine.referencePsd().end());
    for (std::size_t k = 1; k + 1 < engine.numBins(); ++k) {
        const double theta = -2.0 * std::numbers::pi * static_cast<double>(k)
                           * static_cast<double>(kD) / static_cast<double>(kN);
        const double sxx = engine.referencePsd()[k];
        const double tolerance = sxx * 1e-6 + peak * 1e-7;
        REQUIRE(engine.crossPsd()[k].real()
                == Catch::Approx(sxx * std::cos(theta)).margin(tolerance));
        REQUIRE(engine.crossPsd()[k].imag()
                == Catch::Approx(sxx * std::sin(theta)).margin(tolerance));
    }
}

TEST_CASE("reset re-arms the alignment even when the skip is only half spent",
          "[dualfft][reset]") {
    // The reset test below feeds far more samples than the delay, so both skip
    // counters are back to zero before reset() runs -- at which point failing
    // to re-arm them is indistinguishable from re-arming them. Reset MID-SKIP.
    constexpr int kD = 512;
    const auto x = pink(1 << 15, 89);
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t n = static_cast<std::size_t>(kD); n < x.size(); ++n) {
        y[n] = x[n - static_cast<std::size_t>(kD)];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 2; c.fifoDepth = 4096;
    c.referenceDelaySamples = kD;
    DualFftEngine engine(c);

    // Half the skip consumed, no frame produced yet.
    engine.process(std::span<const float>(x.data(), kD / 2),
                   std::span<const float>(y.data(), kD / 2));
    REQUIRE(engine.frameCount() == 0);
    engine.reset();

    // A fresh, correctly aligned stream from the top. If reset() left the skip
    // counter half spent -- or left the 256 already-buffered reference samples
    // in the ring -- the two channels are 256 samples out and Sxy stops being
    // real. This pins BOTH halves of reset(): the counters and the buffers.
    engine.process(x, y);
    REQUIRE(engine.frameCount() > 0);
    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.crossPsd()[k].imag() == Catch::Approx(0.0).margin(1e-15));
    }
}

TEST_CASE("the FIFO forgets exactly at its depth", "[dualfft][fifo]") {
    // Feed loud frames, then quiet ones. Once `depth` quiet frames have gone
    // through, nothing of the loud ones may remain -- that is what "exact
    // drop-off" means, and an exponential average would still show them.
    const auto loud = pink(kN * 8, 13);
    std::vector<float> quiet(kN * 8, 0.0f);
    for (std::size_t n = 0; n < quiet.size(); ++n) quiet[n] = loud[n] * 0.001f;

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.fifoDepth = 4;
    DualFftEngine engine(c);
    engine.process(loud, loud);
    const double afterLoud = engine.referencePsd()[64];
    engine.process(quiet, quiet);
    const double afterQuiet = engine.referencePsd()[64];
    REQUIRE(afterQuiet < afterLoud * 1e-5);
}

TEST_CASE("the exponential mode smooths towards each frame", "[dualfft][exponential]") {
    // TransferAveraging::Exponential appears nowhere else in this file, so
    // deleting accumulate()'s exponential branch, hard-coding alpha_, or
    // dropping timeConstantSeconds validation would stay green. Pin the
    // recursion itself, not just its existence.
    const auto x = pink(kN, 97);  // exactly one frame at hopSize == fftSize

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.averaging = TransferAveraging::Exponential;
    c.sampleRate = 48000.0; c.timeConstantSeconds = 0.5;
    DualFftEngine engine(c);
    engine.process(x, x);

    // 1. Seeded from the first frame, Fifo and Exponential haven't diverged
    // yet -- both just assign the frame's own PSD, bit for bit.
    auto fifoConfig = c;
    fifoConfig.averaging = TransferAveraging::Fifo;
    DualFftEngine fifoEngine(fifoConfig);
    fifoEngine.process(x, x);
    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.referencePsd()[k] == fifoEngine.referencePsd()[k]);
    }

    // 2. The recursion against its own closed form: halving the amplitude
    // quarters the power exactly, so after one pole the mean must be
    // P1 * (1 - 0.75*alpha) -- alpha computed from the config, not a literal.
    const std::vector<double> p1(engine.referencePsd().begin(), engine.referencePsd().end());
    std::vector<float> half(x.size());
    for (std::size_t n = 0; n < x.size(); ++n) half[n] = 0.5f * x[n];
    engine.process(half, half);
    const double hopSeconds = static_cast<double>(c.hopSize) / c.sampleRate;
    const double alpha = 1.0 - std::exp(-hopSeconds / c.timeConstantSeconds);
    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.referencePsd()[k]
                == Catch::Approx(p1[k] * (1.0 - 0.75 * alpha)).epsilon(1e-5));
    }

    // 3. It never forgets outright -- the reason both modes exist. The SAME
    // loud-then-quiet stream that saturates a depth-4 FIFO below 1e-5 of the
    // loud level (see "the FIFO forgets exactly at its depth") must still
    // leave a measurable fraction of it in a fresh Exponential engine.
    const auto loud = pink(kN * 8, 101);
    std::vector<float> quiet(kN * 4, 0.0f);
    for (std::size_t n = 0; n < quiet.size(); ++n) quiet[n] = loud[n] * 0.001f;

    fifoConfig.fifoDepth = 4;
    DualFftEngine satFifo(fifoConfig);
    satFifo.process(loud, loud);
    const double fifoAfterLoud = satFifo.referencePsd()[64];
    satFifo.process(quiet, quiet);
    REQUIRE(satFifo.referencePsd()[64] < fifoAfterLoud * 1e-5);

    DualFftEngine freshExp(c);
    freshExp.process(loud, loud);
    const double expAfterLoud = freshExp.referencePsd()[64];
    freshExp.process(quiet, quiet);
    REQUIRE(freshExp.referencePsd()[64] > expAfterLoud * 1e-3);

    // 4. effectiveAverages() must be the closed form from AverageCount.h, not
    // a loose bound -- the coherence gate (task 3) is built on this number.
    DualFftEngine::Config gc;
    gc.fftSize = kN; gc.hopSize = kN / 2; gc.averaging = TransferAveraging::Exponential;
    gc.sampleRate = 48000.0; gc.timeConstantSeconds = 0.5; gc.window = WindowType::Hann;
    DualFftEngine gateEngine(gc);
    const auto y = pink(kN * 8, 103);  // SyntheticPink's blockSize must be a power of two
    gateEngine.process(y, y);
    const double gateAlpha = 1.0 - std::exp(-(static_cast<double>(gc.hopSize) / gc.sampleRate)
                                            / gc.timeConstantSeconds);
    const Window gateWindow(gc.window, gc.fftSize);
    const double expected = exponentialEffectiveAverages(gateWindow.coefficients(), gc.hopSize,
                                                          gateAlpha, gateEngine.frameCount());
    REQUIRE(gateEngine.effectiveAverages() == Catch::Approx(expected).margin(1e-9));
}

TEST_CASE("reset clears the average and re-arms the alignment", "[dualfft]") {
    const auto x = pink(kN * 4, 17);
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.referenceDelaySamples = 40;
    DualFftEngine engine(c);
    engine.process(x, x);
    REQUIRE(engine.frameCount() > 0);
    engine.reset();
    REQUIRE(engine.frameCount() == 0);
    REQUIRE(engine.effectiveAverages() == Catch::Approx(0.0));
    for (std::size_t k = 0; k < engine.numBins(); ++k) {
        REQUIRE(engine.referencePsd()[k] == 0.0);
    }
}

TEST_CASE("effective averages never exceed the frame count", "[dualfft][average]") {
    const auto x = pink(1 << 14, 19);
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN / 4; c.fifoDepth = 4096;
    DualFftEngine engine(c);
    engine.process(x, x);
    REQUIRE(engine.effectiveAverages() > 1.0);
    REQUIRE(engine.effectiveAverages() < static_cast<double>(engine.frameCount()));
}

TEST_CASE("a saturated FIFO reports its depth, not its frame count", "[dualfft][average]") {
    // Every effectiveAverages() test above uses fifoDepth = 4096, larger than
    // its frame count, so the min(frameCount_, fifoDepth) clamp never
    // engages -- mutating it to the raw frameCount_ would still pass them all.
    const auto x = pink(kN * 8, 107);  // SyntheticPink's blockSize must be a power of two
    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.fifoDepth = 4;  // zero overlap
    DualFftEngine engine(c);
    engine.process(x, x);

    REQUIRE(engine.frameCount() == 8);
    // At zero overlap, fifoEffectiveAverages is the identity (task 1's
    // "non-overlapped frames are fully independent"), so once saturated the
    // effective count is exactly the depth -- a derivation, not a measurement.
    REQUIRE(engine.effectiveAverages() == Catch::Approx(4.0).margin(1e-12));
}
