// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §9, §11): SPL reaches the published
// Snapshot -- ONE optional block, no wall clock, absence when nothing logs.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CodeLines.h"

#include "measure/AnalysisPublish.h"
#include "measure/Analyser.h"
#include "measure/AverageGroup.h"
#include "measure/RoutingPlan.h"
#include "measure/Snapshot.h"
#include "measure/SplSession.h"
#include "measure/SplConfig.h"
#include "measure/SplMeter.h"

#include "rta/meter/Block.h"

#include <array>
#include <cmath>
#include <memory>
#include <string>
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

// --- defect 1, end to end: 17 metrics may not mislabel one ---------------

namespace {

/// `count` metrics, the LAST one C-weighted. At `count > kMaxMetrics` the
/// verifier measured the C metric publishing the A chain's number -- a
/// C label over an A reading, 18.8 dB out.
SplConfig manyMetrics(std::size_t count) {
    SplConfig config;
    config.blockSeconds = 0.1;
    for (std::size_t i = 0; i < count; ++i) {
        const bool last = (i + 1 == count);
        config.metrics.push_back(rta::measure::SplMetricSpec{
            last ? "LCeq_last" : ("LAeq_" + std::to_string(i)),
            last ? rta::dsp::WeightingType::C : rta::dsp::WeightingType::A,
            rta::meter::TimeWeighting::Fast, 4});
    }
    return config;
}

std::vector<float> lowSine(std::size_t count) {
    std::vector<float> x(count);
    for (std::size_t n = 0; n < count; ++n) {
        const double t = static_cast<double>(n) / kFs;
        x[n] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * 100.0 * t));
    }
    return x;
}

/// The real Wave-0 publish path for `metricCount` metrics: a live SplSession
/// fed a 100 Hz sine, its per-metric windows filled exactly as
/// AnalysisThread::fillSplPublishInput does, through buildSplBlockView.
std::optional<rta::measure::SplBlockView> publishThroughSession(std::size_t metricCount,
                                                                 std::size_t* refusedOut) {
    static rta::measure::SplSession session;
    session.stop();
    const int channels[] = {0};
    session.start(manyMetrics(metricCount), kFs, channels);

    const auto hop = lowSine(4800);
    for (int i = 0; i < 6; ++i) session.feedHop(0, hop);

    SplPublishInput in;
    in.config = session.config();
    in.sampleRate = session.sampleRate();
    in.latestBlock = session.latestBlock(0);
    in.window = session.window(0);
    in.refusedMetrics = session.refusedMetrics();
    if (refusedOut != nullptr) *refusedOut = session.refusedMetrics();

    static std::array<std::span<const Block>, SplConfig::kMaxMetrics> windows{};
    windows = {};
    const std::size_t filled = session.fillMetricWindows(0, windows);
    in.metricWindows = std::span<const std::span<const Block>>(windows).first(filled);
    return rta::measure::buildSplBlockView(in);
}

}  // namespace

TEST_CASE("defect 1: a metric list past the cap never publishes a mislabelled reading",
          "[splpublish]") {
    // The C-weighted metric's OWN number, measured at the cap where it is
    // served, so the "wrong" value has something to be wrong against.
    std::size_t refusedAtCap = 99;
    const auto atCap = publishThroughSession(SplConfig::kMaxMetrics, &refusedAtCap);
    REQUIRE(atCap.has_value());
    REQUIRE(atCap->metrics.size() == SplConfig::kMaxMetrics);
    CHECK(refusedAtCap == 0);
    CHECK(atCap->refusedMetrics == 0);

    const double aValue = static_cast<double>(atCap->metrics.front().valueDb);
    const double cValue = static_cast<double>(atCap->metrics.back().valueDb);
    INFO("at the cap: LAeq_0 = " << aValue << " dB, LCeq_last = " << cValue << " dB");
    INFO("A-to-C gap  = " << (aValue - cValue) << " dB");
    CHECK(atCap->metrics.back().id == "LCeq_last");
    // The two are far apart -- a 100 Hz sine through A and through C -- so a
    // fallback to the A chain cannot hide inside a tolerance.
    CHECK(aValue < cValue - 10.0);

    SECTION("one past the cap: the extra metric is DROPPED and COUNTED, not mislabelled") {
        std::size_t refused = 99;
        const auto over = publishThroughSession(SplConfig::kMaxMetrics + 1, &refused);
        REQUIRE(over.has_value());
        CHECK(refused == 1);
        CHECK(over->refusedMetrics == 1);
        // THE MISLABELLING CHECK COMES FIRST, deliberately: a regression must
        // report the dB error, not a count. Under the original defect this
        // loop is what reads "LCeq_last = -28.1735" against its own
        // -9.33053 -- an 18.8 dB lie under a C label -- whereas a size
        // assertion placed first would only have said `17 == 16` and left the
        // next reader to work out why that mattered.
        for (const auto& m : over->metrics) {
            INFO("published metric: " << m.id << " = " << m.valueDb << " dB");
            if (m.id == "LCeq_last") {
                INFO("a C-labelled reading survived the cap; it must NOT be the A number "
                     << aValue << " dB. Its own is " << cValue << " dB.");
                CHECK_THAT(static_cast<double>(m.valueDb), WithinAbs(cValue, 1e-4));
            } else {
                // Every A-weighted survivor reads the A number.
                CHECK_THAT(static_cast<double>(m.valueDb), WithinAbs(aValue, 1e-4));
            }
        }
        // Sixteen published, and the C-weighted one is NOT among them, because
        // it was the seventeenth. So no published reading carries a C label at
        // all, and the loop above has nothing to catch -- which is the point.
        CHECK(over->metrics.size() == SplConfig::kMaxMetrics);
        for (const auto& m : over->metrics) CHECK(m.id != "LCeq_last");
    }
}

TEST_CASE("defect 1: a short metricWindows row is ABSENCE, never the shared window",
          "[splpublish]") {
    // The third layer, tested on its own so it holds even if the cap moves.
    // `metricWindows` non-empty but shorter than `metrics` used to fall back
    // to `window` for the uncovered rows -- which is exactly how a C-weighted
    // metric came to read the A chain.
    SplConfig config;
    config.blockSeconds = 1.0;
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 2});
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "LCeq", rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Fast, 2});

    std::vector<Block> aWindow{blockAtLevel(0, 48000, 70.0), blockAtLevel(1, 48000, 70.0)};
    // ONE row supplied for TWO metrics.
    const std::array<std::span<const Block>, 1> shortRows{aWindow};

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = kFs;
    in.latestBlock = aWindow.back();
    in.window = aWindow;  // the A window, deliberately WRONG for metric 1
    in.metricWindows = shortRows;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    REQUIRE(view->metrics.size() == 2);
    CHECK_THAT(static_cast<double>(view->metrics[0].valueDb), WithinAbs(70.0, 1e-4));
    // FLOORED, not 70.0. The caller said nothing about metric 1, so the
    // publish says nothing about metric 1.
    CHECK(view->metrics[1].valueDb == static_cast<float>(rta::measure::kLevelFloorDb));
    CHECK(view->metrics[1].leqBufferFill == 0.0f);
}

// --- defect 2: Snapshot::spl through the REAL publish path ---------------

namespace {

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
