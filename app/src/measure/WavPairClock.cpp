// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/WavPairClock.h"

#include "measure/WavPairWindows.h"
#include "rta/dsp/ClockDrift.h"
#include "rta/dsp/DelayPolicy.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rta::measure {

namespace {

using detail::WindowPass;
using detail::WindowStatus;
using detail::WindowTally;

constexpr std::int64_t kCoarseWindow = std::int64_t{1} << 17;  // E2
constexpr std::int64_t kPass1Window = std::int64_t{1} << 15;   // E3
constexpr std::int64_t kPass2Window = std::int64_t{1} << 16;   // E6
constexpr int kMinWindows = 3;
constexpr int kMaxWindows = 32;
constexpr std::int64_t kLagMargin = 64;       // E3: +-(round(maxPpm*1e-6*length) + 64)
constexpr std::int64_t kPass2LagBound = 64;   // after pass 1 the pair is within a few samples
constexpr double kSharedClockSamples = 0.05;  // E7
constexpr double kMaxResidualRms = 0.1;       // E6
constexpr double kMinCoverage = 0.6;  // accepted windows must span 60 % of the first-to-last-centre span

struct CoarseLook {
    bool accepted = false;
    std::int64_t offset = 0;
    double trust = 0.0;
    double floor = 0.0;
};

/// E2 for ONE reference window at `refStart`: the window is zero-padded to the
/// length of the measurement span so `suggestDelay` sees equal lengths, and the
/// lag window is the requested offset interval translated into that span's
/// coordinates and clipped to where the whole window still lies inside it.
/// The span itself is clipped to the measurement file, never failed.
CoarseLook coarseLook(const SampleBlockSource& reference, int referenceChannel,
                      const SampleBlockSource& measurement, int measurementChannel,
                      std::int64_t refStart, std::int64_t width, std::int64_t prior,
                      std::int64_t halfWidth, bool& readFailed) {
    CoarseLook look;
    const std::int64_t measLength = measurement.lengthSamples();
    const std::int64_t spanStart = std::max<std::int64_t>(refStart + prior - halfWidth, 0);
    const std::int64_t spanEnd = std::min<std::int64_t>(refStart + prior + halfWidth + width, measLength);
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
    rta::dsp::DelayPolicy policy;
    const std::int64_t shift = spanStart - refStart;  // offset d <-> padded lag = d - shift
    policy.minLag = static_cast<std::ptrdiff_t>(std::max<std::int64_t>(prior - halfWidth - shift, 0));
    policy.maxLag = static_cast<std::ptrdiff_t>(
        std::min<std::int64_t>(prior + halfWidth - shift, static_cast<std::int64_t>(span.size()) - width));
    if (policy.minLag > policy.maxLag) return look;
    const auto suggestion = rta::dsp::suggestDelay(padded, span, phat, policy);
    look.trust = suggestion.trust;
    look.floor = policy.acceptMultiple * suggestion.nullFloor;
    look.accepted = suggestion.verdict == rta::dsp::DelayVerdict::Accepted;
    look.offset = static_cast<std::int64_t>(suggestion.best.delaySamples) + shift;
    return look;
}

ClockResult refuse(ClockRefusal why) {
    ClockResult r;
    r.refusal = why;
    return r;
}

int windowCountFor(std::int64_t rangeLength, std::int64_t width) {
    return static_cast<int>(std::clamp<std::int64_t>(rangeLength / width, kMinWindows, kMaxWindows));
}

ClockResult noCorrelation(const WindowTally& t, double coverage = 1.0) {
    ClockResult r = refuse(ClockRefusal::NoCorrelation);
    r.bestTrust = t.bestTrust;
    r.trustFloor = t.trustFloor;
    r.windowsAccepted = t.accepted;
    r.windowsNeeded = kMinWindows;
    r.coverage = coverage;
    return r;
}

/// The fraction of the span the windows CAN cover (first centre to last centre)
/// that the accepted observations actually cover. A pass that kept only the
/// windows on one side of a bend would fit a straight line through that side
/// alone and call it the whole range's clock (measured: 0 ppm then +250 ppm, the
/// second half's windows rejected for drifting inside themselves, fitted as 0).
double coverageOf(const std::vector<rta::dsp::DelayObservation>& points, std::int64_t rangeLength,
                  std::int64_t width) {
    if (points.empty() || rangeLength <= width) return 0.0;
    double lo = points.front().positionSamples, hi = lo;
    for (const auto& p : points) {
        lo = std::min(lo, p.positionSamples);
        hi = std::max(hi, p.positionSamples);
    }
    return (hi - lo) / static_cast<double>(rangeLength - width);
}

}  // namespace

