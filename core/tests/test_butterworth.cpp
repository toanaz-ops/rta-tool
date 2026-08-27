// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for ButterworthDesign -- see
// docs/plans/2026-08-27-filterbank-impl-plan.md section 5.2. Cases W1-W8.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/dsp/OctaveBands.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::dsp;

namespace {

constexpr double kNyquistEdgeFraction = ButterworthDesign::kNyquistEdgeFraction;

}  // namespace

TEST_CASE("Six sections means six sections and twelve poles", "[butterworth]") {
    // N is scipy's N: the SOS-section count, the analog prototype order, and
    // HALF the true band-pass pole count. This is the order-convention lock --
    // see docs/dsp/2026-08-27-filterbank.md and plan trap 1. Do not relax it.
    const auto result = ButterworthDesign::bandPass(891.25, 1122.02, 48000.0, 6);
    REQUIRE(result.sections.size() == 6);

    const auto zpk = ButterworthDesign::bandPassZpk(891.25, 1122.02, 48000.0, 6);
    CHECK(zpk.poles.size() == 12);
    CHECK(zpk.zeros.size() == 12);
}

TEST_CASE("Every designed pole is inside the unit circle", "[butterworth]") {
    for (const double fs : { 44100.0, 48000.0, 96000.0 }) {
        const OctaveBands bands(3, 20.0, 20000.0);
        const double edgeLimit = kNyquistEdgeFraction * (fs / 2.0);

        for (const auto& band : bands.bands()) {
            if (band.centre >= edgeLimit) continue;
            CAPTURE(fs, band.centre);
            const auto result = ButterworthDesign::bandPass(band.lower, band.upper, fs, 6);
            CHECK(result.maxPoleRadius < 1.0);
            // A stub returning zero-filled coefficients would also read < 1.0,
            // so pin a floor too: measured minima are all above 0.999.
            CHECK(result.maxPoleRadius > 0.9);
        }
    }
}

TEST_CASE("The designed band edges are exactly -3.0103 dB", "[butterworth]") {
    // Bilinear is conformal and both edges were prewarped, so this is exact,
    // not approximate -- a loose tolerance here would hide a prewarp bug.
    constexpr double kHalfPowerDb = 3.0102999566398120;  // 10*log10(2)

    const OctaveBands bands(3, 20.0, 20000.0);
    const double edgeLimit = kNyquistEdgeFraction * (48000.0 / 2.0);

    for (const auto& band : bands.bands()) {
        if (band.centre >= edgeLimit) continue;
        const auto result = ButterworthDesign::bandPass(band.lower, band.upper, 48000.0, 6);
        if (result.nyquistClamped) continue;  // a clamped edge is not the design goal

        const double wLo = 2.0 * std::numbers::pi * result.lowerHz / 48000.0;
        const double wHi = 2.0 * std::numbers::pi * result.upperHz / 48000.0;

        CAPTURE(band.centre);
        CHECK_THAT(BiquadCascade::attenuationDb(result.sections, wLo),
                   WithinAbs(kHalfPowerDb, 1e-9));
        CHECK_THAT(BiquadCascade::attenuationDb(result.sections, wHi),
                   WithinAbs(kHalfPowerDb, 1e-9));
    }
}

TEST_CASE("Every band meets the ANSI S1.11 Class 1 mask", "[butterworth]") {
    // One-third-octave Class 1 breakpoints, ANSI S1.11 Table B1 -- do not
    // apply this table to a 1/1 or 1/6-octave bank (plan trap 6).
    struct MaskRow { double high, low, minDb, maxDb; };
    constexpr double kInf = std::numeric_limits<double>::infinity();
    constexpr MaskRow kClass1[] = {
        {1.00000, 1.00000, -0.3,  0.3},
        {1.02667, 0.97402, -0.3,  0.4},
        {1.05575, 0.94719, -0.3,  0.6},
        {1.08746, 0.91958, -0.3,  1.3},
        {1.12202, 0.89125, -0.3,  5.0},
        {1.29437, 0.77257,  2.0, kInf},
        {1.88173, 0.53143, 17.5, kInf},
        {3.06955, 0.32578, 61.0, kInf},
    };

    int evaluated = 0;

    for (const double fs : { 44100.0, 48000.0, 96000.0 }) {
        const OctaveBands bands(3, 20.0, 20000.0);
        REQUIRE(bands.fraction() == 3);
        const double edgeLimit = kNyquistEdgeFraction * (fs / 2.0);

        for (const auto& band : bands.bands()) {
            if (band.centre >= edgeLimit) continue;
            const auto result = ButterworthDesign::bandPass(band.lower, band.upper, fs, 6);
            if (result.nyquistClamped) continue;

            for (const auto& row : kClass1) {
                for (const double ratio : { row.high, row.low }) {
                    const double f = band.centre * ratio;
                    if (f >= edgeLimit) continue;  // trap 8: a folded evaluation point

                    const double w = 2.0 * std::numbers::pi * f / fs;
                    const double attenDb = BiquadCascade::attenuationDb(result.sections, w);
                    CAPTURE(fs, band.centre, ratio, f, attenDb);
                    // Positive attenDb means DOWN, referenced to the nominal
                    // 0 dB pass-band gain -- not to a normalised |H(fm)|=1
                    // (plan trap 7).
                    CHECK(attenDb >= row.minDb);
                    CHECK(attenDb <= row.maxDb);
                    ++evaluated;
                }
            }
        }
    }

    REQUIRE(evaluated >= 1300);
}

