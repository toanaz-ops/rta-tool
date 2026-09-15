// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The digital A/C cascades in this file are NOT verified against the full
// IEC 61672-1 tolerance envelope for the conformance class this device would
// need to claim (that table is paywalled; only four points are corroborated
// -- see docs/dsp/2026-08-27-weighting-and-meters.md). The claim these tests
// hold in place is the honest one: analytic weighting within 0.05 dB of
// Table 3 (W1), and digital filter error as published in the decision
// record (W3). Nothing here may be read as a conformance claim.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Weighting.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::dsp;

namespace {

/// Pole radius of one section, from a1/a2 directly -- same math as
/// BiquadCascade's private sectionPoleRadius, reimplemented here because
/// that helper isn't public: the denominator is z^2 + a1*z + a2 = 0. A
/// genuine conjugate pair (disc < 0) has |pole|^2 = a2; a section that has
/// degenerated to two real poles (both A/C sections at index 1/2 are exactly
/// this -- see Weighting.cpp) is solved directly via the quadratic formula.
double sectionPoleRadius(const Biquad::Coeffs& c) {
    const double disc = c.a1 * c.a1 - 4.0 * c.a2;
    if (disc < 0.0) {
        return std::sqrt(std::max(0.0, c.a2));
    }
    const double root = std::sqrt(disc);
    const double p1 = (-c.a1 + root) * 0.5;
    const double p2 = (-c.a1 - root) * 0.5;
    return std::max(std::abs(p1), std::abs(p2));
}

}  // namespace

// ---------------------------------------------------------------------------
// W1 -- analytic curve against its own closed form and against Table 3
// ---------------------------------------------------------------------------

TEST_CASE("Analytic A/C weighting matches IEC 61672-1 Table 3", "[weighting]") {
    CHECK_THAT(Weighting::analyticDb(1000.0, WeightingType::A), WithinAbs(0.0, 1e-12));
    CHECK_THAT(Weighting::analyticDb(1000.0, WeightingType::C), WithinAbs(0.0, 1e-12));

    // IEC 61672-1 Table 3, indexed by n where f = 1000*10^(0.1n) -- the
    // EXACT third-octave frequency, not the nominal label. See the trap
    // check at the end of this test.
    //
    // DEVIATION FROM THE IMPLEMENTATION PLAN: the plan's own copy of this
    // table (docs/plans/2026-08-27-weighting-meters-impl-plan.md section 7,
    // W1) has the C column shifted by one index for n >= 2 -- e.g. it lists
    // C(n=13) = -8.5, C(n=2) = -0.0. That is n=12's and n=1's value, one
    // slot early. The A column in the same table is correct. Cross-checked
    // two independent ways: (1) this implementation's own analyticDb, a
    // closed-form transcription of the plan's own A(f)/C(f) formulas in
    // section 4.2, agrees with the values below to <0.05 dB, the same
    // tolerance the plan specifies; (2) the reference CSV shipped with
    // python-acoustics (.venv/Lib/site-packages/acoustics/data/
    // iec_61672_1_2013.csv), an independent third-party transcription of
    // the same published standard, which is the C column used here. This is
    // the same class of transcription error the plan itself already flagged
    // in section 12 for the detector's -1.9895/-1.9920 literal -- assert
    // the correct published number, not the plan's copy of it.
    struct Row { int n; double a; double c; };
    const std::vector<Row> table = {
        {-20, -70.4, -14.3}, {-19, -63.4, -11.2}, {-18, -56.7,  -8.5}, {-17, -50.5,  -6.2},
        {-16, -44.7,  -4.4}, {-15, -39.4,  -3.0}, {-14, -34.6,  -2.0}, {-13, -30.2,  -1.3},
        {-12, -26.2,  -0.8}, {-11, -22.5,  -0.5}, {-10, -19.1,  -0.3}, { -9, -16.1,  -0.2},
        { -8, -13.4,  -0.1}, { -7, -10.9,   0.0}, { -6,  -8.6,   0.0}, { -5,  -6.6,   0.0},
        { -4,  -4.8,   0.0}, { -3,  -3.2,   0.0}, { -2,  -1.9,   0.0}, { -1,  -0.8,   0.0},
        {  0,   0.0,   0.0}, {  1,   0.6,   0.0}, {  2,   1.0,  -0.1}, {  3,   1.2,  -0.2},
        {  4,   1.3,  -0.3}, {  5,   1.2,  -0.5}, {  6,   1.0,  -0.8}, {  7,   0.5,  -1.3},
        {  8,  -0.1,  -2.0}, {  9,  -1.1,  -3.0}, { 10,  -2.5,  -4.4}, { 11,  -4.3,  -6.2},
        { 12,  -6.6,  -8.5}, { 13,  -9.3, -11.2},
    };

    for (const auto& row : table) {
        const double f = 1000.0 * std::pow(10.0, 0.1 * row.n);
        CAPTURE(row.n, f);
        CHECK_THAT(Weighting::analyticDb(f, WeightingType::A), WithinAbs(row.a, 0.05));
        CHECK_THAT(Weighting::analyticDb(f, WeightingType::C), WithinAbs(row.c, 0.05));
        CHECK(Weighting::analyticDb(f, WeightingType::Z) == 0.0);
    }

    // The trap this test exists for: building the loop from the nominal
    // labels (20/25/31.5/.../16000 Hz) instead of 1000*10^(0.1n) manufactures
    // up to 0.28 dB of spurious "error" against Table 3. Pin the gap between
    // nominal 16000 Hz and the exact n=12 frequency so nobody "tidies" the
    // frequency construction later.
    const double exactN12 = 1000.0 * std::pow(10.0, 0.1 * 12);
    CHECK_THAT(std::abs(Weighting::analyticDb(16000.0, WeightingType::A) -
                         Weighting::analyticDb(exactN12, WeightingType::A)),
               WithinAbs(0.104, 0.02));
}

