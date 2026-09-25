// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E1 (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md's
// amendment "W2-E -- the wiring nobody was assigned"; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §4, §5, §6, §7, §15 A6). Fix round
// 2026-09-25: an independent verifier refuted the first version of this
// file -- every consumer read the channel's FIRST configured chain
// regardless of which metric it was actually about. See SplChannelState.h.
#include "measure/SplChannelState.h"

#include "AllocationProbe.h"

#include "measure/SplSession.h"

#include "rta/dsp/Weighting.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <optional>
#include <span>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::ChainBlockAtClose;
using rta::measure::SplAlarmSpec;
using rta::measure::SplAlarmState;
using rta::measure::SplBlockView;
using rta::measure::SplChannelState;
using rta::measure::SplConfig;
using rta::measure::SplMetricSpec;
using rta::measure::SplSession;
using rta::meter::Block;
using rta::meter::DoseSettings;

namespace {

constexpr double kTwoPi = 6.283185307179586;

Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    // A constant-level fixture: the block's own Fast max-hold reads the same
    // level as its Leq (both quantities agree for a steady-state signal),
    // so Ln fixtures built on this helper feed a real value into the
    // histogram rather than the default kLevelFloorDb.
    b.maxFastDb = static_cast<float>(levelDb);
    return b;
}

/// Feeds ONE chain's block (default weighting A, the common case in these
/// fixtures), growing `window` first and building the single-entry
/// `ChainBlockAtClose` array `onBlockClosed` now takes.
void feedOneChain(SplChannelState& state, std::vector<Block>& window, const Block& block,
                  rta::dsp::WeightingType weighting = rta::dsp::WeightingType::A) {
    window.push_back(block);
    ChainBlockAtClose atClose;
    atClose.weighting = weighting;
    atClose.block = block;
    atClose.windowThroughThisBlock = window;
    state.onBlockClosed(std::span<const ChainBlockAtClose>(&atClose, 1));
}

/// Drains a real `SplSession` after `feedHop` and folds every chain's own
/// newly-closed block(s) into `state`, mirroring `AnalysisThread::feedSpl`'s
/// own loop (including `rta::measure::windowAtClose`) so these fixtures
/// exercise the SAME reconstruction production code uses, over REAL,
/// weighting-filtered audio.
///
/// PR #29 round-3 fix pass step 7: this helper diverged from `feedSpl` the
/// moment step 1 added `feedLnTicks` there -- `feedSpl` calls it once per
/// hop, unconditionally, AFTER the closed-block loop, whether or not a
/// block closed on this hop. Updated here to keep mirroring production
/// (rather than leaving `runOrderProbe`'s own `Probe::ln50` silently
/// starved of ticks).
void feedAndDrive(SplSession& session, SplChannelState& state, int channel,
                  std::span<const float> hop) {
    session.feedHop(channel, hop);
    const auto weightings = session.weightings();
    std::vector<std::span<const Block>> closedByChain(weightings.size());
    std::vector<std::span<const Block>> fullWindowByChain(weightings.size());
    std::size_t closedCount = 0;
    for (std::size_t c = 0; c < weightings.size(); ++c) {
        closedByChain[c] = session.newlyClosedBlocks(channel, weightings[c]);
        fullWindowByChain[c] = session.window(channel, weightings[c]);
        closedCount = closedByChain[c].size();
    }
    std::vector<ChainBlockAtClose> atClose(weightings.size());
    for (std::size_t i = 0; i < closedCount; ++i) {
        for (std::size_t c = 0; c < weightings.size(); ++c) {
            atClose[c].weighting = weightings[c];
            atClose[c].block = closedByChain[c][i];
            atClose[c].windowThroughThisBlock =
                rta::measure::windowAtClose(closedByChain[c], fullWindowByChain[c], i);
        }
        state.onBlockClosed(atClose);
    }
    state.feedLnTicks(session.newlyTickedLnLevelsDb(channel, rta::dsp::WeightingType::A));
}

