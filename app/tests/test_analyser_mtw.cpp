// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. The app-side bridge from rta::dsp's
// multi-time-window engine to measure::Snapshot. Task 6 of
// docs/plans/2026-09-05-L3-mtw-impl-plan.md.
#include "measure/Analyser.h"

#include "rta/dsp/MtwEngine.h"
#include "rta/gen/Noise.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {

using rta::measure::Analyser;

// Same fixture length as core/tests/test_mtw_engine.cpp: 327680 samples gives
// every one of the seven default bands exactly 16 frames in its FIFO (Neff =
// 8.5866271, over the coherence gate of 8) -- the length the plan's own "test
// signal length" section derives, not a number this file picked on its own.
constexpr std::size_t kFullLength = 327680;

std::vector<float> whiteNoise(std::size_t n, std::uint64_t seed) {
    rta::gen::WhiteNoise src(rta::gen::Pcg32(seed, 0x9E3779B9u));
    std::vector<float> out(n);
    src.process(out);
    return out;
}

/// y[i] = x[i - delay], leading `delay` samples zero.
std::vector<float> delayedCopy(const std::vector<float>& x, std::size_t delay) {
    std::vector<float> y(x.size(), 0.0f);
    for (std::size_t i = delay; i < x.size(); ++i) y[i] = x[i - delay];
    return y;
}

Analyser::Config baseConfig() {
    Analyser::Config cfg;
    cfg.fftSize = 4096;
    cfg.hopSize = 2048;
    cfg.sampleRate = 48000.0;
    return cfg;
}

}  // namespace

TEST_CASE("no MTW block before the first pushPair", "[analyser][mtw]") {
    // A single-channel capture has no transfer function; it does not have a
    // flat one -- the same principle TransferBlock's own absence follows.
    Analyser analyser(baseConfig());
    const auto block = whiteNoise(48000, 5);
    analyser.pushMeasurement(block);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot != nullptr);
    CHECK_FALSE(snapshot->mtw.has_value());
}

TEST_CASE("Analyser publishes an MTW block once a pair has been pushed", "[analyser][mtw]") {
    Analyser analyser(baseConfig());
    const auto reference = whiteNoise(48000, 31);
    analyser.pushPair(reference, reference);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot != nullptr);
    REQUIRE(snapshot->mtw.has_value());
    const auto& mtw = *snapshot->mtw;
    CHECK(mtw.frequencyHz.size() == 1281);
    CHECK(mtw.magnitudeDb.size() == 1281);
    CHECK(mtw.phaseDeg.size() == 1281);
    CHECK(mtw.coherence.size() == 1281);
}

TEST_CASE("the MTW block carries an explicit frequency vector, not a bin width",
          "[analyser][mtw]") {
    auto cfg = baseConfig();
    Analyser analyser(cfg);
    const auto reference = whiteNoise(48000, 37);
    analyser.pushPair(reference, reference);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot->mtw.has_value());
    const auto& hz = snapshot->mtw->frequencyHz;
    REQUIRE(hz.size() == 1281);
    CHECK(hz[256] == Catch::Approx(187.5));
    CHECK(hz[384] == Catch::Approx(375.0));
    CHECK(hz[896] == Catch::Approx(6000.0));
    CHECK(hz[1280] == Catch::Approx(24000.0));
    for (std::size_t i = 1; i < hz.size(); ++i) {
        CAPTURE(i);
        CHECK(hz[i] > hz[i - 1]);
    }

    // The fixed engine's own size is untouched -- TransferBlock consumers
    // must not be confused by the new block.
    CHECK(snapshot->fftSize == cfg.fftSize);
}

