// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §9, §11): SPL reaches the published
// Snapshot -- ONE optional block, no wall clock, absence when nothing logs.
//
// THE CORE CONTRACT ONLY: absence (C1), no wall clock (C2), the two
// station-4 mirror fields (logDroppedBlocks/logWriteFailed), the alarm-state
// contract (C3) and blockIndex monotonicity (C4). LOW follow-up batch, items
// 4/5/11: split out of a single 804-line file (already past the 400-line
// hard cap) into THREE, along subject seams --
//   test_spl_publish.cpp          this file: the core contract above.
//   test_spl_publish_metrics.cpp  combineBlocks/window/per-metric-window/
//                                 defect-1 (the metric-cap mislabelling bug).
//   test_spl_publish_snapshot.cpp buildPublishedSnapshot end to end, through
//                                 a real Analyser/RoutingPlan/SplSession.
// No TEST_CASE dropped or renamed -- see the PR body's before/after
// `ctest -N` proof.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CodeLines.h"

#include "measure/AnalysisPublish.h"
#include "measure/Snapshot.h"
#include "measure/SplConfig.h"
#include "measure/SplMeter.h"

#include "rta/meter/Block.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
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

std::filesystem::path appSrc() { return std::filesystem::path(RTA_REPO_ROOT) / "app" / "src"; }

}  // namespace

// --- C1: absence is absence ---------------------------------------------

TEST_CASE("C1 with no logging session running, Snapshot::spl is nullopt", "[splpublish]") {
    // memory/a-placeholder-for-an-absent-result-erases-its-state.md: a
    // default-constructed block would read as a measurement of silence and
    // would rewrite "nothing is logging" as "0 dB on an uncalibrated meter".
    // `transfer` (Snapshot.h) already follows the same rule.
    const rta::measure::Snapshot fresh;
    CHECK_FALSE(fresh.spl.has_value());

    SplPublishInput nothingLogging;  // config == nullptr
    CHECK(nothingLogging.config == nullptr);
    CHECK_FALSE(rta::measure::buildSplBlockView(nothingLogging).has_value());

    // A config but no completed block yet is ALSO absence: a block is what a
    // reading is, and half of one is not a smaller reading.
    const SplConfig config = configWithOneMetric();
    SplPublishInput noBlockYet;
    noBlockYet.config = &config;
    noBlockYet.sampleRate = kFs;
    CHECK_FALSE(rta::measure::buildSplBlockView(noBlockYet).has_value());
}

// --- C2: no wall clock in Snapshot --------------------------------------

TEST_CASE("C2 Snapshot carries no wall clock, and the SPL block did not add one",
          "[splpublish]") {
    // SPL-R2. Snapshot.h's own comment: "two snapshots built from the same
    // input must compare equal field-for-field, which is what makes
    // makeSyntheticSnapshot deterministic and rta-view.png reviewable as a
    // byte-for-byte diff". Eight rtatool_snapshot PNGs depend on it.
    //
    // codeText() strips comments and empties literals, so the several
    // "multi-time-window" and "NO timestamp" phrases in the prose cannot
    // match -- a plain word-grep over this file reads five hits on the
    // untouched tree and proves nothing.
    const std::string code = rta::test::codeText(appSrc() / "measure" / "Snapshot.h");
    for (const char* token : {"time", "chrono", "unix", "iso", "clock", "epoch"}) {
        INFO("token: " << token);
        CHECK(code.find(token) == std::string::npos);
    }

    // And the equality property itself, over a snapshot that HAS an SPL block:
    // two builds from the same input agree field for field.
    const SplConfig config = configWithOneMetric();
    std::vector<Block> window;
    for (std::uint64_t i = 0; i < 90; ++i) window.push_back(blockAtLevel(i, 48000, 85.0));

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = window.back();
    in.window = window;

    const auto a = rta::measure::buildSplBlockView(in);
    const auto b = rta::measure::buildSplBlockView(in);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->blockIndex == b->blockIndex);
    CHECK(a->blockSamples == b->blockSamples);
    CHECK(a->sampleRate == b->sampleRate);
    CHECK(a->flags == b->flags);
    REQUIRE(a->metrics.size() == b->metrics.size());
    for (std::size_t i = 0; i < a->metrics.size(); ++i) {
        CHECK(a->metrics[i].id == b->metrics[i].id);
        CHECK(a->metrics[i].valueDb == b->metrics[i].valueDb);  // bitwise
        CHECK(a->metrics[i].leqBufferFill == b->metrics[i].leqBufferFill);
    }
}

// --- station-4 fix round (PR #31, item 3): logDroppedBlocks is copied, not
// dropped, on the way to the published view ---------------------------------