// ---------------------------------------------------------------------------
// W2 -- the designed cascade's structure
// ---------------------------------------------------------------------------

TEST_CASE("Weighting cascades have the pole/zero structure Annex E implies", "[weighting]") {
    CHECK(Weighting(WeightingType::A, 48000.0).cascade().size() == 3u);
    CHECK(Weighting(WeightingType::C, 48000.0).cascade().size() == 2u);
    CHECK(Weighting(WeightingType::Z, 48000.0).cascade().size() == 0u);

    for (const double fs : {44100.0, 48000.0, 96000.0}) {
        for (const auto type : {WeightingType::A, WeightingType::C}) {
            CAPTURE(fs, toString(type));
            const Weighting w(type, fs);

            // THE CLAIM: the cascade has a zero at z = +1 (DC) and a zero at
            // z = -1 (Nyquist). State it where it is exactly true -- on the
            // COEFFICIENTS. A numerator b0 + b1*z^-1 + b2*z^-2 vanishes at
            // z = +1 iff b0 + b1 + b2 == 0 and at z = -1 iff b0 - b1 + b2 == 0.
            // Both sums are exact in binary floating point for this design:
            // the high-pass sections are literally (1, -2, 1) and the low-pass
            // ones (b, 2b, b), so each sum cancels to a true zero with no
            // rounding at all. No transcendental function is involved, so no
            // toolchain can disagree.
            bool zeroAtDc = false, zeroAtNyquist = false;
            for (const auto& c : w.cascade().sections()) {
                if (c.b0 + c.b1 + c.b2 == 0.0) zeroAtDc = true;
                if (c.b0 - c.b1 + c.b2 == 0.0) zeroAtNyquist = true;
            }
            CHECK(zeroAtDc);
            CHECK(zeroAtNyquist);

            // THE CONSEQUENCE, in the response BiquadCascade actually reports
            // (-20*log10|H|, positive = down). DC and Nyquist are NOT
            // symmetric here, and the difference is why this test used to fail
            // on macOS only:
            //
            //  - At omega = 0 the evaluation point is exact. cos(0) == 1.0 and
            //    sin(0) == 0.0 with no error, so the numerator reduces to the
            //    exact sum above and |H| is a true zero. +inf is safe to
            //    assert, on any toolchain.
            //
            //  - At Nyquist it is not. `std::numbers::pi` is the double
            //    NEAREST pi, off by d ~ 1.2246e-16 rad, so this evaluates the
            //    response 1.2e-16 radians away from Nyquist, where |H| is
            //    genuinely non-zero. It came out +inf on x86 only because
            //    sin(pi_double) and sin(2*pi_double) happen to cancel exactly
            //    under one particular rounding path; contract those two
            //    products into an FMA -- which clang does by default, and
            //    which is what arm64 macOS does -- and the residual survives
            //    as ~1.7e-33 instead of 0. Asserting +inf was asserting a
            //    property of x86 code generation, not of the filter.
            //
            // So bound it instead, with the margin argued rather than
            // measured. The zero at z = -1 is DOUBLE ((1 + z^-1)^2), so near
            // Nyquist |N| ~ |b0| * d^2 <= 4 * (1.23e-16)^2 ~ 6e-32: the
            // attenuation cannot be below about 600 dB. Remove the zero and
            // the numerator is O(b0) ~ 0.1..4, i.e. an attenuation of TENS of
            // dB. The two cases are ~600 dB apart, and 200 dB sits in the
            // empty middle of that gap -- far above any response a real filter
            // produces, far below what the algebraic zero produces, and not
            // sensitive to which rounding path a toolchain takes.
            const double dcAtten = w.cascade().attenuationDb(0.0);
            const double nyquistAtten = w.cascade().attenuationDb(std::numbers::pi);
            CHECK(std::isinf(dcAtten));
            CHECK(dcAtten > 0.0);
            CHECK(nyquistAtten > 200.0);

            // Stability, and the ascending-pole-radius ordering contract.
            double previousRadius = -1.0;
            for (const auto& section : w.cascade().sections()) {
                const double radius = sectionPoleRadius(section);
                CHECK(radius < 1.0);
                CHECK(radius > previousRadius);
                previousRadius = radius;
            }
            CHECK(w.cascade().maxPoleRadius() < 1.0);
        }
    }

    CHECK_THROWS_AS(Weighting(WeightingType::A, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(Weighting(WeightingType::A, -48000.0), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// W3 -- digital response near the analytic target, and the 1 kHz residual
// ---------------------------------------------------------------------------

TEST_CASE("Digital weighting tracks the analytic curve within the published error", "[weighting]") {
    const Weighting a48(WeightingType::A, 48000.0);

    // The residual trap (docs/dsp/.../11.5): normalisation happens on the
    // ANALOG zpk before the bilinear transform (python-acoustics parity), so
    // the digital cascade does not read exactly 0 dB at 1 kHz. Measured
    // residual at 48 kHz: +0.004359 dB. WithinAbs(0.0, 1e-9) would fail
    // against a correct design; 0.01 is the published-residual tolerance.
    CHECK_THAT(a48.responseDb(1000.0), WithinAbs(0.0, 0.01));

    // Below 4 kHz (exact third-octaves with f <= 3981.07, i.e. n <= 6),
    // digital and analytic agree to better than 0.1 dB.
    for (int n = -20; n <= 6; ++n) {
        const double f = 1000.0 * std::pow(10.0, 0.1 * n);
        CAPTURE(n, f);
        CHECK_THAT(a48.responseDb(f) - Weighting::analyticDb(f, WeightingType::A),
                   WithinAbs(0.0, 0.1));
    }

    // Regression lock on a documented approximation error (CLAUDE.md: label
    // required when the expectation isn't a closed form). This is the exact
    // table docs/dsp/2026-08-27-weighting-and-meters.md publishes at 48 kHz,
    // exact third-octave frequencies -- not a value the implementation
    // produced and then asserted back against itself.
    struct ErrRow { int n; double a; double c; };
    const std::vector<ErrRow> errTable = {
        { 8, -0.2209, -0.2276}, { 9, -0.5259, -0.5326}, {10, -1.2118, -1.2183},
        {11, -2.7337, -2.7401}, {12, -6.2097, -6.2156},
    };
    const Weighting c48(WeightingType::C, 48000.0);
    for (const auto& row : errTable) {
        const double f = 1000.0 * std::pow(10.0, 0.1 * row.n);
        CAPTURE(row.n, f);
        const double aErr = a48.responseDb(f) - Weighting::analyticDb(f, WeightingType::A);
        const double cErr = c48.responseDb(f) - Weighting::analyticDb(f, WeightingType::C);
        CHECK_THAT(aErr, WithinAbs(row.a, 0.01));
        CHECK_THAT(cErr, WithinAbs(row.c, 0.01));
    }
}

// ---------------------------------------------------------------------------
// W4 -- golden vectors
// ---------------------------------------------------------------------------

TEST_CASE("Weighting design matches scipy's bilinear design", "[weighting][golden]") {
    const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/weighting.txt");

    for (const auto& fs : {44100.0, 48000.0, 96000.0}) {
        for (const auto type : {WeightingType::A, WeightingType::C}) {
            const std::string name =
                "weighting_" + std::string(toString(type)) + "_" + std::to_string(static_cast<long>(fs));
            const auto it = std::find_if(cases.begin(), cases.end(),
                                          [&](const auto& c) { return c.name == name; });
            REQUIRE(it != cases.end());
            const auto& golden = *it;
            CAPTURE(name);

            const Weighting w(type, fs);
            const auto& sections = w.cascade().sections();
            const auto& b0 = golden.row("sos_b0");
            const auto& b1 = golden.row("sos_b1");
            const auto& b2 = golden.row("sos_b2");
            const auto& a1 = golden.row("sos_a1");
            const auto& a2 = golden.row("sos_a2");
            REQUIRE(sections.size() == b0.size());

            for (std::size_t i = 0; i < sections.size(); ++i) {
                CAPTURE(i);
                CHECK_THAT(sections[i].b0, WithinRel(b0[i], 1e-9));
                CHECK_THAT(sections[i].b1, WithinRel(b1[i], 1e-9));
                CHECK_THAT(sections[i].b2, WithinRel(b2[i], 1e-9));
                CHECK_THAT(sections[i].a1, WithinRel(a1[i], 1e-9));
                CHECK_THAT(sections[i].a2, WithinRel(a2[i], 1e-9));
            }

            const auto& freq = golden.row("freq");
            const auto& analyticDb = golden.row("analytic_db");
            const auto& digitalDb = golden.row("digital_db");
            for (std::size_t i = 0; i < freq.size(); ++i) {
                CAPTURE(i, freq[i]);
                CHECK_THAT(Weighting::analyticDb(freq[i], type), WithinAbs(analyticDb[i], 1e-10));
                // DEVIATION FROM THE PLAN: the plan specifies WithinAbs(1e-9)
                // here. Measured, that is slightly too tight at 96 kHz for
                // the lowest few third-octave points (10/12.6/15.8/20 Hz),
                // where the cascade is ~70 dB down: BiquadCascade sums
                // per-section 10*log10(magnitude-squared) via cos/sin
                // identities (see Biquad.h), while scipy's sosfreqz evaluates
                // via complex exponentials -- a different but equally valid
                // rounding path through double precision. Worst observed gap
                // is ~4.9e-9 dB against a ~-70 dB value (relative error
                // ~7e-11); the SOS coefficients themselves already matched
                // at WithinRel(1e-9) above, so this is floating-point
                // rounding, not a design disagreement. 2e-8 keeps ~4x margin
                // over the worst measured gap while staying two orders of
                // magnitude tighter than the coefficient check implies.
                CHECK_THAT(w.responseDb(freq[i]), WithinAbs(digitalDb[i], 2e-8));
            }
        }
    }

    // End-to-end: filter noise with Weighting, sum the Leq ourselves (not
    // via the Leq class -- another agent's track, may not exist yet).
    for (const auto type : {WeightingType::A, WeightingType::C}) {
        const std::string name = "leq_weighted_noise_" + std::string(toString(type));
        const auto it = std::find_if(cases.begin(), cases.end(),
                                      [&](const auto& c) { return c.name == name; });
        REQUIRE(it != cases.end());
        const auto& golden = *it;
        CAPTURE(name);

        const auto input = golden.floatRow("input");
        Weighting w(type, 48000.0);
        std::vector<float> output(input.size());
        w.process(input, output);

        double sumSquares = 0.0;
        for (const float y : output) sumSquares += static_cast<double>(y) * static_cast<double>(y);
        const double leqDb = 10.0 * std::log10(sumSquares / static_cast<double>(output.size()));

        CHECK_THAT(leqDb, WithinAbs(golden.row("leq_db")[0], 1e-4));
    }
}
