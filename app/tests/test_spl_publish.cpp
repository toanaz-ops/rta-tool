// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §9, §11): SPL reaches the published
// Snapshot -- ONE optional block, no wall clock, absence when nothing logs.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CodeLines.h"

#include "measure/AnalysisPublish.h"
#include "measure/Snapshot.h"
#include "measure/SplConfig.h"
#include "measure/SplMeter.h"

#include "rta/meter/Block.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
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

// --- C3: the alarm state is server-computed -----------------------------

TEST_CASE("C3 SplAlarmReading carries state and headroom, and nothing downstream re-derives it",
          "[splpublish]") {
    // Record §9: the alarm `state` is SERVER-computed. A client that compared
    // valueDb to limitDb itself would disagree with the log the moment the
    // window or the exclusion membership differed, and the log is the
    // evidence.
    rta::measure::SplAlarmReading reading;
    CHECK(reading.state == rta::measure::SplAlarmState::Clear);
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

// --- the fold itself: one optional block, the metrics from combineBlocks -

TEST_CASE("the published metric is combineBlocks over that metric's own window", "[splpublish]") {
    const SplConfig config = configWithOneMetric();
    // 90 blocks: the 1 s metric sees the last one, the 60 s metric sees the
    // last 60 -- so a window LONGER than the buffer is what bufferFill is for.
    std::vector<Block> window;
    for (std::uint64_t i = 0; i < 90; ++i) {
        window.push_back(blockAtLevel(i, 48000, (i % 2 == 0) ? 80.0 : 90.0));
    }

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = window.back();
    in.window = window;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    REQUIRE(view->metrics.size() == 2);

    // The 1 s metric is block 89's own level: 90.0 dB (odd index).
    CHECK(view->metrics[0].id == "LAeq_1s");
    CHECK_THAT(static_cast<double>(view->metrics[0].valueDb), WithinAbs(90.0, 1e-5));
    CHECK_THAT(static_cast<double>(view->metrics[0].leqBufferFill), WithinAbs(1.0, 1e-6));

    // The 60 s metric is 30 blocks at 80 and 30 at 90 -- equal counts, equal
    // lengths, so the mean power is 0.5*10^8 + 0.5*10^9 = 5.5*10^8 and the
    // answer is 80 + 10*log10(5.5), which is W0-A A4's own identity.
    const double expected = 80.0 + 10.0 * std::log10(5.5);
    INFO("expected = " << expected << ", got = " << view->metrics[1].valueDb);
    CHECK(view->metrics[1].id == "LAeq_60s");
    CHECK_THAT(static_cast<double>(view->metrics[1].valueDb), WithinAbs(expected, 1e-5));
    CHECK_THAT(static_cast<double>(view->metrics[1].leqBufferFill), WithinAbs(1.0, 1e-6));

    CHECK(view->blockIndex == 89);
    CHECK(view->blockSamples == 48000);
    CHECK(view->calibrated == false);
    CHECK_THAT(view->referenceOffsetDb, WithinAbs(0.0, 1e-15));
}

TEST_CASE("a window longer than the buffer reports bufferFill and no value", "[splpublish]") {
    const SplConfig config = configWithOneMetric();
    // Only 20 blocks: the 60 s metric's window is a third full.
    std::vector<Block> window;
    for (std::uint64_t i = 0; i < 20; ++i) window.push_back(blockAtLevel(i, 48000, 85.0));

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = window.back();
    in.window = window;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    REQUIRE(view->metrics.size() == 2);
    CHECK_THAT(static_cast<double>(view->metrics[1].leqBufferFill), WithinAbs(1.0 / 3.0, 1e-6));
    // The value is still published -- a partial window has a real Leq over
    // what it holds. What must not happen is publishing it without saying the
    // window is not full, which is what leqBufferFill is for (record §9).
    CHECK_THAT(static_cast<double>(view->metrics[1].valueDb), WithinAbs(85.0, 1e-5));
}

TEST_CASE("each metric is averaged over ITS OWN window, not a shared one", "[splpublish]") {
    // The publish half of the one-chain-per-weighting rule. `SplMeter` runs
    // ONE weighting per instance (W0-B), so an A-weighted metric and a
    // C-weighted one come out of different chains and `metricWindows` is what
    // says which. Without it, `SplConfig::metrics` would carry a `weighting`
    // field the publish path ignored.
    SplConfig config;
    config.blockSeconds = 1.0;
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 2});
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LCeq", rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Fast, 2});

    // Two windows 20 dB apart, which no tolerance question can confuse.
    std::vector<Block> aWindow{blockAtLevel(0, 48000, 70.0), blockAtLevel(1, 48000, 70.0)};
    std::vector<Block> cWindow{blockAtLevel(0, 48000, 90.0), blockAtLevel(1, 48000, 90.0)};
    const std::array<std::span<const Block>, 2> metricWindows{aWindow, cWindow};

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = aWindow.back();
    in.window = aWindow;  // the shared fallback, deliberately the WRONG one for metric 1
    in.metricWindows = metricWindows;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    REQUIRE(view->metrics.size() == 2);
    CHECK_THAT(static_cast<double>(view->metrics[0].valueDb), WithinAbs(70.0, 1e-4));
    // 90, not 70: the fallback `window` above is A-weighted and metric 1 must
    // not read it. **Made red** by dropping the metricWindows lookup.
    CHECK_THAT(static_cast<double>(view->metrics[1].valueDb), WithinAbs(90.0, 1e-4));

    SECTION("a metric whose weighting has no chain is ABSENT, not substituted") {
        // An empty span is how SplSession says "this session never built that
        // chain". combineBlocks over nothing has no Leq, so the reading floors
        // rather than quietly carrying another weighting's number.
        const std::array<std::span<const Block>, 2> withHole{aWindow, {}};
        SplPublishInput holed = in;
        holed.metricWindows = withHole;
        const auto holedView = rta::measure::buildSplBlockView(holed);
        REQUIRE(holedView.has_value());
        REQUIRE(holedView->metrics.size() == 2);
        CHECK_THAT(static_cast<double>(holedView->metrics[0].valueDb), WithinAbs(70.0, 1e-4));
        CHECK(holedView->metrics[1].valueDb
              == static_cast<float>(rta::measure::kLevelFloorDb));
        CHECK(holedView->metrics[1].leqBufferFill == 0.0f);
    }
}

TEST_CASE("Wave 0 publishes no dose and no Ln, and says so by absence", "[splpublish]") {
    // The dose accumulators are W1-D and the histogram is W1-A. Wave 0 must
    // not publish a zero that reads as "no exposure" -- so `lnDb` is a row of
    // absences and `dosePercent` is absent, not 0.0 %.
    const SplConfig config = configWithOneMetric();
    std::vector<Block> window{blockAtLevel(0, 48000, 85.0)};
    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = window.back();
    in.window = window;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    for (const auto& ln : view->lnDb) CHECK_FALSE(ln.has_value());
    for (const auto& d : view->dosePercent) CHECK_FALSE(d.has_value());
    for (const auto& d : view->doseProjected) CHECK_FALSE(d.has_value());
    CHECK(view->alarms.empty());
}
