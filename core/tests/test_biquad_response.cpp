// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for BiquadResponse -- see
// docs/plans/2026-09-06-L7-wave0-impl-plan.md Task W0-1. Cases T1-T5, each a
// closed-form identity against Biquad.h's own DF2T H(z) definition (Biquad.h:75).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/BiquadResponse.h"
#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/dsp/OctaveBands.h"

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::dsp;

namespace {
constexpr double kPi = std::numbers::pi;
}  // namespace

TEST_CASE("Biquad response endpoints at DC and Nyquist are real -- z = +-1 exactly",
          "[biquad_response]") {
    // T1 (first). z = e^{j0} = 1 and z = e^{jpi} = -1 are the two points where
    // the DF2T transfer function H(z) = (b0+b1 z^-1+b2 z^-2)/(1+a1 z^-1+a2 z^-2)
    // collapses to real algebra by substitution alone -- no trig identity needed.
    Biquad::Coeffs c{ 0.7, -0.3, 0.15, -0.85, 0.42 };

    const auto hDc = biquadResponse(c, 0.0);
    const double expectedDc = (c.b0 + c.b1 + c.b2) / (1.0 + c.a1 + c.a2);
    CHECK_THAT(hDc.real(), WithinAbs(expectedDc, 1e-12));
    CHECK_THAT(hDc.imag(), WithinAbs(0.0, 1e-12));

    const auto hNy = biquadResponse(c, kPi);
    const double expectedNy = (c.b0 - c.b1 + c.b2) / (1.0 - c.a1 + c.a2);
    CHECK_THAT(hNy.real(), WithinAbs(expectedNy, 1e-12));
    CHECK_THAT(hNy.imag(), WithinAbs(0.0, 1e-12));
}

TEST_CASE("A passthrough section (b0=1, everything else 0) reads 1+0j at every frequency",
          "[biquad_response]") {
    Biquad::Coeffs c{ 1.0, 0.0, 0.0, 0.0, 0.0 };
    for (int k = 1; k < 64; ++k) {
        const double w = kPi * static_cast<double>(k) / 64.0;
        const auto h = biquadResponse(c, w);
        CAPTURE(w);
        CHECK_THAT(h.real(), WithinAbs(1.0, 1e-12));
        CHECK_THAT(h.imag(), WithinAbs(0.0, 1e-12));
    }
}

TEST_CASE("20log10|biquadResponse| equals the NEGATIVE of BiquadCascade::attenuationDb",
          "[biquad_response]") {
    // T3, the consistency lock. ALIGN Sec.5 writes 20log10|H_i| ==
    // sectionAttenuationDb; Biquad.h:75 defines attenuationDb as decibels of
    // ATTENUATION (positive = down), so the two spellings of the same |H| agree
    // only with the sign corrected -- W0-R3. Locked here against real
    // ButterworthDesign::bandPass sections, which exist independently of this
    // task's own code, so the check is not circular. (A second lock against
    // designBiquad's own coefficients is added once BiquadDesign lands --
    // Task W0-3 -- as more cases in this SAME test, not a new TEST_CASE, so the
    // ctest count this task's Accept line names does not move.)
    const auto low = ButterworthDesign::bandPass(891.25, 1122.02, 48000.0, 6);

    for (const auto& c : low.sections) {
        for (int k = 0; k < 64; ++k) {
            // Offset by half a step so w never lands exactly on 0, where the
            // attenuation and the response are both well-defined but a stray
            // sign error in one term is easiest to hide.
            const double w = kPi * (static_cast<double>(k) + 0.5) / 64.0;
            const double lhs = 20.0 * std::log10(std::abs(biquadResponse(c, w)));
            const double rhs = -BiquadCascade::attenuationDb(std::span(&c, 1), w);
            CAPTURE(w, lhs, rhs);
            CHECK_THAT(lhs, WithinAbs(rhs, 1e-9));
        }
    }
}

TEST_CASE("cascadeResponse is the product of its sections' own responses",
          "[biquad_response]") {
    Biquad::Coeffs c1{ 0.5, 0.3, -0.1, -1.2, 0.5 };
    Biquad::Coeffs c2{ 0.9, -0.4, 0.2, -0.8, 0.3 };
    std::vector<Biquad::Coeffs> sections{ c1, c2 };

    for (int k = 1; k < 64; ++k) {
        const double w = kPi * static_cast<double>(k) / 64.0;
        const auto expected = biquadResponse(c1, w) * biquadResponse(c2, w);
        const auto actual = cascadeResponse(sections, w);
        CAPTURE(w);
        CHECK_THAT(actual.real(), WithinAbs(expected.real(), 1e-12));
        CHECK_THAT(actual.imag(), WithinAbs(expected.imag(), 1e-12));
    }
}

TEST_CASE("A pole 8.7e-5 from the unit circle (lowest 1/3-octave band at 48 kHz) "
          "stays finite through its own resonance",
          "[biquad_response]") {
    // T5. Biquad.h:17-18 names this exact fixture as the closest pole radius
    // this bank works at; the point of the test is that biquadResponse (which
    // divides by a denominator that gets very small here) does not overflow or
    // NaN where attenuationDb's own real/imaginary-term form does not either.
    const OctaveBands bands(3, 20.0, 20000.0);
    const auto& lowestBand = bands[0];
    const auto low = ButterworthDesign::bandPass(lowestBand.lower, lowestBand.upper,
                                                  48000.0, 6);
    REQUIRE(low.maxPoleRadius > 1.0 - 2e-4);   // confirms this IS the close-pole fixture
    REQUIRE(low.maxPoleRadius < 1.0);

    const double wResonance = 2.0 * kPi * lowestBand.centre / 48000.0;
    const auto h = cascadeResponse(low.sections, wResonance);

    CHECK(std::isfinite(h.real()));
    CHECK(std::isfinite(h.imag()));
    CHECK(std::isfinite(std::abs(h)));
}
