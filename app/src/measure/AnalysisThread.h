// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (juce::Thread only). See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.4, §6 Wave D, §9 traps
// T-1 / T-3 / T-4 / T-5.
#pragma once

#include "measure/AnalysisPublish.h"
#include "measure/Analyser.h"
#include "measure/AtomicSharedPtr.h"
#include "measure/AverageGroup.h"
#include "measure/RawCaptureBuffer.h"
#include "measure/RoutingPlan.h"
#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"

#include "rta/platform/CaptureBus.h"
#include "rta/platform/Fault.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rta::measure {

/// L7-DELAY task F2's raw-capture handoff (docs/plans/2026-09-07-L7-delay-
/// impl-plan.md; record docs/dsp/2026-09-06-l7-auto-delay.md sec.11.2): two
/// spans of exactly the requested length, published once
/// `AnalysisThread::armLocateCapture`'s accumulator fills. Owned by whoever
/// reads `AnalysisThread::locateCapture()` -- a plain copy, not a view into
/// the thread's own scratch, so it stays valid after the thread moves on.
struct LocateCapture {
    std::vector<float> reference;
    std::vector<float> measurement;
};

// `kMaxTransferFunctions` -- the compile-time cap on live transfer functions --
// now lives in RoutingPlan.h (included above), because it is a routing fact the
// publish-membership sync in AnalysisPublish must also honour and that file
// cannot include this one. See its comment there for the memory arithmetic.

/// The thin, JUCE-owning wrapper plan §1.3 promises around the pure
/// `Analyser` body: drains a live `rta::platform::CaptureBus` (fed by a real
/// `rta::platform::AudioIo` device callback, or by `SyntheticInput` through
/// the same `pushFromCallback` entry point -- this class does not care
/// which) and publishes at a bounded rate.
///
/// Owns no device of its own and outlives no bus: the CALLER (eventually
/// `MainComponent`, trap T-1) must declare the `AudioIo` it reads from
/// before this member and destroy this member before that one, since this
/// thread keeps reading `bus_` until `stopThread()` returns.
class AnalysisThread final : public juce::Thread, public SnapshotSource {
public:
    /// `config` is a template: fftSize, hopSize, fraction, frequency range,
    /// window and averaging are fixed for the life of this thread.
    /// `config.sampleRate` is only the STARTING rate -- every time
    /// `bus.epoch()` changes (a device open or reconfiguration; see
    /// `CaptureBus::prepare`), the internal `Analyser` is rebuilt at
    /// `bus.sampleRate()` instead, and the fresh `Analyser` (no buffered
    /// samples, no running average) is itself the drain on this side of the
    /// bus -- the `RingBuffer`s CaptureBus::prepare() just reset (never
    /// rebuilt; see `CaptureBus.h`) are already empty.
    ///
    /// Starts the thread immediately; there is no separate `start()`.
    AnalysisThread(rta::platform::CaptureBus& bus, const Analyser::Config& config);

    /// Trap T-1: `stopThread(2000)` here, in this class's OWN destructor --
    /// not only relying on the owner's member-declaration order. A thread
    /// still inside `run()` when `bus_` (owned by a sibling member) is
    /// destroyed out from under it is a crash that happens only at
    /// shutdown, on a customer's machine, once.
    ~AnalysisThread() override;

    AnalysisThread(const AnalysisThread&) = delete;
    AnalysisThread& operator=(const AnalysisThread&) = delete;

    /// Safe from any thread (trap T-5: writer = this thread, reader = the
    /// message thread, nothing else).
    [[nodiscard]] SnapshotPtr latest() const override;

    /// The last exception `run()` caught, or `Fault::Kind::None`. Mirrors
    /// `AudioIo::lastFault()`'s mutex-guarded shape (trap T-8: a
    /// `std::string`, never a `juce::String`, crosses this boundary).
    [[nodiscard]] rta::platform::Fault lastFault() const;

