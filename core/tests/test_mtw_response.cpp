// SPDX-License-Identifier: AGPL-3.0-or-later
//
// No production code in this task: it proves the lane did something a fixed
// FFT cannot (record T3/T4). Every tolerance below comes from ONE Neff --
// docs/plans/2026-09-05-L3-mtw-impl-plan.md's Bendat & Piersol derivation at
// Neff = 8.5866271 (12.1% per-bin error on |H1| at gamma^2 = 0.8) -- so the
// reflection cases assert BAND STATISTICS (a mean, a standard deviation),
// never a tight per-bin bound no amount of tightening could make true. The
// noiseless biquad is the exception: gamma^2 = 1 there, so the per-point
// tolerance is Hann leakage and float32 alone, independent of Neff.

#include "rta/dsp/AverageCount.h"
#include "rta/dsp/Biquad.h"
#include "rta/dsp/MtwEngine.h"
#include "rta/dsp/MtwResult.h"
#include "rta/dsp/Window.h"
#include "rta/gen/Noise.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <numeric>
#include <vector>

using namespace rta::dsp;

namespace {

constexpr std::size_t kFullLength = 327680;  // 17 frames at N=65536 (see test_mtw_engine.cpp)

std::vector<float> whiteNoise(std::size_t n, std::uint64_t seed) {
    rta::gen::WhiteNoise src(rta::gen::Pcg32(seed, 0x2545F4914F6CDD1Dull));
    std::vector<float> out(n);
    src.process(out);
    return out;
}

double wrapPi(double radians) {
    return std::remainder(radians, 2.0 * std::numbers::pi);
}

}  // namespace

TEST_CASE("overlapCorrelation on a Hann window matches (2 + cos u)/3", "[mtw][window]") {
    Window hann65536(WindowType::Hann, 65536);
    Window hann1024(WindowType::Hann, 1024);

    const double closedForm =
        (2.0 + std::cos(2.0 * std::numbers::pi * 1440.0 / 65536.0)) / 3.0;
    REQUIRE(overlapCorrelation(hann65536.coefficients(), 1440) ==
            Catch::Approx(closedForm).margin(1e-6));
    REQUIRE(overlapCorrelation(hann65536.coefficients(), 1440) ==
            Catch::Approx(0.99682836).margin(1e-6));
    // Lag beyond the window: no comb at all in the bottom band's own
    // fftSize -- the reason band 0 (top, N=1024) has no comb in case 3 below.
    REQUIRE(overlapCorrelation(hann1024.coefficients(), 1440) == 0.0);

    // At a hop lag (C3 point 2's D), the non-wrapping form and the circular
    // closed form are COMPARED, not conflated: the tail correction near the
    // window's zero is small but real.
    const double cQuarter = overlapCorrelation(hann65536.coefficients(), 16384);
    const double circularQuarter =
        (2.0 + std::cos(2.0 * std::numbers::pi * 16384.0 / 65536.0)) / 3.0;
    REQUIRE(cQuarter == Catch::Approx(0.659154943).margin(1e-8));
    REQUIRE(circularQuarter == Catch::Approx(2.0 / 3.0).margin(1e-9));
    REQUIRE(cQuarter < circularQuarter);
}

TEST_CASE("a biquad reads back its closed form across every seam", "[mtw][response]") {
    const Biquad::Coeffs coeffs{0.3, -0.2, 0.1, -0.5, 0.2};
    const auto x = whiteNoise(kFullLength, 61);

    Biquad::State state{};
    std::vector<float> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        y[i] = static_cast<float>(Biquad::processSample(coeffs, state, x[i]));
    }

    MtwConfig cfg;  // shipping defaults: Fifo, fifoDepth = 16 -> Neff = 8.5866271
    MtwEngine engine(cfg);
    engine.process(x, y);
    const auto result = makeMtwResult(engine, Estimator::H1);

    // Skip DC (degenerate phase) and Nyquist (no log axis draws it either).
    for (std::size_t i = 1; i < 1280; ++i) {
        CAPTURE(i, result.frequencyHz[i]);
        const double w = 2.0 * std::numbers::pi * result.frequencyHz[i] / cfg.sampleRate;
        const std::complex<double> z(std::cos(-w), std::sin(-w));
        const std::complex<double> expected =
            (coeffs.b0 + coeffs.b1 * z + coeffs.b2 * z * z) /
            (1.0 + coeffs.a1 * z + coeffs.a2 * z * z);

        const double magMeas = std::abs(result.h[i]);
        const double magExact = std::abs(expected);
        REQUIRE(std::abs(magMeas / magExact - 1.0) < 5e-3);

        const double argExact = std::atan2(expected.imag(), expected.real());
        REQUIRE(std::abs(wrapPi(result.phaseRadians[i] - argExact)) < 0.01);

        // A noiseless LTI system has unit coherence; 0.99 not 0.999 because
        // the estimator is upward-biased by O(1/Neff) from leakage alone.
        const auto coherence = coherenceAt(result, i);
        REQUIRE(coherence.has_value());
        REQUIRE(*coherence > 0.99f);
    }
    // No separate seam test: a band stitched at the wrong offset or
    // normalised differently fails the loop above at the 128-point
    // boundaries -- this whole test IS the seam test.
}

