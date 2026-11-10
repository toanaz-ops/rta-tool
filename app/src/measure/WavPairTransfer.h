// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE: both files arrive as
// `SampleBlockSource`s the JUCE side implements.
//
// H12 T3 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E1, E8-E10). A reference
// recorded in one file and a measurement in another: find the offset and the
// clock ratio, resample the measurement onto the reference clock, then run G22's
// `analyseWavTransfer` on the corrected pair UNCHANGED.
#pragma once

#include "measure/WavPairClock.h"
#include "measure/WavTransfer.h"

#include <atomic>
#include <optional>
#include <string>

namespace rta::measure {

struct WavPairRequest {
    double startSeconds = 0.0;   ///< on the REFERENCE file's timeline
    double lengthSeconds = 0.0;  ///< 0 = to the end of the reference file
    std::string referenceFileName;    ///< basenames only; they ride in CaptureMeta::deviceName
    std::string measurementFileName;
    ClockSearchOptions clock;
};

/// Refusals decided before the clock is estimated, plus the range against the
/// overlap once it is. Clock and G22 refusals live in their own outcomes.
enum class WavPairRefusal {
    SampleRateMismatch,  ///< E1
    BadSampleRate,
    ChannelOutOfRange,
    RangeOutsideFile,    ///< the range does not fit in the part where both files overlap
};

struct WavPairOutcome {
    std::optional<WavPairRefusal> refusal;
    ClockResult clock;          ///< the estimate, or the clock refusal with its numbers
    WavTransferOutcome transfer;  ///< G22's outcome on the corrected pair (trace or refusal)

    // Echo (0-based channels) so a sentence can name the real files and channels.
    std::string referenceFileName;
    std::string measurementFileName;
    int referenceChannel = 0;
    int measurementChannel = 0;
    int referenceFileChannels = 0;
    int measurementFileChannels = 0;
    bool referenceChannelBad = false;  ///< ChannelOutOfRange: which file's channel it was
    double referenceRate = 0.0;
    double measurementRate = 0.0;
    double requestedStartSeconds = 0.0;
    double requestedLengthSeconds = 0.0;
    /// RangeOutsideFile: the stretch of the reference timeline where both files
    /// have samples (before the clock is known, the whole reference file).
    double overlapFirstSeconds = 0.0;
    double overlapLastSeconds = 0.0;

    // Success (and the readout, record E9).
    double ppm = 0.0;
    double offsetSeconds = 0.0;
    bool sharedClock = false;
    double residualRms = 0.0;

    [[nodiscard]] bool ok() const noexcept { return transfer.ok(); }
};

[[nodiscard]] WavPairOutcome analyseWavPairTransfer(const SampleBlockSource& reference, int referenceChannel,
                                                    const SampleBlockSource& measurement,
                                                    int measurementChannel, const WavPairRequest& request,
                                                    const std::atomic<bool>& cancel);

/// One English sentence for whatever refused the pair, with the measured numbers
/// (ppm and seconds two and three decimals, samples two decimals, trust two
/// decimals). Empty on success.
[[nodiscard]] std::string wavPairRefusalText(const WavPairOutcome& outcome);

}  // namespace rta::measure