TEST_CASE("the MTW block's descriptors mark every band boundary", "[analyser][mtw]") {
    Analyser analyser(baseConfig());
    const auto x = whiteNoise(kFullLength, 41);
    analyser.pushPair(x, x);
    const auto snapshot = analyser.publish(0);

    REQUIRE(snapshot->mtw.has_value());
    const auto& mtw = *snapshot->mtw;
    REQUIRE(mtw.bands.size() == 7);

    const std::vector<float> expectedSeamHz{ 0.0f, 187.5f, 375.0f, 750.0f, 1500.0f, 3000.0f, 6000.0f };
    const std::vector<std::size_t> expectedFirstIndex{ 0, 256, 384, 512, 640, 768, 896 };
    // Record §5, reversed to frames-uniform: seconds differ per band even
    // though effectiveAverages does not -- each entry is exactly twice its
    // neighbour, from the FIFO depth (16) times that band's own hop over fs.
    const std::vector<double> expectedSeconds{ 5.4613, 2.7307, 1.3653, 0.6827,
                                               0.3413, 0.1707, 0.0853 };

    std::size_t totalPoints = 0;
    for (std::size_t i = 0; i < mtw.bands.size(); ++i) {
        CAPTURE(i);
        const auto& band = mtw.bands[i];
        CHECK(band.seamHz == Catch::Approx(expectedSeamHz[i]));
        CHECK(band.firstIndex == expectedFirstIndex[i]);
        // 8.5866271: fifoEffectiveAverages(hann(N), N/4, 16), identical in
        // every band because hop/N = 1/4 everywhere (record §5's corrected
        // digits, not the earlier draft's "about 8.8").
        CHECK(band.effectiveAverages == Catch::Approx(8.5866271).epsilon(1e-6));
        CHECK(band.integrationSeconds == Catch::Approx(expectedSeconds[i]).margin(1e-4));
        totalPoints += band.pointCount;
    }
    CHECK(totalPoints == 1281);
    CHECK(mtw.bands[0].windowSeconds == Catch::Approx(1.3653).margin(1e-4));
}

TEST_CASE("phase crosses to degrees exactly once, in the same place the fixed block does",
          "[analyser][mtw]") {
    // Part 1 -- task 2 case 1's compensated-delay pair: every band's h is
    // unity, so magnitudeDb/phaseDeg are exact numbers a directly constructed
    // MtwEngine must reproduce bit for bit. This is what catches a stray
    // dB/linear conversion in the flattening.
    constexpr int kDelay = 137;
    const auto x = whiteNoise(kFullLength, 43);
    const auto y = delayedCopy(x, kDelay);

    auto cfg = baseConfig();
    cfg.referenceDelaySamples = kDelay;

    Analyser analyser(cfg);
    analyser.pushPair(x, y);
    const auto snapshot = analyser.publish(0);
    REQUIRE(snapshot->mtw.has_value());

    rta::dsp::MtwConfig mtwCfg;
    mtwCfg.referenceDelaySamples = kDelay;
    rta::dsp::MtwEngine engine(mtwCfg);
    engine.process(x, y);
    const auto expected = rta::dsp::makeMtwResult(engine, rta::dsp::Estimator::H1);

    const auto& mtw = *snapshot->mtw;
    REQUIRE(mtw.magnitudeDb.size() == expected.magnitudeDb.size());
    for (std::size_t i = 0; i < mtw.magnitudeDb.size(); ++i) {
        CAPTURE(i);
        CHECK(mtw.magnitudeDb[i] == expected.magnitudeDb[i]);
        const float expectedDeg =
            expected.phaseRadians[i] * static_cast<float>(180.0 / std::numbers::pi);
        CHECK(mtw.phaseDeg[i] == expectedDeg);
        CHECK(mtw.phaseDeg[i] == 0.0f);
    }

    // Part 2 -- the same cheap backstop test_analyser_transfer.cpp's own
    // "phase crosses the seam in degrees, not radians" uses: an UNCOMPENSATED
    // delay gives a real phase slope, up to +-180 degrees but never beyond
    // +-3.15 radians -- a forgotten *180/pi conversion is invisible on the
    // compensated fixture above (0 rad is 0 deg either way) but not on this
    // one.
    Analyser uncompensated(baseConfig());
    uncompensated.pushPair(x, y);
    const auto uncompensatedSnapshot = uncompensated.publish(0);
    REQUIRE(uncompensatedSnapshot->mtw.has_value());
    const auto& phase = uncompensatedSnapshot->mtw->phaseDeg;
    const bool anyBeyondRadians =
        std::any_of(phase.begin(), phase.end(), [](float p) { return std::abs(p) > 3.2f; });
    CHECK(anyBeyondRadians);
}
