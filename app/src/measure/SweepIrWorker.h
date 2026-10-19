// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// SWEEP lane task T4 (plan "Worker thread"). One `std::thread` per job: a
// sweep is pressed by hand, so there is never a queue, and a pool would only
// add a lifetime to get wrong. The job's 2^21-point transforms take about a
// second -- far too long for the message thread -- and the result is handed
// back the way every other thread in this app hands back a result: an
// immutable object published through `AtomicSharedPtr`, polled by the 2 Hz
// timer (AnalysisThread.h:373). This header includes no `rta/ir/*`; the
// deconvolver stays inside SweepIrPipeline.cpp.
//
// Declare it AFTER every reference its `analyse` function captures (trap
// T-1): the destructor joins, so it must run before those references die.
#pragma once

#include "measure/AtomicSharedPtr.h"
#include "measure/SweepIrOutcome.h"
#include "measure/SweepIrPipeline.h"

#include <functional>
#include <memory>
#include <thread>

namespace rta::measure {

class SweepIrWorker {
public:
    using Analyse = std::function<SweepIrOutcome(const SweepCapture&, const SweepIrRequest&)>;

    /// An empty `analyse` means `analyseSweepCapture`. The parameter exists so
    /// a test can block the job on a latch -- see test_sweep_ir_worker.cpp.
    explicit SweepIrWorker(Analyse analyse = {});

    /// Joins a running job: shutdown waits at most one analysis (about 1 s).
    ~SweepIrWorker();

    SweepIrWorker(const SweepIrWorker&) = delete;
    SweepIrWorker& operator=(const SweepIrWorker&) = delete;
    SweepIrWorker(SweepIrWorker&&) = delete;
    SweepIrWorker& operator=(SweepIrWorker&&) = delete;

    /// Message thread. Takes the capture BY VALUE: the thread owns its input,
    /// so nothing the caller does afterwards can reach it. Clears the previous
    /// result, so `latest()` is null until THIS job finishes, then returns
    /// without waiting. A job already running is joined first (the caller's
    /// state machine does not allow that, so this is the second line, not the
    /// first).
    void start(SweepCapture capture, SweepIrRequest request);

    /// Null while a job runs (or none has). Message thread.
    [[nodiscard]] std::shared_ptr<const SweepIrOutcome> latest() const noexcept {
        return result_.load();
    }

private:
    void join();

    Analyse analyse_;
    AtomicSharedPtr<const SweepIrOutcome> result_;
    std::thread thread_;
};

}  // namespace rta::measure
