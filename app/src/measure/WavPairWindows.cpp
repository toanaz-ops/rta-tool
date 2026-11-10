// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/WavPairWindows.h"

#include "rta/dsp/DelayPolicy.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace rta::measure::detail {

namespace {

/// A window that found its integer lag and whose spectrum the coherence gate let through.
struct Candidate {
    std::int64_t start = 0;
    std::int64_t lag = 0;
    double trust = 0.0;
    rta::dsp::WindowSpectrum spectrum;
};

}  // namespace

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

    // Stage 1: per window, the integer lag and the spectrum on that lag.
    std::vector<Candidate> found;
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

        // The spectrum is taken on a window already moved onto the integer lag.
        const std::int64_t lag = look.best.delaySamples;
        if (!fits(centred + lag)) { ++tally.skipped; continue; }
        if (!measurement.read(measurementChannel, centred + lag, meas)) return WindowStatus::ReadFailed;
        auto spectrum = rta::dsp::windowSpectrum(ref, meas, 0, pass.sampleRate);
        if (!spectrum.has_value()) continue;
        found.push_back({start, lag, look.trust, std::move(*spectrum)});
    }
    if (found.empty()) return WindowStatus::Ok;

    // Stage 2: every fraction is measured RELATIVE to the highest-trust window
    // (`relativeFractionalDelay`: the system's phase cancels, so a crossover's
    // group delay cannot push a window out of the fit's validity). The anchor's
    // own fraction is its ABSOLUTE phase-slope delay when that fit is valid
    // (|residual| <= 1 sample: a near-linear-phase system, where it is exact) and
    // 0 otherwise -- then every delay carries the same unknown constant, at most
    // half a sample of PHAT rounding, which the ppm never sees and which only
    // tilts the stored phase by 2 pi f e / fs.
    const auto anchor = std::max_element(found.begin(), found.end(),
                                         [](const Candidate& a, const Candidate& b) { return a.trust < b.trust; });
    const auto absolute = rta::dsp::fractionalDelayOf(anchor->spectrum, 0, slope);
    const double anchorFraction = absolute.has_value() ? absolute->delaySamples : 0.0;
    for (const auto& c : found) {
        std::optional<double> relative = 0.0;
        if (&c != &*anchor) relative = rta::dsp::relativeFractionalDelay(c.spectrum, anchor->spectrum, slope);
        if (!relative.has_value()) continue;
        ++tally.accepted;
        observations.push_back(
            {static_cast<double>(c.start - rangeStart) + 0.5 * static_cast<double>(width),
             static_cast<double>(pass.priorDelay + c.lag) + anchorFraction + *relative});
    }
    return WindowStatus::Ok;
}

double predictedDriftError(double residualRms, int windows, double spread, std::int64_t rangeLength) {
    return residualRms * std::sqrt(12.0 / static_cast<double>(windows)) * static_cast<double>(rangeLength) / spread;
}

CoarseLook coarseLook(const SampleBlockSource& reference, int referenceChannel,
                      const SampleBlockSource& measurement, int measurementChannel,
                      std::int64_t refStart, std::int64_t width, std::int64_t prior,
                      std::int64_t halfWidth, bool& readFailed) {
    CoarseLook look;
    const std::int64_t measLength = measurement.lengthSamples();
    const std::int64_t spanStart = std::max<std::int64_t>(refStart + prior - halfWidth, 0);
    const std::int64_t spanEnd = std::min<std::int64_t>(refStart + prior + halfWidth + width, measLength);
    look.spanSamples = std::max<std::int64_t>(spanEnd - spanStart, 0);
    if (spanEnd - spanStart < width) return look;
    std::vector<float> span(static_cast<std::size_t>(spanEnd - spanStart));
    std::vector<float> padded(span.size(), 0.0f);
    const std::span<float> refPart(padded.data(), static_cast<std::size_t>(width));
    if (!measurement.read(measurementChannel, spanStart, span) ||
        !reference.read(referenceChannel, refStart, refPart)) {
        readFailed = true;
        return look;
    }
    rta::dsp::PhatOptions phat;
    phat.sampleRate = reference.sampleRate();
    const DriftBand band = driftBand(phat.sampleRate);
    phat.minHz = band.minHz;
    phat.maxHz = band.maxHz;
    rta::dsp::DelayPolicy policy;
    const std::int64_t shift = spanStart - refStart;  // offset d <-> padded lag = d - shift
    policy.minLag = static_cast<std::ptrdiff_t>(std::max<std::int64_t>(prior - halfWidth - shift, 0));
    policy.maxLag = static_cast<std::ptrdiff_t>(
        std::min<std::int64_t>(prior + halfWidth - shift, static_cast<std::int64_t>(span.size()) - width));
    if (policy.minLag > policy.maxLag) return look;
    const auto suggestion = rta::dsp::suggestDelay(padded, span, phat, policy);
    look.measured = true;
    look.trust = suggestion.trust;
    look.floor = policy.acceptMultiple * suggestion.nullFloor;
    look.accepted = suggestion.verdict == rta::dsp::DelayVerdict::Accepted;
    look.offset = static_cast<std::int64_t>(suggestion.best.delaySamples) + shift;
    return look;
}

}  // namespace rta::measure::detail
