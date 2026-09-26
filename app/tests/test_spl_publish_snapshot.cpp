// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-C, split out of test_spl_publish.cpp (LOW follow-up batch,
// items 4/5/11 -- see that file's own header comment for the three-way split
// this is part of). THIS file's subject: `buildPublishedSnapshot` end to end
// -- Snapshot::spl attached only when something is logging, through BOTH the
// unrouted and the routed (a live show's actual configuration) branches --
// and the window-array bound as a GATE, not a convention, over a real
// SplSession.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/AnalysisPublish.h"
#include "measure/Analyser.h"
#include "measure/AverageGroup.h"
#include "measure/RoutingPlan.h"
#include "measure/Snapshot.h"
#include "measure/SplConfig.h"
#include "measure/SplSession.h"

#include "rta/meter/Block.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::SplConfig;
using rta::measure::SplPublishInput;
using rta::meter::Block;

namespace {

constexpr double kFs = 48000.0;

Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    b.maxFastDb = static_cast<float>(levelDb + 1.0);
    b.maxSlowDb = static_cast<float>(levelDb + 0.5);
    b.peakDb = static_cast<float>(levelDb + 3.0);
    return b;
}

SplConfig configWithOneMetric() {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq_1s", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1});
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq_60s", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Slow, 60});
    return config;
}

rta::measure::Analyser::Config fastAnalyserConfig() {
    rta::measure::Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 64;
    config.sampleRate = 48000.0;
    config.mtwEnabled = false;
    config.transferFifoDepth = 8;
    return config;
}

}  // namespace

// --- defect 2: Snapshot::spl through the REAL publish path ---------------

TEST_CASE("defect 2: buildPublishedSnapshot attaches spl only when something is logging",
          "[splpublish]") {
    // THE GAP THIS CLOSES. Every earlier case in this file called
    // buildSplBlockView DIRECTLY, so the one line that decides whether
    // `Snapshot::spl` is set at all -- buildPublishedSnapshot's own
    // `(spl != nullptr) ? buildSplBlockView(*spl) : std::nullopt` and the two
    // branches that copy the base Snapshot -- was reachable from no test.
    // Publishing a default-constructed SplBlockView there left 689/689 green.
    std::vector<std::unique_ptr<rta::measure::Analyser>> analysers;
    analysers.push_back(std::make_unique<rta::measure::Analyser>(fastAnalyserConfig()));
    rta::measure::AverageGroup group;
    std::vector<int> lastTfIndices;
    const rta::measure::RoutingPlan plan;  // UNROUTED -- the plain single-channel path
    REQUIRE(plan.routes.empty());

    // Feed the Analyser enough for a real publish.
    std::vector<float> tone(64);
    for (std::size_t n = 0; n < tone.size(); ++n) {
        tone[n] = static_cast<float>(
            0.25 * std::sin(2.0 * 3.14159265358979323846 * 6.0 * static_cast<double>(n) / 64.0));
    }
    for (int frame = 0; frame < 20; ++frame) analysers[0]->pushMeasurement(tone);

    SECTION("nothing logging: the snapshot carries NO spl block") {
        const auto snapshot =
            buildPublishedSnapshot(analysers, group, lastTfIndices, plan, 0, nullptr);
        REQUIRE(snapshot != nullptr);
        CHECK_FALSE(snapshot->spl.has_value());
        // And the rest of the snapshot is still a real one, so absence of SPL
        // is not absence of a publish.
        CHECK(snapshot->sampleRate == 48000.0);
        CHECK_FALSE(snapshot->bands.empty());
    }

    SECTION("a config with no completed block: still NO spl block") {
        const SplConfig config = configWithOneMetric();
        SplPublishInput in;
        in.config = &config;
        in.sampleRate = kFs;  // latestBlock left absent on purpose
        const auto snapshot =
            buildPublishedSnapshot(analysers, group, lastTfIndices, plan, 0, &in);
        REQUIRE(snapshot != nullptr);
        CHECK_FALSE(snapshot->spl.has_value());
    }

    SECTION("logging: the block reaches Snapshot::spl with its own values") {
        const SplConfig config = configWithOneMetric();
        std::vector<Block> window;
        for (std::uint64_t i = 0; i < 90; ++i) window.push_back(blockAtLevel(i, 48000, 85.0));
        window.back().flags |= static_cast<std::uint32_t>(rta::meter::BlockFlag::Gap);
        window.back().droppedSamples = 12000;

        SplPublishInput in;
        in.config = &config;
        in.sampleRate = kFs;
        in.latestBlock = window.back();
        in.window = window;

        const auto snapshot =
            buildPublishedSnapshot(analysers, group, lastTfIndices, plan, 0, &in);
        REQUIRE(snapshot != nullptr);
        REQUIRE(snapshot->spl.has_value());
        // The VALUES, not just the presence: a default-constructed block would
        // satisfy has_value() and every one of these would fail.
        CHECK(snapshot->spl->blockIndex == 89);
        CHECK(snapshot->spl->blockSamples == 48000);
        CHECK(snapshot->spl->sampleRate == kFs);
        CHECK(snapshot->spl->droppedSamples == 12000);
        CHECK(rta::meter::hasFlag(snapshot->spl->flags, rta::meter::BlockFlag::Gap));
        REQUIRE(snapshot->spl->metrics.size() == 2);
        CHECK(snapshot->spl->metrics[0].id == "LAeq_1s");
        CHECK_THAT(static_cast<double>(snapshot->spl->metrics[0].valueDb),
                   WithinAbs(85.0, 1e-4));
        CHECK(snapshot->spl->refusedMetrics == 0);
        // The base snapshot survived the copy the fold makes.
        CHECK(snapshot->sampleRate == 48000.0);
        CHECK_FALSE(snapshot->bands.empty());
    }
}

