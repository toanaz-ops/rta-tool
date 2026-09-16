// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for the five G11 virtual-processor ops -- see
// docs/plans/2026-09-15-L7-align-impl-plan.md Task A, cases A1-A8, and the
// decision record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.5 (the op
// table) and Sec.10.1 / Sec.10.3.
//
// Every acceptance below is a closed-form identity evaluated in the fixture
// itself, per CLAUDE.md's verification standard preference 1. Nothing here
// asserts a value the implementation produced.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Biquad.h"
#include "rta/dsp/VirtualProcessor.h"
#include "rta/eq/BiquadDesign.h"
#include "rta/eq/FilterSpec.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <random>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::dsp;

namespace {

constexpr double kPi = std::numbers::pi;

/// Wrap to [-pi, pi]. std::remainder is the exact-halfway-correct form: it
/// rounds the quotient to nearest, so no bin sitting on +-pi lands a full
/// cycle away the way fmod would.
double wrapToPi(double radians) noexcept { return std::remainder(radians, 2.0 * kPi); }

/// The fixture axis for the delay cases (plan A1/A2): 512 bins at 46.875 Hz,
/// which is a 1024-point FFT at 48 kHz.
constexpr double kBinWidthHz = 46.875;
constexpr std::size_t kBins = 512;
constexpr double kTauSeconds = 17.3 / 48000.0;  // deliberately fractional

}  // namespace

TEST_CASE("G11 delay leaves every magnitude at unity -- a pure phase rotation",
          "[virtual_processor]") {
    // A1. Record Sec.5 row 1: H'(f) = H(f) e^{-j2pi f tau}. |e^{-jx}| == 1 for
    // real x is the identity; with H == 1 the output magnitude is that constant
    // at every bin, with no interpolation anywhere to erode it (the delay is
    // exact for fractional tau because it is applied in the frequency domain).
    const std::vector<std::complex<double>> in(kBins, std::complex<double>{ 1.0, 0.0 });
    std::vector<std::complex<double>> out(kBins);

    applyDelay(in, kBinWidthHz, kTauSeconds, out);

    double worst = 0.0;
    for (std::size_t k = 0; k < kBins; ++k) {
        worst = std::max(worst, std::abs(std::abs(out[k]) - 1.0));
    }
    INFO("worst |1 - |out|| over " << kBins << " bins: " << worst);
    CHECK(worst <= 1e-15);
}

TEST_CASE("G11 delay carries the NEGATIVE exponent -- the sign a flipped convention loses",
          "[virtual_processor]") {
    // A2. The sign is the load-bearing half: memory/dual-fft-conventions.md
    // item 2 records that a flipped delay sign does not fail to compensate, it
    // compensates the wrong way and doubles the error. Two assertions:
    //   (i) the whole phase ramp equals wrapToPi(-2 pi f tau) at every bin;
    //   (ii) at the bin nearest f = 1/(4 tau) -- a quarter cycle of lag -- the
    //        argument is NEGATIVE. (ii) is what goes red on e^{+j2pi f tau};
    //        (i) alone would too, but (ii) says so in one number.
    const std::vector<std::complex<double>> in(kBins, std::complex<double>{ 1.0, 0.0 });
    std::vector<std::complex<double>> out(kBins);

    applyDelay(in, kBinWidthHz, kTauSeconds, out);

    double worst = 0.0;
    for (std::size_t k = 0; k < kBins; ++k) {
        const double f = static_cast<double>(k) * kBinWidthHz;
        const double expected = wrapToPi(-2.0 * kPi * f * kTauSeconds);
        worst = std::max(worst, std::abs(wrapToPi(std::arg(out[k]) - expected)));
    }
    INFO("worst wrapped phase error over " << kBins << " bins: " << worst);
    CHECK(worst <= 1e-12);

    // f = 1/(4 tau) = 693.6 Hz -> bin 15 (703.125 Hz), a lag of just over a
    // quarter cycle, so the wrapped argument is in (-pi, 0).
    const std::size_t quarterCycleBin =
        static_cast<std::size_t>(std::llround(1.0 / (4.0 * kTauSeconds) / kBinWidthHz));
    INFO("quarter-cycle bin " << quarterCycleBin << " arg = " << std::arg(out[quarterCycleBin]));
    CHECK(std::arg(out[quarterCycleBin]) < 0.0);
}

