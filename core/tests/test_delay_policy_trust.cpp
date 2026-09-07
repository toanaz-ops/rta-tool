// SPDX-License-Identifier: AGPL-3.0-or-later
// Task B (docs/plans/2026-09-07-L7-delay-impl-plan.md): trust, the derived
// null floor, the accept gate and the band-limit justification (record
// docs/dsp/2026-09-06-l7-auto-delay.md sec.4, sec.7). Split from
// test_delay_policy.cpp (which carries Task A's window/candidate machinery)
// to keep both files under the project's 400-line cap.
#include "rta/dsp/DelayPolicy.h"
#include "rta/gen/Synthetic.h"

#include "DelayFilterFixtures.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace rta::dsp;

namespace {
std::vector<float> aperiodicPink(std::size_t total, std::uint32_t seed) {
    rta::gen::SyntheticPink source(total, -12.0, seed);
    std::vector<float> out(total);
    source.render(out);
    return out;
}

/// Mirrors DelayPolicy.cpp's own M_in count, for computing the predicted
/// null floor from the test side without reaching into private state.
std::size_t inBandBinCount(std::size_t m, double sampleRate, double minHz, double maxHz) {
    const std::size_t numBins = m / 2 + 1;
    std::size_t count = 0;
    for (std::size_t k = 0; k < numBins; ++k) {
        const double freq = static_cast<double>(k) * sampleRate / static_cast<double>(m);
        if (freq >= minHz && freq <= maxHz) ++count;
    }
    return count;
}
}  // namespace

TEST_CASE("trust=1 on a clean single arrival", "[delay][policy][trust]") {
    constexpr std::size_t kTotal = 1 << 14;
    const auto x = aperiodicPink(kTotal, 131);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t n = 300; n < kTotal; ++n) y[n] = x[n - 300];
    const auto suggestion = suggestDelay(x, y, PhatOptions{}, DelayPolicy{});
    REQUIRE(suggestion.trust == Catch::Approx(1.0).margin(0.05));
    REQUIRE(suggestion.verdict == DelayVerdict::Accepted);
}

TEST_CASE("null floor, full-band form", "[delay][policy][nullfloor]") {
    for (std::size_t targetM : {std::size_t{1 << 14}, std::size_t{1 << 15}, std::size_t{1 << 16}}) {
        const std::size_t inputLen = targetM / 2;  // bit_ceil(2*inputLen) == targetM
        double sum = 0.0;
        double worst = 0.0;
        for (std::uint32_t trial = 0; trial < 40; ++trial) {
            const auto x = aperiodicPink(inputLen, 1000 + trial * 2);
            const auto y = aperiodicPink(inputLen, 1000 + trial * 2 + 1);
            const auto suggestion = suggestDelay(x, y, PhatOptions{}, DelayPolicy{});
            sum += suggestion.trust;
            worst = std::max(worst, suggestion.trust);
        }
        const double mean = sum / 40.0;
        const double predicted =
                std::sqrt(2.0 * std::log(static_cast<double>(targetM)) / static_cast<double>(targetM));
        REQUIRE(mean == Catch::Approx(predicted).margin(predicted * 0.15));
        REQUIRE(worst <= predicted * 1.4);
    }
}

TEST_CASE("null floor, band-limited form", "[delay][policy][nullfloor]") {
    constexpr std::size_t kInputLen = 1 << 14;  // m == 1<<15
    constexpr double kSampleRate = 48000.0;
    PhatOptions options;
    options.minHz = 200.0;
    options.maxHz = 4000.0;

    double sum = 0.0;
    double worst = 0.0;
    for (std::uint32_t trial = 0; trial < 40; ++trial) {
        const auto x = aperiodicPink(kInputLen, 2000 + trial * 2);
        const auto y = aperiodicPink(kInputLen, 2000 + trial * 2 + 1);
        const auto suggestion = suggestDelay(x, y, options, DelayPolicy{});
        sum += suggestion.trust;
        worst = std::max(worst, suggestion.trust);
    }
    const double mean = sum / 40.0;
    const std::size_t m = 1 << 15;
    const std::size_t mIn = inBandBinCount(m, kSampleRate, options.minHz, options.maxHz);
    const double predicted =
            std::sqrt(std::log(static_cast<double>(m)) / static_cast<double>(mIn));
    REQUIRE(mean == Catch::Approx(predicted).margin(predicted * 0.15));
    REQUIRE(worst <= predicted * 1.4);
}

