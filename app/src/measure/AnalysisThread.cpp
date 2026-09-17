// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (juce::Thread only). See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.4, §6 Wave D;
// docs/plans/2026-09-06-L6b-impl-plan.md task B2 (record §6).
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
    , baseConfig_(config) {
    // Built once, at kMaxTransferFunctions, and never resized -- see that
    // constant's own header comment. Every entry shares baseConfig_ today;
    // a per-position Config (its own delay, its own trim) is B3/B4's job.
    analysers_.reserve(static_cast<std::size_t>(kMaxTransferFunctions));
    for (int i = 0; i < kMaxTransferFunctions; ++i) {
        analysers_.push_back(std::make_unique<Analyser>(config));
    }
    for (auto& scratch : channelScratch_) {
        scratch.assign(config.hopSize, 0.0f);
    }
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

std::uint64_t AnalysisThread::routeHopCount(int routeIndex) const noexcept {
    if (routeIndex < 0 || routeIndex >= kMaxTransferFunctions) {
        return 0;
    }
    return routeHopCounts_[static_cast<std::size_t>(routeIndex)].load(std::memory_order_relaxed);
}

void AnalysisThread::armLocateCapture(int routeIndex, std::size_t length) noexcept {
    requestedRouteIndex_.store(routeIndex, std::memory_order_relaxed);
    requestedCaptureLength_.store(length, std::memory_order_relaxed);
    // Clear any stale result BEFORE the request is visible, so a caller that
    // polls locateCapture() right after this call never mistakes the
    // PREVIOUS Locate's answer for the one just armed.
    locateCapture_.store(nullptr, std::memory_order_relaxed);
    locateArmRequested_.store(true, std::memory_order_release);
}

std::shared_ptr<const LocateCapture> AnalysisThread::locateCapture() const noexcept {
    return locateCapture_.load(std::memory_order_acquire);
}

void AnalysisThread::applyReferenceDelay(int delaySamples) noexcept {
    pendingReferenceDelay_.store(delaySamples, std::memory_order_relaxed);
    applyDelayRequested_.store(true, std::memory_order_release);
}

int AnalysisThread::appliedReferenceDelaySamples() const noexcept {
    return appliedReferenceDelay_.load(std::memory_order_acquire);
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

        rebuildAnalysersIfEpochChanged();
        applyPendingReferenceDelay();

        drain();

        publishIfDue();
    }
}

void AnalysisThread::rebuildAnalysersIfEpochChanged() {
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
    // would splice onto the new one and mislabel every bin-to-Hz
    // conversion. Every position is rebuilt together: a device
    // reconfiguration invalidates every engine's running state, not only
    // the one a particular route happens to name.
    for (auto& analyser : analysers_) {
        analyser = std::make_unique<Analyser>(cfg);
    }
}

void AnalysisThread::applyPendingReferenceDelay() {
    if (!applyDelayRequested_.exchange(false, std::memory_order_acquire)) {
        return;
    }
    baseConfig_.referenceDelaySamples = pendingReferenceDelay_.load(std::memory_order_relaxed);

    // Same rebuild shape as rebuildAnalysersIfEpochChanged: every position
    // together, sample rate re-read from the bus (record sec.1.6/sec.8 --
    // an Apply is an engine rebuild, and the coherence gate re-fill that
    // follows -- ~16 frames of nullopt before it reopens -- is the accepted
    // cost, not a defect).
    Analyser::Config cfg = baseConfig_;
    const double rate = bus_.sampleRate();
    if (rate > 0.0) {
        cfg.sampleRate = rate;
    }
    for (auto& analyser : analysers_) {
        analyser = std::make_unique<Analyser>(cfg);
    }
    appliedReferenceDelay_.store(baseConfig_.referenceDelaySamples, std::memory_order_release);
}

void AnalysisThread::drain() {
    // Picked up HERE, on this thread, exactly like the Locate arm below and
    // `applyPendingReferenceDelay` above: a session that started mid-drain
    // would see a meter half-built.
    applyPendingSplRequest();

    if (locateArmRequested_.exchange(false, std::memory_order_acquire)) {
        locateRouteIndex_ = requestedRouteIndex_.load(std::memory_order_relaxed);
        locateBuffer_.arm(requestedCaptureLength_.load(std::memory_order_relaxed));
        locateCapturing_ = true;
    }

    // bus_.numChannels(): the channel count this bus was last prepare()d
    // with (CaptureBus.h) -- the same bound ring()/config-consuming code
    // already trusts, not whatever the device advertised at open time.
    const RoutingPlan plan = planRouting(bus_.config(), bus_.numChannels());

    if (plan.routes.empty()) {
        // No transfer function is routed at all -- the plain single-channel
        // RTA path, exactly as before this task: analysers_[0] carries the
        // band/spectrum readouts (B3's AverageGroup is what gives every
        // OTHER index its own published output, and is out of scope here).
        drainRole(rta::platform::ChannelRole::Measurement, false);
        drainRole(rta::platform::ChannelRole::Reference, true);
        return;
    }

    // One call per distinct reference channel, not one call for the whole
    // plan: see drainPaired's own header comment for why the grouping
    // itself is the property record §11 T11 checks.
    for (int refChannel : plan.distinctReferences) {
        drainPaired(refChannel, plan);
    }
}

