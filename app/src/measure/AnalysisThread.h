// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. JUCE (juce::Thread only). See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.4, §6 Wave D, §9 traps
// T-1 / T-3 / T-4 / T-5.
#pragma once

#include "measure/Analyser.h"
#include "measure/Snapshot.h"
#include "measure/SnapshotSource.h"

#include "rta/platform/CaptureBus.h"
#include "rta/platform/Fault.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rta::measure {

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

private:
    /// Trap T-3: `SpectrumEngine::process` (reached through
    /// `Analyser::pushMeasurement` / `pushReference`) can throw. An
    /// exception escaping `juce::Thread::run()` terminates the process --
    /// during a show. This override is the whole of the catch; the real
    /// loop is `runBody()`.
    void run() override;
    void runBody();

    void rebuildAnalyserIfEpochChanged();
    void drainRole(rta::platform::ChannelRole role, bool isReference);
    void publishIfDue();
    void recordFault(rta::platform::Fault::Kind kind, const std::string& message);

    rta::platform::CaptureBus& bus_;
    Analyser::Config baseConfig_;
    std::unique_ptr<Analyser> analyser_;
    std::uint64_t lastEpoch_ = 0;

    /// Exactly one hop's worth of scratch, allocated once here rather than
    /// per drain call -- the analysis thread may allocate (T-4 says so
    /// explicitly for `publish()`), but there is no reason to here.
    std::vector<float> hopScratch_;

    std::atomic<SnapshotPtr> latest_;
    std::uint32_t lastPublishMs_ = 0;

    mutable std::mutex faultLock_;
    rta::platform::Fault fault_;
};

}  // namespace rta::measure
