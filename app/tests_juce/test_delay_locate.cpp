// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-DELAY task F2 (docs/plans/2026-09-07-L7-delay-impl-plan.md; record
// docs/dsp/2026-09-06-l7-auto-delay.md sec.11.2, sec.1.6, sec.8). A REAL
// AnalysisThread over a REAL CaptureBus -- same shape test_routing_live.cpp
// already uses for drainPaired, since only a live thread proves the raw-
// capture accumulator sees the SAME hops the Analyser does, in the SAME
// order, not a stand-in for it.

#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
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

// A distinctive, checkable ramp -- not a flat tone -- so F2a can verify the
// accumulator's CONTENT and ORDER, not merely its length. `channel` offsets
// the ramp so the reference and measurement streams are never bit-identical.
void pushRamp(CaptureBus& bus, int referenceChannel, int measurementChannel,
              int channelsInCallback, int samplesPerBlock, int blocks) {
    std::vector<float> refBlock(static_cast<std::size_t>(samplesPerBlock));
    std::vector<float> measBlock(static_cast<std::size_t>(samplesPerBlock));
    std::vector<float> silence(static_cast<std::size_t>(samplesPerBlock), 0.0f);
    std::vector<const float*> ptrs(static_cast<std::size_t>(channelsInCallback), silence.data());

    int counter = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < samplesPerBlock; ++i) {
            const float v = static_cast<float>(counter) * 0.001f;
            refBlock[static_cast<std::size_t>(i)] = v;
            measBlock[static_cast<std::size_t>(i)] = -v;
            ++counter;
        }
        ptrs[static_cast<std::size_t>(referenceChannel)] = refBlock.data();
        ptrs[static_cast<std::size_t>(measurementChannel)] = measBlock.data();
        bus.pushFromCallback(ptrs.data(), channelsInCallback, samplesPerBlock);
    }
}

std::shared_ptr<const rta::measure::LocateCapture> waitForCapture(const AnalysisThread& thread,
                                                                    int timeoutMs) {
    const auto deadline =
            juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (auto capture = thread.locateCapture()) return capture;
        juce::Thread::sleep(5);
    }
    return thread.locateCapture();
}

// Polls a condition the production code guarantees, bounded -- never a fixed
// sleep followed by a hope. Returns the predicate's final value.
template <typename Predicate>
bool waitUntil(Predicate&& predicate, int timeoutMs) {
    const auto deadline =
            juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (predicate()) return true;
        juce::Thread::sleep(5);
    }
    return predicate();
}

}  // namespace

TEST_CASE("the armed accumulator sees the engine's hops, same order",
          "[delay_locate][F2a]") {
    CaptureBus bus(8192);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.prepare(48000.0, 2);
    bus.setActive(true);

    AnalysisThread thread(bus, fastConfig());

    constexpr std::size_t kCaptureLength = 512;
    thread.armLocateCapture(/*routeIndex=*/0, kCaptureLength);

    // 40 blocks of 16 samples = 640 samples fed, comfortably past the
    // 512-sample capture and past the config's 16-sample hop.
    pushRamp(bus, 0, 1, 2, 16, 40);

    const auto capture = waitForCapture(thread, 3000);
    REQUIRE(capture != nullptr);
    REQUIRE(capture->reference.size() == kCaptureLength);
    REQUIRE(capture->measurement.size() == kCaptureLength);

    // Exactly the ramp fed, in order, from sample 0 -- capture->reference[i]
    // == i*0.001, capture->measurement[i] == -(i*0.001), for every i.
    for (std::size_t i = 0; i < kCaptureLength; i += 37) {  // sparse check, still exhaustive-ish
        const float expected = static_cast<float>(i) * 0.001f;
        REQUIRE(std::abs(capture->reference[i] - expected) < 1e-6f);
        REQUIRE(std::abs(capture->measurement[i] - (-expected)) < 1e-6f);
    }
    REQUIRE(std::abs(capture->reference[0] - 0.0f) < 1e-6f);
    REQUIRE(std::abs(capture->reference[kCaptureLength - 1] -
                      static_cast<float>(kCaptureLength - 1) * 0.001f) < 1e-6f);
}

