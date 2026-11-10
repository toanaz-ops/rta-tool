// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 T3: the sentences for a two-file pair. Every number a sentence names is
// the one the refusing run measured (record E9: ppm two decimals, seconds three
// in the readout, samples two decimals; trust two decimals as in G22).
#include "measure/WavPairTransfer.h"

#include <cstdio>
#include <string>

namespace rta::measure {

namespace {

std::string fixed(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

std::string shortest(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}

std::string named(const std::string& file, int zeroBased) {
    return (file.empty() ? std::string("the file") : file) + " ch" + std::to_string(zeroBased + 1);
}

std::string clockSentence(const WavPairOutcome& o) {
    const ClockResult& c = o.clock;
    switch (*c.refusal) {
        case ClockRefusal::NoCoarseAlignment:
            return "The two files could not be lined up within " + fixed(c.halfWidthSeconds, 1) +
                   " s of the expected start offset: the best delay trust was " + fixed(c.bestTrust, 2) +
                   " and " + fixed(c.trustFloor, 2) +
                   " is needed. They may not carry the same programme, or one recording started more "
                   "than that from the other -- check the channel choice.";
        case ClockRefusal::NoCorrelation:
            if (c.windowsAccepted >= c.windowsNeeded) {
                return "The delay windows that could be trusted cover only " + fixed(100.0 * c.coverage, 0) +
                       " % of the range, so the clock cannot be fitted across all of it: the delay may "
                       "bend, or one part of the range carries a different programme -- pick a shorter range.";
            }
            return "Only " + std::to_string(c.windowsAccepted) + " of the delay windows that measure clock drift "
                   "could be trusted and " + std::to_string(c.windowsNeeded) +
                   " are needed (best delay trust " + fixed(c.bestTrust, 2) + ", " + fixed(c.trustFloor, 2) +
                   " needed). The files may not share the programme for the whole range -- pick a "
                   "range where both carry it.";
        case ClockRefusal::DriftTooLarge:
            return "The clocks differ by " + fixed(c.ppm, 2) + " ppm, more than the " + shortest(c.maxPpm) +
                   " ppm this tool corrects. That is not a clock error: check for a wrong sample-rate "
                   "label or a varispeed file.";
        case ClockRefusal::DriftNotLinear:
            return "The delay does not follow a straight line (residual " + fixed(c.residualRms, 2) +
                   " samples rms, " + fixed(c.maxResidual, 2) +
                   " samples at worst): a clock wandered or the acoustic path changed -- pick a shorter "
                   "range.";
        case ClockRefusal::RangeTooShort:
            return "The range is too short to measure clock drift: at least " + fixed(c.minRangeSeconds, 2) +
                   " s are needed. Pick a longer range.";
        case ClockRefusal::ReadFailed:
            return "A file could not be read.";
        case ClockRefusal::Cancelled:
            return "Analysis cancelled.";
    }
    return {};
}

}  // namespace

std::string wavPairRefusalText(const WavPairOutcome& o) {
    if (o.refusal.has_value()) {
        switch (*o.refusal) {
            case WavPairRefusal::SampleRateMismatch:
                return o.referenceFileName + " is " + fixed(o.referenceRate, 0) + " Hz and " +
                       o.measurementFileName + " is " + fixed(o.measurementRate, 0) +
                       " Hz: both files need the same sample rate. Convert one of them first.";
            case WavPairRefusal::BadSampleRate:
                return "A file reports an unusable sample rate (" + fixed(o.referenceRate, 0) + " Hz and " +
                       fixed(o.measurementRate, 0) + " Hz).";
            case WavPairRefusal::ChannelOutOfRange: {
                const std::string& file = o.referenceChannelBad ? o.referenceFileName : o.measurementFileName;
                const int channels = o.referenceChannelBad ? o.referenceFileChannels : o.measurementFileChannels;
                return "That channel does not exist: " + (file.empty() ? std::string("the file") : file) + " has " +
                       std::to_string(channels) + " channels (ch1 to ch" + std::to_string(channels) + ").";
            }
            case WavPairRefusal::RangeOutsideFile:
                return "The range (start " + fixed(o.requestedStartSeconds, 2) + " s, length " +
                       (o.requestedLengthSeconds > 0.0 ? fixed(o.requestedLengthSeconds, 2) + " s"
                                                        : std::string("to the end")) +
                       ") does not fit inside the part of the reference where both files overlap, which "
                       "runs from " + fixed(o.overlapFirstSeconds, 2) + " s to " +
                       fixed(o.overlapLastSeconds, 2) + " s.";
        }
        return {};
    }
    if (o.clock.refusal.has_value()) return clockSentence(o);
    if (o.transfer.refusal.has_value()) {
        // G22's own sentences, except the one that would read "ch1 and ch1" for two files.
        if (*o.transfer.refusal == WavTransferRefusal::NoCorrelation) {
            return "No delay between " + named(o.referenceFileName, o.referenceChannel) + " and " +
                   named(o.measurementFileName, o.measurementChannel) +
                   " could be trusted after the clock was corrected: the best delay trust was " +
                   fixed(o.transfer.bestTrust, 2) + " and " + fixed(o.transfer.trustFloor, 2) +
                   " is needed -- pick a louder passage.";
        }
        return wavTransferRefusalText(o.transfer);
    }
    return {};
}

}  // namespace rta::measure
