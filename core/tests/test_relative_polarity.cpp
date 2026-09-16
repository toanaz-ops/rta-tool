// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for relativePolarity -- see
// docs/plans/2026-09-15-L7-align-impl-plan.md Task E, cases E1-E9, and the
// decision record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.7 /
// Sec.10.8-10.9.
//
// NO VERDICT SHIPS. Record Sec.8 forbids a threshold until two independent
// grids agree (task F), so rho is a figure and never a gate. E6 is the
// structural case that keeps it that way.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Biquad.h"
#include "rta/dsp/BiquadResponse.h"
#include "rta/ir/RelativePolarity.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::ir;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double kFs = 48000.0;

/// An IR-shaped fixture: fixed-seed broadband noise under an exponential
/// envelope, placed inside the window with silence either side. Broadband
/// rather than a tone because a tone's autocorrelation has sidelobes almost as
/// tall as its peak, and E4 would then be testing the fixture.
std::vector<float> burst(std::size_t length, std::size_t start, std::size_t span,
                         std::uint32_t seed, double decay = 0.02) {
    std::vector<float> out(length, 0.0f);
    std::mt19937 rng{ seed };
    std::normal_distribution<double> noise{ 0.0, 1.0 };
    for (std::size_t i = 0; i < span && start + i < length; ++i) {
        out[start + i] = static_cast<float>(noise(rng) * std::exp(-decay * static_cast<double>(i)));
    }
    return out;
}

Deconvolution wrap(std::vector<float> samples) {
    Deconvolution d;
    d.samples = std::move(samples);
    d.originIndex = 0;
    d.sampleRate = kFs;
    return d;
}

RelativePolarityConfig windowOf(std::size_t samples) {
    RelativePolarityConfig config;
    config.searchSeconds = static_cast<double>(samples) / kFs;
    return config;
}

/// RBJ cookbook low-pass / high-pass, built here because ButterworthDesign
/// offers only bandPass (ButterworthDesign.h:41-52) -- the record's Sec.1 note
/// is correct and a low-pass/high-pass design is not this lane's job.
rta::dsp::Biquad::Coeffs rbj(double fcHz, double q, bool highPass) {
    const double w0 = 2.0 * kPi * fcHz / kFs;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double shared = highPass ? (1.0 + cw) : (1.0 - cw);
    rta::dsp::Biquad::Coeffs c;
    c.b0 = (shared * 0.5) / a0;
    c.b1 = (highPass ? -shared : shared) / a0;
    c.b2 = c.b0;
    c.a1 = (-2.0 * cw) / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
}

/// Butterworth section Q values: 1/(2 cos(pi(2k+1)/(2N))).
std::vector<rta::dsp::Biquad::Coeffs> butterworth(double fcHz, int order, bool highPass) {
    std::vector<rta::dsp::Biquad::Coeffs> sections;
    for (int k = 0; k < order / 2; ++k) {
        const double q =
            1.0 / (2.0 * std::cos(kPi * (2.0 * k + 1.0) / (2.0 * static_cast<double>(order))));
        sections.push_back(rbj(fcHz, q, highPass));
    }
    return sections;
}

std::vector<float> filtered(const std::vector<float>& in,
                            std::vector<rta::dsp::Biquad::Coeffs> sections) {
    std::vector<float> out(in.size());
    rta::dsp::BiquadCascade cascade{ std::move(sections) };
    cascade.process(in, out);
    return out;
}

std::vector<float> impulse(std::size_t length) {
    std::vector<float> out(length, 0.0f);
    out[0] = 1.0f;
    return out;
}

void append(std::vector<rta::dsp::Biquad::Coeffs>& into,
            const std::vector<rta::dsp::Biquad::Coeffs>& from) {
    into.insert(into.end(), from.begin(), from.end());
}

}  // namespace

TEST_CASE("rho is 1 for a signal against itself", "[relative_polarity]") {
    // E1.
    const auto a = burst(1000, 100, 200, 11u);
    const auto result = relativePolarity(wrap(a), wrap(a), windowOf(1000));
    REQUIRE(result.refusal == Refusal::None);
    CHECK_THAT(result.rho, WithinAbs(1.0, 1e-12));
    CHECK(result.sign == Sign::Positive);
    CHECK(result.lag == 0);
}

