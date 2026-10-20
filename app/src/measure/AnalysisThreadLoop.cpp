// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. AnalysisThread's thread loop, split off
// AnalysisThread.cpp (400-line cap) along the real seam: this file decides WHEN
// things run each tick; AnalysisThread.cpp is what they do. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md section 3.4.
#include "measure/AnalysisThread.h"

#include <exception>
#include <stdexcept>

namespace rta::measure {

namespace {

// Poll granularity: how often run() wakes to check for new samples even
// with nothing to do. Short enough that a hop's worth of audio at any
// reasonable buffer size is never left waiting more than one tick.
constexpr int kPollMs = 10;


}  // namespace

void AnalysisThread::run() {
    // Trap T-3, verbatim: wrap the whole body, record the message as a
    // fault, stop cleanly. Nothing below this point may let an exception
    // reach juce::Thread's caller.
    try {
        runBody();
    } catch (const std::exception& e) {
        recordFault(rta::platform::Fault::Kind::DeviceError, e.what());
        bus_.detachConsumer();
    } catch (...) {
        recordFault(rta::platform::Fault::Kind::DeviceError,
                     "AnalysisThread::run: unknown exception");
        bus_.detachConsumer();
    }
    // Detached on a fault: this thread is gone, and a gate raised by the next
    // prepare() would never be lowered -- the callback would write nothing
    // for the rest of the process. Detached, prepare() raises no gate.
}

void AnalysisThread::runBody() {
    while (!threadShouldExit()) {
        wait(kPollMs);
        if (threadShouldExit()) {
            return;
        }
        if (throwNextTick_.exchange(false, std::memory_order_acquire)) {
            throw std::runtime_error("injected analysis fault");
        }

        // Before any read of this tick: empties every ring behind the
        // producer gate after a prepare() or a change of written channels.
        // Never in prepare() itself: only this thread may write a read index.
        bus_.serviceResyncAsConsumer();
        rebuildAnalysersIfEpochChanged();
        applyPendingReferenceDelay();

        // While the gate is up and not yet acknowledged the rings still hold
        // the previous session: reading them now would splice it into the
        // fresh Analysers. They are emptied, not read.
        if (!bus_.resyncPending()) {
            drain();
        }

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
    // The ring side of this reset is runBody()'s serviceResyncAsConsumer(),
    // gated on the bus's resync flag rather than on this epoch. The fresh
    // Analysers below are the analysis-side drain; every position is rebuilt
    // together, since a device reconfiguration invalidates every engine.
    for (auto& analyser : analysers_) {
        analyser = std::make_unique<Analyser>(cfg);
    }
}

}  // namespace rta::measure
