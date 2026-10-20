// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// RT60-UI lane wave B, decision 11 (docs/plans/2026-10-20-rt60-ui-lane-plan.md):
// the one thread the RT60 analysis may run on. Wave A analysed one IR in octaves
// on the message thread (measured by test_rt60_analysis, which prints the time);
// wave B's work -- third-octave bands, and an ensemble of up to eight captures --
// is three to thirty times that, so it goes here and the message thread only
// polls.
//
// One worker thread, one job at a time, newest request wins: `submit` cancels the
// job that is running (the analysis checks the flag between bands) and replaces
// anything queued, so a quick series of toggles costs one analysis, not one each.
// The thread owns shared_ptr copies of every IR it reads; it writes only the
// outcome slot under a mutex the message thread also takes, and only to take.
//
// Shutdown is the destructor's: set the stop and cancel flags, wake the thread,
// join. The join is bounded by one band's analysis (the flag is read between
// bands), not by the job. `this` is safe to capture because of that join, and the
// thread is declared LAST so it is also the first member to be destroyed.
#pragma once

#include "measure/Rt60Analysis.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace rta::measure {

class Rt60Worker {
public:
    struct Request {
        std::uint64_t ticket = 0;
        Rt60BandSet set = Rt60BandSet::Octave;
        /// The trace the pane is plotting: analysed on its own, for the table and
        /// the EDC plot. Null when only an ensemble is wanted.
        std::shared_ptr<const SweepIr> primary;
        /// Two or more captures: analysed together. Fewer means no ensemble.
        std::vector<std::shared_ptr<const SweepIr>> ensemble;
    };

    struct Outcome {
        std::uint64_t ticket = 0;
        std::shared_ptr<const Rt60Result> single;
        std::shared_ptr<const Rt60EnsembleResult> ensemble;
    };

    Rt60Worker() = default;
    ~Rt60Worker();
    Rt60Worker(const Rt60Worker&) = delete;
    Rt60Worker& operator=(const Rt60Worker&) = delete;

    void submit(Request request);

    /// The finished outcome of the newest job, once; empty while it runs or after
    /// it was taken. A job superseded before it finished never appears.
    [[nodiscard]] std::optional<Outcome> poll();

    /// True from `submit` until the outcome is taken by `poll`.
    [[nodiscard]] bool busy() const;

private:
    void loop();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Request> pending_;
    std::optional<Outcome> done_;
    std::uint64_t newestTicket_ = 0;
    bool running_ = false;
    bool stop_ = false;
    std::atomic<bool> cancel_{false};
    std::thread thread_;  // LAST: joined first on destruction
};

}  // namespace rta::measure
