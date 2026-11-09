// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane app/xover-applied-delay, task T0 (docs/dsp/2026-11-09-xover-applied-delay.md).
//
// QUESTION THIS FILE ANSWERS WITH THE REAL ENGINE, NOT A HAND-BUILT FIXTURE:
// when `referenceDelaySamples = D` is applied, what phase does the STORED
// transfer function carry relative to the physical one?
//
// Closed form. A measurement that lags the reference by L samples has
// H_true[k] = e^{-j 2 pi k L / N}. A positive D discards the first D samples of
// the measurement (DualFftEngine.cpp armDelaySkip), i.e. the engine sees
// y'[n] = y[n + D], an ADVANCE by D, so
//     H_stored[k] = e^{-j 2 pi k (L - D) / N} = H_true[k] * e^{+j 2 pi k D / N}.
// A negative D discards |D| samples of the reference instead, which delays the
// measurement relative to it by |D|: the same expression with D < 0.
//
// The sign is load-bearing for anything that reconciles two captures stored
// with different D. Every ALIGN test builds its own fixture with an assumed
// sign, so none of them can detect a flip; this one drives the engine.
#include "rta/dsp/DualFftEngine.h"
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

/// Sxy[k] against Sxx[k] * exp(-i 2 pi k expectedShift / N), for a kN-periodic
/// signal under a rectangular window (exact, no leakage -- see test_dualfft.cpp's
/// SIGN case for why the periodicity makes the closed form exact). y is a
/// circular rotation by `lag`, so there is no start transient.
void requireStoredPhase(int lag, int appliedDelay, int expectedShift) {
    rta::gen::SyntheticPink source(kN, -12.0, 83);
    std::vector<float> x(kN * 32);
    source.render(x);
    std::vector<float> y(x.size());
    for (std::size_t n = 0; n < x.size(); ++n) {
        y[n] = x[(n + x.size() - static_cast<std::size_t>(lag)) % x.size()];
    }

    DualFftEngine::Config c;
    c.fftSize = kN; c.hopSize = kN; c.window = WindowType::Rectangular;
    c.fifoDepth = 4096;
    c.referenceDelaySamples = appliedDelay;
    DualFftEngine engine(c);
    engine.process(x, y);

    // Same two-term tolerance as test_dualfft.cpp's SIGN case, for the same
    // reason: RealFft is float32, so the absolute error floor is set by the
    // largest (near-DC) bin, not by the bin under test.
    const double peak = *std::max_element(engine.referencePsd().begin(),
                                          engine.referencePsd().end());
    for (std::size_t k = 1; k + 1 < engine.numBins(); ++k) {
        // expectedShift = L - D; H_stored = e^{-j 2 pi k expectedShift / N}.
        const double theta = -2.0 * std::numbers::pi * static_cast<double>(k)
                           * static_cast<double>(expectedShift) / static_cast<double>(kN);
        const double sxx = engine.referencePsd()[k];
        const double tolerance = sxx * 1e-6 + peak * 1e-7;
        REQUIRE(engine.crossPsd()[k].real()
                == Catch::Approx(sxx * std::cos(theta)).margin(tolerance));
        REQUIRE(engine.crossPsd()[k].imag()
                == Catch::Approx(sxx * std::sin(theta)).margin(tolerance));
    }
}

}  // namespace

TEST_CASE("T0: an applied positive D leaves H_stored = H_true * e^{+j w D}",
          "[dualfft][applied_delay]") {
    // L = 9, D = 4: stored delay is L - D = 5, NOT L + D = 13. The two differ
    // in the cosine at k = 1, so a flipped reading cannot satisfy this.
    requireStoredPhase(/*lag=*/9, /*appliedDelay=*/4, /*expectedShift=*/5);
}

TEST_CASE("T0: an applied negative D follows the same expression with D < 0",
          "[dualfft][applied_delay]") {
    // L = 9, D = -4: reference is skipped, stored delay is L - D = 13.
    requireStoredPhase(/*lag=*/9, /*appliedDelay=*/-4, /*expectedShift=*/13);
}

TEST_CASE("T0: D equal to the true lag leaves a real transfer function",
          "[dualfft][applied_delay]") {
    requireStoredPhase(/*lag=*/9, /*appliedDelay=*/9, /*expectedShift=*/0);
}
