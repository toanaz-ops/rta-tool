// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Task W2-E2b fix round (verifier MEDIUM finding, mutant M5): a NEW file
// rather than growing test_spl_publish.cpp (already near/at the 400-line cap
// this task's own instructions forbid growing further) -- the
// `logDroppedBlocks`/`logWriteFailed` precedent in that file, one field,
// same shape: `SplPublishInput::calibrationInvalid` must reach
// `SplBlockView::calibrationInvalid` unmodified.
#include "measure/AnalysisPublish.h"
#include "measure/SplConfig.h"

#include "rta/meter/Block.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

using rta::measure::SplConfig;
using rta::measure::SplPublishInput;
using rta::meter::Block;

namespace {
Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    return b;
}
}  // namespace

TEST_CASE("calibrationInvalid is copied straight into the published view, not dropped",
         "[splpublish_calibration_invalid]") {
    // Deleting AnalysisPublish.cpp's `view.calibrationInvalid =
    // input.calibrationInvalid;` leaves this case reading false either way --
    // this test is what would have caught it (mutant M5, verifier fix round).
    // Independent of channelState/config, same reasoning as the
    // logDroppedBlocks/logWriteFailed cases this file's own header mirrors:
    // a minimal publish with no metrics and no channelState isolates it.
    const SplConfig config;
    std::vector<Block> window;
    window.push_back(blockAtLevel(0, 48000, 85.0));

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = 48000.0;
    in.latestBlock = window.back();
    in.window = window;
    in.calibrationInvalid = true;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    CHECK(view->calibrationInvalid);
}

TEST_CASE("calibrationInvalid defaults false when the input never set it",
         "[splpublish_calibration_invalid]") {
    const SplConfig config;
    std::vector<Block> window;
    window.push_back(blockAtLevel(0, 48000, 85.0));

    SplPublishInput in;
    in.config = &config;
    in.sampleRate = 48000.0;
    in.latestBlock = window.back();
    in.window = window;

    const auto view = rta::measure::buildSplBlockView(in);
    REQUIRE(view.has_value());
    CHECK_FALSE(view->calibrationInvalid);
}
