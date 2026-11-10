// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H11 W2: the sentences. One English sentence per refusal, every number it
// names measured by the run that refused (D9: dB and ms one decimal, Hz whole,
// delays in whole samples). Channels are 1-based here because the operator
// counts from 1; the request and outcome carry 0-based indices.
#include "measure/WavTransfer.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace rta::measure {

namespace {

std::string fixed(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

std::string channelName(int zeroBased) { return "ch" + std::to_string(zeroBased + 1); }

/// "-12 samples (-0.3 ms)": whole samples, then milliseconds to one decimal.
std::string delayText(int samples, double sampleRate) {
    const double ms = sampleRate > 0.0 ? 1000.0 * samples / sampleRate : 0.0;
    return std::to_string(samples) + " samples (" + fixed(ms, 1) + " ms)";
}

}  // namespace

std::string wavTransferRefusalText(const WavTransferOutcome& o) {
    if (!o.refusal.has_value()) return {};
    switch (*o.refusal) {
        case WavTransferRefusal::NotEnoughChannels:
            return "This file has " + std::to_string(o.fileChannels) +
                   (o.fileChannels == 1 ? " channel" : " channels") +
                   "; a transfer function needs two (a reference and a measurement).";
        case WavTransferRefusal::SameChannel:
            return "Reference and measurement are both " + channelName(o.referenceChannel) +
                   "; pick two different channels.";
        case WavTransferRefusal::ChannelOutOfRange:
            return "That channel does not exist: this file has " + std::to_string(o.fileChannels) +
                   " channels (ch1 to ch" + std::to_string(o.fileChannels) + ").";
        case WavTransferRefusal::BadSampleRate:
            return "The file reports an unusable sample rate (" +
                   fixed(o.sampleRate, 0) + " Hz).";
        case WavTransferRefusal::RangeOutsideFile:
            return "The range (start " + fixed(o.requestedStartSeconds, 2) + " s, length " +
                   (o.requestedLengthSeconds > 0.0 ? fixed(o.requestedLengthSeconds, 2) + " s"
                                                    : std::string("to the end")) +
                   ") does not fit inside the file, which is " + fixed(o.fileLengthSeconds, 2) +
                   " s long.";
        case WavTransferRefusal::RangeTooShort:
            return "The range is too short: after the " + std::to_string(o.discardedSamples) +
                   "-sample delay discard it reaches " + fixed(o.reachableAverages, 1) +
                   " effective averages and " + fixed(o.neededAverages, 1) +
                   " are needed. Pick a longer range.";
        case WavTransferRefusal::NoCorrelation:
            return "No delay between " + channelName(o.referenceChannel) + " and " +
                   channelName(o.measurementChannel) + " could be trusted: the best delay trust was " +
                   fixed(o.bestTrust, 2) + " and " + fixed(o.trustFloor, 2) +
                   " is needed. The channels may not carry the same programme here -- check the "
                   "channel choice, or pick a louder passage.";
        case WavTransferRefusal::DelayMoved:
            return "The delay moved from " + delayText(o.delayFirst, o.sampleRate) + " to " +
                   delayText(o.delayLast, o.sampleRate) +
                   " within the range -- the system changed or the channels do not share a clock "
                   "-- pick a shorter range.";
        case WavTransferRefusal::ReadFailed:
            return "The file could not be read.";
        case WavTransferRefusal::Cancelled:
            return "Analysis cancelled.";
    }
    return {};
}

}  // namespace rta::measure
