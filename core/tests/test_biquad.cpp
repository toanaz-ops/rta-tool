// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for Biquad.h / BiquadCascade -- see
// docs/plans/2026-08-27-filterbank-impl-plan.md section 5.1. Cases B1-B7,
// each resting on a closed form named in its title rather than on a recorded
// value from the implementation itself.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Biquad.h"

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::dsp;

TEST_CASE("A section with b = {1,0,0} and no poles passes its input through", "[biquad]") {
    Biquad::Coeffs c{ 1.0, 0.0, 0.0, 0.0, 0.0 };
    Biquad::State s;
    for (double x : { 1.0, -3.5, 0.0, 42.25, -0.001 }) {
        CHECK(Biquad::processSample(c, s, x) == x);
    }
}

TEST_CASE("The impulse response of a zero-pole section is exactly b0, b1, b2, 0, 0",
          "[biquad]") {
    // No poles (a1 = a2 = 0): the DF2T recursion degenerates to a plain FIR,
    // so the impulse response is the coefficient vector itself, padded with
    // zeros -- a closed form, not a recorded value.
    Biquad::Coeffs c{ 0.25, -0.5, 0.125, 0.0, 0.0 };
    Biquad::State s;

    double y0 = Biquad::processSample(c, s, 1.0);
    double y1 = Biquad::processSample(c, s, 0.0);
    double y2 = Biquad::processSample(c, s, 0.0);
    double y3 = Biquad::processSample(c, s, 0.0);

    CHECK_THAT(y0, WithinRel(c.b0, 1e-15));
    CHECK_THAT(y1, WithinRel(c.b1, 1e-15));
    CHECK_THAT(y2, WithinRel(c.b2, 1e-15));
    CHECK(y3 == 0.0);
}

TEST_CASE("A one-pole section decays as r^n", "[biquad]") {
    // b = {1,0,0}, a1 = -r, a2 = 0 gives H(z) = 1 / (1 - r z^-1), whose
    // impulse response is r^n exactly.
    constexpr double r = 0.97;
    Biquad::Coeffs c{ 1.0, 0.0, 0.0, -r, 0.0 };
    Biquad::State s;

    double x = 1.0;
    for (int n = 0; n <= 200; ++n) {
        const double y = Biquad::processSample(c, s, x);
        x = 0.0;
        CAPTURE(n);
        CHECK_THAT(y, WithinRel(std::pow(r, n), 1e-12));
    }
}

TEST_CASE("reset() restores a cascade to its construction state", "[biquad]") {
    std::vector<Biquad::Coeffs> sections{
        { 0.5, 0.3, -0.1, -1.2, 0.5 },
        { 0.9, -0.4, 0.2, -0.8, 0.3 },
    };
    BiquadCascade cascade(sections);

    std::vector<float> in(500), out1(500), out2(500);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<float>(std::sin(0.1 * static_cast<double>(i)));
    }

    cascade.process(in, out1);
    cascade.reset();
    cascade.process(in, out2);

    for (std::size_t i = 0; i < in.size(); ++i) {
        CAPTURE(i);
        CHECK(out1[i] == out2[i]);
    }
}

TEST_CASE("A cascade equals its sections applied one after another", "[biquad]") {
    std::vector<Biquad::Coeffs> sections{
        { 0.2, 0.1, -0.05, -1.1, 0.4 },
        { 0.7, -0.2, 0.1, -0.6, 0.2 },
        { 1.0, 0.0, 0.0, -0.3, 0.05 },
    };
    BiquadCascade cascade(sections);

    std::vector<Biquad::State> state(sections.size());

    for (int n = 0; n < 100; ++n) {
        const double x = std::sin(0.07 * n) + 0.3 * std::cos(0.31 * n);

        double byHand = x;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            byHand = Biquad::processSample(sections[i], state[i], byHand);
        }

        CAPTURE(n);
        CHECK_THAT(cascade.processSample(x), WithinRel(byHand, 1e-12));
    }
}

TEST_CASE("attenuationDb of a half-sum section is -20log10 of cos(w/2)", "[biquad]") {
    // b = {0.5, 0.5, 0}, a = 0 gives H(e^{jw}) = e^{-jw/2} cos(w/2) exactly.
    Biquad::Coeffs c{ 0.5, 0.5, 0.0, 0.0, 0.0 };
    for (double w : { 0.01, 0.3, 1.0, 2.0, 3.0 }) {
        const double expected = -20.0 * std::log10(std::abs(std::cos(w / 2.0)));
        CAPTURE(w);
        CHECK_THAT(BiquadCascade::attenuationDb(std::span(&c, 1), w), WithinAbs(expected, 1e-10));
    }
}

TEST_CASE("attenuationDb of a cascade is the sum of its sections'", "[biquad]") {
    std::vector<Biquad::Coeffs> sections{
        { 0.5, 0.5, 0.0, 0.0, 0.0 },
        { 0.3, -0.1, 0.2, -0.5, 0.1 },
        { 1.0, 0.2, -0.3, -0.9, 0.4 },
    };
    constexpr double w = 1.3;

    double expected = 0.0;
    for (const auto& c : sections) {
        expected += BiquadCascade::attenuationDb(std::span(&c, 1), w);
    }

    CHECK_THAT(BiquadCascade::attenuationDb(sections, w), WithinAbs(expected, 1e-10));
}
