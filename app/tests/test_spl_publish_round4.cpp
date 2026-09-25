// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PR #29 round-4 fix pass items 3/4: two counters that already existed on
// `SplMeter`/`SplSession` and reached no published view --
// `SplMeter::overflowedLnTicks()` and `SplSession::blockSecondsTooSmall()`.
// Split out of test_spl_publish.cpp (already over the 400-line cap before
// this round) rather than grown further.
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisPublish.h"
#include "measure/Snapshot.h"
#include "measure/SplConfig.h"
#include "measure/SplMeter.h"
#include "measure/SplSession.h"

#include "rta/meter/Block.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

using rta::measure::SplConfig;
using rta::measure::SplPublishInput;
using rta::measure::SplSession;
using rta::meter::Block;

namespace {

constexpr double kFs = 48000.0;

SplConfig oneMetricConfig(double blockSeconds) {
    SplConfig config;
    config.blockSeconds = blockSeconds;
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1});
    return config;
}

/// Mirrors `AnalysisThread::fillSplPublishInput`'s own field-by-field copy
/// (test_spl_publish.cpp's own `publishThroughSession` precedent), plus the
/// two round-4 fields that function now also fills.
std::optional<rta::measure::SplBlockView> publishThroughSession(SplSession& session) {
    SplPublishInput in;
    in.config = session.config();
    in.sampleRate = session.sampleRate();
    in.latestBlock = session.latestBlock(0);
    in.window = session.window(0);
    in.refusedMetrics = session.refusedMetrics();
    in.overflowedLnTicks = session.overflowedLnTicks(0, rta::dsp::WeightingType::A);
    in.blockSecondsTooSmall = session.blockSecondsTooSmall();

    static std::array<std::span<const Block>, SplConfig::kMaxMetrics> windows{};
    windows = {};
    const std::size_t filled = session.fillMetricWindows(0, windows);
    in.metricWindows = std::span<const std::span<const Block>>(windows).first(filled);
    return rta::measure::buildSplBlockView(in);
}

}  // namespace

// --- item 3: an Ln-tick overflow reaches the published view ---------------

TEST_CASE("an Ln-tick overflow publishes lnTicksOverflowed, not silence",
         "[splpublish][round4]") {
    // A tiny blockSeconds floors SplMeter's own tick-buffer sizing
    // (ticksPerBlockFor) to 1, so `lnTickBufferCapacity` = max(16, 6*1) = 16
    // -- SplMeter.cpp's own `lnTickBufferCapacity` comment. One HOP spanning
    // more than 16 100 ms ticks (> 16*4800 = 76 800 samples at 48 kHz)
    // overflows it within a single push() call, since `lnTicks_` is cleared
    // at the top of every push().
    SplConfig config = oneMetricConfig(0.05);
    SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);

    // 20 ticks' worth of samples: 20 - 16 = 4 ticks must overflow.
    std::vector<float> hop(20 * 4800, 0.1f);
    session.feedHop(0, hop);

    REQUIRE(session.overflowedLnTicks(0, rta::dsp::WeightingType::A) == 4);

    const auto view = publishThroughSession(session);
    REQUIRE(view.has_value());
    CHECK(view->lnTicksOverflowed == 4);
}

TEST_CASE("no Ln-tick overflow publishes lnTicksOverflowed == 0", "[splpublish][round4]") {
    SplConfig config = oneMetricConfig(1.0);  // blockSamples = 48 000 at 48 kHz
    SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);

    std::vector<float> hop(4800, 0.1f);
    for (int i = 0; i < 10; ++i) session.feedHop(0, hop);  // 48 000 samples: one block closes

    REQUIRE(session.overflowedLnTicks(0, rta::dsp::WeightingType::A) == 0);
    const auto view = publishThroughSession(session);
    REQUIRE(view.has_value());
    CHECK(view->lnTicksOverflowed == 0);
}

// --- item 4: the advisory blockSeconds floor flag reaches the published ---
// view, without refusing the session -----------------------------------

TEST_CASE("a below-floor session publishes blockSecondsBelowRecommendedFloor",
         "[splpublish][round4]") {
    SplConfig config = oneMetricConfig(0.002);  // below the advisory floor at 48 kHz
    SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);
    REQUIRE(session.blockSecondsTooSmall());

    std::vector<float> hop(4800, 0.1f);
    session.feedHop(0, hop);

    const auto view = publishThroughSession(session);
    REQUIRE(view.has_value());
    CHECK(view->blockSecondsBelowRecommendedFloor);
    // ADVISORY ONLY: the session still ran and still logged and published a
    // real reading -- never refused.
    CHECK(session.running());
    CHECK_FALSE(view->metrics.empty());
}

TEST_CASE("a normal session publishes blockSecondsBelowRecommendedFloor == false",
         "[splpublish][round4]") {
    SplConfig config = oneMetricConfig(1.0);  // the shipped default, comfortably above the floor
    SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);
    REQUIRE_FALSE(session.blockSecondsTooSmall());

    std::vector<float> hop(4800, 0.1f);
    for (int i = 0; i < 10; ++i) session.feedHop(0, hop);  // 48 000 samples: one block closes

    const auto view = publishThroughSession(session);
    REQUIRE(view.has_value());
    CHECK_FALSE(view->blockSecondsBelowRecommendedFloor);
}
