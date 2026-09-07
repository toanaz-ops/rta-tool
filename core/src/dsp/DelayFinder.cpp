// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DelayFinder.h"

#include "PhatCorrelation.h"

#include <limits>

namespace rta::dsp {

// Re-expressed on the shared spectral half (DEL-R1,
// docs/plans/2026-09-07-L7-delay-impl-plan.md task A): whiten-and-weight,
// IFFT and peak-refinement now live in PhatCorrelation.h, shared with
// suggestDelay (DelayPolicy.cpp) and ResidualDelayTracker
// (ResidualTracker.cpp). This function's signature, its arithmetic and its
// nine fixtures (test_delay_finder.cpp) are the refactor lock: a single best
// pick over the FULL linear range with no amplitude floor -- detail::
// pickPeaks(..., maxCandidates=1) with an unrestricted window and the same
// tie-break (stable sort keeps the lowest index) as the original single-pass
// `a > peakAbs` argmax, so this reproduces the pre-refactor output
// bit-for-bit, including the silence case (an all-zero correlation yields no
// candidate above nothing, and `peaks.empty()` leaves `result` at its
// default-constructed {0, 0.0, 0.0, false} -- exactly what the original loop
// computed for that input too).
DelayEstimate findDelayPhat(std::span<const float> reference, std::span<const float> measurement,
                             const PhatOptions& options) {
    detail::validateSpans(reference, measurement, options.minHz, options.maxHz);

    const auto phat = detail::computePhatCorrelation(reference, measurement, options.sampleRate,
                                                       options.regularisation, options.minHz,
                                                       options.maxHz);

    const auto peaks = detail::pickPeaks(phat.correlation, phat.m,
                                          std::numeric_limits<std::ptrdiff_t>::min(),
                                          std::numeric_limits<std::ptrdiff_t>::max(), 1);

    DelayEstimate result;
    if (!peaks.empty()) {
        result.delaySamples = peaks.front().lag;
        result.subSample = peaks.front().subSample;
        result.peak = std::abs(peaks.front().height);
        result.inverted = peaks.front().height < 0.0;
    }
    return result;
}

}  // namespace rta::dsp