struct Probe {
    float alarmValueDb = 0.0f;
    std::optional<double> dose0;
    std::optional<double> ln50;
};

/// Reproduces the verifier's own probe: metrics [LCeq, LAeq] (or the reverse,
/// via `aFirst`), an alarm on LAeq, a real low-frequency tone fed through the
/// REAL weighting filters via a real `SplSession`.
Probe runOrderProbe(bool aFirst, double freqHz, double fs, std::uint32_t blockSamples) {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    const SplMetricSpec aMetric{"LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1};
    const SplMetricSpec cMetric{"LCeq", rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Fast, 1};
    config.metrics = aFirst ? std::vector<SplMetricSpec>{aMetric, cMetric}
                           : std::vector<SplMetricSpec>{cMetric, aMetric};
    SplAlarmSpec spec;
    spec.metricId = "LAeq";
    spec.limitDb = 1000.0;  // never fires; only valueDb is read
    spec.windowBlocks = 1;
    config.alarms = {spec};

    SplSession session;
    const int channels[] = {0};
    session.start(config, fs, channels);
    SplChannelState state(config, fs);

    std::vector<float> hop(blockSamples);
    for (int block = 0; block < 3; ++block) {  // let the biquad cascade settle
        for (std::uint32_t n = 0; n < blockSamples; ++n) {
            const double t = static_cast<double>(block) + static_cast<double>(n) / fs;
            hop[n] = static_cast<float>(std::sin(kTwoPi * freqHz * t));
        }
        feedAndDrive(session, state, 0, hop);
    }

    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.alarms.size() == 1);
    REQUIRE(view.dosePercent[0].has_value());

    Probe p;
    p.alarmValueDb = view.alarms[0].valueDb;
    p.dose0 = view.dosePercent[0];
    p.ln50 = view.lnDb[3];  // config.lnPercents default {1,5,10,50,90,95}: index 3 = 50.0
    return p;
}

}  // namespace

// --- 1: every consumer reads the chain its own definition names -----------

TEST_CASE("1a a low-frequency tone: alarm follows A, matching the shipped filter's own analytic gap",
         "[spl_channel_state]") {
    constexpr double kFs = 48000.0;
    constexpr double kFreqHz = 20.0;  // A rolls off sharply here; C stays near flat
    constexpr std::uint32_t kBlockSamples = 48000;  // blockSeconds = 1.0

    const auto withCFirst = runOrderProbe(/*aFirst=*/false, kFreqHz, kFs, kBlockSamples);
    const auto withAFirst = runOrderProbe(/*aFirst=*/true, kFreqHz, kFs, kBlockSamples);

    // ORDER-INDEPENDENCE: config.metrics listing LAeq first or LCeq first
    // must publish the identical number. Mutant "feed the first chain
    // again" flips this: with LCeq first, the alarm would read LCeq's own
    // (much louder, unattenuated) level instead.
    CHECK_THAT(withCFirst.alarmValueDb, WithinAbs(withAFirst.alarmValueDb, 1e-4));

    // AND it is actually the A-WEIGHTED level, not merely order-stable:
    // derive the expected level from the shipped weighting filter's own
    // ANALYTIC magnitude at 20 Hz -- computed here, never typed. A
    // unit-amplitude sine reads 0 dBFS sine-referenced; mean-square
    // referenced (record §13 Q1's own seam) that is kFullScaleSineOffsetDb
    // below, and a steady-state tone through the filter adds its own
    // analytic gain on top.
    constexpr double kMeanSquareSeamDb = -3.0102999566398120;
    const double expectedALevel =
        kMeanSquareSeamDb + rta::dsp::Weighting::analyticDb(kFreqHz, rta::dsp::WeightingType::A);
    const double gapDb = rta::dsp::Weighting::analyticDb(kFreqHz, rta::dsp::WeightingType::C) -
                         rta::dsp::Weighting::analyticDb(kFreqHz, rta::dsp::WeightingType::A);
    // Sanity on the fixture itself: 20 Hz really does separate A from C
    // sharply, or this test would not be able to tell the bug from a pass.
    REQUIRE(gapDb > 20.0);

    // Generous (digital-filter approximation + block-boundary settling,
    // record 8's own "the design target and closed form" note): still tight
    // enough to fail by a wide margin under the first-chain bug, which would
    // read within a fraction of a dB of LCeq's level, `gapDb` away.
    CHECK_THAT(static_cast<double>(withCFirst.alarmValueDb), WithinAbs(expectedALevel, 1.0));
}

