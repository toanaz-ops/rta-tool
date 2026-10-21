// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Audit prj010-fable-audit-20261021 (medium), "APPLY / device change / SYNTHETIC
// toggle rebuilds all 8 Analysers synchronously on the analysis thread". The
// analysis thread now replaces the set ONE slot per tick (AnalyserRebuild.h).
// This drives a REAL AnalysisThread over a REAL CaptureBus and proves the two
// halves of that change:
//
//   1. the stall is bounded -- during an APPLY at 192 kHz with SYNTHETIC
//      running the ring never fills past what ~3 slot builds of audio would,
//      and nothing is dropped;
//   2. a stale slot is never fed and never published, a failed build surfaces
//      through the fault slot once, and the sweep recovers.
//
// Decision record: docs/dsp/2026-10-21-analyser-rebuild.md.
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"
#include "measure/SyntheticInput.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::measure::SyntheticInput;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;
using rta::platform::Fault;

namespace {

bool waitUntil(const std::function<bool()>& condition, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (condition()) return true;
        juce::Thread::sleep(2);
    }
    return condition();
}

void pushHops(CaptureBus& bus, int hops, int hop) {
    const std::vector<float> block(static_cast<std::size_t>(hop), 0.1f);
    const float* channels[2] = {block.data(), block.data()};
    for (int i = 0; i < hops; ++i) bus.pushFromCallback(channels, 2, hop);
}

Analyser::Config tinyConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 64;
    config.sampleRate = 48000.0;
    config.transferFifoDepth = 4;
    config.mtwEnabled = false;
    return config;
}

double milliseconds(std::chrono::steady_clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

}  // namespace

TEST_CASE("a stale slot is never fed or published, a failed build faults once, the sweep recovers",
          "[analyser_rebuild_live]") {
    CaptureBus bus(8192);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));

    AnalysisThread thread(bus, tinyConfig());  // epoch still 0: no sweep yet
    thread.failRebuildsForTest(1000000);       // every Analyser build now throws bad_alloc

    bus.prepare(48000.0, 2);  // device change: the sweep starts and stalls on slot 0
    REQUIRE(waitUntil([&] { return !bus.resyncPending(); }, 2000));
    bus.setActive(true);

    REQUIRE(waitUntil([&] { return thread.lastFault().sequence >= 1; }, 2000));
    const auto fault = thread.lastFault();
    CHECK(fault.message.find("out of memory") != std::string::npos);

    // ONE fault per sweep, however many ticks retry: the slot is retried every
    // 10 ms, so 250 ms is ~25 retries.
    const auto snapshotWhileStale = thread.latest();
    juce::Thread::sleep(250);
    CHECK(thread.lastFault().sequence == fault.sequence);

    // Published nothing while a slot a snapshot reads is stale.
    CHECK(thread.latest() == snapshotWhileStale);

    // The ring keeps draining (the drop counters and the SPL tap depend on it)...
    pushHops(bus, 20, 64);
    REQUIRE(waitUntil([&] { return bus.ring(0)->availableToRead() == 0; }, 2000));
    // ...but no hop reached the old Analyser: those 20 hops belong to the NEW
    // epoch, and the only Analyser there is was built for the previous one.
    CHECK(thread.routeHopCount(0) == 0);

    // Memory comes back: the next tick's retry completes the sweep, hops feed
    // again, and snapshots publish again.
    thread.failRebuildsForTest(0);
    REQUIRE(waitUntil(
        [&] {
            pushHops(bus, 1, 64);
            return thread.routeHopCount(0) >= 5;
        },
        3000));
    CHECK(waitUntil([&] { return thread.latest() != snapshotWhileStale; }, 2000));
    // The recoverable fault is cleared (sequence moves once more) when the sweep lands.
    CHECK(waitUntil([&] { return thread.lastFault().kind == Fault::Kind::None; }, 2000));
    CHECK(thread.lastFault().sequence == fault.sequence + 1);
}

TEST_CASE("an APPLY at 192 kHz with SYNTHETIC running drops nothing and stalls the drain for one slot",
          "[analyser_rebuild_live]") {
    CaptureBus bus(rta::platform::kFixedRingCapacitySamples);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));

    Analyser::Config config;  // MTW defaults: the 53 MB-per-Analyser case
    AnalysisThread thread(bus, config);

    SyntheticInput::Config synthetic;
    synthetic.sampleRate = 192000.0;
    synthetic.bufferSize = 512;
    SyntheticInput input(bus, synthetic);

    // Past the construction sweep and the first epoch's sweep (8 slots).
    REQUIRE(waitUntil([&] { return thread.routeHopCount(0) >= 40; }, 20000));
    REQUIRE(bus.totalDrops() == 0);

    // The bound, derived on THIS machine this run rather than quoted: one slot
    // build, measured here under the same load as the thread under test.
    Analyser::Config at192 = config;
    at192.sampleRate = 192000.0;
    std::vector<double> buildMs;
    for (int i = 0; i < 3; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto probe = std::make_unique<Analyser>(at192);
        buildMs.push_back(milliseconds(std::chrono::steady_clock::now() - t0));
    }
    std::sort(buildMs.begin(), buildMs.end());
    const double slotMs = buildMs[1];

    // Between two drains the ring gains (poll wait + one slot build + one drain)
    // of audio; 3 slots + 30 ms is that with ~2x to spare. The old single-call
    // rebuild stalled for all 8 slots + the wait, i.e. ~(8 slots + 10 ms), which
    // is over this bound for any slot time above 4 ms (measured 14 ms).
    const double boundSamples = 192000.0 * (3.0 * slotMs + 30.0) / 1000.0;

    const auto dropsBefore = bus.totalDrops();
    thread.applyReferenceDelay(37);
    std::size_t maxFill = 0;
    const auto deadline = juce::Time::getMillisecondCounter() + 20000u;
    while (thread.appliedReferenceDelaySamples() != 37 &&
           juce::Time::getMillisecondCounter() < deadline) {
        maxFill = std::max({maxFill, bus.ring(0)->availableToRead(), bus.ring(1)->availableToRead()});
    }
    REQUIRE(thread.appliedReferenceDelaySamples() == 37);

    INFO("slot build " << slotMs << " ms, ring high-water " << maxFill << " samples, bound "
                       << boundSamples << " samples, capacity "
                       << rta::platform::kFixedRingCapacitySamples);
    CHECK(static_cast<double>(maxFill) < boundSamples);
    CHECK(bus.totalDrops() == dropsBefore);
    CHECK(bus.totalDrops() == 0);
}
