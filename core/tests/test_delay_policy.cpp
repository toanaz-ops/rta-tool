// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DelayPolicy.h"
#include "rta/gen/Synthetic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace rta::dsp;

namespace {
std::vector<float> aperiodicPink(std::size_t total, std::uint32_t seed) {
    rta::gen::SyntheticPink source(total, -12.0, seed);
    std::vector<float> out(total);
    source.render(out);
    return out;
}

/// Two arrivals: y = x[n-300] + a*x[n-420]. White (pink-generator) x, aperiodic
/// over the whole record so no loop artefact contaminates the correlation
/// (record sec.1.5). Used by A2-A4 and reproduced here rather than shared with
/// test_delay_finder.cpp so this file's fixtures stand on their own the way
/// the impl plan's table lists them.
struct TwoArrival {
    std::vector<float> x;
    std::vector<float> y;
};

TwoArrival twoArrivalFixture(double a, std::uint32_t seed) {
    constexpr std::size_t kTotal = 1 << 14;
    TwoArrival fixture;
    fixture.x = aperiodicPink(kTotal, seed);
    fixture.y.assign(kTotal, 0.0f);
    for (std::size_t n = 300; n < kTotal; ++n) {
        fixture.y[n] += fixture.x[n - 300];
    }
    for (std::size_t n = 420; n < kTotal; ++n) {
        fixture.y[n] += static_cast<float>(a) * fixture.x[n - 420];
    }
    return fixture;
}
}  // namespace