void AnalysisThread::drainPaired(int refChannel, const RoutingPlan& plan) {
    auto* refRing = bus_.ring(refChannel);
    if (refRing == nullptr) return;

    const std::size_t hop = channelScratch_[static_cast<std::size_t>(refChannel)].size();
    if (hop == 0) return;

    // This reference's own hop budget, intersected with every route that
    // names it -- NOT with routes naming a different reference, which is
    // the whole point of calling this once per distinct reference rather
    // than once for the whole plan. `analysers_`/`routeHopCounts_` are
    // indexed by each route's POSITION in `plan.routes` (ascending
    // measurement channel), never by `TransferRoute::tfIndex` itself --
    // see `routeHopCount`'s own header comment for why: `tfIndex` is the
    // GROUPING tag two routes here deliberately share, and using it as the
    // array slot would collide two same-reference routes onto one Analyser.
    std::size_t hops = refRing->availableToRead() / hop;
    for (const auto& route : plan.routes) {
        if (route.referenceChannel != refChannel) continue;
        auto* measurementRing = bus_.ring(route.measurementChannel);
        if (measurementRing == nullptr) {
            hops = 0;
            break;
        }
        hops = std::min(hops, measurementRing->availableToRead() / hop);
    }

    for (std::size_t i = 0; i < hops; ++i) {
        // Peek every ring in this group BEFORE discarding any of them: a
        // short read on a later one after an earlier one was already
        // discarded would drop a hop from only part of the group, the
        // misalignment PairedDrain.h documents one level down.
        auto& refScratch = channelScratch_[static_cast<std::size_t>(refChannel)];
        if (refRing->peek(refScratch) != hop) break;

        bool allPeeked = true;
        for (const auto& route : plan.routes) {
            if (route.referenceChannel != refChannel) continue;
            auto& measurementScratch =
                channelScratch_[static_cast<std::size_t>(route.measurementChannel)];
            if (bus_.ring(route.measurementChannel)->peek(measurementScratch) != hop) {
                allPeeked = false;
                break;
            }
        }
        if (!allPeeked) break;

        refRing->discard(hop);
        for (std::size_t routeIndex = 0; routeIndex < plan.routes.size(); ++routeIndex) {
            const auto& route = plan.routes[routeIndex];
            if (route.referenceChannel != refChannel) continue;
            bus_.ring(route.measurementChannel)->discard(hop);

            // THE SPL TAP, and its position is load-bearing. It sits ABOVE
            // the kMaxTransferFunctions check below, because for a route at
            // or past that cap the measurement ring has just been `discard`ed
            // and the loop `continue`s past every consumer -- a tap below the
            // check would drop those channels entirely WHILE THEIR SAMPLES
            // WERE BEING CONSUMED, producing a log short by an unknown amount
            // with NO `Gap`, because `CaptureBus::dropCount` never rises when
            // a ring is drained on purpose. That is
            // memory/a-cap-checked-on-the-drain-path-is-unchecked-on-the-
            // publish-path.md, on this same function, one release later. The
            // SPL meters are NOT `analysers_`: their array is sized by LOGGED
            // channels, and kMaxTransferFunctions is an MTW-memory policy
            // (RoutingPlan.h:25-29), not a channel-count limit.
            //
            // It reads the measurement channel's scratch, which the peek loop
            // above filled for EVERY matching route regardless of position --
            // and must not go in that loop, which can `break` on
            // `allPeeked == false` BEFORE anything is discarded, so a tap
            // there would double-count on the retry.
            feedSpl(route.measurementChannel);

            if (routeIndex >= static_cast<std::size_t>(kMaxTransferFunctions)) continue;
            const auto& measurementScratch =
                channelScratch_[static_cast<std::size_t>(route.measurementChannel)];
            analysers_[routeIndex]->pushPair(refScratch, measurementScratch);
            routeHopCounts_[routeIndex].fetch_add(1, std::memory_order_relaxed);

            // L7-DELAY task F2: feed the SAME hops just pushed to the
            // Analyser into the raw-capture accumulator, when this route is
            // the one a Locate armed -- from these exact scratch buffers,
            // never a second read of the ring (record sec.11.2: "from the
            // same hops the engine already receives").
            if (locateCapturing_ && locateRouteIndex_ >= 0 &&
                routeIndex == static_cast<std::size_t>(locateRouteIndex_)) {
                locateBuffer_.feedHop(refScratch, measurementScratch);
                if (locateBuffer_.isFull()) {
                    auto capture = std::make_shared<LocateCapture>();
                    capture->reference.assign(locateBuffer_.reference().begin(),
                                               locateBuffer_.reference().end());
                    capture->measurement.assign(locateBuffer_.measurement().begin(),
                                                 locateBuffer_.measurement().end());
                    locateCapture_.store(std::move(capture), std::memory_order_release);
                    locateCapturing_ = false;
                }
            }
        }
    }
}

