// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DelayPolicy.h"

#include "PhatCorrelation.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rta::dsp {

namespace {

/// Whether `policy` narrowed the search window below the full linear range.
/// Distinguishes the two refusal reasons (record sec.3, sec.4): a restricted
/// window with nothing above even the STRUCTURAL (1x) null floor is
/// `WindowEmpty` -- the operator said where to look and nothing resembling a
/// real peak turned up there at all. The default, unrestricted window never
/// reports `WindowEmpty` for this reason; whatever the single global best
/// candidate is, the stricter `acceptMultiple * nullFloor` gate alone decides
/// `Accepted` vs `BelowFloor`.
[[nodiscard]] bool isRestrictedWindow(const DelayPolicy& policy) noexcept {
    return policy.minLag != std::numeric_limits<std::ptrdiff_t>::min() ||
           policy.maxLag != std::numeric_limits<std::ptrdiff_t>::max();
}

}  // namespace

DelaySuggestion suggestDelay(std::span<const float> reference, std::span<const float> measurement,
                              const PhatOptions& options, const DelayPolicy& policy) {
    detail::validateSpans(reference, measurement, options.minHz, options.maxHz);

    const auto phat = detail::computePhatCorrelation(reference, measurement, options.sampleRate,
                                                       options.regularisation, options.minHz,
                                                       options.maxHz);

    DelaySuggestion suggestion;

    // The null floor (record sec.4) needs only m (the padded transform
    // length) and M_in (the in-band bin count) -- both closed-form, both
    // known before any peak is picked. No in-band bin at all means nothing
    // can ever clear any threshold; 1.0 is an unreachable sentinel (trust is
    // bounded to [0,1] by construction) rather than a division by zero.
    suggestion.nullFloor = (phat.inBandBins > 0)
            ? std::sqrt(std::log(static_cast<double>(phat.m)) /
                        static_cast<double>(phat.inBandBins))
            : 1.0;

    // f_band: the same resolved-band fraction findDelayPhat's own band-limit
    // fixture pins (test_delay_finder.cpp's "the band limit restricts what is
    // correlated"). trust = peak/f_band undoes PHAT's own band normalisation
    // so a narrowband system does not read as untrustworthy purely because it
    // was asked about on a narrow band (record sec.7).
    const double loMinHz = std::max(options.minHz, 0.0);
    const double loMaxHz = (options.maxHz > 0.0) ? options.maxHz : (options.sampleRate * 0.5);
    const double fBand = (loMaxHz - loMinHz) / (options.sampleRate * 0.5);

    const int maxCandidates = std::max(policy.maxCandidates, 0);
    const auto peaks = detail::pickPeaks(phat.correlation, phat.m, policy.minLag, policy.maxLag,
                                          maxCandidates);

    if (peaks.empty()) {
        suggestion.verdict = DelayVerdict::WindowEmpty;
        return suggestion;
    }

    const double bestPeakAbs = std::abs(peaks.front().height);
    const double bestTrust = (fBand > 0.0) ? (bestPeakAbs / fBand) : 0.0;

    if (isRestrictedWindow(policy) && bestTrust < suggestion.nullFloor) {
        // Nothing in the STATED window even clears the structural (1x) floor
        // -- report the stronger refusal and leave best/candidates at their
        // defaults rather than offer a number nobody should read.
        suggestion.trust = bestTrust;
        suggestion.verdict = DelayVerdict::WindowEmpty;
        return suggestion;
    }

    suggestion.best.delaySamples = peaks.front().lag;
    suggestion.best.subSample = peaks.front().subSample;
    suggestion.best.peak = bestPeakAbs;
    suggestion.best.inverted = peaks.front().height < 0.0;

    suggestion.candidates.reserve(peaks.size());
    for (const auto& p : peaks) {
        suggestion.candidates.push_back(
                DelayCandidate{p.lag, p.subSample, p.height, p.height < 0.0});
    }
    if (peaks.size() >= 2) {
        suggestion.ambiguity = std::abs(peaks[1].height) / bestPeakAbs;
    }

    suggestion.trust = bestTrust;
    suggestion.verdict = (bestTrust >= policy.acceptMultiple * suggestion.nullFloor)
            ? DelayVerdict::Accepted
            : DelayVerdict::BelowFloor;
    return suggestion;
}

}  // namespace rta::dsp