TEST_CASE("the refactor lock: suggestDelay's best equals findDelayPhat bit-for-bit",
          "[delay][policy][refactor]") {
    // Every one of test_delay_finder.cpp's 9 fixtures, reproduced verbatim
    // (impl plan task A, record sec.12.1). suggestDelay(...).best must equal
    // findDelayPhat(...) FIELD-FOR-FIELD on each -- not Approx -- because both
    // now run through the exact same PhatCorrelation.h pipeline (DEL-R1).
    constexpr std::size_t kTotal14 = 1 << 14;
    constexpr std::size_t kTotal15 = 1 << 15;

    SECTION("an integer delay is found exactly") {
        for (int d : {0, 1, 37, 512, 4095}) {
            const auto x = aperiodicPink(kTotal14, 53);
            std::vector<float> y(kTotal14, 0.0f);
            for (std::size_t n = static_cast<std::size_t>(d); n < kTotal14; ++n) {
                y[n] = x[n - static_cast<std::size_t>(d)];
            }
            const auto expected = findDelayPhat(x, y, PhatOptions{});
            const auto actual = suggestDelay(x, y, PhatOptions{}, DelayPolicy{}).best;
            REQUIRE(actual.delaySamples == expected.delaySamples);
            REQUIRE(actual.subSample == expected.subSample);
            REQUIRE(actual.peak == expected.peak);
            REQUIRE(actual.inverted == expected.inverted);
        }
    }

    SECTION("a delay in the other direction is negative") {
        const auto y = aperiodicPink(kTotal14, 59);
        std::vector<float> x(kTotal14, 0.0f);
        for (std::size_t n = 200; n < kTotal14; ++n) x[n] = y[n - 200];
        const auto expected = findDelayPhat(x, y, PhatOptions{});
        const auto actual = suggestDelay(x, y, PhatOptions{}, DelayPolicy{}).best;
        REQUIRE(actual.delaySamples == expected.delaySamples);
        REQUIRE(actual.subSample == expected.subSample);
        REQUIRE(actual.peak == expected.peak);
        REQUIRE(actual.inverted == expected.inverted);
    }

    SECTION("an inverted cable is reported, not hidden in the delay") {
        const auto x = aperiodicPink(kTotal14, 61);
        std::vector<float> y(kTotal14, 0.0f);
        for (std::size_t n = 64; n < kTotal14; ++n) y[n] = -x[n - 64];
        const auto expected = findDelayPhat(x, y, PhatOptions{});
        const auto actual = suggestDelay(x, y, PhatOptions{}, DelayPolicy{}).best;
        REQUIRE(actual.delaySamples == expected.delaySamples);
        REQUIRE(actual.subSample == expected.subSample);
        REQUIRE(actual.peak == expected.peak);
        REQUIRE(actual.inverted == expected.inverted);
    }

    SECTION("sub-sample interpolation beats the sample grid") {
        const auto x = aperiodicPink(kTotal14, 67);
        std::vector<float> y(kTotal14, 0.0f);
        for (std::size_t n = 100; n < kTotal14; ++n) {
            y[n] = 0.5f * (x[n - 100] + x[n - 101]);
        }
        const auto expected = findDelayPhat(x, y, PhatOptions{});
        const auto actual = suggestDelay(x, y, PhatOptions{}, DelayPolicy{}).best;
        REQUIRE(actual.delaySamples == expected.delaySamples);
        REQUIRE(actual.subSample == expected.subSample);
        REQUIRE(actual.peak == expected.peak);
        REQUIRE(actual.inverted == expected.inverted);
    }

    SECTION("noise degrades the peak height without moving the peak") {
        const auto x = aperiodicPink(kTotal15, 71);
        const auto n = aperiodicPink(kTotal15, 73);
        std::vector<float> y(kTotal15, 0.0f);
        for (std::size_t i = 300; i < kTotal15; ++i) y[i] = x[i - 300] + 2.0f * n[i];
        const auto expected = findDelayPhat(x, y, PhatOptions{});
        const auto actual = suggestDelay(x, y, PhatOptions{}, DelayPolicy{}).best;
        REQUIRE(actual.delaySamples == expected.delaySamples);
        REQUIRE(actual.subSample == expected.subSample);
        REQUIRE(actual.peak == expected.peak);
        REQUIRE(actual.inverted == expected.inverted);
    }

    SECTION("the band limit restricts what is correlated") {
        const auto x = aperiodicPink(kTotal14, 79);
        std::vector<float> y(kTotal14, 0.0f);
        for (std::size_t i = 150; i < kTotal14; ++i) y[i] = x[i - 150];
        PhatOptions options;
        options.minHz = 200.0;
        options.maxHz = 4000.0;
        const auto expected = findDelayPhat(x, y, options);
        const auto actual = suggestDelay(x, y, options, DelayPolicy{}).best;
        REQUIRE(actual.delaySamples == expected.delaySamples);
        REQUIRE(actual.subSample == expected.subSample);
        REQUIRE(actual.peak == expected.peak);
        REQUIRE(actual.inverted == expected.inverted);
    }

    SECTION("the finder refuses arguments it cannot use") {
        const std::vector<float> a(128, 0.0f), b(64, 0.0f), empty;
        REQUIRE_THROWS_AS(suggestDelay(a, b, PhatOptions{}, DelayPolicy{}), std::invalid_argument);
        REQUIRE_THROWS_AS(suggestDelay(empty, empty, PhatOptions{}, DelayPolicy{}),
                           std::invalid_argument);
        PhatOptions bad;
        bad.minHz = 5000.0;
        bad.maxHz = 100.0;
        REQUIRE_THROWS_AS(suggestDelay(a, a, bad, DelayPolicy{}), std::invalid_argument);
    }

    SECTION("silence produces a zero peak rather than a NaN") {
        const std::vector<float> silence(4096, 0.0f);
        const auto expected = findDelayPhat(silence, silence, PhatOptions{});
        const auto actual = suggestDelay(silence, silence, PhatOptions{}, DelayPolicy{}).best;
        REQUIRE(actual.delaySamples == expected.delaySamples);
        REQUIRE(actual.subSample == expected.subSample);
        REQUIRE(actual.peak == expected.peak);
        REQUIRE(actual.inverted == expected.inverted);
        REQUIRE(std::isfinite(actual.peak));
        REQUIRE(std::isfinite(actual.subSample));
    }

    SECTION("a relative regularisation floor tracks the input level") {
        const auto xFull = aperiodicPink(kTotal14, 83);
        std::vector<float> yFull(kTotal14, 0.0f);
        for (std::size_t n = 400; n < kTotal14; ++n) yFull[n] = xFull[n - 400];

        std::vector<float> xQuiet(kTotal14), yQuiet(kTotal14);
        for (std::size_t i = 0; i < kTotal14; ++i) {
            xQuiet[i] = xFull[i] * 1e-4f;
            yQuiet[i] = yFull[i] * 1e-4f;
        }

        PhatOptions options;
        options.regularisation = 0.1;

        const auto expectedFull = findDelayPhat(xFull, yFull, options);
        const auto actualFull = suggestDelay(xFull, yFull, options, DelayPolicy{}).best;
        REQUIRE(actualFull.delaySamples == expectedFull.delaySamples);
        REQUIRE(actualFull.subSample == expectedFull.subSample);
        REQUIRE(actualFull.peak == expectedFull.peak);
        REQUIRE(actualFull.inverted == expectedFull.inverted);

        const auto expectedQuiet = findDelayPhat(xQuiet, yQuiet, options);
        const auto actualQuiet = suggestDelay(xQuiet, yQuiet, options, DelayPolicy{}).best;
        REQUIRE(actualQuiet.delaySamples == expectedQuiet.delaySamples);
        REQUIRE(actualQuiet.subSample == expectedQuiet.subSample);
        REQUIRE(actualQuiet.peak == expectedQuiet.peak);
        REQUIRE(actualQuiet.inverted == expectedQuiet.inverted);
    }

    // test_delay_finder.cpp itself stays green, unchanged, alongside this --
    // the refactor lock is both files agreeing, not just this one.
}