TEST_CASE("Sections come out ordered by ascending pole radius", "[butterworth]") {
    const auto result = ButterworthDesign::bandPass(20.0, 25.0, 48000.0, 6);
    REQUIRE(result.sections.size() == 6);

    double previous = 0.0;
    for (const auto& c : result.sections) {
        const double disc = c.a1 * c.a1 - 4.0 * c.a2;
        const double radius = disc < 0.0 ? std::sqrt(c.a2)
                                          : std::max(std::abs((-c.a1 + std::sqrt(disc)) / 2.0),
                                                     std::abs((-c.a1 - std::sqrt(disc)) / 2.0));
        CHECK(radius >= previous);
        previous = radius;
    }
}

TEST_CASE("SOS coefficients match scipy", "[butterworth][golden]") {
    const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/filterbank.txt");

    for (const auto& tc : cases) {
        if (tc.name.rfind("design_", 0) != 0) continue;
        CAPTURE(tc.name);

        const double fs = tc.row("fs")[0];
        const double lower = tc.row("lower")[0];
        const double upper = tc.row("upper")[0];
        const int sections = static_cast<int>(tc.row("sections")[0]);

        const auto result = ButterworthDesign::bandPass(lower, upper, fs, sections);
        const auto& sos = tc.row("sos");
        REQUIRE(result.sections.size() * 6 == sos.size());

        for (std::size_t i = 0; i < result.sections.size(); ++i) {
            const auto& c = result.sections[i];
            const double* row = &sos[i * 6];
            CAPTURE(i);
            CHECK_THAT(c.b0, WithinRel(row[0], 1e-9) || WithinAbs(row[0], 1e-12));
            CHECK_THAT(c.b1, WithinRel(row[1], 1e-9) || WithinAbs(row[1], 1e-12));
            CHECK_THAT(c.b2, WithinRel(row[2], 1e-9) || WithinAbs(row[2], 1e-12));
            // row[3] is a0, always 1 -- not stored, nothing to compare.
            CHECK_THAT(c.a1, WithinRel(row[4], 1e-9) || WithinAbs(row[4], 1e-12));
            CHECK_THAT(c.a2, WithinRel(row[5], 1e-9) || WithinAbs(row[5], 1e-12));
        }
    }
}

TEST_CASE("Poles and zeros match scipy", "[butterworth][golden]") {
    const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/filterbank.txt");

    for (const auto& tc : cases) {
        if (tc.name.rfind("design_", 0) != 0) continue;
        CAPTURE(tc.name);

        const double fs = tc.row("fs")[0];
        const double lower = tc.row("lower")[0];
        const double upper = tc.row("upper")[0];
        const int sections = static_cast<int>(tc.row("sections")[0]);

        const auto zpk = ButterworthDesign::bandPassZpk(lower, upper, fs, sections);

        auto sortedPairs = [](const std::vector<double>& re, const std::vector<double>& im) {
            std::vector<std::pair<double, double>> pairs;
            for (std::size_t i = 0; i < re.size(); ++i) pairs.emplace_back(re[i], im[i]);
            std::sort(pairs.begin(), pairs.end());
            return pairs;
        };

        auto expectedPoles = sortedPairs(tc.row("pole_re"), tc.row("pole_im"));
        auto expectedZeros = sortedPairs(tc.row("zero_re"), tc.row("zero_im"));

        std::vector<std::pair<double, double>> actualPoles, actualZeros;
        for (const auto& p : zpk.poles) actualPoles.emplace_back(p.real(), p.imag());
        for (const auto& z : zpk.zeros) actualZeros.emplace_back(z.real(), z.imag());
        std::sort(actualPoles.begin(), actualPoles.end());
        std::sort(actualZeros.begin(), actualZeros.end());

        REQUIRE(actualPoles.size() == expectedPoles.size());
        REQUIRE(actualZeros.size() == expectedZeros.size());

        for (std::size_t i = 0; i < actualPoles.size(); ++i) {
            CAPTURE(i);
            CHECK_THAT(actualPoles[i].first, WithinAbs(expectedPoles[i].first, 1e-12));
            CHECK_THAT(actualPoles[i].second, WithinAbs(expectedPoles[i].second, 1e-12));
        }
        for (std::size_t i = 0; i < actualZeros.size(); ++i) {
            CAPTURE(i);
            CHECK_THAT(actualZeros[i].first, WithinAbs(expectedZeros[i].first, 1e-12));
            CHECK_THAT(actualZeros[i].second, WithinAbs(expectedZeros[i].second, 1e-12));
        }

        CHECK_THAT(zpk.gain, WithinRel(tc.row("zpk_gain")[0], 1e-12));
    }
}

TEST_CASE("A band that reaches Nyquist is clamped and says so", "[butterworth]") {
    const OctaveBands bands441(3, 20.0, 20000.0);
    const auto& highest441 = bands441[bands441.size() - 1];
    REQUIRE_THAT(highest441.centre, WithinAbs(19952.6, 1.0));

    const auto clamped = ButterworthDesign::bandPass(highest441.lower, highest441.upper,
                                                      44100.0, 6);
    CHECK(clamped.nyquistClamped);
    CHECK_THAT(clamped.upperHz, WithinRel(kNyquistEdgeFraction * 22050.0, 1e-12));
    CHECK(clamped.maxPoleRadius < 1.0);

    const auto unclamped = ButterworthDesign::bandPass(highest441.lower, highest441.upper,
                                                        48000.0, 6);
    CHECK_FALSE(unclamped.nyquistClamped);

    CHECK_THROWS_AS(ButterworthDesign::bandPass(1000.0, 900.0, 48000.0, 6), std::invalid_argument);
}
