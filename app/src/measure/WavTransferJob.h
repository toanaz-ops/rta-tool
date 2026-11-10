// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. H11 W3 (record D7): the one thread a WAV
// transfer analysis may run on. Never the live `AnalysisThread`.
//
// Same shape as ExcessPhaseJob: `std::async`, newest request wins, results
// returned through the future and polled on the message thread. A new `start`
// raises the running job's cancel flag and waits for it, so an older answer can
// never be taken for the newest one. The flag is checked between 65536-sample
// averaging blocks and between delay windows; the uncancellable unit is one 2^18-sample
// window-pair read plus one PHAT, so start(), LOAD and the destructor can block the
// message thread for that long. The worker opens ITS OWN `WavFileSource`; no reader is ever
// shared with the message thread. Destruction cancels and waits.
#pragma once

#include "measure/WavTransfer.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <future>
#include <memory>
#include <optional>

namespace rta::measure {

class WavTransferJob {
public:
    WavTransferJob() = default;
    ~WavTransferJob();
    WavTransferJob(const WavTransferJob&) = delete;
    WavTransferJob& operator=(const WavTransferJob&) = delete;

    /// Cancels any running job, then launches this one.
    void start(const juce::File& file, WavTransferRequest request);

    /// The finished outcome of the newest request, once; empty while it runs or
    /// when nothing is pending.
    [[nodiscard]] std::optional<WavTransferOutcome> poll();

    [[nodiscard]] bool busy() const noexcept { return running_.valid(); }

    /// Stops the running job and drops its answer (a new file, or destruction).
    void cancelAndDiscard();

private:
    std::future<WavTransferOutcome> running_;
    std::shared_ptr<std::atomic<bool>> cancel_;
};

}  // namespace rta::measure
