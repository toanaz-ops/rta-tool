// SPDX-License-Identifier: AGPL-3.0-or-later
// Split out of test_spl_channel_state.cpp (PR #29 round-3 fix pass step 6,
// 400-line hard cap) -- the round-3 verifier's own steps 3/4/5, following
// the test_spl_report_fixes.cpp precedent for a fix round's own cases
// living in a "_fixes" file next to the class's main test file.
#include "measure/SplChannelState.h"

#include "rta/dsp/Weighting.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using rta::measure::ChainBlockAtClose;
using rta::measure::SplAlarmSpec;
using rta::measure::SplBlockView;
using rta::measure::SplChannelState;
using rta::measure::SplConfig;
using rta::meter::Block;

namespace {

Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
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

// --- PR #29 round-3 fix pass step 3: a marker's real identity is an index,
// not MarkerQuantity's 31-char-truncated string -----------------------------

TEST_CASE("two metric ids sharing a 31-char prefix produce DISTINGUISHABLE alarm markers",
         "[spl_channel_state]") {
    // MarkerQuantity truncates at 31 characters (SplHistory.h). Two 32-char
    // ids identical in their first 31 characters collide under it: the OLD
    // string-based identity (`quantity == quantity`) cannot tell these two
    // alarms' markers apart. buildAlarmGroups now resolves each alarm's
    // metric INDEX at construction (the same place it already resolves the
    // weighting) and threads it onto every marker that alarm writes.
    const std::string id1(31, 'A');
    const std::string id2(31, 'A');
    const std::string longId1 = id1 + "1";  // 32 chars, first 31 identical
    const std::string longId2 = id2 + "2";

    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.metrics = {
        {longId1, rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1},
        {longId2, rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1},
    };
    SplAlarmSpec spec1;
    spec1.metricId = longId1;
    spec1.limitDb = -1000.0;  // fires on any real level
    spec1.windowBlocks = 1;
    SplAlarmSpec spec2;
    spec2.metricId = longId2;
    spec2.limitDb = -1000.0;
    spec2.windowBlocks = 1;
    config.alarms = {spec1, spec2};

    SplChannelState state(config, 48000.0);
    std::vector<Block> window;
    feedOneChain(state, window, blockAtLevel(0, 48000, 0.0));

    const auto& markers = state.history().markers();
    REQUIRE(markers.size() == 2);

    // The collision this fix closes: both markers' TRUNCATED string
    // identity really is byte-identical.
    CHECK(markers[0].quantity == markers[1].quantity);

    // The fix: the resolved metric INDEX distinguishes them, even though
    // the string collided. Neither is absent (both metricId's matched a
    // configured metric).
    REQUIRE(markers[0].metricIndex.has_value());
    REQUIRE(markers[1].metricIndex.has_value());
    CHECK(*markers[0].metricIndex != *markers[1].metricIndex);
    // And each index actually names the RIGHT metric in config.metrics.
    std::vector<std::size_t> indices{*markers[0].metricIndex, *markers[1].metricIndex};
    std::sort(indices.begin(), indices.end());
    CHECK(indices == std::vector<std::size_t>{0, 1});
}

// --- PR #29 round-3 fix pass step 4: publish the marker overflow count -----

TEST_CASE("a forced marker overflow reaches the published view", "[spl_channel_state]") {
    // SplHistory::overflowedMarkers() already existed (SplHistory.h's own
    // kMaxMarkers comment); nothing published it. fillPublish now carries
    // it unconditionally on SplBlockView::markersOverflowed.
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;

    SplChannelState state(config, 48000.0);

    SplBlockView before;
    state.fillPublish(before);
    CHECK(before.markersOverflowed == 0);

    // Directly at the ring, bypassing alarm computation -- this fixture's
    // own subject is the PUBLISH path, not how a marker comes to exist.
    const std::size_t pushed = rta::measure::SplHistory::kMaxMarkers + 100;
    for (std::size_t i = 0; i < pushed; ++i) {
        rta::measure::SplMarker marker;
        marker.blockIndex = i;
        marker.kind = rta::measure::SplMarkerKind::Note;
        state.history().addMarker(marker);
    }
    REQUIRE(state.history().overflowedMarkers() == 100);

    SplBlockView after;
    state.fillPublish(after);
    CHECK(after.markersOverflowed == 100);
}

// --- PR #29 round-3 fix pass step 5: published order matches config order -

TEST_CASE("published alarm order matches config.alarms order, never the weighting-grouped order",
         "[spl_channel_state]") {
    // Three alarms across two weightings, INTERLEAVED (C, A, C) -- the
    // shape that actually distinguishes the fix from the old behaviour.
    // With only TWO alarms on two different weightings (one each), the
    // internal weighting partition's own first-seen bucket order already
    // coincides with config order by construction (each weighting occurs
    // exactly once, so "first occurrence" is "the only occurrence"); it
    // takes a THIRD alarm reusing an earlier weighting, after a different
    // one, to actually interleave and reveal the bug: alarmGroups_
    // partitions by weighting, which reorders config.alarms whenever
    // weightings interleave (old flattened order here would be
    // [C1, C2, A1], not [C1, A1, C2]).
    SplConfig config;
    config.blockSeconds = 1.0;
    config.logSpanSeconds = 100.0;
    config.metrics = {
        {"LCeq", rta::dsp::WeightingType::C, rta::meter::TimeWeighting::Fast, 1},
        {"LAeq", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 1},
    };
    SplAlarmSpec c1;
    c1.metricId = "LCeq";
    c1.limitDb = 100.0;
    c1.windowBlocks = 1;
    SplAlarmSpec a1;
    a1.metricId = "LAeq";
    a1.limitDb = 100.0;
    a1.windowBlocks = 1;
    SplAlarmSpec c2;
    c2.metricId = "LCeq";
    c2.limitDb = 90.0;  // distinguishes c2 from c1: same metricId, different limit
    c2.windowBlocks = 1;
    config.alarms = {c1, a1, c2};

    SplChannelState state(config, 48000.0);

    SplBlockView view;
    state.fillPublish(view);
    REQUIRE(view.alarms.size() == 3);
    CHECK(view.alarms[0].metricId == "LCeq");
    CHECK(view.alarms[0].limitDb == 100.0);
    CHECK(view.alarms[1].metricId == "LAeq");
    CHECK(view.alarms[2].metricId == "LCeq");
    CHECK(view.alarms[2].limitDb == 90.0);
}