TEST_CASE("rho is scale-invariant and the SIGN survives the scaling", "[relative_polarity]") {
    // E2. b = -2a. The normaliser sqrt(E_a E_b) is what makes this exactly 1;
    // max(E_a, E_b) would read 0.5 and call a perfect match a poor one.
    const auto a = burst(1000, 100, 200, 12u);
    std::vector<float> b(a.size());
    for (std::size_t i = 0; i < a.size(); ++i) b[i] = -2.0f * a[i];

    const auto result = relativePolarity(wrap(a), wrap(b), windowOf(1000));
    REQUIRE(result.refusal == Refusal::None);
    CHECK_THAT(result.rho, WithinAbs(1.0, 1e-12));
    CHECK(result.sign == Sign::Negative);
    CHECK(result.lag == 0);
}

TEST_CASE("rho is shift-invariant and reports the shift as the lag", "[relative_polarity]") {
    // E3. The burst sits at 100..300 in a 1000-sample window, so a 96-sample
    // shift keeps the whole of it inside BOTH windows and no energy is lost --
    // which is what makes rho exactly 1 rather than approximately so.
    const auto a = burst(1000, 100, 200, 13u);
    const auto b = burst(1000, 196, 200, 13u);

    const auto result = relativePolarity(wrap(a), wrap(b), windowOf(1000));
    REQUIRE(result.refusal == Refusal::None);
    INFO("rho " << result.rho << ", lag " << result.lag);
    CHECK_THAT(result.rho, WithinAbs(1.0, 1e-12));
    CHECK(result.lag == 96);
    CHECK(result.sign == Sign::Positive);
}

TEST_CASE("rho against noise follows the coherence-shaped bound", "[relative_polarity]") {
    // E4. b = a + n at signal-to-noise power ratio S. Then
    //     E[r(0)] = E_a,  E[E_b] = E_a (1 + 1/S)
    // so rho -> sqrt(S/(1+S)) -- the same form dual-fft.md Sec.7.3 gives for
    // gamma^2 = S/(1+S). The tolerance 3/sqrt(N_W) is the sampling error of a
    // correlation over N_W points, not a number read off a run.
    constexpr std::size_t kWindow = 480;
    const auto a = burst(kWindow, 40, 300, 14u);
    double energyA = 0.0;
    for (const float v : a) energyA += static_cast<double>(v) * static_cast<double>(v);

    std::mt19937 rng{ 99u };
    std::normal_distribution<double> noise{ 0.0, 1.0 };
    for (const double s : { 1.0, 10.0, 100.0 }) {
        // Scale the noise so its window energy is exactly E_a / S.
        std::vector<double> raw(kWindow);
        double energyRaw = 0.0;
        for (auto& v : raw) {
            v = noise(rng);
            energyRaw += v * v;
        }
        const double scale = std::sqrt((energyA / s) / energyRaw);
        std::vector<float> b(kWindow);
        for (std::size_t i = 0; i < kWindow; ++i) {
            b[i] = a[i] + static_cast<float>(raw[i] * scale);
        }

        const auto result = relativePolarity(wrap(a), wrap(b), windowOf(kWindow));
        REQUIRE(result.refusal == Refusal::None);
        const double expected = std::sqrt(s / (1.0 + s));
        const double residual = std::abs(result.rho - expected);
        const double bound = 3.0 / std::sqrt(static_cast<double>(kWindow));
        INFO("S = " << s << ": rho " << result.rho << ", expected " << expected << ", residual "
                    << residual << ", bound " << bound);
        CHECK(residual <= bound);
    }
}

TEST_CASE("rho is bounded in [0,1] by Cauchy-Schwarz over 200 draws", "[relative_polarity]") {
    // E5. The bound is the one property the three dead L4a gate variables
    // lacked (memory/a-threshold-read-off-a-grid-is-that-grids-floor.md,
    // answer kind 1): rho cannot run away even if a threshold inside [0,1]
    // still needs surveying. The 200 draws demonstrate the implementation
    // computes the thing the inequality is about; the inequality itself is the
    // proof.
    constexpr std::size_t kWindow = 256;
    std::mt19937 rng{ 2026u };
    std::normal_distribution<double> noise{ 0.0, 1.0 };

    double worst = 0.0;
    for (int draw = 0; draw < 200; ++draw) {
        std::vector<float> a(kWindow), b(kWindow);
        for (std::size_t i = 0; i < kWindow; ++i) {
            a[i] = static_cast<float>(noise(rng));
            b[i] = static_cast<float>(noise(rng));
        }
        const auto result = relativePolarity(wrap(a), wrap(b), windowOf(kWindow));
        REQUIRE(result.refusal == Refusal::None);
        CHECK(result.rho >= 0.0);
        CHECK(result.rho <= 1.0);
        worst = std::max(worst, result.rho);
    }
    WARN("E5 largest rho over 200 uncorrelated draws: " << worst);
}

