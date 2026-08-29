// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DelayFinder.h"
#include "rta/gen/Synthetic.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace rta::dsp;

namespace {
std::vector<float> aperiodicPink(std::size_t total, std::uint32_t seed) {
    // blockSize == total, so nothing repeats inside the record. A LOOPED source
    // would put correlation peaks at D plus every multiple of the loop, and the
    // finder would be choosing between them arbitrarily.
    rta::gen::SyntheticPink source(total, -12.0, seed);
    std::vector<float> out(total);
    source.render(out);
    return out;
}
}  // namespace

TEST_CASE("an integer delay is found exactly", "[delay][phat]") {
    constexpr std::size_t kTotal = 1 << 14;
    for (int d : {0, 1, 37, 512, 4095}) {
        const auto x = aperiodicPink(kTotal, 53);
        std::vector<float> y(kTotal, 0.0f);
        for (std::size_t n = static_cast<std::size_t>(d); n < kTotal; ++n) {
            y[n] = x[n - static_cast<std::size_t>(d)];
        }
        const auto est = findDelayPhat(x, y, PhatOptions{});
        REQUIRE(est.delaySamples == d);
        REQUIRE(std::abs(est.subSample) < 0.05);
        REQUIRE(est.peak > 0.5);
        REQUIRE_FALSE(est.inverted);
    }
}

TEST_CASE("a delay in the other direction is negative", "[delay][phat]") {
    constexpr std::size_t kTotal = 1 << 14;
    const auto y = aperiodicPink(kTotal, 59);
    std::vector<float> x(kTotal, 0.0f);
    for (std::size_t n = 200; n < kTotal; ++n) x[n] = y[n - 200];
    const auto est = findDelayPhat(x, y, PhatOptions{});
    REQUIRE(est.delaySamples == -200);
}

TEST_CASE("an inverted cable is reported, not hidden in the delay", "[delay][polarity]") {
    constexpr std::size_t kTotal = 1 << 14;
    const auto x = aperiodicPink(kTotal, 61);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t n = 64; n < kTotal; ++n) y[n] = -x[n - 64];
    const auto est = findDelayPhat(x, y, PhatOptions{});
    REQUIRE(est.delaySamples == 64);
    REQUIRE(est.inverted);
}

TEST_CASE("sub-sample interpolation beats the sample grid", "[delay][subsample]") {
    // Half a sample at 48 kHz is 3.6 mm of air. Without interpolation the
    // finder cannot see it at all, and Friture's own source concedes that this
    // caps delay resolution at roughly 3 cm.
    constexpr std::size_t kTotal = 1 << 14;
    const auto x = aperiodicPink(kTotal, 67);
    std::vector<float> y(kTotal, 0.0f);
    // Linear interpolation is a mild low-pass, but it shifts the peak by
    // exactly 0.5 samples, which is what is being measured here.
    for (std::size_t n = 100; n < kTotal; ++n) {
        y[n] = 0.5f * (x[n - 100] + x[n - 101]);
    }
    const auto est = findDelayPhat(x, y, PhatOptions{});
    const double total = static_cast<double>(est.delaySamples) + est.subSample;
    REQUIRE(total == Catch::Approx(100.5).margin(0.1));
}

TEST_CASE("noise degrades the peak height without moving the peak", "[delay][phat]") {
    constexpr std::size_t kTotal = 1 << 15;
    const auto x = aperiodicPink(kTotal, 71);
    const auto n = aperiodicPink(kTotal, 73);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t i = 300; i < kTotal; ++i) y[i] = x[i - 300] + 2.0f * n[i];
    const auto est = findDelayPhat(x, y, PhatOptions{});
    REQUIRE(est.delaySamples == 300);
    REQUIRE(est.peak < 0.9);   // the operator must be able to see it is poor
    REQUIRE(est.peak > 0.0);
}