ClockResult estimateClockMapping(const SampleBlockSource& reference, int referenceChannel,
                                 const SampleBlockSource& measurement, int measurementChannel,
                                 std::int64_t rangeStart, std::int64_t rangeLength,
                                 const ClockSearchOptions& options, const std::atomic<bool>& cancel) {
    const double fs = reference.sampleRate();
    if (rangeStart < 0 || rangeLength <= 0 || rangeStart + rangeLength > reference.lengthSamples()) {
        return refuse(ClockRefusal::ReadFailed);
    }
    if (rangeLength < kPass1Window) {
        ClockResult r = refuse(ClockRefusal::RangeTooShort);
        r.minRangeSeconds = static_cast<double>(kPass1Window) / fs;
        return r;
    }
    const std::int64_t halfWidth = std::llround(options.coarseHalfWidthSeconds * fs);

    // --- E2 coarse offset ----------------------------------------------------
    // The window at the range start; if THAT one is not Accepted (the start may
    // be silence or a count-in) retry ONCE at the range middle. One retry, not a
    // scan: a scan is what pass 1 already is, and a third look would only hide a
    // wrong prior. The offset (meas minus ref) is a property of the two files, so
    // a middle window answers the same question.
    const std::int64_t coarseWidth = std::min(kCoarseWindow, rangeLength);
    bool readFailed = false;
    CoarseLook coarse = coarseLook(reference, referenceChannel, measurement, measurementChannel, rangeStart,
                                   coarseWidth, options.priorOffsetSamples, halfWidth, readFailed);
    if (!readFailed && !coarse.accepted && !cancel.load()) {
        const std::int64_t middle = rangeStart + (rangeLength - coarseWidth) / 2;
        const CoarseLook second = coarseLook(reference, referenceChannel, measurement, measurementChannel,
                                             middle, coarseWidth, options.priorOffsetSamples, halfWidth,
                                             readFailed);
        if (second.accepted || second.trust > coarse.trust) coarse = second;
    }
    if (readFailed) return refuse(ClockRefusal::ReadFailed);
    if (cancel.load()) return refuse(ClockRefusal::Cancelled);
    if (!coarse.accepted) {
        ClockResult r = refuse(ClockRefusal::NoCoarseAlignment);
        r.bestTrust = coarse.trust;
        r.trustFloor = coarse.floor;
        r.halfWidthSeconds = options.coarseHalfWidthSeconds;
        return r;
    }

    // --- E3 pass 1 -----------------------------------------------------------
    WindowPass pass1;
    pass1.windowLength = kPass1Window;
    pass1.windowCount = windowCountFor(rangeLength, kPass1Window);
    pass1.priorDelay = coarse.offset;
    pass1.lagBound = std::llround(options.maxPpm * 1e-6 * static_cast<double>(rangeLength)) + kLagMargin;
    pass1.sampleRate = fs;
    pass1.measFirst = 0;
    pass1.measLast = measurement.lengthSamples() - 1;
    std::vector<rta::dsp::DelayObservation> observations;
    WindowTally tally1;
    const WindowStatus status1 = detail::measureWindows(reference, referenceChannel, measurement,
                                                        measurementChannel, rangeStart, rangeLength, pass1,
                                                        cancel, observations, tally1);
    if (status1 == WindowStatus::Cancelled) return refuse(ClockRefusal::Cancelled);
    if (status1 == WindowStatus::ReadFailed) return refuse(ClockRefusal::ReadFailed);
    if (tally1.accepted < kMinWindows) return noCorrelation(tally1);
    if (const double c = coverageOf(observations, rangeLength, kPass1Window); c < kMinCoverage) {
        return noCorrelation(tally1, c);
    }
    const auto fit1 = rta::dsp::fitDriftTheilSen(observations);
    if (!fit1.has_value()) return noCorrelation(tally1);

    // --- E4 / E7 -------------------------------------------------------------
    const double eps = fit1->slope;
    if (std::abs(eps) * 1e6 > options.maxPpm) {
        ClockResult r = refuse(ClockRefusal::DriftTooLarge);
        r.ppm = eps * 1e6;
        r.maxPpm = options.maxPpm;
        return r;
    }
    ClockEstimate estimate;
    estimate.windowsUsed = tally1.accepted;
    const auto rangeStartD = static_cast<double>(rangeStart);
    // E7 declares a shared clock from the SLOPE alone, but a Theil-Sen slope is a
    // median: a delay that is flat for half the range and then drifts has a median
    // slope of ~0 and would be waved through (measured: 0 then +250 ppm read
    // "shared clock", and G22's DelayMoved cannot see it on a range under 2^19
    // samples, where it has one delay window). So the shared branch also needs
    // pass 1's own line to fit (residual RMS within the E6 gate); otherwise the
    // pair goes through pass 2, which refuses a bend as DriftNotLinear.
    if (std::abs(eps) * static_cast<double>(rangeLength) <= kSharedClockSamples &&
        fit1->residualRms <= kMaxResidualRms) {
        // One clock: only the integer offset, taken at the range start. No pass 2
        // and no resampling -- the pair source copies samples bitwise.
        const double delay = std::round(fit1->intercept);
        estimate.sharedClock = true;
        estimate.mapping = {delay, 1.0, true};  // t(n) = n + delay
        estimate.offsetSeconds = delay / fs;
        ClockResult r;
        r.estimate = estimate;
        return r;
    }

    // --- E5/E6 pass 2 --------------------------------------------------------
    // Pass 1's line (positions relative to the range start, x = n - rangeStart):
    //   t1(n) = (rangeStart + b) + x (1 + eps).
    ClockMapping first;
    first.ratio = 1.0 + eps;
    first.offsetSamples = (rangeStartD + fit1->intercept) - rangeStartD * first.ratio;
    first.integerOnly = false;
    const ResampledPairSource corrected(reference, referenceChannel, measurement, measurementChannel, first);
    const auto valid = corrected.validRange();

    WindowPass pass2;
    pass2.windowLength = rangeLength >= kPass2Window ? kPass2Window : kPass1Window;
    pass2.windowCount = windowCountFor(rangeLength, pass2.windowLength);
    pass2.priorDelay = 0;
    pass2.lagBound = kPass2LagBound;
    pass2.sampleRate = fs;
    pass2.measFirst = valid.first;
    pass2.measLast = valid.last;
    std::vector<rta::dsp::DelayObservation> residuals;
    WindowTally tally2;
    const WindowStatus status2 =
        detail::measureWindows(corrected, 0, corrected, 1, rangeStart, rangeLength, pass2, cancel, residuals, tally2);
    if (status2 == WindowStatus::Cancelled) return refuse(ClockRefusal::Cancelled);
    if (status2 == WindowStatus::ReadFailed) return refuse(ClockRefusal::ReadFailed);
    if (tally2.accepted < kMinWindows) return noCorrelation(tally2);
    if (const double c = coverageOf(residuals, rangeLength, pass2.windowLength); c < kMinCoverage) {
        return noCorrelation(tally2, c);
    }
    const auto fit2 = rta::dsp::fitDriftTheilSen(residuals);
    if (!fit2.has_value()) return noCorrelation(tally2);
    if (fit2->residualRms > kMaxResidualRms) {
        ClockResult r = refuse(ClockRefusal::DriftNotLinear);
        r.residualRms = fit2->residualRms;
        r.maxResidual = fit2->maxAbsResidual;
        return r;
    }

    // The corrected pair's delay is d2(x) = b2 + eps2 x: reference sample n pairs
    // with corrected sample n + d2, which IS measurement position t1(n + d2):
    //   t(n) = t1(rangeStart + x + d2(x))
    //        = (rangeStart + b) + (x + b2 + eps2 x)(1 + eps)
    //        = (rangeStart + b) + b2 (1 + eps) + x (1 + eps)(1 + eps2).
    ClockMapping composed;
    composed.integerOnly = false;
    composed.ratio = (1.0 + eps) * (1.0 + fit2->slope);
    const double atRangeStart = (rangeStartD + fit1->intercept) + fit2->intercept * (1.0 + eps);
    composed.offsetSamples = atRangeStart - rangeStartD * composed.ratio;
    estimate.mapping = composed;
    estimate.ppm = (composed.ratio - 1.0) * 1e6;
    estimate.offsetSeconds = (atRangeStart - rangeStartD) / fs;
    estimate.residualRms = fit2->residualRms;
    estimate.maxResidual = fit2->maxAbsResidual;
    ClockResult r;
    r.estimate = estimate;
    return r;
}

}  // namespace rta::measure