    /// How many paired hops the route currently at position `routeIndex` of
    /// the live `RoutingPlan` has been fed, or 0 for an out-of-range index.
    ///
    /// Deliberately keyed by ROUTE POSITION (ascending measurement channel,
    /// the same order `planRouting` returns), not by `TransferRoute::tfIndex`
    /// directly: `tfIndex` is the GROUPING tag two routes share on purpose
    /// when they name the same reference channel (record §6), so using it as
    /// the `analysers_` slot would make two same-reference routes collide on
    /// one Analyser instead of getting one each -- exactly the "N Analysers"
    /// property this task exists to provide. Position in `plan.routes` is
    /// always distinct per measurement channel, which is what `analysers_`
    /// is actually indexed by (see `drainPaired`'s own comment).
    ///
    /// An `std::atomic` counter this class owns and increments itself (task
    /// B2/T11), NOT `Analyser::framesAnalysed()` -- that is a plain,
    /// unsynchronised counter written only by this thread, and reading it
    /// from another thread with no atomic between them would be a data race
    /// even though a stale value would look harmless. Safe from any thread
    /// for the same reason `latest()` is.
    [[nodiscard]] std::uint64_t routeHopCount(int routeIndex) const noexcept;

    /// L7-DELAY task F2 (record sec.11.2). Arms the raw-capture accumulator
    /// for route `routeIndex` at `length` samples -- message-thread call,
    /// picked up on the NEXT `drain()`, never inside the audio callback (the
    /// accumulator lives entirely on this thread; see RawCaptureBuffer.h).
    /// Overwrites any capture already in progress or already published and
    /// not yet read: a second Locate before the first is consumed simply
    /// restarts it, no queue -- this thread runs exactly one Locate at a
    /// time, matching `DelayLocator`'s own one-shot shape.
    void armLocateCapture(int routeIndex, std::size_t length) noexcept;

    /// The most recently COMPLETED capture, or nullptr before the first one
    /// finishes filling (or right after a fresh `armLocateCapture` clears
    /// the previous result). Safe from any thread -- same atomic-pointer-
    /// swap shape as `latest()`. The caller (`MainComponent`) is expected to
    /// compare the returned pointer's identity against the last one it
    /// handled, since this always reports the latest capture, published
    /// exactly once per Locate.
    [[nodiscard]] std::shared_ptr<const LocateCapture> locateCapture() const noexcept;

    /// L7-DELAY task F2 (record sec.1.6, sec.8): an Apply is an explicit
    /// engine rebuild, never a live nudge -- picked up on the next
    /// `drain()`, rebuilding every `Analyser` (same "every position
    /// together" rule `rebuildAnalysersIfEpochChanged` already follows) with
    /// `Config::referenceDelaySamples = delaySamples`. Per-route delay
    /// compensation and its persistence are explicitly deferred by the plan
    /// (record sec.13: "already a [tf] field in L6b schema 3") -- this
    /// session-only, whole-`baseConfig_` form is exactly that plan's scope,
    /// not a shortcut past it.
    void applyReferenceDelay(int delaySamples) noexcept;

    /// The `referenceDelaySamples` the live `Analyser`s were LAST rebuilt
    /// with -- 0 until the first `applyReferenceDelay`. Safe from any
    /// thread; what a caller polls to confirm an Apply has taken effect.
    [[nodiscard]] int appliedReferenceDelaySamples() const noexcept;

private:
    /// Trap T-3: `SpectrumEngine::process` (reached through
    /// `Analyser::pushMeasurement` / `pushReference`) can throw. An
    /// exception escaping `juce::Thread::run()` terminates the process --
    /// during a show. This override is the whole of the catch; the real
    /// loop is `runBody()`.
    void run() override;
    void runBody();