TEST_CASE("logDroppedBlocks is copied straight into the published view",
         "[splpublish]") {
    // Deleting AnalysisPublish.cpp's `view.logDroppedBlocks =
    // static_cast<std::uint32_t>(input.logDroppedBlocks);` left this whole
    // OFF suite green: nothing in it read the field at all. Independent of
    // channelState/config (that line's own comment: "a queue can overflow
    // even on a channel whose SplChannelState allocation succeeded"), so a
    // minimal publish with no metrics and no channelState isolates it.
    const SplConfig config;
    std::vector<Block> window;
    window.push_back(blockAtLevel(0, 48000, 85.0));

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = window.back();
    in.window = window;
    in.logDroppedBlocks = 7;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    CHECK(view->logDroppedBlocks == 7);
}

// --- station-4 fix round (PR #31, finding 6): logWriteFailed reaches the
// published view, the same way logDroppedBlocks just above does -----------

TEST_CASE("logWriteFailed is copied straight into the published view",
         "[splpublish]") {
    const SplConfig config;
    std::vector<Block> window;
    window.push_back(blockAtLevel(0, 48000, 85.0));

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = window.back();
    in.window = window;
    in.logWriteFailed = true;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    CHECK(view->logWriteFailed == true);
}

// --- C3: the alarm state is server-computed -----------------------------

TEST_CASE("C3 SplAlarmReading carries state and headroom, and nothing downstream re-derives it",
          "[splpublish]") {
    // Record §9: the alarm `state` is SERVER-computed. A client that compared
    // valueDb to limitDb itself would disagree with the log the moment the
    // window or the exclusion membership differed, and the log is the
    // evidence.
    rta::measure::SplAlarmReading reading;
    // Filling, not Clear (record §15 A6, round 4): a default-constructed
    // reading has never been compared against anything.
    CHECK(reading.state == rta::measure::SplAlarmState::Filling);
    CHECK_FALSE(reading.headroomDb.has_value());

    // Structural: no file under app/src compares a published valueDb against
    // a limitDb. Task G repeats this grep over the viewer's JS.
    int offenders = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(appSrc())) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        const std::string code = rta::test::codeText(entry.path());
        if (code.find("valuedb > ") != std::string::npos
            || code.find("valuedb >= ") != std::string::npos
            || code.find("valuedb >limitdb") != std::string::npos) {
            INFO("re-derives an alarm: " << entry.path().string());
            ++offenders;
        }
    }
    CHECK(offenders == 0);
}

// --- C4: blockIndex is monotonic across a publish -----------------------

TEST_CASE("C4 spl->blockIndex is monotonic and advances by the blocks completed",
          "[splpublish]") {
    // Independent of kMinPublishIntervalMs's jitter: the clock is a sample
    // count (record §2), so a publish that happens to straddle two blocks
    // sees the index advance by two, and one that sees none republishes the
    // same index rather than inventing a fresh one.
    SplConfig config = configWithOneMetric();
    // 0.1 s blocks so ten publishes of two to four 4800-sample hops each
    // actually straddle block boundaries -- at the 1 s default a hop is a
    // tenth of a block and nine of the ten publishes would see nothing, which
    // is a fixture too well-behaved to test what C4 is about
    // (memory/a-fixture-can-be-too-well-behaved-to-fail.md).
    config.blockSeconds = 0.1;
    rta::measure::SplMeter meter(config, rta::dsp::WeightingType::Z, kFs);
    REQUIRE(meter.blockSamples() == 4800);

    std::vector<Block> ring;
    std::vector<float> hop(4800, 0.2f);
    std::uint64_t previousIndex = 0;
    bool seenAny = false;
    int publishes = 0;

    // Ten publishes, each consuming a deliberately non-block-aligned number
    // of hops, so the number of blocks completed per publish varies.
    for (int publish = 0; publish < 10; ++publish) {
        const int hopsThisPublish = 2 + (publish % 3);  // 2, 3, 4, 2, 3, 4, ...
        std::size_t completedThisPublish = 0;
        for (int h = 0; h < hopsThisPublish; ++h) {
            meter.push(hop);
            while (auto b = meter.poll()) {
                ring.push_back(*b);
                ++completedThisPublish;
            }
        }
        if (ring.empty()) continue;

        SplPublishInput in;
        in.config = &config;
        in.sampleRate = kFs;
        in.latestBlock = ring.back();
        in.window = ring;
        const auto view = rta::measure::buildSplBlockView(in);
        REQUIRE(view.has_value());
        ++publishes;

        INFO("publish " << publish << ": blockIndex = " << view->blockIndex
                        << ", completed this publish = " << completedThisPublish);
        if (seenAny) {
            CHECK(view->blockIndex >= previousIndex);
            CHECK(view->blockIndex - previousIndex == completedThisPublish);
        }
        previousIndex = view->blockIndex;
        seenAny = true;
    }
    CHECK(publishes == 10);
}