TEST_CASE("Apply rebuilds and takes effect: referenceDelaySamples, then the gate re-fills",
          "[delay_locate][F2b]") {
    CaptureBus bus(8192);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.prepare(48000.0, 2);
    bus.setActive(true);

    Analyser::Config config = fastConfig();
    config.fftSize = 64;
    config.hopSize = 64;  // hop == fftSize: effectiveAverages == frameCount exactly
    // fastConfig()'s fifoDepth=4 saturates BELOW minimumEffectiveAverages'
    // default of 8.0 (a Fifo average caps at its own depth once saturated),
    // so the gate this test checks could never open at all with it -- this
    // test needs its own, larger depth.
    config.transferFifoDepth = 32;
    AnalysisThread thread(bus, config);

    // Clear frames through the ORIGINAL config so a real transfer function
    // exists to check coherence on at all.
    constexpr int kFramesBeforeApply = 12;
    pushRamp(bus, 0, 1, 2, 64, kFramesBeforeApply);

    // WAIT FOR THE DRAIN, do not sleep for it. `runBody` applies a pending
    // Apply BEFORE it drains (rebuild, then drain()), so an Apply that lands
    // while pre-Apply audio is still in the ring rebuilds first and then feeds
    // that audio to the FRESH analyser -- 12 frames, gate open, and the
    // closed-gate check below fails. A fixed sleep(300) hid this until the
    // analysis thread was starved for longer than that; with the sleep
    // removed the case fails 20/20 at the CHECK_FALSE below. What production
    // guarantees is that routeHopCount(0) reaches one hop per pushed block
    // and that a snapshot with a transfer block is published after it.
    REQUIRE(waitUntil(
        [&] { return thread.routeHopCount(0) >= static_cast<std::uint64_t>(kFramesBeforeApply); },
        10000));
    // Also proves the premise the closed-gate check below depends on: the gate
    // WAS open before Apply (12 frames >= minimumEffectiveAverages' 8), so
    // "closed afterwards" is a change and not the state the case started in.
    REQUIRE(waitUntil(
        [&] {
            const auto s = thread.latest();
            return s && s->transfer.has_value() && s->transfer->coherence.has_value() &&
                   s->framesAnalysed >= static_cast<std::uint64_t>(kFramesBeforeApply);
        },
        10000));
    REQUIRE(thread.appliedReferenceDelaySamples() == 0);

    thread.applyReferenceDelay(37);

    // The rebuild lands on the analysis thread's next poll (kPollMs = 10 ms
    // in AnalysisThread.cpp); appliedReferenceDelaySamples() confirms it.
    REQUIRE(waitUntil([&] { return thread.appliedReferenceDelaySamples() == 37; }, 10000));

    // Fewer fresh frames than minimumEffectiveAverages' default of 8 -- the
    // rebuilt Analyser must be refilling with the gate still closed (record
    // sec.1.6's accepted ~16-frame re-fill), even though 12 frames were
    // flowing before Apply. Pushed only AFTER the rebuild is confirmed, so
    // every one of them reaches the new Analyser. The old snapshot may still
    // be `latest()` (publish is throttled to 50 ms), and it is told apart from
    // the rebuilt one by frame count: 12 before, 0 < n < 12 after. Without the
    // rebuild the count would climb to 16 and this wait would time out.
    constexpr int kFramesRefilling = 4;
    pushRamp(bus, 0, 1, 2, 64, kFramesRefilling);
    std::shared_ptr<const rta::measure::Snapshot> refilling;
    REQUIRE(waitUntil(
        [&] {
            refilling = thread.latest();
            return refilling && refilling->framesAnalysed > 0 &&
                   refilling->framesAnalysed < static_cast<std::uint64_t>(kFramesBeforeApply);
        },
        10000));
    REQUIRE(refilling->transfer.has_value());
    CHECK_FALSE(refilling->transfer->coherence.has_value());

    // Push enough fresh frames (well past minimumEffectiveAverages' default
    // of 8) for the gate to re-open on the REBUILT Analyser.
    pushRamp(bus, 0, 1, 2, 64, 20);
    REQUIRE(waitUntil(
        [&] {
            const auto later = thread.latest();
            return later && later->transfer.has_value() && later->transfer->coherence.has_value();
        },
        10000));
}