TEST_CASE("the band limit restricts what is correlated", "[delay][band]") {
    constexpr std::size_t kTotal = 1 << 14;
    const auto x = aperiodicPink(kTotal, 79);
    std::vector<float> y(kTotal, 0.0f);
    for (std::size_t i = 150; i < kTotal; ++i) y[i] = x[i - 150];
    PhatOptions options;
    options.minHz = 200.0;
    options.maxHz = 4000.0;
    const auto est = findDelayPhat(x, y, options);
    REQUIRE(est.delaySamples == 150);

    // The band limit is the whole point of this case, and asserting only the
    // delay does not test it -- deleting the band limit entirely passes every
    // fixture in this file. PHAT whitens each in-band bin to unit magnitude and
    // zeroes the rest, so the correlation peak is the in-band FRACTION of the
    // spectrum: (maxHz - minHz) / (sampleRate / 2). That also pins the bin-to-
    // hertz mapping directly, rather than through the default maxHz by accident.
    const double inBandFraction = (options.maxHz - options.minHz) / (48000.0 / 2.0);
    REQUIRE(est.peak == Catch::Approx(inBandFraction).margin(0.05));
}

TEST_CASE("the finder refuses arguments it cannot use", "[delay][edge]") {
    const std::vector<float> a(128, 0.0f), b(64, 0.0f), empty;
    REQUIRE_THROWS_AS(findDelayPhat(a, b, PhatOptions{}), std::invalid_argument);
    REQUIRE_THROWS_AS(findDelayPhat(empty, empty, PhatOptions{}), std::invalid_argument);
    PhatOptions bad; bad.minHz = 5000.0; bad.maxHz = 100.0;
    REQUIRE_THROWS_AS(findDelayPhat(a, a, bad), std::invalid_argument);
}

TEST_CASE("silence produces a zero peak rather than a NaN", "[delay][edge]") {
    const std::vector<float> silence(4096, 0.0f);
    const auto est = findDelayPhat(silence, silence, PhatOptions{});
    REQUIRE(std::isfinite(est.peak));
    REQUIRE(std::isfinite(est.subSample));
}

TEST_CASE("a relative regularisation floor tracks the input level", "[delay][phat][regularisation]") {
    // The decision record rejects an ABSOLUTE floor (Open Sound Meter's -140
    // dBFS) precisely because it couples the estimator to input level: the
    // same pair, scaled down, would be regularised differently and could shift
    // the answer. Scaling both channels by 1e-4 (-80 dB) must not move the
    // delay and must not collapse the peak -- if it does, the floor has
    // silently become absolute.
    //
    // For that difference to be OBSERVABLE, the floor has to actually bind
    // somewhere. With the default 1e-10 it never does at either level tested
    // here -- max|G| stays many orders of magnitude above it even after an
    // 80 dB cut, so a relative and an absolute implementation would agree
    // trivially and this test would not be exercising anything. 0.1 is a
    // deliberately large regularisation, NOT a recommended operating value:
    // it is chosen purely so that RELATIVE (floor = 0.1 * max|G|, identical
    // shape at both levels) and ABSOLUTE (floor = 0.1, negligible at full
    // scale but far above max|G| ~ 4.7e-3 at the quiet level, so the quiet
    // weighting collapses towards a near-uniform 1/floor) produce visibly
    // different answers -- which is the whole point of the case.
    constexpr std::size_t kTotal = 1 << 14;
    const auto xFull = aperiodicPink(kTotal, 83);
    std::vector<float> yFull(kTotal, 0.0f);
    for (std::size_t n = 400; n < kTotal; ++n) yFull[n] = xFull[n - 400];

    std::vector<float> xQuiet(kTotal), yQuiet(kTotal);
    for (std::size_t i = 0; i < kTotal; ++i) {
        xQuiet[i] = xFull[i] * 1e-4f;
        yQuiet[i] = yFull[i] * 1e-4f;
    }

    PhatOptions options;
    options.regularisation = 0.1;

    const auto estFull = findDelayPhat(xFull, yFull, options);
    const auto estQuiet = findDelayPhat(xQuiet, yQuiet, options);

    REQUIRE(estQuiet.delaySamples == estFull.delaySamples);
    REQUIRE(estQuiet.subSample == Catch::Approx(estFull.subSample).margin(0.05));
    REQUIRE(estQuiet.peak == Catch::Approx(estFull.peak).margin(0.05));
}
