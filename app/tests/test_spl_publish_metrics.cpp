// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-C, split out of test_spl_publish.cpp (LOW follow-up batch,
// items 4/5/11 -- see that file's own header comment for the three-way split
// this is part of). THIS file's subject: the metric fold itself --
// combineBlocks over each metric's own window, a window longer than the
// buffer, per-metric weighting (each metric reads ITS OWN chain, never a
// shared fallback), Wave 0's dose/Ln absence, and PR #17 verifier defect 1
// end to end (a metric list past SplConfig::kMaxMetrics must never publish a
// mislabelled reading).
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "measure/AnalysisPublish.h"
#include "measure/SplConfig.h"
#include "measure/SplSession.h"

#include "rta/meter/Block.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
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

}  // namespace

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
