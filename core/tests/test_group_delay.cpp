// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/GroupDelay.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rta::dsp;

TEST_CASE("a pure delay has the discrete group delay its own formula predicts",
          "[groupdelay]") {
    // H[k] = exp(-2*pi*i*k*D/N). Feeding that through the central difference
    // gives, exactly:
    //     tau = N * sin(2*pi*m*D/N) / (2*pi*m*fs)
    // which tends to D/fs as the smoothing width m shrinks. Asserting D/fs
    // directly would be asserting the CONTINUOUS answer against a DISCRETE
    // routine, and would only pass by being loose enough to hide a real error.
    constexpr std::size_t kN = 1024, kBins = kN / 2 + 1;
    constexpr double kFs = 48000.0, kD = 24.0;
    const double binWidth = kFs / static_cast<double>(kN);

    std::vector<std::complex<double>> h(kBins);
    for (std::size_t k = 0; k < kBins; ++k) {
        const double theta = -2.0 * std::numbers::pi * static_cast<double>(k) * kD / kN;
        h[k] = {std::cos(theta), std::sin(theta)};
    }

    for (std::size_t m : {1u, 2u, 4u}) {
        std::vector<double> tau(kBins, 0.0);
        groupDelaySeconds(h, binWidth, m, tau);
        const double md = static_cast<double>(m);
        const double expected = kN * std::sin(2.0 * std::numbers::pi * md * kD / kN)
                              / (2.0 * std::numbers::pi * md * kFs);
        for (std::size_t k = m; k + m < kBins; ++k) {
            REQUIRE(tau[k] == Catch::Approx(expected).margin(1e-12));
        }
        // And it converges to the continuous answer as m shrinks: sin(x)/x
        // deviates from 1 by ~x^2/6 (x = 2*pi*m*D/N), so the discrete/
        // continuous gap grows with m -- 0.36 % at m=1, 1.44 % at m=2, 5.68 %
        // at m=4 for this N and D. A flat 2 % bound holds for m=1 and m=2 but
        // is mathematically false at m=4 regardless of the implementation, so
        // the tolerance here tracks that same x^2/6 term rather than
        // asserting a fixed percentage the closed form itself does not obey.
        const double x = 2.0 * std::numbers::pi * md * kD / kN;
        REQUIRE(expected == Catch::Approx(kD / kFs).epsilon(x * x / 6.0 + 0.005));

        // The edges (k < m and k >= kBins - m) fall back to a one-sided
        // difference whose width the routine fits to the space available --
        // asserting that exact width would just restate the implementation's
        // choice, not test it. Instead assert the BAND any legal fallback
        // width must land in: the sinc factor N*sin(2*pi*j*D/N)/(2*pi*j*fs) is
        // monotone decreasing in j on this range (2*pi*m*D/N < pi here), so
        // every width from j=1 up to j=m produces a value between the
        // m-width answer (`expected`, the narrowest -- least like the
        // continuous limit) and the continuous D/fs (the j->0 limit,
        // widest). This is a BOUND derived from that monotonicity, not an
        // identity: it cannot pin the exact fallback width, but it does rule
        // out a fallback denominator that is wrong by a fixed factor -- such
        // as a doubled one-sided denominator, which halves the result and
        // lands far outside this band.
        const double continuous = kD / kFs;
        for (std::size_t k = 0; k < m; ++k) {
            REQUIRE(tau[k] >= expected * 0.999);
            REQUIRE(tau[k] <= continuous * 1.001);
        }
        for (std::size_t k = kBins - m; k < kBins; ++k) {
            REQUIRE(tau[k] >= expected * 0.999);
            REQUIRE(tau[k] <= continuous * 1.001);
        }
    }
}

TEST_CASE("a constant response has zero group delay", "[groupdelay]") {
    std::vector<std::complex<double>> h(513, {0.7, 0.0});
    std::vector<double> tau(513, 1.0);
    groupDelaySeconds(h, 46.875, 1, tau);
    for (const double t : tau) REQUIRE(t == Catch::Approx(0.0).margin(1e-15));
}

TEST_CASE("a pure delay at non-unit gain still gives the discrete closed form",
          "[groupdelay]") {
    // The constant-response case above uses |H| == 1, so it cannot see a
    // missing `/|H|^2` divisor (dividing or not dividing by 1 looks the
    // same). This is the case the plan calls out as needed to catch that
    // mutation: a non-unit magnitude carried through a pure delay. |H|^2 is
    // constant across k (it does not depend on k at all), so it must cancel
    // out of the closed form -- the expected tau is IDENTICAL to the unit-gain
    // pure-delay case above, for every k and every m.
    constexpr std::size_t kN = 1024, kBins = kN / 2 + 1;
    constexpr double kFs = 48000.0, kD = 24.0, kGain = 0.5;
    const double binWidth = kFs / static_cast<double>(kN);

    std::vector<std::complex<double>> h(kBins);
    for (std::size_t k = 0; k < kBins; ++k) {
        const double theta = -2.0 * std::numbers::pi * static_cast<double>(k) * kD / kN;
        h[k] = {kGain * std::cos(theta), kGain * std::sin(theta)};
    }

    constexpr std::size_t m = 2;
    std::vector<double> tau(kBins, 0.0);
    groupDelaySeconds(h, binWidth, m, tau);
    const double md = static_cast<double>(m);
    const double expected = kN * std::sin(2.0 * std::numbers::pi * md * kD / kN)
                          / (2.0 * std::numbers::pi * md * kFs);
    for (std::size_t k = m; k + m < kBins; ++k) {
        REQUIRE(tau[k] == Catch::Approx(expected).margin(1e-12));
    }
}

TEST_CASE("group delay refuses impossible arguments", "[groupdelay]") {
    std::vector<std::complex<double>> h(16, {1.0, 0.0});
    std::vector<double> tau(16, 0.0), shortOut(4, 0.0);
    REQUIRE_THROWS_AS(groupDelaySeconds(h, 1.0, 0, tau), std::invalid_argument);
    REQUIRE_THROWS_AS(groupDelaySeconds(h, 1.0, 1, shortOut), std::invalid_argument);
    REQUIRE_THROWS_AS(groupDelaySeconds(h, 0.0, 1, tau), std::invalid_argument);
}

TEST_CASE("a null bin does not poison its neighbours", "[groupdelay]") {
    // |H| = 0 makes the closed form 0/0. It must produce 0, not NaN: one NaN
    // in a trace propagates through every min/max the view computes.
    std::vector<std::complex<double>> h(65, {1.0, 0.0});
    h[32] = {0.0, 0.0};
    std::vector<double> tau(65, 0.0);
    groupDelaySeconds(h, 46.875, 1, tau);
    for (const double t : tau) REQUIRE(std::isfinite(t));
}