// --- the ROUTED branch, which is the normal live-show configuration ------

TEST_CASE("round 2: a ROUTED session publishes spl too -- the branch a live show uses",
          "[splpublish]") {
    // ROUND-2 VERIFIER GAP. `buildPublishedSnapshot` has TWO branches that
    // attach the SPL block: the unrouted one (`plan.routes.empty()`) and the
    // routed one below it. Every earlier case in this file used an EMPTY
    // RoutingPlan, so deleting the routed branch's
    // `snapshot->spl = std::move(splView)` left the whole suite green -- and a
    // routed session, which is what a dual-FFT measurement rig actually runs
    // during a show, would have published no SPL at all. Silently: the block
    // would simply be absent, which every consumer is required to tolerate.
    //
    // Two routes on one reference, so the routed branch really runs
    // `publishAverageGroup` and `mergeRoutePositions` rather than falling into
    // some degenerate shape that happens to skip the line under test.
    std::vector<std::unique_ptr<rta::measure::Analyser>> analysers;
    for (int i = 0; i < 2; ++i) {
        analysers.push_back(std::make_unique<rta::measure::Analyser>(fastAnalyserConfig()));
    }
    rta::measure::AverageGroup group;
    std::vector<int> lastTfIndices;

    rta::measure::RoutingPlan plan;
    plan.routes.push_back(rta::measure::TransferRoute{1, 0, 0});
    plan.routes.push_back(rta::measure::TransferRoute{2, 0, 1});
    plan.distinctReferences = {0};
    REQUIRE_FALSE(plan.routes.empty());

    // Real paired frames, so the routed branch has something to publish.
    std::vector<float> reference(64), measurement(64);
    for (std::size_t n = 0; n < 64; ++n) {
        const double t = static_cast<double>(n);
        reference[n] = static_cast<float>(
            std::sin(2.0 * 3.14159265358979323846 * 6.0 * t / 64.0));
        measurement[n] = reference[n] * 0.7f;
    }
    for (int frame = 0; frame < 30; ++frame) {
        for (auto& a : analysers) a->pushPair(reference, measurement);
    }

    const SplConfig config = configWithOneMetric();
    std::vector<Block> window;
    for (std::uint64_t i = 0; i < 90; ++i) window.push_back(blockAtLevel(i, 48000, 85.0));
    window.back().flags |= static_cast<std::uint32_t>(rta::meter::BlockFlag::Gap);
    window.back().droppedSamples = 12000;

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = window.back();
    in.window = window;

    const auto snapshot =
        buildPublishedSnapshot(analysers, group, lastTfIndices, plan, 0, &in);
    REQUIRE(snapshot != nullptr);
    // The routed branch really ran: it is the one that fills `positions`.
    REQUIRE(snapshot->positions.size() == plan.routes.size());

    // THE LINE UNDER TEST.
    REQUIRE(snapshot->spl.has_value());
    // The block's own VALUES, not just its presence -- the same set the
    // unrouted case asserts, because the two branches must agree.
    CHECK(snapshot->spl->blockIndex == 89);
    CHECK(snapshot->spl->blockSamples == 48000);
    CHECK(snapshot->spl->sampleRate == kFs);
    CHECK(snapshot->spl->droppedSamples == 12000);
    CHECK(rta::meter::hasFlag(snapshot->spl->flags, rta::meter::BlockFlag::Gap));
    REQUIRE(snapshot->spl->metrics.size() == 2);
    CHECK(snapshot->spl->metrics[0].id == "LAeq_1s");
    CHECK_THAT(static_cast<double>(snapshot->spl->metrics[0].valueDb), WithinAbs(85.0, 1e-4));
    CHECK(snapshot->spl->refusedMetrics == 0);

    SECTION("and a routed session with nothing logging still has no spl block") {
        const auto none =
            buildPublishedSnapshot(analysers, group, lastTfIndices, plan, 0, nullptr);
        REQUIRE(none != nullptr);
        CHECK_FALSE(none->spl.has_value());
        // Still a real routed snapshot: absence of SPL is not absence of a
        // publish, on this branch either.
        CHECK(none->positions.size() == plan.routes.size());
    }
}