    void rebuildAnalysersIfEpochChanged();
    void applyPendingReferenceDelay();
    void drain();
    /// Peeks `refChannel`'s ring and every route in `plan.routes` naming it
    /// as `referenceChannel`, discarding NOTHING until every one of those
    /// peeks has succeeded for the hop about to be consumed (PairedDrain.h's
    /// precedent, one level up: a short read on any ring in the group after
    /// an earlier one was already discarded would misalign that whole
    /// group). Two routes sharing `refChannel` are therefore always driven
    /// the SAME number of hops in the same call (R6's "identical reference
    /// hops" case); a route naming a DIFFERENT reference channel is driven
    /// by a separate call to this function, with its own independently
    /// computed hop count (R6's "independent counters" case) -- the grouping
    /// is what makes the two halves of T11 both true from the same drain.
    void drainPaired(int refChannel, const RoutingPlan& plan);
    void drainRole(rta::platform::ChannelRole role, bool isReference);
    void publishIfDue();
    void recordFault(rta::platform::Fault::Kind kind, const std::string& message);

    rta::platform::CaptureBus& bus_;
    Analyser::Config baseConfig_;
    /// Built once, at `kMaxTransferFunctions`, and never resized -- see that
    /// constant's own comment for why. `analysers_[0]` also serves the plain
    /// single-channel RTA path (drainRole, publishIfDue) when no route
    /// exists at all, exactly as the single `analyser_` this replaces did;
    /// B3's `AverageGroup` is what gives every OTHER index its own published
    /// output, and is out of scope here.
    std::vector<std::unique_ptr<Analyser>> analysers_;
    std::uint64_t lastEpoch_ = 0;

    /// One hop's worth of scratch PER CHANNEL, allocated once here rather
    /// than per drain call (T-4: the analysis thread may allocate, but there
    /// is no reason to here). Indexed by channel number directly -- simpler
    /// than tracking which channels are "in use" this hop, and
    /// `kMaxChannels` vectors of `hopSize` floats is a few hundred KB, paid
    /// once at construction, never during a live drain.
    std::array<std::vector<float>, static_cast<std::size_t>(rta::platform::kMaxChannels)>
        channelScratch_;

    /// Paired-hop count per transfer function, read back by `routeHopCount`.
    std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(kMaxTransferFunctions)>
        routeHopCounts_{};

    /// AtomicSharedPtr rather than a bare std::atomic over the shared_ptr --
    /// see measure/AtomicSharedPtr.h for why the C++20 specialisation cannot
    /// be named directly. The publish is still one pointer swap; no lock
    /// moved onto this path.
    AtomicSharedPtr<const Snapshot> latest_;
    std::uint32_t lastPublishMs_ = 0;

    /// Task F2 (record §6): the one live group this thread publishes.
    /// Written and read from this thread ALONE (`publishIfDue`'s own call to
    /// `syncAverageGroupMembership`/`publishAverageGroup`, both in
    /// AnalysisPublish.h) -- there is no cross-thread accessor, so no lock
    /// is needed the way `faultLock_` below needs one for a value the
    /// message thread also reads.
    AverageGroup averageGroup_;
    /// `syncAverageGroupMembership`'s own memory of what it last built
    /// `averageGroup_` from -- see that function's header comment.
    std::vector<int> lastGroupTfIndices_;

    mutable std::mutex faultLock_;
    rta::platform::Fault fault_;

    // --- L7-DELAY task F2: raw-capture handoff and Apply ------------------
    // This thread's OWN accumulator, never the callback's (RawCaptureBuffer.h).
    RawCaptureBuffer locateBuffer_;
    int locateRouteIndex_ = -1;     // analysis-thread-local once armed
    bool locateCapturing_ = false;  // ditto
    std::atomic<int> requestedRouteIndex_{-1};
    std::atomic<std::size_t> requestedCaptureLength_{0};
    std::atomic<bool> locateArmRequested_{false};
    /// Same substitution, same reason (measure/AtomicSharedPtr.h): this one
    /// is written by the analysis thread when a Locate capture fills and read
    /// by the message thread that polls `locateCapture()`.
    AtomicSharedPtr<const LocateCapture> locateCapture_;

    std::atomic<int> pendingReferenceDelay_{0};
    std::atomic<bool> applyDelayRequested_{false};
    std::atomic<int> appliedReferenceDelay_{0};
};

}  // namespace rta::measure
