// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-OUT task D (docs/plans/2026-09-07-L7-out-impl-plan.md; decision record
// docs/dsp/2026-09-06-l7-output-path.md sec.3, sec.9, sec.13.2's owner
// ruling). Policy over rta::platform::OutputEngine, JUCE-free -- routeOutput
// and CaptureSequencer are both JUCE-free, so this whole file is provable
// with no device.

#include "measure/CaptureSequencer.h"
#include "measure/OutputPolicy.h"
#include "rta/gen/Oscillator.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using rta::measure::applyToggle;
using rta::measure::CaptureSequencer;
using rta::measure::muteAll;
using rta::measure::soloOutput;
using rta::measure::SoloMode;
using rta::platform::kMaxChannels;
using rta::platform::OutputEngine;
using rta::platform::OutputRole;

namespace {

std::vector<float*> channelPointers(std::vector<std::vector<float>>& buffers) {
    std::vector<float*> ptrs;
    ptrs.reserve(buffers.size());
    for (auto& channel : buffers) ptrs.push_back(channel.data());
    return ptrs;
}

}  // namespace

TEST_CASE("soloOutput: strict solo routes exactly one channel; render confirms only it plays") {
    OutputEngine engine;
    engine.prepare(48000.0, 8);
    soloOutput(engine, 3);

    CHECK(engine.role(3) == OutputRole::Routed);
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        if (ch == 3) continue;
        CHECK(engine.role(ch) == OutputRole::None);
    }

    REQUIRE(engine.setSource(rta::gen::Oscillator(48000.0, 1000.0, -6.0)));
    engine.armSource();
    std::vector<std::vector<float>> buffers(8, std::vector<float>(600, 0.0f));
    auto ptrs = channelPointers(buffers);
    engine.render(ptrs.data(), 8, 600);

    bool channel3NonZero = false;
    for (float s : buffers[3]) {
        if (s != 0.0f) channel3NonZero = true;
    }
    CHECK(channel3NonZero);
    for (int ch = 0; ch < 8; ++ch) {
        if (ch == 3) continue;
        for (float s : buffers[static_cast<std::size_t>(ch)]) CHECK(s == 0.0f);
    }
}

TEST_CASE("muteAll: every routing role clears and render is exact silence") {
    OutputEngine engine;
    engine.prepare(48000.0, 4);
    soloOutput(engine, 2);
    muteAll(engine);

    for (int ch = 0; ch < kMaxChannels; ++ch) {
        CHECK(engine.role(ch) == OutputRole::None);
    }

    REQUIRE(engine.setSource(rta::gen::Oscillator(48000.0, 1000.0, -6.0)));
    engine.armSource();
    std::vector<std::vector<float>> buffers(4, std::vector<float>(600, 0.0f));
    auto ptrs = channelPointers(buffers);
    engine.render(ptrs.data(), 4, 600);
    for (const auto& channel : buffers) {
        for (float s : channel) CHECK(s == 0.0f);
    }
}

TEST_CASE("applyToggle: additive leaves other routes alone; strict solo clears them") {
    OutputEngine engine;
    engine.prepare(48000.0, 8);

    applyToggle(engine, 2, true, SoloMode::Additive);
    applyToggle(engine, 5, true, SoloMode::Additive);
    CHECK(engine.role(2) == OutputRole::Routed);
    CHECK(engine.role(5) == OutputRole::Routed);

    applyToggle(engine, 5, true, SoloMode::StrictSolo);
    CHECK(engine.role(5) == OutputRole::Routed);
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        if (ch == 5) continue;
        CHECK(engine.role(ch) == OutputRole::None);
    }
}

TEST_CASE("G20: CaptureSequencer::onStep bound to soloOutput solos exactly the stepped member's output, no device") {
    OutputEngine engine;
    engine.prepare(48000.0, 8);

    // Task D's own note: the member->output map is a plain std::vector<int>
    // identity-style injection here; its persistence is deferred to schema 4
    // (record sec.12).
    const std::vector<int> outputOfMember = {2, 5, 7};
    CaptureSequencer sequencer({"L", "R", "Sub"});
    sequencer.onStep = [&](int step) {
        soloOutput(engine, outputOfMember[static_cast<std::size_t>(step)]);
    };

    sequencer.arm();
    sequencer.beginWait();
    sequencer.beginCapture();
    sequencer.endCapture(true);  // step 0 ("L") -> solos output 2

    CHECK(engine.role(2) == OutputRole::Routed);
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        if (ch != 2) CHECK(engine.role(ch) == OutputRole::None);
    }

    // endCapture() auto-advances Idle-less straight to Armed for the next
    // member (CaptureSequencer.cpp) -- no explicit arm() needed here.
    sequencer.beginWait();
    sequencer.beginCapture();
    sequencer.endCapture(true);  // step 1 ("R") -> solos output 5

    CHECK(engine.role(5) == OutputRole::Routed);
    for (int ch = 0; ch < kMaxChannels; ++ch) {
        if (ch != 5) CHECK(engine.role(ch) == OutputRole::None);
    }
}