TEST_CASE("G11 polarity is the sign bit -- magnitude bitwise unchanged, phase turned by pi",
          "[virtual_processor]") {
    // A3. H' = -H. Negation in IEEE-754 touches only the sign bit of each
    // component, and std::abs(complex) is a function of the components'
    // magnitudes alone, so the magnitude comparison is exact -- BITWISE, not
    // within a tolerance. Anything looser would not notice a `-1.0 *` that
    // rounds.
    std::mt19937 rng{ 20260915u };
    std::uniform_real_distribution<double> dist{ -4.0, 4.0 };

    std::vector<std::complex<double>> in(256);
    for (auto& z : in) z = std::complex<double>{ dist(rng), dist(rng) };
    std::vector<std::complex<double>> out(in.size());

    applyPolarity(in, out);

    bool magnitudesBitwiseEqual = true;
    double worstPhase = 0.0;
    for (std::size_t k = 0; k < in.size(); ++k) {
        magnitudesBitwiseEqual = magnitudesBitwiseEqual && (std::abs(out[k]) == std::abs(in[k]));
        worstPhase =
            std::max(worstPhase, std::abs(wrapToPi(std::arg(out[k]) - std::arg(in[k]) - kPi)));
    }
    CHECK(magnitudesBitwiseEqual);
    INFO("worst |wrapToPi(arg out - arg in - pi)|: " << worstPhase);
    CHECK(worstPhase <= 1e-15);
}

TEST_CASE("G11 gain moves dB by exactly 20log10(g) and leaves the phase where it was",
          "[virtual_processor]") {
    // A4. Two different tolerances, on purpose.
    //
    // The dB identity is exact algebra: 20log10|g H| - 20log10|H| = 20log10 g.
    //
    // The phase is NOT asserted bitwise for a general g. atan2(g*b, g*a) is
    // not required by IEEE-754 to equal atan2(b, a) -- the products round
    // separately and the three libms CI runs (MSVC, glibc, Apple) each round
    // their own way. For the exact powers of two the scaling IS exact, so
    // there the phase is asserted bitwise.
    std::mt19937 rng{ 7u };
    std::uniform_real_distribution<double> dist{ -4.0, 4.0 };

    std::vector<std::complex<double>> in(256);
    for (auto& z : in) z = std::complex<double>{ dist(rng), dist(rng) };
    std::vector<std::complex<double>> out(in.size());

    constexpr double kEps = std::numeric_limits<double>::epsilon();

    for (const double g : { 0.5, 1.0, 2.0, 10.0 }) {
        applyGain(in, g, out);

        const double expectedDb = 20.0 * std::log10(g);
        double worstDb = 0.0;
        double worstPhase = 0.0;
        bool phaseBitwise = true;
        for (std::size_t k = 0; k < in.size(); ++k) {
            const double deltaDb = 20.0 * std::log10(std::abs(out[k]))
                                 - 20.0 * std::log10(std::abs(in[k]));
            worstDb = std::max(worstDb, std::abs(deltaDb - expectedDb));
            worstPhase =
                std::max(worstPhase, std::abs(wrapToPi(std::arg(out[k]) - std::arg(in[k]))));
            phaseBitwise = phaseBitwise && (std::arg(out[k]) == std::arg(in[k]));
        }
        INFO("g = " << g << " worst dB error " << worstDb << ", worst phase error " << worstPhase);
        CHECK(worstDb <= 1e-12);
        CHECK(worstPhase <= 4.0 * kEps);
        if (g == 0.5 || g == 1.0 || g == 2.0) {
            CHECK(phaseBitwise);  // scaling by a power of two is exact
        }
    }
}

