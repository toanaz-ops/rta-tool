// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/WavPairClock.h"

#include "measure/WavPairWindows.h"
#include "rta/dsp/ClockDrift.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rta::measure {

namespace {

using detail::CoarseLook;
using detail::WindowPass;
using detail::WindowStatus;
using detail::WindowTally;

constexpr std::int64_t kCoarseWindow = std::int64_t{1} << 17;  // E2
constexpr std::int64_t kPass1Window = std::int64_t{1} << 15;   // E3
constexpr std::int64_t kPass2Window = std::int64_t{1} << 16;   // E6
// Four pass-1 windows (2^17 / 2^15) spread over at least 3/4 of the range: the
// least that gives a baseline, i.e. a ppm, worth fitting. Below it the drift
// line stands on a few hundred samples of baseline (3 windows on a ~33600-sample
// range spread over 832 samples).
constexpr std::int64_t kMinRange = std::int64_t{1} << 17;
constexpr int kMinWindows = 3;
constexpr int kMaxWindows = 32;
constexpr std::int64_t kLagMargin = 64;       // E3: +-(round(maxPpm*1e-6*length) + 64)
constexpr std::int64_t kPass2LagBound = 64;   // after pass 1 the pair is within a few samples
constexpr std::int64_t kKernelTaps = 32;      // the resampler reads +-32 taps
constexpr double kSharedClockSamples = 0.05;  // E7
constexpr double kMaxResidualRms = 0.1;       // E6
constexpr double kMaxUncertainty = 0.1;       // samples of total drift error over the range
constexpr double kMinCoverage = 0.6;  // accepted windows must span 60 % of the first-to-last-centre span

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

/// The pass-1 lag search runs within +-lagBound of the coarse offset; a window of
/// 2^15 samples only survives a delay that moves inside it if that is a small
/// part of it. Past a quarter window the search window cannot be trusted.
bool rangeTooLong(std::int64_t rangeLength, double maxPpm) {
    return maxPpm * 1e-6 * static_cast<double>(rangeLength) > static_cast<double>(kPass1Window) / 4.0;
}

ClockResult tooLong(double maxPpm, double fs) {
    ClockResult r = refuse(ClockRefusal::RangeTooLong);
    r.maxPpm = maxPpm;
    r.maxRangeMinutes = (static_cast<double>(kPass1Window) / 4.0 / (maxPpm * 1e-6)) / fs / 60.0;
    return r;
}

}  // namespace

