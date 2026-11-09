// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// H10 (docs/plans/2026-11-10-h10-g24-excess-phase-display-plan.md, decision 2):
// the one thread the G24 decomposition of a gated response may run on.
//
// MEASURED on this machine (Release, 8193 bins, factor 128, one call of
// `excessPhaseFor`): 526 / 561 / 603 / 532 ms -- ten times the plan's 50 ms
// limit, and a 2^21-point transform the audio thread must never see. So the
// message thread only submits and polls.
//
// One job at a time, newest request wins: `submit` while one runs queues the
// request (replacing any queued one), and `invalidate` drops whatever is queued
// and makes the running job's answer stale. A stale answer is computed and then
// thrown away at `poll` -- never shown -- by comparing its ticket with the
// newest one. Every member is touched from the message thread only; the worker
// owns its input by value and returns its output through the future.
//
// Destruction waits for a running job (std::async's future blocks), so the
// owner can be destroyed without a flag or a join of its own: at most one call,
// about half a second, on exit.
#pragma once

#include "measure/IrGateModel.h"

#include <chrono>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rta::measure {

class ExcessPhaseJob {
public:
    struct Input {
        std::vector<std::complex<float>> bins;  ///< the gated spectrum, 2^k + 1 bins
        double sampleRate = 0.0;
        double fMinHz = 0.0;
        double trustedHighHz = 0.0;
    };

    /// Starts `input` now, or queues it behind the running job.
    void submit(Input input) {
        ++ticket_;
        if (running_.valid()) {
            queued_ = std::make_pair(std::move(input), ticket_);
            return;
        }
        start(std::move(input), ticket_);
    }

    /// The answer to whatever was submitted so far is no longer wanted.
    void invalidate() {
        ++ticket_;
        queued_.reset();
    }

    /// The finished result of the newest submission, once; null while it runs,
    /// after it was taken, or when it went stale.
    [[nodiscard]] std::shared_ptr<const IrExcessPhase> poll() {
        if (!running_.valid() || running_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return nullptr;
        auto done = running_.get();
        if (queued_) {
            auto next = std::move(*queued_);
            queued_.reset();
            start(std::move(next.first), next.second);
        }
        return done.first == ticket_ ? done.second : nullptr;
    }

    /// How many decompositions have actually begun (a test's "did it recompute").
    [[nodiscard]] std::size_t startedCountForTest() const noexcept { return started_; }

private:
    using Done = std::pair<std::uint64_t, std::shared_ptr<const IrExcessPhase>>;

    void start(Input input, std::uint64_t ticket) {
        ++started_;
        running_ = std::async(std::launch::async, [in = std::move(input), ticket]() -> Done {
            // Nothing may escape through the future: poll() would rethrow on the message thread.
            IrExcessPhase out;
            try {
                out = excessPhaseFor(in.bins, in.sampleRate, in.fMinHz, in.trustedHighHz);
            } catch (const std::exception& e) {
                out = IrExcessPhase{};
                out.refusal = std::string("excess phase failed: ") + e.what();
            }
            return {ticket, std::make_shared<const IrExcessPhase>(std::move(out))};
        });
    }

    std::future<Done> running_;
    std::optional<std::pair<Input, std::uint64_t>> queued_;
    std::uint64_t ticket_ = 0;
    std::size_t started_ = 0;
};

}  // namespace rta::measure