TEST_CASE("G11 sum of two unit sources is 2|cos(phi/2)| -- the interference identity",
          "[virtual_processor]") {
    // A7. |1 + e^{j phi}| = |e^{-j phi/2} + e^{+j phi/2}| = 2|cos(phi/2)|.
    // Record Sec.5's sum row, and the reference marks the G18 surface draws:
    // +6.0206 dB at 0 deg, +3.0103 dB at 90 deg, 0 dB at 120 deg, a null at
    // 180 deg. All doubles constructed in the fixture, so
    // memory/float32-fft-precision.md does not apply and 1e-9 is generous.
    for (const double degrees : { 0.0, 30.0, 60.0, 90.0, 120.0, 150.0, 180.0 }) {
        const double phi = degrees * kPi / 180.0;
        const std::vector<std::complex<double>> a(8, std::complex<double>{ 1.0, 0.0 });
        std::vector<std::complex<double>> b(8);
        for (auto& z : b) z = std::polar(1.0, phi);

        const auto summed = sumResponses(a, b, std::nullopt, std::nullopt);
        REQUIRE(summed.h.size() == 8);

        if (degrees == 180.0) {
            INFO("180 deg null |H_sum| = " << std::abs(summed.h.front()));
            CHECK(std::abs(summed.h.front()) < 1e-15);
            continue;
        }

        const double expectedDb = 20.0 * std::log10(2.0 * std::abs(std::cos(phi * 0.5)));
        for (const auto& z : summed.h) {
            INFO(degrees << " deg, expected " << expectedDb << " dB");
            CHECK_THAT(20.0 * std::log10(std::abs(z)), WithinAbs(expectedDb, 1e-9));
        }
    }

    // The three marks, stated as numbers so a reader can check them by hand.
    CHECK_THAT(20.0 * std::log10(2.0), WithinAbs(6.020599913279624, 1e-12));
    CHECK_THAT(20.0 * std::log10(2.0 * std::cos(kPi / 4.0)), WithinAbs(3.010299956639812, 1e-12));
    CHECK_THAT(20.0 * std::log10(2.0 * std::cos(kPi / 3.0)), WithinAbs(0.0, 1e-12));
}

TEST_CASE("The sum's trust is min(gamma^2), and an absent coherence leaves it EMPTY",
          "[virtual_processor]") {
    // A8. Two halves.
    //
    // (i) When both inputs carry coherence the trust is the per-bin minimum --
    //     record Sec.5. The field is deliberately NOT called `coherence`: the
    //     sum is not an estimate a cross-spectrum defines, and
    //     coherence_gate_is_not_bypassed stays true by construction rather
    //     than by exemption.
    //
    // (ii) When either input's coherence is absent the trust vector is EMPTY
    //      and trustPresent is false -- NOT a vector of zeros. A zero-filled
    //      placeholder would be plotted as "totally untrusted", which is a
    //      different claim from "not measured"
    //      (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    const std::vector<std::complex<double>> a(4, std::complex<double>{ 1.0, 0.0 });
    const std::vector<std::complex<double>> b(4, std::complex<double>{ 0.0, 1.0 });

    const std::optional<std::vector<float>> cohA{ std::vector<float>{ 0.9f, 0.2f, 0.55f, 1.0f } };
    const std::optional<std::vector<float>> cohB{ std::vector<float>{ 0.3f, 0.8f, 0.55f, 0.0f } };
    const std::vector<float> expected{ 0.3f, 0.2f, 0.55f, 0.0f };

    const auto both = sumResponses(a, b, cohA, cohB);
    CHECK(both.trustPresent);
    REQUIRE(both.summationTrust.size() == expected.size());
    for (std::size_t k = 0; k < expected.size(); ++k) {
        CHECK_THAT(static_cast<double>(both.summationTrust[k]),
                   WithinAbs(static_cast<double>(expected[k]), 1e-7));
    }

    for (const auto& pair : { std::pair{ cohA, std::optional<std::vector<float>>{} },
                              std::pair{ std::optional<std::vector<float>>{}, cohB },
                              std::pair{ std::optional<std::vector<float>>{},
                                         std::optional<std::vector<float>>{} } }) {
        const auto absent = sumResponses(a, b, pair.first, pair.second);
        CHECK_FALSE(absent.trustPresent);
        CHECK(absent.summationTrust.empty());
        CHECK(absent.h.size() == a.size());  // the SUM still exists; only its trust does not
    }
}