TEST_CASE("1b dose and Ln read the A-weighted chain, never the first configured chain",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.dose[0] = DoseSettings{85.0, 10.0, 10.0, 80.0};  // T_c=10s, L_c=85, q=10, threshold=80

    SplChannelState state(config, 48000.0);
    std::vector<Block> cWindow, aWindow;

    // C is fed FIRST in the per-hop array (as a config listing LCeq before
    // LAeq would produce) at an absurd level that would blow dose past
    // 100 % many times over if it were ever read; A sits EXACTLY at the
    // criterion, which is the D1a closed form: L = L_c, T = T_c => D = 100 %.
    for (std::uint64_t i = 0; i < 10; ++i) {
        const Block cBlock = blockAtLevel(i, 48000, 130.0);
        const Block aBlock = blockAtLevel(i, 48000, 85.0);
        cWindow.push_back(cBlock);
        aWindow.push_back(aBlock);
        std::array<ChainBlockAtClose, 2> atClose{
            ChainBlockAtClose{rta::dsp::WeightingType::C, cBlock, cWindow},
            ChainBlockAtClose{rta::dsp::WeightingType::A, aBlock, aWindow},
        };
        state.onBlockClosed(atClose);
    }

    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.dosePercent[0].has_value());
    // Mutant "feed the first chain again" reads the 130 dB C block instead:
    // D = 100 * 10 * 10^((130-85)/10) / 10 = 3.16e6 %, nowhere near 100 %.
    CHECK_THAT(*view.dosePercent[0], WithinAbs(100.0, 1e-9));
}

TEST_CASE("1c onBlockClosed no longer feeds Ln at all -- feedLnTicks is the only path",
         "[spl_channel_state]") {
    // Fix round 2026-09-25 (PR #29 round-3 fix pass step 1): Ln used to be
    // fed from the A-weighted chain's own closed block (Block::maxFastDb)
    // inside onBlockClosed -- the wrong granularity entirely (record §5:
    // "Fast, 100 ms sampling", not once per block). It now comes
    // EXCLUSIVELY from feedLnTicks (test_spl_ln_ticks.cpp exercises that
    // path through a real SplSession+SplMeter). A caller that closes many
    // blocks but never calls feedLnTicks must see Ln stay ABSENT -- proof
    // the two paths are actually decoupled, not merely that the old wrong
    // value stopped appearing. The "feed maxFastDb again" mutant (reverting
    // this fix) makes Ln PRESENT here, at ~80 dB, failing every CHECK_FALSE
    // below.
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 200.0;

    SplChannelState state(config, 48000.0);
    std::vector<Block> aWindow;
    for (std::uint64_t i = 0; i < 20; ++i) {
        feedOneChain(state, aWindow, blockAtLevel(i, 48000, 80.0));
    }

    SplBlockView view;
    state.fillPublish(view);
    for (const auto& ln : view.lnDb) CHECK_FALSE(ln.has_value());
}

