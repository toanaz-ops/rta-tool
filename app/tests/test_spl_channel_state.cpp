// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E1 (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md's
// amendment "W2-E -- the wiring nobody was assigned"; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §4, §5, §6, §7, §15 A6). Fix round
// 2026-09-25: an independent verifier refuted the first version of this
// file -- every consumer read the channel's FIRST configured chain
// regardless of which metric it was actually about. See SplChannelState.h.
//
// PR #29 round-3 fix pass step 6: split at the 400-line hard cap. The 1a/
// 1b/1c "every consumer reads the chain its own definition names" routing
// cases and their own SplSession-driven fixtures moved to
// test_spl_channel_state_routing.cpp; the round-3 step 3/4/5 fixes moved to
// test_spl_channel_state_fixes.cpp; construction/allocation/windowAtClose
// stayed here.
#include "measure/SplChannelState.h"

#include "AllocationProbe.h"

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
using rta::meter::Block;
using rta::meter::DoseSettings;

namespace {

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

}  // namespace

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

// The round-3 fix pass step 3/4/5 cases that used to follow here moved to
// test_spl_channel_state_fixes.cpp (PR #29 round-3 fix pass step 6,
// 400-line hard cap).

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