TEST_CASE("the window is L4a's ARRIVAL, and the room tail is what pulls rho down",
          "[relative_polarity]") {
    // E7. Two systems that agree about their arrival and disagree about their
    // tail. Over the arrival window rho is high; over the whole capture the
    // uncorrelated tails own the energy and it falls. That is record Sec.7's
    // reason for windowing at all, stated as a difference rather than as two
    // numbers nobody compares.
    constexpr std::size_t kLength = 9600;  // 0.2 s at 48 kHz
    const auto arrival = burst(kLength, 50, 200, 21u);
    auto a = arrival;
    auto b = arrival;
    const auto tailA = burst(kLength, 400, 9000, 22u, 0.0002);
    const auto tailB = burst(kLength, 400, 9000, 23u, 0.0002);
    for (std::size_t i = 0; i < kLength; ++i) {
        a[i] += 3.0f * tailA[i];  // the tail is LOUDER than the arrival
        b[i] += 3.0f * tailB[i];
    }

    const auto onArrival = relativePolarity(wrap(a), wrap(b), windowOf(300));
    const auto onWhole = relativePolarity(wrap(a), wrap(b), windowOf(kLength));
    REQUIRE(onArrival.refusal == Refusal::None);
    REQUIRE(onWhole.refusal == Refusal::None);
    WARN("E7 rho over the arrival window " << onArrival.rho << " vs over the whole capture "
                                           << onWhole.rho);
    CHECK(onArrival.rho - onWhole.rho > 0.1);
}

TEST_CASE("DOCUMENTED FAILURE: rho reads NEGATIVE across a correctly wired BW2 crossover",
          "[relative_polarity]") {
    // E8, record Sec.10.9. This is not a bug to be fixed. It is THE RULE THAT
    // PRODUCED L4a's ORDER-4 READING, finally written down:
    // relativePolarity() is L4a decision 6b's un-whitened rho = |peak|/sqrt(E1 E2)
    // and the sign at that peak (docs/dsp/2026-08-30-sweep-ir-l4a.md:1181 on
    // main), and docs/research/2026-09-15-l7-align-order4-probe.md Sec.5 showed
    // it is the estimator whose peak-sign reading reproduced the order-4
    // discrepancy on a band-pass pair.
    //
    // Across a BW2 crossover the two IRs are 180 degrees apart at every
    // frequency BY DESIGN, so their un-whitened cross-correlation at the
    // aligned lag is negative with CORRECT wiring, rho is high, and the sign is
    // confidently wrong. No re-weighting fixes it: the information that decides
    // the sign is the designer's convention, which is wizard question (c).
    //
    // A future session that "fixes" rho into agreeing with the wiring will land
    // here and read why. Shipping rho is fine -- it answers the same-system
    // question (record Sec.7 table row 3). Letting it answer the
    // across-a-crossover question is not.
    constexpr std::size_t kLength = 4096;
    const auto lpSections = butterworth(100.0, 2, false);
    const auto hpSections = butterworth(100.0, 2, true);
    const auto lp = filtered(impulse(kLength), lpSections);
    const auto hp = filtered(impulse(kLength), hpSections);

    const auto result = relativePolarity(wrap(lp), wrap(hp), windowOf(kLength));
    REQUIRE(result.refusal == Refusal::None);
    WARN("E8 correctly wired BW2 pair reads sign "
         << (result.sign == Sign::Negative ? "NEGATIVE" : "positive") << ", rho " << result.rho
         << ", lag " << result.lag);
    CHECK(result.sign == Sign::Negative);
    CHECK(result.lag == 0);

    // MEASURED CORRECTION to record Sec.7, which says rho is HIGH here. It is
    // not: rho is LOW, and the closed form says why. arg(H) - arg(L) is 180
    // degrees at every frequency, so Re(L conj(H)) = -|L||H| at every bin and,
    // by Parseval,
    //     rho = sum_k w_k |L_k||H_k| / sqrt(sum_k w_k |L_k|^2 * sum_k w_k |H_k|^2)
    // -- a Cauchy-Schwarz ratio between two magnitude responses that barely
    // OVERLAP. |L| lives below 100 Hz; |H| spans 100 Hz to 24 kHz. The ratio is
    // therefore of order sqrt(fc/(fs/2)) = sqrt(100/24000) = 0.065, not 1.
    //
    // This STRENGTHENS the record's conclusion rather than weakening it: across
    // a crossover rho is both low AND confidently wrong about the sign, so
    // there are now two reasons it may not arbitrate the wizard's question
    // instead of one. The record's sentence needs amending.
    double numerator = 0.0;
    double energyL = 0.0;
    double energyH = 0.0;
    for (std::size_t k = 0; k <= kLength / 2; ++k) {
        const double omega = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(kLength);
        const double weight = (k == 0 || k == kLength / 2) ? 1.0 : 2.0;
        const double magL = std::abs(rta::dsp::cascadeResponse(lpSections, omega));
        const double magH = std::abs(rta::dsp::cascadeResponse(hpSections, omega));
        numerator += weight * magL * magH;
        energyL += weight * magL * magL;
        energyH += weight * magH * magH;
    }
    const double predicted = numerator / std::sqrt(energyL * energyH);
    WARN("E8 rho predicted from Parseval: " << predicted << ", measured " << result.rho);
    CHECK_THAT(result.rho, WithinAbs(predicted, 1e-3));
    CHECK(predicted < 0.2);  // "high" is not what this is
}