ClockResult estimateClockMapping(const SampleBlockSource& reference, int referenceChannel,
                                 const SampleBlockSource& measurement, int measurementChannel,
                                 std::int64_t rangeStart, std::int64_t rangeLength,
                                 const ClockSearchOptions& options, const std::atomic<bool>& cancel) {
    const double fs = reference.sampleRate();
    // LENGTH 0: to the end of the overlap, which is only known once the offset is.
    const bool toEnd = rangeLength == 0;
    if (toEnd) rangeLength = reference.lengthSamples() - rangeStart;
    if (rangeStart < 0 || rangeLength <= 0 || rangeStart + rangeLength > reference.lengthSamples()) {
        return refuse(ClockRefusal::ReadFailed);
    }
    const auto tooShort = [&] {
        ClockResult r = refuse(ClockRefusal::RangeTooShort);
        r.minRangeSeconds = static_cast<double>(kMinRange) / fs;
        return r;
    };
    if (rangeLength < kMinRange) return tooShort();
    if (!toEnd && rangeTooLong(rangeLength, options.maxPpm)) return tooLong(options.maxPpm, fs);
    const std::int64_t halfWidth = std::llround(options.coarseHalfWidthSeconds * fs);

    // --- E2 coarse offset ----------------------------------------------------
    // The window at the range start; if THAT one is not Accepted (the start may
    // be silence or a count-in) retry ONCE at the range middle. One retry, not a
    // scan: a scan is what pass 1 already is, and a third look would only hide a
    // wrong prior. The offset (meas minus ref) is a property of the two files, so
    // a middle window answers the same question.
    const std::int64_t coarseWidth = std::min(kCoarseWindow, rangeLength);
    bool readFailed = false;
    CoarseLook coarse = detail::coarseLook(reference, referenceChannel, measurement, measurementChannel,
                                           rangeStart, coarseWidth, options.priorOffsetSamples, halfWidth,
                                           readFailed);
    if (!readFailed && !coarse.accepted && !cancel.load()) {
        const std::int64_t middle = rangeStart + (rangeLength - coarseWidth) / 2;
        const CoarseLook second = detail::coarseLook(reference, referenceChannel, measurement,
                                                     measurementChannel, middle, coarseWidth,
                                                     options.priorOffsetSamples, halfWidth, readFailed);
        const bool better = second.accepted ||
                            (second.measured && (!coarse.measured || second.trust > coarse.trust)) ||
                            (!second.measured && !coarse.measured && second.spanSamples > coarse.spanSamples);
        if (better) coarse = second;
    }
    if (readFailed) return refuse(ClockRefusal::ReadFailed);
    if (cancel.load()) return refuse(ClockRefusal::Cancelled);
    if (!coarse.accepted && !coarse.measured) {
        // Nothing was correlated: say what was missing, not "trust 0.00 of 0.00".
        ClockResult r = refuse(ClockRefusal::CoarseSpanTooShort);
        r.coarseSpanSeconds = static_cast<double>(coarse.spanSamples) / fs;
        r.coarseNeededSeconds = static_cast<double>(coarseWidth) / fs;
        r.priorSeconds = static_cast<double>(options.priorOffsetSamples) / fs;
        r.measurementSeconds = static_cast<double>(measurement.lengthSamples()) / fs;
        r.halfWidthSeconds = options.coarseHalfWidthSeconds;
        return r;
    }
    if (!coarse.accepted) {
        ClockResult r = refuse(ClockRefusal::NoCoarseAlignment);
        r.bestTrust = coarse.trust;
        r.trustFloor = coarse.floor;
        r.halfWidthSeconds = options.coarseHalfWidthSeconds;
        return r;
    }

    // --- the range against the overlap, BEFORE pass 1 (M2) -------------------
    // Reference sample n sits near n + offset on the measurement, give or take the
    // ppm margin and the resampler's taps. A range that runs past the measurement's
    // end must be named for what it is: left to pass 1 its windows are skipped and
    // the refusal reads "the delay may bend".
    const auto overlapFor = [&](std::int64_t length) {
        const std::int64_t margin =
            static_cast<std::int64_t>(std::ceil(options.maxPpm * 1e-6 * static_cast<double>(length))) + kKernelTaps;
        struct Overlap { std::int64_t first, last; } o;
        o.first = std::max<std::int64_t>(0, margin - coarse.offset);
        o.last = std::min(reference.lengthSamples() - 1, measurement.lengthSamples() - 1 - coarse.offset - margin);
        return o;
    };
    const auto outside = [&](std::int64_t first, std::int64_t last) {
        ClockResult r = refuse(ClockRefusal::RangeOutsideOverlap);
        r.overlapFirst = first;
        r.overlapLast = last;
        return r;
    };
    {
        const auto o = overlapFor(rangeLength);
        if (rangeStart < o.first) return outside(o.first, o.last);
        if (toEnd) rangeLength = std::min(rangeLength, o.last - rangeStart + 1);
        if (rangeStart + rangeLength - 1 > o.last) return outside(o.first, o.last);
        if (rangeLength < kMinRange) return toEnd ? outside(o.first, o.last) : tooShort();
        if (toEnd && rangeTooLong(rangeLength, options.maxPpm)) return tooLong(options.maxPpm, fs);
    }

    // --- E3 pass 1 -----------------------------------------------------------
    WindowPass pass1;
    pass1.windowLength = kPass1Window;
    pass1.windowCount = windowCountFor(rangeLength, kPass1Window);
    pass1.priorDelay = coarse.offset;
    pass1.lagBound = std::llround(options.maxPpm * 1e-6 * static_cast<double>(rangeLength)) + kLagMargin;
    pass1.sampleRate = fs;
    pass1.minHz = detail::driftBand(fs).minHz;
    pass1.maxHz = detail::driftBand(fs).maxHz;
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
    estimate.rangeLength = rangeLength;
    const auto rangeStartD = static_cast<double>(rangeStart);
    // E7. The slope alone can wave a bend through (a Theil-Sen slope is a median: flat
    // for 3/4 of the range then drifting reads ~0), so a shared clock is not
    // believed on pass 1: it goes through pass 2 below, whose residual gate refuses
    // a bend as DriftNotLinear exactly as for a drifting pair.
    const bool shared = std::abs(eps) * static_cast<double>(rangeLength) <= kSharedClockSamples;
    // Pass 1 works in a LOW band (see driftBand), where a system with a non-flat
    // phase has a group delay of its own: on the +6 dB biquad that moved an exact
    // integer offset by a whole sample. So even a shared clock goes through pass 2,
    // in the full band, to settle the integer -- it just never resamples.
    const double sharedDelay = std::round(fit1->intercept);

    // --- E5/E6 pass 2 --------------------------------------------------------
    // Pass 1's line (positions relative to the range start, x = n - rangeStart):
    //   t1(n) = (rangeStart + b) + x (1 + eps).
    ClockMapping first;
    if (shared) {
        first = {sharedDelay, 1.0, true};  // t(n) = n + delay, a bitwise copy
    } else {
        first.ratio = 1.0 + eps;
        first.offsetSamples = (rangeStartD + fit1->intercept) - rangeStartD * first.ratio;
        first.integerOnly = false;
    }
    const ResampledPairSource corrected(reference, referenceChannel, measurement, measurementChannel, first);
    const auto valid = corrected.validRange();
    // The range must lie in the overlap this mapping leaves, BEFORE pass 2 reads
    // it: windows that fall outside are skipped, and skipping them would turn a
    // range-outside-the-overlap refusal into a thin-coverage one.
    if (valid.empty() || rangeStart < valid.first || rangeStart + rangeLength - 1 > valid.last) {
        return outside(valid.first, valid.last);
    }

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

    // A drift the windows scatter too much to pin down: refused before it is applied. The scatter that
    // counts is PASS 2's. The final ratio is pass 1's slope plus the slope pass 2 measures on the
    // corrected pair, so pass 1's own error is removed by pass 2; pass 1's residual also carries the
    // smear of a window drifting inside itself (0.2 sample rms at 380 ppm, which pass 2 then removes:
    // the final ppm is exact to 0.02), so gating on it would refuse the pairs the ceiling admits.
    {
        double lo = residuals.front().positionSamples, hi = lo;
        for (const auto& p : residuals) {
            lo = std::min(lo, p.positionSamples);
            hi = std::max(hi, p.positionSamples);
        }
        const double u = detail::predictedDriftError(fit2->residualRms, tally2.accepted, hi - lo, rangeLength);
        if (u > kMaxUncertainty) {
            ClockResult r = refuse(ClockRefusal::ClockUncertain);
            r.uncertaintySamples = u;
            r.uncertaintyLimit = kMaxUncertainty;
            return r;
        }
        estimate.uncertaintySamples = u;
    }

    // E7 needs pass 2 to agree: a slope that adds up to more than the shared-clock
    // threshold over the range means the clock was NOT shared after all.
    if (shared && std::abs(fit2->slope) * static_cast<double>(rangeLength) <= kSharedClockSamples) {
        // E7: only the integer offset, the full-band residual rounded into it.
        const double delay = sharedDelay + std::round(fit2->intercept);
        estimate.sharedClock = true;
        estimate.mapping = {delay, 1.0, true};
        estimate.offsetSeconds = delay / fs;
        ClockResult shared_r;
        shared_r.estimate = estimate;
        return shared_r;
    }

    // The corrected pair's delay is d2(x) = b2 + eps2 x: reference sample n pairs
    // with corrected sample n + d2, which IS measurement position t1(n + d2):
    //   t(n) = t1(rangeStart + x + d2(x))
    //        = first.offset + (rangeStart + x + b2 + eps2 x) first.ratio
    // -- for a drifting pair first = (rangeStart + b) + x (1 + eps), so
    //   t(n) = (rangeStart + b) + b2 (1 + eps) + x (1 + eps)(1 + eps2).
    ClockMapping composed;
    composed.integerOnly = false;
    composed.ratio = first.ratio * (1.0 + fit2->slope);
    const double atRangeStart = first.offsetSamples + (rangeStartD + fit2->intercept) * first.ratio;
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