void AnalysisThread::drainRole(rta::platform::ChannelRole role, bool isReference) {
    const int channel = bus_.config().firstChannelWithRole(role);
    if (channel < 0) {
        return;
    }

    // Re-fetched every call rather than cached across iterations -- see the
    // history in this function's git blame for the use-after-free that
    // motivated it; CaptureBus's rings are permanent now (CaptureBus.h's
    // class comment), but a role reassignment can still make a previously
    // valid channel index report absent, which caching would miss.
    auto* ring = bus_.ring(channel);
    if (ring == nullptr) {
        return;
    }

    auto& scratch = channelScratch_[static_cast<std::size_t>(channel)];
    const std::size_t hop = scratch.size();
    // peek() + discard(): drain in whole Analyser-sized hops, so
    // pushMeasurement()/pushReference() always see a frame-aligned chunk
    // rather than whatever fraction of a hop happened to have arrived by
    // this poll. A short remainder is left in the ring for next time.
    while (ring->peek(scratch) == hop) {
        ring->discard(hop);
        if (isReference) {
            analysers_[0]->pushReference(scratch);
        } else {
            analysers_[0]->pushMeasurement(scratch);
        }
        // Research §C4's actual requirement: "the SPL meter must sit on the
        // measurement channel's own drain, so that a session with no
        // reference still logs". In THIS path it is met exactly -- the
        // measurement channel drains on its own, with no reference to wait
        // for. In the routed path (drainPaired) it cannot be, because that
        // drain advances the measurement channel only when the reference can
        // advance with it, which is what the `Gap` there reports (SPL-R1).
        feedSpl(channel);
    }
}

void AnalysisThread::publishIfDue() {
    const std::uint32_t now = juce::Time::getMillisecondCounter();
    if (lastPublishMs_ != 0 && (now - lastPublishMs_) < kMinPublishIntervalMs) {
        return;
    }
    lastPublishMs_ = now;

    // Trap T-4: this allocation belongs here, on the analysis thread, at
    // most 20 times a second -- never in the audio callback, which is what
    // test_capture_bus.cpp's allocation test is for.
    //
    // Task F2 wires B3's AverageGroup into this call; the orchestration
    // itself lives in buildPublishedSnapshot() (AnalysisPublish.h) so this
    // file stays under the project's line cap and so that logic is
    // testable with no bus and no thread in the path.
    //
    // Corrected AGAIN (task F2, record §6): app/tests/test_average_group.cpp
    // now measures AverageGroup::publish() alone, fed REAL TransferSnapshots
    // from REAL Analysers routed through a REAL RoutingPlan (not the
    // hand-built snapshots B3's own version of this comment cited) --
    // bytes(1) = 1359, bytes(4) = 1575, bytes(8) = 1863 in that test's own
    // small fixture (bin count is fixture-specific, so the ABSOLUTE figures
    // move with fftSize; what does not move is bytes(8) - bytes(4) = 288,
    // against a bound of 4*sizeof(PositionSummary) + 4096 = 4352, because
    // that delta comes from N additional small structs, never from bin
    // count. That property holds because AverageGroup::publish() reads its
    // TransferSnapshot span straight into rta::dsp::spatialAverage, which
    // allocates only 5 vectors sized by BIN COUNT, never by member count
    // (SpatialAverage.cpp) -- the design answer record §6 gives to the
    // 2.21 MB-per-position-per-publish churn a naive N-Analyser publish
    // (a FULL app-level TransferBlock+MtwBlock per position) would
    // otherwise cost.
    //
    // What that bound deliberately does NOT cover: publishAverageGroup()
    // below also gathers each member's OWN TransferSnapshot every publish
    // (transferSnapshotForAverage(), a raw core-level read -- a few KB at
    // realistic bin counts, nothing like the app-level duplication above) --
    // an unavoidable, N-scaling, but much smaller cost every design pays,
    // measured and reported (not bounded) in the same test: bytes(1) =
    // 3343, bytes(4) = 9511, bytes(8) = 17735 in that same small fixture.
    // buildPublishedSnapshot()'s own base-Snapshot copy is a THIRD, separate
    // cost -- FIXED, sized by bins, the same every
    // publish regardless of N -- not the quantity T12 bounds.
    const RoutingPlan plan = planRouting(bus_.config(), bus_.numChannels());
    SnapshotPtr snapshot = buildPublishedSnapshot(analysers_, averageGroup_, lastGroupTfIndices_,
                                                   plan, bus_.totalDrops());
    latest_.store(std::move(snapshot), std::memory_order_release);
}

}  // namespace rta::measure
