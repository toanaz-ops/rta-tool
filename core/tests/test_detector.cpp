// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for meter/Detector -- see
// docs/plans/2026-08-27-weighting-meters-impl-plan.md section 7, Track D.
//
// Fast and Slow are IEC 61672-1 clause 5 exponential mean-square detectors.
// Impulse here is NOT the IEC quasi-peak rectifier: it is the two-time-constant
// (35 ms rise / 1.5 s decay) approximation of the historical circuit, labelled
// as an approximation because none of the surveyed implementations reproduce
// the original either. It is outside the current IEC normative scope and no
// conformance claim is made for it anywhere in this file.
//
// Every dB expectation below rests on a closed form (the step response
// 1 - exp(-t/tau), or the decay slope 10*log10(e)/tau), never on "whatever the
// code printed" -- per CLAUDE.md's verification standard.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/meter/Detector.h"

#include <cmath>
#include <random>
#include <span>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::meter;

namespace {

/// Least-squares fit of levelDb[i] against t[i], plus the largest residual --
/// used instead of a two-point slope because a two-point slope also passes
/// for a decay that is not exponential; the residual is the test that a
/// straight line was actually the right shape.
struct LineFit {
    double slope;
    double maxAbsResidualDb;
};

LineFit fitDecayLineDb(const std::vector<double>& t, const std::vector<double>& levelDb) {
    const auto n = t.size();
    double sumT = 0.0, sumY = 0.0, sumTT = 0.0, sumTY = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sumT += t[i];
        sumY += levelDb[i];
        sumTT += t[i] * t[i];
        sumTY += t[i] * levelDb[i];
    }
    const double nD = static_cast<double>(n);
    const double slope = (nD * sumTY - sumT * sumY) / (nD * sumTT - sumT * sumT);
    const double intercept = (sumY - slope * sumT) / nD;
    double maxResid = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        maxResid = std::max(maxResid, std::abs(levelDb[i] - (slope * t[i] + intercept)));
    }
    return {slope, maxResid};
}

/// Charges `d` toward steady state with the alternating +1/-1 unit signal
/// (x[n]^2 == 1 exactly, unlike a sine, whose x^2 ripples at 2f) for
/// 20*riseTau, then feeds 0.5 s of silence and fits the decay slope.
LineFit chargeThenMeasureDecay(Detector& d) {
    const double riseTau = Detector::riseTimeConstant(d.weighting());
    const auto chargeN =
        static_cast<std::size_t>(std::llround(20.0 * riseTau * d.sampleRate()));
    for (std::size_t n = 0; n < chargeN; ++n) d.processSample((n % 2 == 0) ? 1.0f : -1.0f);

    const auto silenceN = static_cast<std::size_t>(std::llround(0.5 * d.sampleRate()));
    std::vector<float> zeros(silenceN, 0.0f);
    std::vector<double> trace(silenceN);
    d.process(zeros, trace);

    std::vector<double> t(silenceN), levelDb(silenceN);
    for (std::size_t i = 0; i < silenceN; ++i) {
        t[i] = static_cast<double>(i) / d.sampleRate();
        levelDb[i] = 10.0 * std::log10(trace[i]);
    }
    return fitDecayLineDb(t, levelDb);
}

}  // namespace