TEST_CASE("an alarm naming no configured metric is published absent, not refused",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    // NO config.metrics at all -- "LAeq" (spec.metricId below) matches
    // nothing. This class's own choice (SplChannelState.h): still published
    // (so an operator can see the alarm exists in the config), never fed.
    SplAlarmSpec spec;
    spec.metricId = "LAeq";
    spec.limitDb = 50.0;  // would fire immediately if ever fed anything
    spec.windowBlocks = 1;
    config.alarms = {spec};

    SplChannelState state(config, 48000.0);
    std::vector<Block> aWindow;
    for (std::uint64_t i = 0; i < 5; ++i) {
        // Well over limitDb -- would fire if this ever reached the alarm.
        feedOneChain(state, aWindow, blockAtLevel(i, 48000, 90.0));
    }

    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.alarms.size() == 1);
    CHECK(view.alarms[0].state == SplAlarmState::Filling);
    CHECK_FALSE(view.alarms[0].headroomDb.has_value());
}

// --- windowAtClose: the pure reconstruction, including the underflow fix --

TEST_CASE("windowAtClose reconstructs the window ending at each block, never underflowing",
         "[spl_channel_state]") {
    std::vector<Block> closed;
    for (std::uint64_t i = 0; i < 4; ++i) closed.push_back(blockAtLevel(i, 48000, 70.0));

    SECTION("common case: the window outlives the whole batch") {
        std::vector<Block> fullWindow = closed;  // capacity 4, batch 4 -- boundary C == F
        for (std::size_t i = 0; i < closed.size(); ++i) {
            const auto w = rta::measure::windowAtClose(closed, fullWindow, i);
            REQUIRE(w.size() == i + 1);
            CHECK(w.back().blockIndex == closed[i].blockIndex);
            CHECK(w.front().blockIndex == closed[0].blockIndex);
        }
    }

    SECTION("verifier D3 repro: one hop closes 4 blocks against a 1-block window") {
        const std::vector<Block> fullWindow = {closed.back()};  // windowBlocks == 1
        for (std::size_t i = 0; i < closed.size(); ++i) {
            const auto w = rta::measure::windowAtClose(closed, fullWindow, i);
            // MUST NOT underflow/crash, and capacity 1 means exactly one
            // block -- itself -- at every step, never a stale neighbour.
            REQUIRE(w.size() == 1);
            CHECK(w.front().blockIndex == closed[i].blockIndex);
        }
    }

    SECTION("intermediate capacity: 4 closed against a 2-block window") {
        const std::vector<Block> fullWindow = {closed[2], closed[3]};
        // Newest (i=3): the full window, unchanged.
        {
            const auto w = rta::measure::windowAtClose(closed, fullWindow, 3);
            REQUIRE(w.size() == 2);
            CHECK(w[0].blockIndex == 2);
            CHECK(w[1].blockIndex == 3);
        }
        // i=0: capacity 2 but only 1 element of `closed` precedes/includes
        // it -- under-fills to 1 rather than reaching for history that
        // rolled out of `fullWindow` within this same batch.
        {
            const auto w = rta::measure::windowAtClose(closed, fullWindow, 0);
            REQUIRE(w.size() == 1);
            CHECK(w.front().blockIndex == 0);
        }
    }
}

// --- a: a constant-level stream reaches Filling then Clear, with headroom -