TEST_CASE("a reflection is admitted in proportion to the window autocorrelation",
          "[mtw][response]") {
    constexpr double kGain = 0.5;
    constexpr std::size_t kLag = 1440;  // 30 ms at 48 kHz
    const auto x = whiteNoise(kFullLength, 67);
    std::vector<float> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const float reflected = (i >= kLag) ? x[i - kLag] : 0.0f;
        y[i] = x[i] + static_cast<float>(kGain) * reflected;
    }

    MtwConfig cfg;
    MtwEngine engine(cfg);
    engine.process(x, y);
    const auto result = makeMtwResult(engine, Estimator::H1);
    const auto bands = mtwBands(cfg);

    for (std::size_t b = 0; b < bands.size(); ++b) {
        CAPTURE(b, bands[b].fftSize);
        Window w(cfg.window, bands[b].fftSize);
        const double r = overlapCorrelation(w.coefficients(), kLag);

        double sumSquaredResidual = 0.0;
        std::size_t count = 0;
        std::vector<double> magnitudes;
        for (std::size_t i = bands[b].firstIndex;
             i < bands[b].firstIndex + (bands[b].lastBin - bands[b].firstBin + 1); ++i) {
            const double w0 = 2.0 * std::numbers::pi * result.frequencyHz[i] / cfg.sampleRate;
            const std::complex<double> expected =
                1.0 + kGain * r * std::complex<double>(std::cos(-w0 * kLag), std::sin(-w0 * kLag));
            const double magMeas = std::abs(result.h[i]);
            const double magExact = std::abs(expected);
            sumSquaredResidual += (magMeas - magExact) * (magMeas - magExact);
            magnitudes.push_back(magMeas);
            ++count;
        }
        const double rms = std::sqrt(sumSquaredResidual / static_cast<double>(count));
        REQUIRE(rms < 0.20);

        const double mean =
            std::accumulate(magnitudes.begin(), magnitudes.end(), 0.0) / static_cast<double>(count);
        double variance = 0.0;
        for (const double m : magnitudes) variance += (m - mean) * (m - mean);
        const double stddev = std::sqrt(variance / static_cast<double>(count));

        if (b == bands.size() - 1) {
            // The top band (N = 1024 < 1440): the reflection lag is beyond
            // the window, so r == 0 and the band is the anchor -- no comb.
            REQUIRE(r == 0.0);
            REQUIRE(std::abs(mean - 1.0) < 0.03);
            REQUIRE(stddev < 0.20);
        }
        if (b == 0) {
            // The bottom band (N = 65536): the whole reflection survives the
            // window, so it carries the full comb.
            REQUIRE(r > 0.99);
            double maxMag = 0.0, minMag = 1e9;
            for (const double m : magnitudes) {
                maxMag = std::max(maxMag, m);
                minMag = std::min(minMag, m);
            }
            REQUIRE(maxMag > 1.35);
            REQUIRE(minMag < 0.65);
            REQUIRE(stddev > 0.25);
        }
    }
}

TEST_CASE("the reflection lands in the top band's coherence, not its magnitude",
          "[mtw][response]") {
    constexpr double kGain = 0.5;
    constexpr std::size_t kLag = 1440;
    const auto x = whiteNoise(kFullLength, 71);
    std::vector<float> y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) {
        const float reflected = (i >= kLag) ? x[i - kLag] : 0.0f;
        y[i] = x[i] + static_cast<float>(kGain) * reflected;
    }

    MtwConfig cfg;
    MtwEngine engine(cfg);
    engine.process(x, y);
    const auto result = makeMtwResult(engine, Estimator::H1);
    const auto bands = mtwBands(cfg);

    auto meanCoherence = [&](const MtwBand& band) {
        double sum = 0.0;
        std::size_t n = 0;
        for (std::size_t i = band.firstIndex;
             i < band.firstIndex + (band.lastBin - band.firstBin + 1); ++i) {
            const auto c = coherenceAt(result, i);
            REQUIRE(c.has_value());
            sum += *c;
            ++n;
        }
        return sum / static_cast<double>(n);
    };

    const auto& bottom = bands.front();  // r ~ 1: comb in magnitude, not coherence
    const auto& top = bands.back();      // r == 0: independent-noise case, gamma^2 = 1/(1+a^2)

    // Per-point coherence at Neff = 8.5866271 has sd ~ 0.077 and a +0.023
    // upward bias around the exact 0.80 -- a per-point bound is arithmetically
    // unreliable here (record's own reasoning), so only the mean is asserted.
    const double topMean = meanCoherence(top);
    REQUIRE(std::abs(topMean - 0.80) < 0.05);

    for (std::size_t i = bottom.firstIndex;
         i < bottom.firstIndex + (bottom.lastBin - bottom.firstBin + 1); ++i) {
        const auto c = coherenceAt(result, i);
        REQUIRE(c.has_value());
        REQUIRE(*c > 0.97f);
    }
    const double bottomMean = meanCoherence(bottom);
    REQUIRE(bottomMean > 0.99);

    // The actual claim: the same room reads two different coherences at the
    // two ends of the spectrum -- the reason the 187.5 Hz seam legitimately
    // steps rather than being cross-faded away (record §4).
    REQUIRE(bottomMean - topMean > 0.10);
}