TEST_CASE("two-arrival: the ghost is causal-inverted, argmax picks the direct path",
          "[delay][policy][two-arrival]") {
    for (double a : {0.3, 0.6, 0.9}) {
        const auto fixture = twoArrivalFixture(a, 101);
        const auto suggestion =
                suggestDelay(fixture.x, fixture.y, PhatOptions{}, DelayPolicy{});

        REQUIRE(suggestion.best.delaySamples == 300);

        bool foundGhost = false;
        bool foundSecond = false;
        for (const auto& c : suggestion.candidates) {
            if (c.delaySamples == 180) {
                foundGhost = true;
                REQUIRE(c.inverted);
            }
            if (c.delaySamples == 420) {
                foundSecond = true;
                REQUIRE_FALSE(c.inverted);
            }
        }
        REQUIRE(foundGhost);
        REQUIRE(foundSecond);

        if (a == 0.3) {
            double heightAt180 = 0.0;
            double heightAt300 = 0.0;
            for (const auto& c : suggestion.candidates) {
                if (c.delaySamples == 180) heightAt180 = c.height;
                if (c.delaySamples == 300) heightAt300 = c.height;
            }
            const double ratio = std::abs(heightAt180) / std::abs(heightAt300);
            REQUIRE(ratio == Catch::Approx(a / 2.0).margin(0.05));
        }
    }
}

TEST_CASE("two-arrival: argmax flips past a=1, labelled as what argmax does",
          "[delay][policy][two-arrival]") {
    // NOT a correctness claim -- record sec.3: a later arrival louder than the
    // direct path is what soloOutput (L7-OUT) removes before this ever runs
    // live. Here it is simply what the argmax primitive does.
    const auto fixture = twoArrivalFixture(1.5, 101);
    const auto suggestion = suggestDelay(fixture.x, fixture.y, PhatOptions{}, DelayPolicy{});
    REQUIRE(suggestion.best.delaySamples == 420);
    REQUIRE(suggestion.candidates.size() >= 2);
    REQUIRE(suggestion.candidates[1].delaySamples == 300);
}

TEST_CASE("the window is honoured and inversion survives a wrong window",
          "[delay][policy][window]") {
    const auto fixture = twoArrivalFixture(1.5, 211);

    SECTION("maxLag excludes the louder later arrival") {
        DelayPolicy policy;
        policy.maxLag = 350;
        const auto suggestion =
                suggestDelay(fixture.x, fixture.y, PhatOptions{}, policy);
        REQUIRE(suggestion.best.delaySamples == 300);
    }

    SECTION("a narrow window lands on the inverted ghost, visibly") {
        DelayPolicy policy;
        policy.minLag = 100;
        policy.maxLag = 200;
        const auto suggestion =
                suggestDelay(fixture.x, fixture.y, PhatOptions{}, policy);
        REQUIRE(suggestion.best.delaySamples == 180);
        REQUIRE(suggestion.best.inverted);
    }

    SECTION("a window with no real signal in it refuses, named") {
        DelayPolicy policy;
        policy.minLag = 1000;
        policy.maxLag = 2000;
        const auto suggestion =
                suggestDelay(fixture.x, fixture.y, PhatOptions{}, policy);
        REQUIRE(suggestion.verdict == DelayVerdict::WindowEmpty);
    }
}

TEST_CASE("ambiguity is display-only and in range", "[delay][policy][ambiguity]") {
    for (double a : {0.3, 0.6, 0.9}) {
        const auto fixture = twoArrivalFixture(a, 101);
        const auto suggestion =
                suggestDelay(fixture.x, fixture.y, PhatOptions{}, DelayPolicy{});
        if (suggestion.candidates.size() >= 2) {
            REQUIRE(suggestion.ambiguity >= 0.0);
            REQUIRE(suggestion.ambiguity <= 1.0);
            const double expected = std::abs(suggestion.candidates[1].height) /
                                     std::abs(suggestion.candidates[0].height);
            REQUIRE(suggestion.ambiguity == Catch::Approx(expected));
        }
    }
    // No code path in DelayPolicy.cpp branches on `ambiguity` -- it is
    // computed once, from the candidate list, and never read back by this
    // file's own control flow (record sec.3: L4a's `margin` failed as a
    // gate).
}