TEST_CASE("accept above the floor", "[delay][policy][gate]") {
    constexpr std::size_t kTotal = 1 << 15;
    const auto x = aperiodicPink(kTotal, 171);
    const auto n = aperiodicPink(kTotal, 173);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t i = 300; i < kTotal; ++i) y[i] = x[i - 300] + 2.0f * n[i];  // -6 dB SNR
    const auto suggestion = suggestDelay(x, y, PhatOptions{}, DelayPolicy{});
    REQUIRE(suggestion.verdict == DelayVerdict::Accepted);
    REQUIRE(suggestion.trust > 0.3);
    REQUIRE(suggestion.best.delaySamples == 300);
}

TEST_CASE("refuse below the floor, no NaN", "[delay][policy][gate]") {
    SECTION("-30 dB SNR refuses") {
        constexpr std::size_t kTotal = 1 << 15;
        const auto x = aperiodicPink(kTotal, 271);
        const auto n = aperiodicPink(kTotal, 273);
        std::vector<float> y(kTotal, 0.0f);
        const float noiseScale = static_cast<float>(std::pow(10.0, 30.0 / 20.0));  // -30 dB SNR
        for (std::size_t i = 300; i < kTotal; ++i) y[i] = x[i - 300] + noiseScale * n[i];
        const auto suggestion = suggestDelay(x, y, PhatOptions{}, DelayPolicy{});
        REQUIRE(suggestion.verdict == DelayVerdict::BelowFloor);
        REQUIRE(std::isfinite(suggestion.trust));
    }

    SECTION("silence refuses, never NaN") {
        const std::vector<float> silence(4096, 0.0f);
        const auto suggestion = suggestDelay(silence, silence, PhatOptions{}, DelayPolicy{});
        REQUIRE(suggestion.verdict == DelayVerdict::BelowFloor);
        REQUIRE(std::isfinite(suggestion.trust));
        REQUIRE(suggestion.trust == 0.0);
    }
}

TEST_CASE("the band limit rescues a narrowband box, in both directions",
          "[delay][policy][band]") {
    constexpr std::size_t kTotal = 1 << 14;
    constexpr double kSampleRate = 48000.0;
    const auto x = aperiodicPink(kTotal, 331);
    const auto xFiltered = rta::test::applyButterworthBandPass(x, 60.0, 960.0, kSampleRate, 4);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t n = 300; n < kTotal; ++n) y[n] = xFiltered[n - 300];

    SECTION("band limit set to the box's own band") {
        PhatOptions options;
        options.sampleRate = kSampleRate;
        options.minHz = 60.0;
        options.maxHz = 960.0;
        const auto suggestion = suggestDelay(x, y, options, DelayPolicy{});
        REQUIRE(suggestion.trust > 0.75);
        REQUIRE(suggestion.verdict == DelayVerdict::Accepted);
    }

    SECTION("full band reads untrustworthy despite a perfectly good box") {
        PhatOptions options;
        options.sampleRate = kSampleRate;
        const auto suggestion = suggestDelay(x, y, options, DelayPolicy{});
        REQUIRE(suggestion.trust < 0.2);
    }
}

TEST_CASE("excess phase does not move the verdict", "[delay][policy][band]") {
    constexpr std::size_t kTotal = 1 << 14;
    constexpr double kSampleRate = 48000.0;
    PhatOptions options;
    options.sampleRate = kSampleRate;

    SECTION("a 2nd-order allpass at 1 kHz, Q 0.7") {
        const auto x = aperiodicPink(kTotal, 431);
        const auto coeffs = rta::test::rbjAllpass(1000.0, 0.7, kSampleRate);
        const auto xAllpass = rta::test::applyBiquad(coeffs, x);
        std::vector<float> y(kTotal, 0.0f);
        for (std::size_t n = 300; n < kTotal; ++n) y[n] = xAllpass[n - 300];
        const auto suggestion = suggestDelay(x, y, options, DelayPolicy{});
        REQUIRE(suggestion.best.delaySamples == 300);
        REQUIRE(suggestion.trust > 0.75);
    }

    SECTION("an LR4 crossover sum") {
        const auto x = aperiodicPink(kTotal, 433);
        const auto xCrossed = rta::test::lr4CrossoverSum(x, 1000.0, kSampleRate);
        std::vector<float> y(kTotal, 0.0f);
        for (std::size_t n = 300; n < kTotal; ++n) y[n] = xCrossed[n - 300];
        const auto suggestion = suggestDelay(x, y, options, DelayPolicy{});
        // +-4: the crossover's OWN in-band group delay near 1 kHz, a real
        // latency -- not an estimator error (record sec.7).
        REQUIRE(suggestion.best.delaySamples >= 296);
        REQUIRE(suggestion.best.delaySamples <= 304);
        REQUIRE(suggestion.trust > 0.75);
    }
}