// --- the window-array bound is a GATE, not a convention -----------------

TEST_CASE("round 2: every metric the config can express gets a PRESENT reading",
          "[splpublish]") {
    // The property: a session started with as many metrics as the config can
    // express must publish a PRESENT, non-floored value for EVERY one of them.
    // That is what makes `buildSplBlockView`'s no-fallback rule safe to ship --
    // absence is the right answer for an unfilled row and the wrong answer for
    // a configured metric, so something has to assert that no configured
    // metric ends up in the first category.
    //
    // WHAT THIS CASE DOES *NOT* DO, stated because an earlier draft of this
    // comment claimed it did: it does **not** detect drift between
    // `AnalysisThread::kMaxSplMetricWindows` and `SplConfig::kMaxMetrics`. It
    // cannot -- `AnalysisThread.h` includes JUCE, so this OFF-build file
    // cannot name that constant, and sizing the buffer below from
    // `kMaxMetrics` means a production array stuck at a literal 16 while
    // `kMaxMetrics` rose to 24 leaves this case green. MEASURED: under exactly
    // that mutation this reads "metrics = 24, windows filled = 24" and passes.
    //
    // Drift is caught by the other two guards instead, and both were shown red
    // under that mutation: the `static_assert` at the top of
    // `AnalysisThread.cpp`, in the TU that declares the array (compile time),
    // and `app/tests_juce/test_spl_drain.cpp`'s D5, which sizes its buffer from
    // `kMaxSplMetricWindows` itself and read
    // "kMaxSplMetricWindows = 16, kMaxMetrics = 24, metrics = 24, filled = 16".
    SplConfig config;
    config.blockSeconds = 0.1;
    for (std::size_t i = 0; i < SplConfig::kMaxMetrics; ++i) {
        config.metrics.push_back(rta::measure::SplMetricSpec{
            "L" + std::to_string(i), rta::dsp::WeightingType::A,
            rta::meter::TimeWeighting::Fast, 4});
    }
    REQUIRE(config.refusedMetricCount() == 0);

    rta::measure::SplSession session;
    const int channels[] = {0};
    session.start(config, kFs, channels);
    REQUIRE(session.config()->metrics.size() == SplConfig::kMaxMetrics);

    std::vector<float> hop(4800, 0.2f);
    for (int i = 0; i < 6; ++i) session.feedHop(0, hop);

    // Sized from `kMaxMetrics`, which is the cap the CONFIG enforces. The
    // production array is sized from `kMaxSplMetricWindows`, and keeping those
    // two equal is the static_assert's job and D5's, not this one's (see the
    // comment above).
    std::array<std::span<const Block>, SplConfig::kMaxMetrics> windows{};
    const std::size_t filled = session.fillMetricWindows(0, windows);
    INFO("metrics = " << SplConfig::kMaxMetrics << ", windows filled = " << filled);
    REQUIRE(filled == SplConfig::kMaxMetrics);

    SplPublishInput in;
    in.config = session.config();
    in.sampleRate = session.sampleRate();
    in.latestBlock = session.latestBlock(0);
    in.window = session.window(0);
    in.refusedMetrics = session.refusedMetrics();
    in.metricWindows = std::span<const std::span<const Block>>(windows).first(filled);

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    REQUIRE(view->metrics.size() == SplConfig::kMaxMetrics);
    CHECK(view->refusedMetrics == 0);
    const auto floorDb = static_cast<float>(rta::measure::kLevelFloorDb);
    for (std::size_t i = 0; i < view->metrics.size(); ++i) {
        INFO("metric " << i << " (" << view->metrics[i].id << ") = "
                       << view->metrics[i].valueDb << " dB");
        // PRESENT: not the floor, and its window reported as full.
        CHECK(view->metrics[i].valueDb != floorDb);
        CHECK(view->metrics[i].leqBufferFill > 0.0f);
    }
}
