// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/WavPairTransfer.h"

#include "measure/ResampledPairSource.h"

#include <cmath>

namespace rta::measure {

WavPairOutcome analyseWavPairTransfer(const SampleBlockSource& reference, int referenceChannel,
                                      const SampleBlockSource& measurement, int measurementChannel,
                                      const WavPairRequest& request, const std::atomic<bool>& cancel) {
    WavPairOutcome out;
    out.referenceFileName = request.referenceFileName;
    out.measurementFileName = request.measurementFileName;
    out.referenceChannel = referenceChannel;
    out.measurementChannel = measurementChannel;
    out.referenceFileChannels = reference.numChannels();
    out.measurementFileChannels = measurement.numChannels();
    out.referenceRate = reference.sampleRate();
    out.measurementRate = measurement.sampleRate();
    out.requestedStartSeconds = request.startSeconds;
    out.requestedLengthSeconds = request.lengthSeconds;
    const auto refuse = [&](WavPairRefusal why) {
        out.refusal = why;
        return out;
    };

    // --- validation, cheapest first (E1) -------------------------------------
    const double fs = reference.sampleRate();
    if (!(fs > 0.0) || !std::isfinite(fs) || !(measurement.sampleRate() > 0.0) ||
        !std::isfinite(measurement.sampleRate())) {
        return refuse(WavPairRefusal::BadSampleRate);
    }
    if (reference.sampleRate() != measurement.sampleRate()) return refuse(WavPairRefusal::SampleRateMismatch);
    // No SameChannel check: two different files may both be "ch1".
    if (referenceChannel < 0 || referenceChannel >= reference.numChannels()) {
        out.referenceChannelBad = true;
        return refuse(WavPairRefusal::ChannelOutOfRange);
    }
    if (measurementChannel < 0 || measurementChannel >= measurement.numChannels()) {
        return refuse(WavPairRefusal::ChannelOutOfRange);
    }

    // --- the range on the reference timeline, before any read ----------------
    const std::int64_t refLength = reference.lengthSamples();
    out.overlapFirstSeconds = 0.0;
    out.overlapLastSeconds = static_cast<double>(refLength) / fs;
    if (!(request.startSeconds >= 0.0) || !(request.lengthSeconds >= 0.0)) {
        return refuse(WavPairRefusal::RangeOutsideFile);
    }
    const std::int64_t rangeStart = std::llround(request.startSeconds * fs);
    // LENGTH 0 = to the end of the part both files cover: only the clock stage knows
    // where that is (the overlap moves with the offset), so it resolves the length.
    const bool toEnd = !(request.lengthSeconds > 0.0);
    std::int64_t rangeLength = toEnd ? 0 : std::llround(request.lengthSeconds * fs);
    if (rangeStart >= refLength || (!toEnd && (rangeLength <= 0 || rangeStart + rangeLength > refLength))) {
        return refuse(WavPairRefusal::RangeOutsideFile);
    }

    // --- E2-E7 ---------------------------------------------------------------
    out.clock = estimateClockMapping(reference, referenceChannel, measurement, measurementChannel, rangeStart,
                                     rangeLength, request.clock, cancel);
    if (out.clock.refusal == ClockRefusal::RangeOutsideOverlap) {
        out.clock.refusal.reset();
        out.overlapFirstSeconds = static_cast<double>(out.clock.overlapFirst) / fs;
        out.overlapLastSeconds = static_cast<double>(out.clock.overlapLast + 1) / fs;
        if (out.clock.overlapLast < out.clock.overlapFirst) out.overlapFirstSeconds = out.overlapLastSeconds = 0.0;
        return refuse(WavPairRefusal::RangeOutsideFile);
    }
    if (!out.clock.ok()) return out;
    const ClockEstimate& estimate = *out.clock.estimate;
    rangeLength = estimate.rangeLength;
    out.ppm = estimate.ppm;
    out.offsetSeconds = estimate.offsetSeconds;
    out.sharedClock = estimate.sharedClock;
    out.residualRms = estimate.residualRms;

    // --- the range against the overlap the clock leaves (never a ReadFailed) --
    const ResampledPairSource pair(reference, referenceChannel, measurement, measurementChannel,
                                   estimate.mapping);
    const auto valid = pair.validRange();
    out.overlapFirstSeconds = valid.empty() ? 0.0 : static_cast<double>(valid.first) / fs;
    out.overlapLastSeconds = valid.empty() ? 0.0 : static_cast<double>(valid.last + 1) / fs;
    if (valid.empty() || rangeStart < valid.first || rangeStart + rangeLength - 1 > valid.last) {
        return refuse(WavPairRefusal::RangeOutsideFile);
    }

    // --- E8: G22 on the corrected pair, unchanged, naming the REAL files -----
    WavTransferRequest transfer;
    transfer.referenceChannel = 0;
    transfer.measurementChannel = 1;
    transfer.startSeconds = request.startSeconds;
    transfer.lengthSeconds = toEnd ? static_cast<double>(rangeLength) / fs : request.lengthSeconds;
    transfer.fileName = request.measurementFileName;
    transfer.referenceFileName = request.referenceFileName;
    transfer.displayReferenceChannel = referenceChannel;
    transfer.displayMeasurementChannel = measurementChannel;
    out.transfer = analyseWavTransfer(pair, transfer, cancel);
    return out;
}

}  // namespace rta::measure
