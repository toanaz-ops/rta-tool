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
//
// H12 T4: the same job also runs a TWO-file pair (record docs/dsp/2026-11-10-g22b-two-file-wav-drift.md
// E2, E9). The worker opens both files, takes the offset-search prior from their `bext` time
// references when BOTH carry one, and calls `analyseWavPairTransfer`.
#pragma once

#include "measure/WavPairTransfer.h"
#include "measure/WavTransfer.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <future>
#include <memory>
#include <optional>

namespace rta::measure {

/// The offset-search prior (measurement-file sample index of reference sample 0, samples) from the
/// two `bext` time references: reference minus measurement when BOTH files carry one, else 0 (record
/// E2: one reference alone says nothing about the other recorder's clock).
///
/// Sign. `ClockMapping` t(n) = offset + n * ratio maps a REFERENCE index to a MEASUREMENT index. A
/// time reference is the wall-clock sample count of a file's FIRST sample. If the measurement began
/// later by D (T_m = T_r + D), reference sample n is measurement sample n - D, so offset = T_r - T_m.
[[nodiscard]] inline std::int64_t bextPriorSamples(std::optional<std::int64_t> reference,
                                                   std::optional<std::int64_t> measurement) noexcept {
    return reference.has_value() && measurement.has_value() ? *reference - *measurement : 0;
}

/// A two-file request. `request.clock.priorOffsetSamples` is overwritten by the worker.
struct WavPairJobRequest {
    juce::File reference;
    juce::File measurement;
    int referenceChannel = 0;  ///< 0-based, in the reference file
    int measurementChannel = 0;  ///< 0-based, in the measurement file
    WavPairRequest request;
};

/// What the worker returns: the one-file outcome, or the pair's.
struct WavJobOutcome {
    bool pairMode = false;
    WavTransferOutcome single;
    WavPairOutcome pair;

    [[nodiscard]] bool ok() const noexcept { return pairMode ? pair.ok() : single.ok(); }
    [[nodiscard]] WavTransferOutcome& transfer() noexcept { return pairMode ? pair.transfer : single; }
};

class WavTransferJob {
public:
    WavTransferJob() = default;
    ~WavTransferJob();
    WavTransferJob(const WavTransferJob&) = delete;
    WavTransferJob& operator=(const WavTransferJob&) = delete;

    /// Cancels any running job, then launches this one.
    void start(const juce::File& file, WavTransferRequest request);

    /// Cancels any running job, then launches a two-file pair.
    void start(WavPairJobRequest job);

    /// The finished outcome of the newest request, once; empty while it runs or
    /// when nothing is pending.
    [[nodiscard]] std::optional<WavJobOutcome> poll();

    [[nodiscard]] bool busy() const noexcept { return running_.valid(); }

    /// Stops the running job and drops its answer (a new file, or destruction).
    void cancelAndDiscard();

private:
    std::future<WavJobOutcome> running_;
    std::shared_ptr<std::atomic<bool>> cancel_;
};

}  // namespace rta::measure
