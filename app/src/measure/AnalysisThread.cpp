// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (juce::Thread only). See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.4, §6 Wave D.
#include "measure/AnalysisThread.h"

#include <exception>
#include <utility>

namespace rta::measure {

namespace {

// Poll granularity: how often run() wakes to check for new samples even
// with nothing to do. Short enough that a hop's worth of audio at any
// reasonable buffer size is never left waiting more than one tick.
constexpr int kPollMs = 10;

// Plan §1.4: analysis publish rate is at most 20 Hz. A view repainting
// faster than the eye resolves costs CPU during a show and buys nothing.
constexpr std::uint32_t kMinPublishIntervalMs = 50;

}  // namespace

AnalysisThread::AnalysisThread(rta::platform::CaptureBus& bus, const Analyser::Config& config)
    : juce::Thread("rta AnalysisThread")
    , bus_(bus)
    , baseConfig_(config)
    , analyser_(std::make_unique<Analyser>(config))
    , hopScratch_(config.hopSize, 0.0f)
    , referenceScratch_(config.hopSize, 0.0f) {
    startThread();
}

AnalysisThread::~AnalysisThread() {
    // Trap T-1: belt and braces even though the owner's member-declaration
    // order also guarantees this thread stops before bus_ is torn down.
    stopThread(2000);
}

SnapshotPtr AnalysisThread::latest() const {
    return latest_.load(std::memory_order_acquire);
}

rta::platform::Fault AnalysisThread::lastFault() const {
    const std::lock_guard<std::mutex> lock(faultLock_);
    return fault_;
}

void AnalysisThread::recordFault(rta::platform::Fault::Kind kind, const std::string& message) {
    const std::lock_guard<std::mutex> lock(faultLock_);
    fault_.kind = kind;
    fault_.message = message;
    ++fault_.sequence;
}

void AnalysisThread::run() {
    // Trap T-3, verbatim: wrap the whole body, record the message as a
    // fault, stop cleanly. Nothing below this point may let an exception
    // reach juce::Thread's caller.
    try {
        runBody();
    } catch (const std::exception& e) {
        recordFault(rta::platform::Fault::Kind::DeviceError, e.what());
    } catch (...) {
        recordFault(rta::platform::Fault::Kind::DeviceError,
                     "AnalysisThread::run: unknown exception");
    }
}

void AnalysisThread::runBody() {
    while (!threadShouldExit()) {
        wait(kPollMs);
        if (threadShouldExit()) {
            return;
        }

        rebuildAnalyserIfEpochChanged();

        drain();

        publishIfDue();
    }
}

void AnalysisThread::rebuildAnalyserIfEpochChanged() {
    const std::uint64_t epoch = bus_.epoch();
    if (epoch == lastEpoch_) {
        return;
    }
    lastEpoch_ = epoch;

    Analyser::Config cfg = baseConfig_;
    const double rate = bus_.sampleRate();
    if (rate > 0.0) {
        cfg.sampleRate = rate;
    }
    // A fresh Analyser has no buffered samples and no running average --
    // that IS the drain on this side of the bus, matching the freshly
    // RESET (not rebuilt -- CaptureBus::prepare() no longer reallocates,
    // see CaptureBus.h) RingBuffer objects CaptureBus::prepare() just
    // emptied for the new device session (decision record: "drains every
    // ring"). Without this, stale frames analysed at the old sample rate
    // would splice onto the
    // new one and mislabel every bin-to-Hz conversion.
    analyser_ = std::make_unique<Analyser>(cfg);
}

void AnalysisThread::drain() {
    const int reference = bus_.config().firstChannelWithRole(rta::platform::ChannelRole::Reference);
    const int measurement =
        bus_.config().firstChannelWithRole(rta::platform::ChannelRole::Measurement);

    // Both roles present means a transfer function is being measured, and the
    // two streams must advance together -- see PairedDrain.h for what
    // independent drains cost. With only one role assigned there is no pairing
    // to preserve and the original path is both correct and cheaper.
    if (reference >= 0 && measurement >= 0) {
        drainPaired(reference, measurement);
        return;
    }
    drainRole(rta::platform::ChannelRole::Measurement, false);
    drainRole(rta::platform::ChannelRole::Reference, true);
}

void AnalysisThread::drainPaired(int referenceChannel, int measurementChannel) {
    auto* referenceRing = bus_.ring(referenceChannel);
    auto* measurementRing = bus_.ring(measurementChannel);
    if (referenceRing == nullptr || measurementRing == nullptr) return;

    const std::size_t hop = hopScratch_.size();
    const std::size_t hops = pairedHopCount(referenceRing->availableToRead(),
                                            measurementRing->availableToRead(), hop);

    for (std::size_t i = 0; i < hops; ++i) {
        // peek both BEFORE discarding either: a short read on the second ring
        // after the first has already been discarded would drop a hop from one
        // channel only -- creating the very misalignment this function exists
        // to prevent, and doing it under the code that prevents it.
        if (referenceRing->peek(referenceScratch_) != hop) break;
        if (measurementRing->peek(hopScratch_) != hop) break;
        referenceRing->discard(hop);
        measurementRing->discard(hop);
        analyser_->pushPair(referenceScratch_, hopScratch_);
    }
}

void AnalysisThread::drainRole(rta::platform::ChannelRole role, bool isReference) {
    const int channel = bus_.config().firstChannelWithRole(role);
    if (channel < 0) {
        return;
    }

    // Re-fetched every call rather than cached across iterations. This used
    // to be load-bearing against a genuine hazard: CaptureBus::prepare()
    // once rebuilt its ring objects (not just their contents) on every
    // call, so a pointer held across a prepare() -- exactly what a cached
    // local here would be -- could dangle, a real use-after-free reachable
    // whenever a device change landed mid-drain. That hazard is closed now:
    // CaptureBus allocates every ring once, in its constructor, and
    // prepare() only resets them in place (see CaptureBus.h's class
    // comment), so a pointer from ring() stays valid for the CaptureBus's
    // whole lifetime. Re-fetching here is kept anyway because it is still
    // meaningful, just for a smaller reason: `channel` itself can change
    // (ChannelConfig role reassignment) and a shrinking prepare() can make a
    // previously-valid channel index report absent (ring() bounds-checks
    // against preparedChannels_) -- caching would miss both.
    auto* ring = bus_.ring(channel);
    if (ring == nullptr) {
        return;
    }

    const std::size_t hop = hopScratch_.size();
    // peek() + discard(): drain in whole Analyser-sized hops, so
    // pushMeasurement()/pushReference() always see a frame-aligned chunk
    // rather than whatever fraction of a hop happened to have arrived by
    // this poll. A short remainder is left in the ring for next time.
    while (ring->peek(hopScratch_) == hop) {
        ring->discard(hop);
        if (isReference) {
            analyser_->pushReference(hopScratch_);
        } else {
            analyser_->pushMeasurement(hopScratch_);
        }
    }
}

void AnalysisThread::publishIfDue() {
    const std::uint32_t now = juce::Time::getMillisecondCounter();
    if (lastPublishMs_ != 0 && (now - lastPublishMs_) < kMinPublishIntervalMs) {
        return;
    }
    lastPublishMs_ = now;

    // Trap T-4: this allocation (~8 KB, mostly spectrumDb) belongs here, on
    // the analysis thread, at most 20 times a second -- never in the audio
    // callback, which is what test_capture_bus.cpp's allocation test is for.
    SnapshotPtr snapshot = analyser_->publish(bus_.totalDrops());
    latest_.store(std::move(snapshot), std::memory_order_release);
}

}  // namespace rta::measure
