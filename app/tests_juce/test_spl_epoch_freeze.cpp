// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Station-4 fix round, PR #31, round 3, verifier finding 1 (MEDIUM): the
// epoch race. MainComponentSpl.cpp's own pollSplLogging() only notices a
// sample-rate/device change at its 2 Hz poll -- up to 500 ms after
// CaptureBus::prepare() has already bumped the bus's epoch and
// AnalysisThread::rebuildAnalysersIfEpochChanged has already rebuilt every
// Analyser at the NEW rate. Until that poll calls disableSplLogging then
// enableSplLogging (EnableFresh, SplLoggingDecision.h), feedSpl() kept
// feeding NEW-rate samples through the OLD session's SplSession/
// SplChannelState/SplLogPipeline -- all sized and clocked for the rate the
// session STARTED at -- which can close one mixed-rate block with nothing
// to flag it (CaptureBus::prepare() zeroes the drop counter, so there is no
// Gap either).
//
// The fix freezes feedSpl() at the exact tick the epoch changes:
// applyPendingSplRequest() records splSessionEpoch_ = lastEpoch_ every time
// it runs, and feedSpl()'s first line compares that against the CURRENT
// lastEpoch_ (kept current by rebuildAnalysersIfEpochChanged(), which always
// runs before drain() in the same tick -- AnalysisThread.cpp's own
// runBody()). A session started at epoch N sees lastEpoch_ jump to N+1 the
// moment the bus reconfigures, freezes on the very next feedSpl() call
// (there is no tick in between), and never advances again until a fresh
// enableSplLogging() call updates splSessionEpoch_ to match.
//
// This test drives a REAL AnalysisThread over a REAL CaptureBus (the
// test_spl_drain.cpp shape) and proves the freeze WITHOUT ever calling
// MainComponent at all: nothing here calls enableSplLogging a second time,
// so if feedSpl did not freeze on its own, splBlockCount would keep
// climbing on samples pushed after CaptureBus::prepare() bumps the epoch.
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::measure::SplConfig;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {

Analyser::Config fastConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 16;
    config.sampleRate = 48000.0;
    config.transferFifoDepth = 4;
    config.mtwEnabled = false;
    return config;
}

// Same "64 samples = 4 hops" shape test_spl_drain.cpp's own splConfig()
// uses: short enough that a handful of 16-sample hops closes a block, so the
// property under test (does the count keep climbing) is decided quickly.
SplConfig splConfig(double sampleRate) {
    SplConfig config;
    config.blockSeconds = 64.0 / sampleRate;
    config.metrics.push_back(rta::measure::SplMetricSpec{
        "L", rta::dsp::WeightingType::A, rta::meter::TimeWeighting::Fast, 4});
    return config;
}

void pushBlocks(CaptureBus& bus, int channelsInCallback, int toneChannel, int samplesPerBlock,
               int blocks) {
    std::vector<float> silence(static_cast<std::size_t>(samplesPerBlock), 0.0f);
    std::vector<float> tone(static_cast<std::size_t>(samplesPerBlock), 0.1f);
    std::vector<const float*> ptrs(static_cast<std::size_t>(channelsInCallback), silence.data());
    if (toneChannel >= 0 && toneChannel < channelsInCallback) {
        ptrs[static_cast<std::size_t>(toneChannel)] = tone.data();
    }
    for (int b = 0; b < blocks; ++b) {
        bus.pushFromCallback(ptrs.data(), channelsInCallback, samplesPerBlock);
    }
}

bool waitForSplBlocks(const AnalysisThread& thread, int channel, std::uint64_t target,
                      int timeoutMs) {
    const auto deadline =
        juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (thread.splBlockCount(channel) >= target) return true;
        juce::Thread::sleep(5);
    }
    return thread.splBlockCount(channel) >= target;
}

}  // namespace

TEST_CASE("feedSpl freezes the instant the bus epoch changes, with no second "
         "enableSplLogging call",
         "[spl_epoch_freeze]") {
    CaptureBus bus(4096);
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.prepare(48000.0, 2);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());
    const std::array<int, 1> logged{ 1 };
    thread.enableSplLogging(splConfig(48000.0), logged);  // state only, no log directory

    pushBlocks(bus, 2, 1, 16, 80);
    REQUIRE(waitForSplBlocks(thread, 1, 4, 3000));

    const auto beforeEpochChange = thread.splBlockCount(1);
    REQUIRE(beforeEpochChange >= 4);

    // Bump the bus's epoch WITHOUT touching enableSplLogging/
    // disableSplLogging at all -- reproducing the up-to-500ms window the
    // verifier's finding describes, where MainComponentSpl.cpp's poll has
    // not yet noticed. CaptureBus::prepare() bumps epoch_ unconditionally
    // (platform/types/src/CaptureBus.cpp), so this alone is the whole
    // reproduction.
    bus.prepare(96000.0, 2);
    bus.setActive(true);

    // kPollMs (AnalysisThread.cpp) is 10 ms, so 300 ms is generous headroom
    // for rebuildAnalysersIfEpochChanged() to have run at least once before
    // this keeps pushing "new-rate" audio at the still-alive OLD session.
    juce::Thread::sleep(50);
    pushBlocks(bus, 2, 1, 16, 200);
    juce::Thread::sleep(300);

    const auto afterEpochChange = thread.splBlockCount(1);
    INFO("splBlockCount before the epoch change = " << beforeEpochChange);
    INFO("splBlockCount after the epoch change  = " << afterEpochChange);
    // FROZEN: without splSessionEpoch_, these 200 further blocks' worth of
    // hops would close several more SPL blocks from a rate the session's own
    // meters were never rebuilt for; with the fix, feedSpl's first line
    // returns immediately, every tick, until a fresh enableSplLogging lands
    // -- which this test deliberately never calls.
    CHECK(afterEpochChange == beforeEpochChange);
}