TEST_CASE("a a constant-level stream publishes Filling then Clear with the record section 15 A6 headroom",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.metrics = {{"LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1}};
    SplAlarmSpec spec;
    spec.metricId = "LAeq";
    spec.limitDb = 100.0;
    spec.windowBlocks = 3;
    config.alarms = {spec};

    SplChannelState state(config, 48000.0);
    std::vector<Block> window;

    // Block 0: the window (capacity 3) is not yet full -- Filling, and
    // headroom is still published while it fills (record §15 A6).
    feedOneChain(state, window, blockAtLevel(0, 48000, 90.0));
    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.alarms.size() == 1);
    CHECK(view.alarms[0].state == SplAlarmState::Filling);
    REQUIRE(view.alarms[0].headroomDb.has_value());
    {
        const double T = 3.0, t = 1.0, Llim = 100.0, Lt = 90.0;
        const double expected =
            10.0 * std::log10((T * std::pow(10.0, Llim / 10.0) - t * std::pow(10.0, Lt / 10.0)) /
                              (T - t));
        CHECK_THAT(*view.alarms[0].headroomDb, WithinAbs(expected, 1e-9));
    }

    // Blocks 1, 2: the window is now full -- Filling -> Clear.
    feedOneChain(state, window, blockAtLevel(1, 48000, 90.0));
    feedOneChain(state, window, blockAtLevel(2, 48000, 90.0));
    state.fillPublish(view);
    CHECK(view.alarms[0].state == SplAlarmState::Clear);
    REQUIRE(view.alarms[0].headroomDb.has_value());
    {
        const double T = 3.0, t = 2.0, Llim = 100.0, Lt = 90.0;
        const double expected =
            10.0 * std::log10((T * std::pow(10.0, Llim / 10.0) - t * std::pow(10.0, Lt / 10.0)) /
                              (T - t));
        CHECK_THAT(*view.alarms[0].headroomDb, WithinAbs(expected, 1e-9));
    }
}

// --- b: dose after N blocks equals W1-D's closed form ----------------------

TEST_CASE("b dose over N blocks at the criterion level reaches the closed form via the published view",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.dose[0] = DoseSettings{85.0, 10.0, 10.0, 80.0};

    SplChannelState state(config, 48000.0);
    std::vector<Block> window;
    for (std::uint64_t i = 0; i < 10; ++i) {  // T_c == 10 s at 1 s blocks
        feedOneChain(state, window, blockAtLevel(i, 48000, 85.0));
    }

    SplBlockView view;
    state.fillPublish(view);

    REQUIRE(view.dosePercent[0].has_value());
    CHECK_THAT(*view.dosePercent[0], WithinAbs(100.0, 1e-9));
    REQUIRE(view.doseProjected[0].has_value());
    CHECK_THAT(*view.doseProjected[0], WithinAbs(100.0, 1e-9));

    REQUIRE(view.dosePercent[1].has_value());
    CHECK_THAT(*view.dosePercent[1], WithinAbs(0.0, 1e-9));
}

// --- c: Ln of a two-level stream lands within the histogram's w/2 bound ----

TEST_CASE("c Ln of a two-level tick stream reads L10 near 80 and L90 near 60, within 0.05 dB",
         "[spl_channel_state]") {
    // Fed via feedLnTicks directly (fix round 2026-09-25, PR #29 round-3
    // step 1: Ln is on the detector's own 100 ms clock, not the block
    // clock) -- one tick per iteration, which is the same shape the old
    // one-per-block feed had, so this fixture's own percentile-math
    // assertions carry over unchanged.
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 200.0;
    config.referenceOffsetDb = 20.0;

    SplChannelState state(config, 48000.0);
    for (std::uint64_t i = 0; i < 100; ++i) {
        const double publishedLevelDb = (i % 2 == 0) ? 60.0 : 80.0;
        const double tickDb = publishedLevelDb - config.referenceOffsetDb;
        state.feedLnTicks(std::span<const double>(&tickDb, 1));
    }

    SplBlockView view;
    state.fillPublish(view);

    constexpr double kHalfBinBoundDb = 0.05 + 1e-9;
    REQUIRE(view.lnDb[2].has_value());
    CHECK_THAT(*view.lnDb[2], WithinAbs(80.0, kHalfBinBoundDb));
    REQUIRE(view.lnDb[4].has_value());
    CHECK_THAT(*view.lnDb[4], WithinAbs(60.0, kHalfBinBoundDb));
}

// --- d: every slot is absent before it has a result -------------------------

TEST_CASE("d every alarm/dose/Ln slot is absent before it has a result", "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.metrics = {{"LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1}};
    SplAlarmSpec spec;
    spec.metricId = "LAeq";
    spec.limitDb = 100.0;
    spec.windowBlocks = 5;
    config.alarms = {spec};

    SplChannelState state(config, 48000.0);  // NOT fed a single block

    SplBlockView view;
    state.fillPublish(view);

    REQUIRE(view.alarms.size() == 1);
    CHECK(view.alarms[0].state == SplAlarmState::Filling);
    CHECK_FALSE(view.alarms[0].headroomDb.has_value());

    CHECK_FALSE(view.dosePercent[0].has_value());
    CHECK_FALSE(view.dosePercent[1].has_value());
    CHECK_FALSE(view.doseProjected[0].has_value());
    CHECK_FALSE(view.doseProjected[1].has_value());

    for (const auto& ln : view.lnDb) {
        CHECK_FALSE(ln.has_value());
    }
}

