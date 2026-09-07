// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DelayFinder.h"

#include "PhatCorrelation.h"

namespace rta::dsp {

// Re-expressed on the shared spectral half (DEL-R1,
// docs/plans/2026-09-07-L7-delay-impl-plan.md task A): whiten-and-weight,
// IFFT and peak-refinement now live in PhatCorrelation.h, shared with
// suggestDelay (DelayPolicy.cpp) and ResidualDelayTracker
// (ResidualTracker.cpp). This function's signature, its arithmetic and its
// nine fixtures (test_delay_finder.cpp) are the refactor lock: a single best
// pick over the FULL linear range with no amplitude floor -- detail::
// pickBestPeak is the exact single-pass `a > bestAbs` argmax the original
// code ran inline, moved verbatim, so this reproduces the pre-refactor
// output bit-for-bit, including the silence case (an all-zero correlation
// still has SOME index 0, and that is what both the old loop and this one
// report).
DelayEstimate findDelayPhat(std::span<const float> reference, std::span<const float> measurement,
                             const PhatOptions& options) {
    detail::validateSpans(reference, measurement, options.minHz, options.maxHz);

    const auto phat = detail::computePhatCorrelation(reference, measurement, options.sampleRate,
                                                       options.regularisation, options.minHz,
                                                       options.maxHz);

    const auto peak = detail::pickBestPeak(phat.correlation, phat.m);

    DelayEstimate result;
    result.delaySamples = peak.lag;
    result.subSample = peak.subSample;
    result.peak = std::abs(peak.height);
    result.inverted = peak.height < 0.0;
    return result;
}

}  // namespace rta::dsp
