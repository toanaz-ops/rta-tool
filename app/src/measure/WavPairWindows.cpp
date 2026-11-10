// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/WavPairWindows.h"

#include "rta/dsp/DelayPolicy.h"

#include <algorithm>
#include <cmath>

namespace rta::measure::detail {

WindowStatus measureWindows(const SampleBlockSource& reference, int referenceChannel,
                            const SampleBlockSource& measurement, int measurementChannel,
                            std::int64_t rangeStart, std::int64_t rangeLength, const WindowPass& pass,
                            const std::atomic<bool>& cancel,
                            std::vector<rta::dsp::DelayObservation>& observations,
                            WindowTally& tally) {
    const std::int64_t width = pass.windowLength;
    const int count = std::max(pass.windowCount, 1);
    rta::dsp::PhatOptions phat;
    phat.sampleRate = pass.sampleRate;
    phat.minHz = pass.minHz;
    phat.maxHz = pass.maxHz;
    rta::dsp::DelayPolicy policy;
    policy.minLag = static_cast<std::ptrdiff_t>(-pass.lagBound);
    policy.maxLag = static_cast<std::ptrdiff_t>(pass.lagBound);
    rta::dsp::PhaseSlopeOptions slope;
    slope.sampleRate = pass.sampleRate;
    slope.minHz = pass.minHz;
    slope.maxHz = pass.maxHz;

    std::vector<float> ref(static_cast<std::size_t>(width));
    std::vector<float> meas(ref.size());
    const auto fits = [&](std::int64_t at) { return at >= pass.measFirst && at + width - 1 <= pass.measLast; };

    for (int k = 0; k < count; ++k) {
        if (cancel.load()) return WindowStatus::Cancelled;
        const std::int64_t spread = count > 1 ? rangeLength - width : 0;
        const std::int64_t start =
            rangeStart + (count > 1 ? std::llround(static_cast<double>(k) * static_cast<double>(spread) /
                                                   static_cast<double>(count - 1))
                                    : 0);
        const std::int64_t centred = start + pass.priorDelay;
        if (!fits(centred)) { ++tally.skipped; continue; }
        if (!reference.read(referenceChannel, start, ref) ||
            !measurement.read(measurementChannel, centred, meas)) {
            return WindowStatus::ReadFailed;
        }
        const auto look = rta::dsp::suggestDelay(ref, meas, phat, policy);
        if (look.trust > tally.bestTrust) {
            tally.bestTrust = look.trust;
            tally.trustFloor = policy.acceptMultiple * look.nullFloor;
        }
        if (look.verdict != rta::dsp::DelayVerdict::Accepted) continue;
        tally.lowestAcceptedTrust = std::min(tally.lowestAcceptedTrust, look.trust);

        // The fraction is measured on a window already moved onto the integer
        // lag, so the phase-slope fit only ever sees |residual| <= 0.5 + drift.
        const std::int64_t lag = look.best.delaySamples;
        if (!fits(centred + lag)) { ++tally.skipped; continue; }
        if (!measurement.read(measurementChannel, centred + lag, meas)) return WindowStatus::ReadFailed;
        const auto fraction = rta::dsp::fractionalDelayByPhaseSlope(ref, meas, 0, slope);
        if (!fraction.has_value()) continue;
        ++tally.accepted;
        observations.push_back(
            {static_cast<double>(start - rangeStart) + 0.5 * static_cast<double>(width),
             static_cast<double>(pass.priorDelay + lag) + fraction->delaySamples});
    }
    return WindowStatus::Ok;
}

}  // namespace rta::measure::detail