// --- CalibrationInvalid excludes Ln/dose the same way it excludes a window -

TEST_CASE("a CalibrationInvalid block is excluded from dose, honestly",
         "[spl_channel_state]") {
    // Ln is NOT part of this fixture's own claim any more (fix round
    // 2026-09-25, PR #29 round-3 step 1): onBlockClosed no longer feeds Ln
    // at all -- feedLnTicks does, unconditionally, with NO per-tick
    // CalibrationInvalid gating (SplChannelState.h's own documented scope
    // decision: doing so would require knowing which block-interval each
    // 100 ms tick falls in, which record §5 does not ask for). Asserting
    // Ln absence here would be vacuously true regardless of this fixture's
    // CalibrationInvalid flag and would misstate that as a guarded property.
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.dose[0] = DoseSettings{85.0, 10.0, 10.0, 80.0};

    SplChannelState state(config, 48000.0);
    std::vector<Block> window;

    Block invalid = blockAtLevel(0, 48000, 150.0);  // an absurd level, to fail loudly if counted
    invalid.flags = rta::meter::flagMask(rta::meter::BlockFlag::CalibrationInvalid);
    feedOneChain(state, window, invalid);

    SplBlockView view;
    state.fillPublish(view);
    CHECK_FALSE(view.dosePercent[0].has_value());
}

// --- e: allocated once, at construction, and never again -------------------

TEST_CASE("e SplChannelState allocates once at construction and never again, including "
         "alarm Fired/Cleared transitions",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;  // 100 blocks -- small on purpose, still generous below
    config.metrics = {{"LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1}};
    SplAlarmSpec spec;
    spec.metricId = "LAeq";
    spec.limitDb = 80.0;
    spec.windowBlocks = 1;  // fires/clears on EVERY block, maximising transitions
    config.alarms = {spec};

    std::optional<SplChannelState> state;
    {
        rta::test::AllocationProbe probe;
        state.emplace(config, 48000.0);
        // Construction must allocate SOMETHING (the history ring, the
        // marker store, the alarm groups) -- a reading of zero would mean
        // the allocation was elided, not that it is free.
        CHECK(probe.bytes() > 0);
    }

    std::vector<Block> window;
    window.reserve(400);
    rta::test::AllocationProbe probe;
    int transitions = 0;
    for (std::uint64_t i = 0; i < 400; ++i) {
        // Alternates 70/90 dB around the 80 dB limit -- Fired then Cleared
        // on every single block, the densest transition rate this alarm can
        // produce (C4's own "smallest step that could be swallowed" fixture
        // shape, reused here to maximise marker traffic).
        const double levelDb = (i % 2 == 0) ? 90.0 : 70.0;
        feedOneChain(state.value(), window, blockAtLevel(i, 48000, levelDb));
        ++transitions;
    }
    const std::size_t bytes = probe.bytes();
    INFO("bytes allocated by " << transitions << " blocks, each an alarm transition = " << bytes);
    CHECK(bytes == 0);

    // Non-elidable: read the accumulated, escaped result right after the
    // probe scope so the optimiser cannot have removed the work that
    // produced it (memory/an-allocation-the-optimiser-removed-...).
    SplBlockView view;
    state->fillPublish(view);
    REQUIRE(view.alarms.size() == 1);
    CHECK(view.alarms[0].sinceBlock.has_value());
}