TEST_CASE("DOCUMENTED FAILURE: two band-pass boxes move the peak off lag 0 at order 4",
          "[relative_polarity]") {
    // E9, probe Sec.4 / Sec.5. The L4a geometry rebuilt: a sub band-passed
    // BELOW as well as a main band-passed ABOVE. Each box carries a second
    // skirt of its own whose phase does not cancel in the ratio, the HP-LP
    // offset stops being constant across the overlap, and the correlation
    // envelope stretches until a neighbouring, oppositely-signed lobe outgrows
    // the value at lag 0.
    //
    // The identity constrains only the value AT LAG 0, and at order 4 that
    // value is still POSITIVE -- which is asserted here directly, from the
    // fixture, beside the negative sign the rule reports. `lag` leaving 0 is
    // the observable that says the peak has stopped tracking the identity, and
    // nothing in the rule reports it. That is the whole argument for never
    // reading a topology sign off a correlation peak.
    constexpr std::size_t kLength = 8192;
    std::vector<rta::dsp::Biquad::Coeffs> subSections;
    append(subSections, butterworth(30.0, 4, true));    // the sub's own high-pass
    append(subSections, butterworth(120.0, 4, false));  // the crossover's low-pass
    std::vector<rta::dsp::Biquad::Coeffs> mainSections;
    append(mainSections, butterworth(100.0, 4, true));    // the crossover's high-pass
    append(mainSections, butterworth(8000.0, 4, false));  // the main's own low-pass

    const auto sub = filtered(impulse(kLength), subSections);
    const auto main = filtered(impulse(kLength), mainSections);

    const auto result = relativePolarity(wrap(sub), wrap(main), windowOf(kLength));
    REQUIRE(result.refusal == Refusal::None);

    // The value AT LAG 0, computed here in the fixture, not read off the result.
    double atLagZero = 0.0;
    for (std::size_t i = 0; i < kLength; ++i) {
        atLagZero += static_cast<double>(sub[i]) * static_cast<double>(main[i]);
    }
    WARN("E9 band-pass pair: sign " << (result.sign == Sign::Negative ? "NEGATIVE" : "positive")
                                    << ", rho " << result.rho << ", lag " << result.lag << " ("
                                    << (static_cast<double>(result.lag) * 1000.0 / kFs)
                                    << " ms), r(0) = " << atLagZero);
    CHECK(atLagZero > 0.0);           // the identity survives AT LAG 0
    CHECK(result.lag != 0);           // but the peak is no longer there
    CHECK(result.sign == Sign::Negative);  // and it reports that lobe's sign
}
