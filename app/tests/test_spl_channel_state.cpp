// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a task W2-E1 (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md's
// amendment "W2-E -- the wiring nobody was assigned"; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §4, §5, §6, §7, §15 A6).
#include "measure/SplChannelState.h"

#include "AllocationProbe.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <optional>
#include <vector>

using Catch::Matchers::WithinAbs;
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
    return b;
}

}  // namespace

// --- e: allocated once, at construction, and never again ------------------

TEST_CASE("e SplChannelState allocates once at construction and never again", "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;  // 100 blocks -- small on purpose, still 10x below

    std::optional<SplChannelState> state;
    {
        rta::test::AllocationProbe probe;
        state.emplace(config, 48000.0);
        // Construction must allocate SOMETHING (the history ring's backing
        // store) -- a reading of zero would mean the allocation was elided,
        // not that it is free (the same caveat W2-A1's own fixture states).
        CHECK(probe.bytes() > 0);
    }

    std::vector<Block> window;
    // Reserved BEFORE the probe arms: an unreserved push_back would grow
    // this TEST'S OWN vector and be counted as an allocation indistinguishable
    // from one inside SplChannelState -- the probe measures the production
    // path, not this fixture's bookkeeping.
    window.reserve(1000);
    rta::test::AllocationProbe probe;
    for (std::uint64_t i = 0; i < 1000; ++i) {  // 10x the ring's own capacity
        const Block b = blockAtLevel(i, 48000, 70.0);
        window.push_back(b);
        state->onBlockClosed(b, window);
    }
    CHECK(probe.bytes() == 0);
}

// --- a: a constant-level stream reaches Filling then Clear, with headroom -

TEST_CASE("a a constant-level stream publishes Filling then Clear with the record section 15 A6 headroom",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    SplAlarmSpec spec;
    spec.metricId = "LAeq";
    spec.limitDb = 100.0;
    spec.windowBlocks = 3;
    config.alarms = {spec};

    SplChannelState state(config, 48000.0);
    std::vector<Block> window;

    auto feed = [&](std::uint64_t index, double levelDb) {
        const Block b = blockAtLevel(index, 48000, levelDb);
        window.push_back(b);
        state.onBlockClosed(b, window);
    };

    // Block 0: the window (capacity 3) is not yet full -- Filling, and
    // headroom is still published while it fills (record §15 A6).
    feed(0, 90.0);
    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.alarms.size() == 1);
    CHECK(view.alarms[0].state == SplAlarmState::Filling);
    REQUIRE(view.alarms[0].headroomDb.has_value());
    // FILLING branch: T = windowSeconds (3) - excludedSecondsSoFar (0);
    // t = measured seconds so far (1); L_t = 90.
    {
        const double T = 3.0, t = 1.0, Llim = 100.0, Lt = 90.0;
        const double expected =
            10.0 * std::log10((T * std::pow(10.0, Llim / 10.0) - t * std::pow(10.0, Lt / 10.0)) /
                              (T - t));
        CHECK_THAT(*view.alarms[0].headroomDb, WithinAbs(expected, 1e-9));
    }

    // Blocks 1, 2: the window is now full (3 blocks, all at 90 dBA, 10 dB
    // under the 100 dBA limit) -- the FIRST full window that yields a value
    // promotes Filling -> Clear (record §15 A6's own "a fully excluded first
    // window is still Filling" case does not apply here: nothing is
    // excluded).
    feed(1, 90.0);
    feed(2, 90.0);
    state.fillPublish(view);
    CHECK(view.alarms[0].state == SplAlarmState::Clear);
    REQUIRE(view.alarms[0].headroomDb.has_value());
    // FULL branch: recent = blocks[1,2], s = 2 s, L_t = 90, T = s + Delta = 3.
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
    // A small, self-contained criterion (record §7's own identity: L = L_c,
    // T = T_c => D = 100 % exactly) -- accumulator 0 only; accumulator 1
    // stays the shipped OSHA PEL default (90 dBA threshold).
    config.dose[0] = DoseSettings{85.0, 10.0, 10.0, 80.0};

    SplChannelState state(config, 48000.0);
    std::vector<Block> window;
    for (std::uint64_t i = 0; i < 10; ++i) {  // T_c == 10 s at 1 s blocks
        const Block b = blockAtLevel(i, 48000, 85.0);
        window.push_back(b);
        state.onBlockClosed(b, window);
    }

    SplBlockView view;
    state.fillPublish(view);

    REQUIRE(view.dosePercent[0].has_value());
    CHECK_THAT(*view.dosePercent[0], WithinAbs(100.0, 1e-9));
    REQUIRE(view.doseProjected[0].has_value());
    CHECK_THAT(*view.doseProjected[0], WithinAbs(100.0, 1e-9));  // T_elapsed == T_c here

    // Accumulator 1 (OSHA PEL, 90 dBA threshold): every block here is 85 dBA,
    // strictly below it, so it contributes EXACTLY zero -- a REAL measured
    // 0.0 %, not an absence, because it has seen ten seconds of real blocks
    // (record's D1e: "below threshold contributes exactly zero").
    REQUIRE(view.dosePercent[1].has_value());
    CHECK_THAT(*view.dosePercent[1], WithinAbs(0.0, 1e-9));
}

