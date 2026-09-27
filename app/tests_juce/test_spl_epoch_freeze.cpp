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

// CI run 36260617729 (windows-latest) failed here with "splBlockCount before
// = 12, after = 13" -- not the freeze failing, but this test's OWN "before"
// snapshot landing early. AnalysisThread.cpp's drainRole()/drainPaired()
// drain every WHOLE hop the ring currently holds in a single drain() call (a
// `while` loop, not "one hop per tick"), but how many of pushBlocks' 80
// writes have actually landed in the ring by the analysis thread's next
// kPollMs (10 ms, AnalysisThread.cpp) tick is a genuine scheduler race
// against THIS thread's own push loop -- worse on a loaded CI runner.
// waitForSplBlocks above only proves ">= 4 blocks have closed"; it says
// nothing about whether the rest of the 80-hop backlog is still sitting in
// the ring. If `beforeEpochChange` is read while a few hops remain
// unconsumed, the very next drain() tick closes one more block from that
// STILL-PRE-EPOCH backlog -- correctly, since CaptureBus::prepare() (which
// bumps the epoch feedSpl's freeze checks) has not even been called yet at
// that point -- and the count climbs by exactly the amount this test's own
// polling arrived early by.
//
// CaptureBus::prepare() also RESETS every ring to empty (CaptureBus.h's own
// comment), so a leftover pre-epoch hop can never survive to be miscounted
// as post-epoch: either it is drained (and counted) before prepare() runs,
// or prepare() erases it. That is what makes this a test-only race rather
// than a product one -- confirmed by reading AnalysisThread.cpp's drain
// path and CaptureBus::prepare() (platform/types/src/CaptureBus.cpp) rather
// than assumed.
//
// Wait for the backlog to go quiet before trusting a snapshot: the ring has
// nothing left for drainRole()/drainPaired() to consume (`availableToRead()
// < hopSize`) AND splBlockCount itself has not moved, both sustained for
// `stableForMs` straight. Requiring both, sustained, rules out the ring
// reading momentarily empty mid-push and being refilled a moment later.
std::uint64_t waitForSplBacklogDrained(const AnalysisThread& thread, CaptureBus& bus, int channel,
                                       std::size_t hopSize, int stableForMs, int timeoutMs) {
    const auto deadline =
        juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    std::uint64_t lastCount = thread.splBlockCount(channel);
    std::uint32_t quietSinceMs = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() < deadline) {
        juce::Thread::sleep(5);
        const std::uint64_t count = thread.splBlockCount(channel);
        auto* ring = bus.ring(channel);
        const bool ringHasWholeHop = ring != nullptr && ring->availableToRead() >= hopSize;
        const auto nowMs = juce::Time::getMillisecondCounter();
        if (count != lastCount || ringHasWholeHop) {
            lastCount = count;
            quietSinceMs = nowMs;
            continue;
        }
        if (nowMs - quietSinceMs >= static_cast<std::uint32_t>(stableForMs)) {
            return lastCount;
        }
    }
    // Lane-end LOW batch, 2026-09-27 (PR #46 LOW): falling out of the loop
    // here means the backlog never went quiet for `stableForMs` straight --
    // either it is still draining (a real bug: the ring should empty well
    // inside `timeoutMs` on this test's own small fixture) or it kept
    // re-filling for the whole window. Both are worth failing loudly on,
    // never worth handing the caller a `lastCount` silently taken mid-drain
    // and letting it read as a normal, stable value.
    FAIL("waitForSplBacklogDrained: backlog on channel " << channel << " did not go quiet for "
        << stableForMs << " ms within " << timeoutMs << " ms (last count " << lastCount << ")");
    return lastCount;
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

    // See waitForSplBacklogDrained's own comment above: read the count only
    // once the 80-hop backlog has stopped draining, not the instant it first
    // crosses 4 -- otherwise a block still closing from that SAME pre-epoch
    // push can land after this snapshot and read as a false failure.
    const auto beforeEpochChange =
        waitForSplBacklogDrained(thread, bus, 1, static_cast<std::size_t>(fastConfig().hopSize),
                                 /*stableForMs=*/100, /*timeoutMs=*/3000);
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
