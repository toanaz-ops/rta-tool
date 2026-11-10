// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE: the file reader is an interface
// (`SampleBlockSource`) the JUCE side implements (W3), so every claim here is
// provable on CI with an in-memory source.
//
// H11 W2 (docs/dsp/2026-11-10-g22-offline-wav-transfer.md D1-D6, D9). One
// recording, two of its channels, one chosen range -> one stored transfer trace.
#pragma once

#include "trace/Trace.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace rta::measure {

/// Where the samples come from. Both channels of ONE file, so they share a
/// clock by construction (record D1). `read` fills `out` with
/// `out.size()` consecutive samples of `channel` from `startSample`; false is a
/// read failure, never a short read.
class SampleBlockSource {
public:
    virtual ~SampleBlockSource() = default;
    [[nodiscard]] virtual int numChannels() const = 0;
    [[nodiscard]] virtual double sampleRate() const = 0;
    [[nodiscard]] virtual std::int64_t lengthSamples() const = 0;
    [[nodiscard]] virtual bool read(int channel, std::int64_t startSample, std::span<float> out) const = 0;
};

struct WavTransferRequest {
    int referenceChannel = 0;    ///< 0-based
    int measurementChannel = 1;  ///< 0-based
    double startSeconds = 0.0;
    double lengthSeconds = 0.0;  ///< 0 = to the end of the file
    std::string fileName;        ///< basename only; rides in CaptureMeta::deviceName (record D6)
};

enum class WavTransferRefusal {
    NotEnoughChannels,
    SameChannel,
    ChannelOutOfRange,
    BadSampleRate,
    RangeOutsideFile,
    RangeTooShort,
    NoCorrelation,
    DelayMoved,
    ReadFailed,
    Cancelled,
};

/// Either a trace or a refusal, plus every measured number a refusal sentence
/// or the readout prints. Fields a given outcome did not measure stay at 0.
struct WavTransferOutcome {
    std::optional<rta::trace::Trace> trace;
    std::optional<WavTransferRefusal> refusal;

    // Request echo (0-based channels, so the sentence can say ch1-based).
    int referenceChannel = 0;
    int measurementChannel = 0;
    int fileChannels = 0;
    double sampleRate = 0.0;
    double fileLengthSeconds = 0.0;
    double requestedStartSeconds = 0.0;
    double requestedLengthSeconds = 0.0;

    // Success (and the readout).
    int delaySamples = 0;       ///< applied bulk delay D (positive: measurement lags)
    double trust = 0.0;         ///< of the window D came from
    std::size_t frames = 0;
    double effectiveAverages = 0.0;

    // RangeTooShort.
    double reachableAverages = 0.0;
    double neededAverages = 0.0;
    int discardedSamples = 0;   ///< |D| the engine throws away (0 for the pre-check)

    // NoCorrelation: the best window's trust, and the trust it needed.
    double bestTrust = 0.0;
    double trustFloor = 0.0;

    // DelayMoved: the earlier and the later window that disagree.
    int delayFirst = 0;
    int delayLast = 0;

    [[nodiscard]] bool ok() const noexcept { return trace.has_value(); }
};

/// "good" / "fair" / "poor", the words Locate uses (MainComponentDelay.cpp).
[[nodiscard]] const char* wavTransferTrustWord(double trust) noexcept;

/// Blocks until done (a long file is many seconds of reading): the caller runs
/// it on a worker. `cancel` is polled between blocks; a cancelled run returns
/// `Cancelled` and no trace.
[[nodiscard]] WavTransferOutcome analyseWavTransfer(const SampleBlockSource& source,
                                                    const WavTransferRequest& request,
                                                    const std::atomic<bool>& cancel);

/// One English sentence for a refusal, with the measured numbers: dB one
/// decimal, ms one decimal, Hz whole, delays in whole samples (D9). Empty when
/// the outcome is a success.
[[nodiscard]] std::string wavTransferRefusalText(const WavTransferOutcome& outcome);

}  // namespace rta::measure