TEST_CASE("Detector step response is 10*log10(1-exp(-t/tau))", "[detector]") {
    CHECK_THAT(Detector::riseTimeConstant(TimeWeighting::Fast), WithinRel(0.125, 1e-12));
    CHECK_THAT(Detector::decayTimeConstant(TimeWeighting::Fast), WithinRel(0.125, 1e-12));
    CHECK_THAT(Detector::riseTimeConstant(TimeWeighting::Slow), WithinRel(1.0, 1e-12));

    SECTION("Fast at 48 kHz, N = tau*fs = 6000") {
        Detector d(TimeWeighting::Fast, 48000.0);
        std::vector<float> x(6000);
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        const double ms = d.process(x);
        CHECK_THAT(10.0 * std::log10(ms),
                   WithinAbs(10.0 * std::log10(1.0 - std::exp(-1.0)), 1e-9));

        // Off-by-one is the point of this test (plan 11.3): after N samples the
        // state is the continuous solution at t = N*T, because y[0] = alpha
        // already advances one step. One MORE sample (index N, the (N+1)-th
        // fed) reads the continuous solution at N+1 steps -- a different,
        // smaller number, not -1.9920008 again.
        const double ms2 = d.processSample(1.0f);  // x[6000] continues the alternation: +1
        CHECK_THAT(10.0 * std::log10(ms2),
                   WithinAbs(10.0 * std::log10(1.0 - std::exp(-6001.0 / 6000.0)), 1e-9));
        // Cross-check the formula against a literal, same pattern as
        // test_window.cpp. NOTE: the impl plan (section 7, D1) quotes this
        // figure as "-1.99080", but 10*log10(1-exp(-6001/6000)) computes to
        // -1.9915797 (verified independently in Python), not -1.99080 -- a
        // second slip in the plan's hand-typed literals alongside the
        // -1.9895 one section 12 already documents. Asserting the correct,
        // independently-verified value here rather than the plan's literal.
        CHECK_THAT(10.0 * std::log10(1.0 - std::exp(-6001.0 / 6000.0)),
                   WithinAbs(-1.9915797, 1e-7));
    }

    SECTION("Slow at 48 kHz, N = tau*fs = 48000") {
        Detector d(TimeWeighting::Slow, 48000.0);
        std::vector<float> x(48000);
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        const double ms = d.process(x);
        CHECK_THAT(10.0 * std::log10(ms),
                   WithinAbs(10.0 * std::log10(1.0 - std::exp(-1.0)), 1e-9));
    }

    SECTION("Fast at 8 kHz, N = tau*fs = 1000 -- tau does not move with fs") {
        Detector d(TimeWeighting::Fast, 8000.0);
        std::vector<float> x(1000);
        for (std::size_t n = 0; n < x.size(); ++n) x[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        const double ms = d.process(x);
        CHECK_THAT(10.0 * std::log10(ms),
                   WithinAbs(10.0 * std::log10(1.0 - std::exp(-1.0)), 1e-9));
    }
}

TEST_CASE("Detector decay is a straight line in dB at 10*log10(e)/tau", "[detector]") {
    struct Case {
        TimeWeighting weighting;
        double expectedSlope;
    };
    const Case cases[] = {
        {TimeWeighting::Fast, -34.7435585522},
        {TimeWeighting::Slow, -4.3429448190},
        {TimeWeighting::Impulse, -2.8952965460},
    };

    for (const auto& c : cases) {
        Detector d(c.weighting, 48000.0);
        const auto fit = chargeThenMeasureDecay(d);
        CHECK_THAT(fit.slope, WithinRel(c.expectedSlope, 1e-6));
        CHECK(fit.maxAbsResidualDb < 1e-9);

        // Cross-check the literal against the closed form computed here, the
        // same pattern test_window.cpp uses: a closed form and a published
        // value each catch the other's typo.
        const double closedForm =
            -10.0 * std::log10(std::exp(1.0)) / Detector::decayTimeConstant(c.weighting);
        CHECK_THAT(closedForm, WithinAbs(c.expectedSlope, 1e-6));
    }
}

TEST_CASE("Impulse detector rises fast and decays slowly (approximation)", "[detector]") {
    CHECK(Detector::riseTimeConstant(TimeWeighting::Impulse) !=
          Detector::decayTimeConstant(TimeWeighting::Impulse));

    Detector d(TimeWeighting::Impulse, 48000.0);
    const double riseTau = Detector::riseTimeConstant(TimeWeighting::Impulse);
    const auto riseN = static_cast<std::size_t>(std::llround(riseTau * d.sampleRate()));
    for (std::size_t n = 0; n < riseN; ++n) d.processSample((n % 2 == 0) ? 1.0f : -1.0f);

    // Same closed form as D1, at the rise tau (0.035 s), not the decay tau.
    CHECK_THAT(10.0 * std::log10(d.meanSquare()),
               WithinAbs(10.0 * std::log10(1.0 - std::exp(-1.0)), 1e-9));

    // Continuing in the same run -- not a fresh detector -- decay is exactly
    // linear in dB regardless of the amplitude it starts from, so this is a
    // valid check of the 1.5 s decay tau even though the rise never reached
    // full steady state.
    const auto silenceN = static_cast<std::size_t>(std::llround(0.5 * d.sampleRate()));
    std::vector<float> zeros(silenceN, 0.0f);
    std::vector<double> trace(silenceN);
    d.process(zeros, trace);
    std::vector<double> t(silenceN), levelDb(silenceN);
    for (std::size_t i = 0; i < silenceN; ++i) {
        t[i] = static_cast<double>(i) / d.sampleRate();
        levelDb[i] = 10.0 * std::log10(trace[i]);
    }
    const auto fit = fitDecayLineDb(t, levelDb);
    CHECK_THAT(fit.slope, WithinRel(-2.8952965460, 1e-6));
    CHECK(fit.maxAbsResidualDb < 1e-9);
}

TEST_CASE("Detector block API is bit-identical to the sample loop", "[detector]") {
    // Ragged block sizes catch a detector that resets or re-primes per block.
    const std::vector<std::size_t> blockSizes = {1, 7, 4096, 3, 512, 1, 89, 1201};
    std::size_t total = 0;
    for (auto s : blockSizes) total += s;

    std::mt19937 rng(20260827);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> samples(total);
    for (auto& s : samples) s = dist(rng);

    for (TimeWeighting w : {TimeWeighting::Fast, TimeWeighting::Slow, TimeWeighting::Impulse}) {
        Detector blockDetector(w, 48000.0);
        Detector sampleDetector(w, 48000.0);

        std::size_t offset = 0;
        double blockResult = 0.0;
        for (auto size : blockSizes) {
            blockResult =
                blockDetector.process(std::span<const float>(samples.data() + offset, size));
            offset += size;
        }

        double sampleResult = 0.0;
        for (float x : samples) sampleResult = sampleDetector.processSample(x);

        REQUIRE(blockResult == sampleResult);
        REQUIRE(blockDetector.meanSquare() == sampleDetector.meanSquare());
    }

    Detector d(TimeWeighting::Fast, 48000.0);
    d.reset();
    CHECK(d.meanSquare() == 0.0);
    d.reset(4.0);
    CHECK(d.meanSquare() == 4.0);
}
