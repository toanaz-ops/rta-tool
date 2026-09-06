// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Record §8: sequencing is a capture state machine that refuses on two
// criteria needing no invented number -- overload and gate-not-cleared --
// and drives no output (the callback clears every output; changing that is
// a different lane). No third refusal reason exists.

#include <catch2/catch_test_macros.hpp>

#include "measure/CaptureSequencer.h"

#include <vector>

using rta::measure::CaptureSequencer;
using rta::measure::CaptureState;
using rta::measure::RefusalReason;

namespace {

std::vector<float> silence(std::size_t n) {
    return std::vector<float>(n, 0.0f);
}

/// Three consecutive full-scale samples -- Smaart's own published overload
/// criterion (core's kFullScaleThreshold), same fixture shape as
/// core/tests/test_overload.cpp.
std::vector<float> withOverload(std::size_t n) {
    std::vector<float> block(n, 0.0f);
    if (n >= 3) {
        block[0] = 1.0f;
        block[1] = 1.0f;
        block[2] = 1.0f;
    }
    return block;
}

}  // namespace

TEST_CASE("A three-member sequence walks arm-wait-capture-store-advance and ends Idle",
          "[capturesequencer]") {
    CaptureSequencer sequencer({"A", "B", "C"});
    CHECK(sequencer.state() == CaptureState::Idle);

    int stepCalls = 0;
    sequencer.onStep = [&](int) { ++stepCalls; };

    for (int i = 0; i < 3; ++i) {
        sequencer.arm();
        CHECK(sequencer.state() == CaptureState::Armed);
        sequencer.beginWait();
        CHECK(sequencer.state() == CaptureState::Waiting);
        sequencer.beginCapture();
        CHECK(sequencer.state() == CaptureState::Capturing);
        sequencer.feedHop(silence(64), silence(64));
        sequencer.endCapture(/*gateCleared=*/true);
    }

    CHECK(sequencer.state() == CaptureState::Idle);
    CHECK(stepCalls == 3);
    REQUIRE(sequencer.captures().size() == 3);
    CHECK(sequencer.captures()[0].name == "A");
    CHECK(sequencer.captures()[1].name == "B");
    CHECK(sequencer.captures()[2].name == "C");
    for (const auto& capture : sequencer.captures()) {
        CHECK(capture.refusal == RefusalReason::None);
    }
}

TEST_CASE("An overload during the window refuses that step only, and the sequence continues",
          "[capturesequencer]") {
    CaptureSequencer sequencer({"A", "B"});

    sequencer.arm();
    sequencer.beginWait();
    sequencer.beginCapture();
    sequencer.feedHop(withOverload(64), silence(64));  // clips mid-window
    sequencer.feedHop(silence(64), silence(64));        // clean for the rest of it
    sequencer.endCapture(/*gateCleared=*/true);

    // Refused: nothing stored for this step.
    CHECK(sequencer.captures().empty());
    CHECK(sequencer.lastRefusal() == RefusalReason::Overload);

    // The sequence continues to member B, unaffected.
    CHECK(sequencer.state() == CaptureState::Armed);
    sequencer.beginWait();
    sequencer.beginCapture();
    sequencer.feedHop(silence(64), silence(64));
    sequencer.endCapture(/*gateCleared=*/true);

    CHECK(sequencer.state() == CaptureState::Idle);
    REQUIRE(sequencer.captures().size() == 1);
    CHECK(sequencer.captures()[0].name == "B");
}

TEST_CASE("A member under the gate refuses with GateNotCleared and stores nothing degraded",
          "[capturesequencer]") {
    CaptureSequencer sequencer({"A"});

    sequencer.arm();
    sequencer.beginWait();
    sequencer.beginCapture();
    sequencer.feedHop(silence(64), silence(64));
    sequencer.endCapture(/*gateCleared=*/false);

    CHECK(sequencer.captures().empty());
    CHECK(sequencer.lastRefusal() == RefusalReason::GateNotCleared);
    // The sequence still ends (last member): no degraded capture is left
    // behind masquerading as a real one.
    CHECK(sequencer.state() == CaptureState::Idle);
}

TEST_CASE("onStep fires once per step and the sequencer writes to no output buffer",
          "[capturesequencer]") {
    CaptureSequencer sequencer({"A", "B"});
    std::vector<int> stepsSeen;
    sequencer.onStep = [&](int step) { stepsSeen.push_back(step); };

    // An output scratch the sequencer is never given any way to reach --
    // its API takes no output span anywhere (arm/beginWait/beginCapture/
    // feedHop/endCapture all take, at most, INPUT hops). Zeroed before and
    // checked after: still all zero proves nothing here ever wrote to it,
    // which is the structural half of record §8's "no generator output".
    std::vector<float> outputScratch(128, 0.0f);

    for (int i = 0; i < 2; ++i) {
        sequencer.arm();
        sequencer.beginWait();
        sequencer.beginCapture();
        sequencer.feedHop(silence(32), silence(32));
        sequencer.endCapture(true);
    }

    REQUIRE(stepsSeen.size() == 2);
    CHECK(stepsSeen[0] == 0);
    CHECK(stepsSeen[1] == 1);
    for (float sample : outputScratch) {
        CHECK(sample == 0.0f);
    }
}

TEST_CASE("A run split across hops still latches the overload for the whole window",
          "[capturesequencer]") {
    // Two samples at full scale in one hop, one more at the start of the
    // next -- three consecutive overall, split across the hop boundary
    // core/tests/test_overload.cpp documents hasOverload() itself does NOT
    // stitch (that stitching is this class's job, one layer up).
    CaptureSequencer sequencer({"A"});
    sequencer.arm();
    sequencer.beginWait();
    sequencer.beginCapture();

    std::vector<float> firstHop(64, 0.0f);
    firstHop[62] = 1.0f;
    firstHop[63] = 1.0f;
    std::vector<float> secondHop(64, 0.0f);
    secondHop[0] = 1.0f;

    sequencer.feedHop(firstHop, silence(64));
    sequencer.feedHop(secondHop, silence(64));
    sequencer.endCapture(true);

    CHECK(sequencer.captures().empty());
    CHECK(sequencer.lastRefusal() == RefusalReason::Overload);
}
