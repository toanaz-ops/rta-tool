// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/AverageCount.h"
#include "rta/dsp/Window.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace rta::dsp;

TEST_CASE("overlap correlation has closed-form values", "[average][overlap]") {
    const Window hann(WindowType::Hann, 1024);
    const Window rect(WindowType::Rectangular, 1024);

    // Lag 0 is the definition's normalisation.
    REQUIRE(overlapCorrelation(hann.coefficients(), 0) == Catch::Approx(1.0).margin(1e-12));

    // Periodic Hann, 50 % overlap. w[n]w[n+N/2] = 0.25*sin^2(2*pi*n/N), summed
    // over the N/2 samples that overlap = N/16; sum(w^2) = 3N/8; ratio = 1/6.
    // This is a derivation, not a measurement -- if it fails, the code is wrong.
    REQUIRE(overlapCorrelation(hann.coefficients(), 512) == Catch::Approx(1.0 / 6.0).margin(1e-12));

    // Rectangular at 50 %: N/2 ones over N ones.
    REQUIRE(overlapCorrelation(rect.coefficients(), 512) == Catch::Approx(0.5).margin(1e-12));

    // No overlap at all: the frames share nothing.
    REQUIRE(overlapCorrelation(hann.coefficients(), 1024) == Catch::Approx(0.0).margin(1e-15));
    REQUIRE(overlapCorrelation(hann.coefficients(), 4096) == Catch::Approx(0.0).margin(1e-15));
}

TEST_CASE("non-overlapped frames are fully independent", "[average]") {
    const Window hann(WindowType::Hann, 1024);
    for (std::size_t k : {1u, 2u, 7u, 64u}) {
        REQUIRE(fifoEffectiveAverages(hann.coefficients(), 1024, k)
                == Catch::Approx(static_cast<double>(k)).margin(1e-12));
    }
}

TEST_CASE("50 % overlapped Hann frames are derated by exactly 1/6", "[average]") {
    const Window hann(WindowType::Hann, 1024);
    // Only m = 1 contributes: c(1024) is already zero. So the whole formula
    // collapses to K / (1 + 2*(1 - 1/K)*(1/6)^2).
    for (std::size_t k : {1u, 2u, 10u, 1000u}) {
        const double kd = static_cast<double>(k);
        const double expected = kd / (1.0 + 2.0 * (1.0 - 1.0 / kd) * (1.0 / 36.0));
        REQUIRE(fifoEffectiveAverages(hann.coefficients(), 512, k)
                == Catch::Approx(expected).margin(1e-9));
    }
    // One frame is one average, whatever the overlap.
    REQUIRE(fifoEffectiveAverages(hann.coefficients(), 256, 1) == Catch::Approx(1.0).margin(1e-12));
}

TEST_CASE("75 % overlap sums EVERY lag, not just the first", "[average]") {
    const Window hann(WindowType::Hann, 1024);
    // Harris 1978 Table 1 gives 0.659 for the one-hop correlation at 75 %.
    REQUIRE(overlapCorrelation(hann.coefficients(), 256) == Catch::Approx(0.659).margin(0.01));

    // At hop = N/4 three lags overlap -- 256, 512, 768 -- and c(1024) is zero.
    // Assert the FORMULA against those three correlations rather than a loose
    // band: a band wide enough to hold 0.5196 also holds the 0.5351 an
    // implementation produces when it stops summing after the first lag, so it
    // would pass for code that ignores two thirds of the overlap.
    //
    // The correlations are independently pinned ONLY at lags 0, 512, 1024 and
    // 4096 -- exactly, by the closed-form test above -- and loosely at 256
    // (Harris 1978, +/-0.01, checked again just above). c(768) is pinned
    // nowhere else in the suite: it is read here straight from
    // overlapCorrelation, the function under test, so a bug that affects only
    // that one lag would cancel between this test's expectation and its
    // result. Low risk in practice -- three of the four lags this test relies
    // on ARE independently pinned -- but it is not the "stops being a mirror
    // of the implementation" guarantee that claim implied.
    constexpr std::size_t kK = 4000;
    double penalty = 0.0;
    for (std::size_t m = 1; m <= 3; ++m) {
        const double c = overlapCorrelation(hann.coefficients(), m * 256);
        penalty += (1.0 - static_cast<double>(m) / kK) * c * c;
    }
    const double expected = static_cast<double>(kK) / (1.0 + 2.0 * penalty);
    REQUIRE(fifoEffectiveAverages(hann.coefficients(), 256, kK)
            == Catch::Approx(expected).margin(1e-6));

    // And the headline number an operator would recognise: 75 % overlap costs
    // roughly half the averages.
    REQUIRE(expected / kK == Catch::Approx(0.52).margin(0.01));
}

TEST_CASE("exponential averaging counts its own memory", "[average][exponential]") {
    const Window hann(WindowType::Hann, 1024);

    // Seeded from frame one: one frame is one average, for every alpha.
    for (double a : {0.01, 0.2, 1.0}) {
        REQUIRE(exponentialEffectiveAverages(hann.coefficients(), 1024, a, 1)
                == Catch::Approx(1.0).margin(1e-12));
    }
    // alpha = 1 keeps no memory at all: always exactly one average.
    REQUIRE(exponentialEffectiveAverages(hann.coefficients(), 1024, 1.0, 500)
            == Catch::Approx(1.0).margin(1e-12));

    // Settled, non-overlapped: (2-a)/a. (0.9)^998 is ~1e-46, so 500 frames is
    // the limit for any tolerance we can measure.
    REQUIRE(exponentialEffectiveAverages(hann.coefficients(), 1024, 0.1, 500)
            == Catch::Approx(19.0).margin(1e-9));

    // Monotone in frames, and never above the settled value.
    double previous = 0.0;
    for (std::size_t k = 1; k <= 40; ++k) {
        const double n = exponentialEffectiveAverages(hann.coefficients(), 1024, 0.1, k);
        REQUIRE(n >= previous);
        REQUIRE(n <= 19.0 + 1e-9);
        previous = n;
    }

    // Overlap derates it, and never below one average.
    const double settledOverlapped = exponentialEffectiveAverages(hann.coefficients(), 512, 0.1, 500);
    REQUIRE(settledOverlapped < 19.0);
    REQUIRE(settledOverlapped == Catch::Approx(1.0 + 18.0 / (1.0 + 2.0 / 36.0)).margin(1e-9));
}

TEST_CASE("hop == 0 is one average, not an infinite search for a zero lag",
          "[average][hop-zero]") {
    // hop == 0 means every "next" frame is the SAME samples again: the block
    // never advances, so c(m*hop) == c(0) == 1.0 for every m the overlap
    // loops would try, and neither loop ever reaches a lag at which the
    // correlation is finally zero. A function that keeps searching for one
    // hangs instead of returning. Whatever frame count is claimed, hop 0
    // frames carry exactly one independent average's worth of information.
    const Window hann(WindowType::Hann, 1024);

    for (std::size_t frames : {1u, 2u, 64u}) {
        REQUIRE(fifoEffectiveAverages(hann.coefficients(), 0, frames)
                == Catch::Approx(1.0).margin(1e-12));
    }
    for (double alpha : {0.01, 0.1, 1.0}) {
        for (std::size_t frames : {1u, 2u, 64u}) {
            REQUIRE(exponentialEffectiveAverages(hann.coefficients(), 0, alpha, frames)
                    == Catch::Approx(1.0).margin(1e-12));
        }
    }
}