// --- c: Ln of a two-level stream lands within the histogram's w/2 bound ----

TEST_CASE("c Ln of a two-level stream reads L10 near 80 and L90 near 60, within 0.05 dB",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 200.0;
    // A small offset so the published 80 dB sample sits well INSIDE the span
    // rather than exactly on its exclusive top edge: uncalibrated
    // (referenceOffsetDb == 0.0) the span is [-120, +80) (record §5, SPL-R8),
    // and 80.0 is the one value that boundary excludes. Each block is fed
    // its UN-OFFSET level (the published level minus this offset), so
    // combineBlocks' own offset application inside onBlockClosed publishes
    // exactly 60/80.
    config.referenceOffsetDb = 20.0;

    SplChannelState state(config, 48000.0);
    std::vector<Block> window;
    for (std::uint64_t i = 0; i < 100; ++i) {  // 50 at 60 dB, 50 at 80 dB, published
        const double publishedLevelDb = (i % 2 == 0) ? 60.0 : 80.0;
        const Block b = blockAtLevel(i, 48000, publishedLevelDb - config.referenceOffsetDb);
        window.push_back(b);
        state.onBlockClosed(b, window);
    }

    SplBlockView view;
    state.fillPublish(view);

    // config.lnPercents defaults to {1, 5, 10, 50, 90, 95} (record §13 Q3) --
    // index 2 is L10, index 4 is L90. The theorem's own bound is w/2 = 0.05
    // dB EXACTLY (record §5); this fixture lands exactly on it (89.1/9.9
    // interpolate to a whole bin edge), so the tolerance carries a tiny
    // float-roundoff allowance rather than widening the bound itself.
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

TEST_CASE("a CalibrationInvalid block is excluded from Ln and dose, honestly",
         "[spl_channel_state]") {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.dose[0] = DoseSettings{85.0, 10.0, 10.0, 80.0};

    SplChannelState state(config, 48000.0);
    std::vector<Block> window;

    Block invalid = blockAtLevel(0, 48000, 150.0);  // an absurd level, to fail loudly if counted
    invalid.flags = rta::meter::flagMask(rta::meter::BlockFlag::CalibrationInvalid);
    window.push_back(invalid);
    state.onBlockClosed(invalid, window);

    SplBlockView view;
    state.fillPublish(view);
    // Nothing valid has been fed yet: dose has seen no elapsed seconds, and
    // the Ln histogram has no sample at all -- both ABSENT, not a number
    // derived from the excluded block.
    CHECK_FALSE(view.dosePercent[0].has_value());
    for (const auto& ln : view.lnDb) {
        CHECK_FALSE(ln.has_value());
    }
}
