// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
//
// DEL-R1 (docs/plans/2026-09-07-L7-delay-impl-plan.md): the spectral half of
// GCC-PHAT -- whiten-and-weight, IFFT, windowed pick of local maxima,
// parabolic refinement -- factored out of DelayFinder.cpp so `findDelayPhat`,
// `suggestDelay` (DelayPolicy.cpp) and `ResidualDelayTracker`
// (ResidualTracker.cpp) share ONE implementation rather than three. Header-
// only, namespace rta::dsp::detail, NOT installed under core/include: a
// private seam between three .cpp files in this directory, not a public API.
// See docs/dsp/2026-09-06-l7-auto-delay.md sec.2 for why one correlator with
// two front doors, and DelayFinder.h's own comments for the arithmetic this
// file lifts verbatim (the regularisation floor, the signed-triple parabola).
#pragma once

#include "rta/dsp/RealFft.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace rta::dsp::detail {

/// One candidate lag: signed integer lag, sub-sample refinement, and the
/// SIGNED correlation height at that lag (the sign carries polarity -- see
/// DelayEstimate::inverted). |subSample| <= 0.5.
struct SpectralPeak {
    std::ptrdiff_t lag = 0;
    double subSample = 0.0;
    double height = 0.0;
};

/// The one-shot's whitened, IFFT'd correlation and the bookkeeping its caller
/// needs afterwards: `m` (the padded transform length) and `inBandBins`
/// (M_in, the count of bins the band limit admitted) are exactly the two
/// quantities DelayPolicy.cpp's null-floor formula needs -- see the decision
/// record sec.4, "the null floor is sqrt(ln m / M_in)".
struct PhatResult {
    std::vector<float> correlation;
    std::size_t m = 0;
    std::size_t inBandBins = 0;
};

[[nodiscard]] inline double magnitude(const std::complex<float>& c) noexcept {
    return std::hypot(static_cast<double>(c.real()), static_cast<double>(c.imag()));
}

/// Shared by findDelayPhat and suggestDelay: both spans must be equal length
/// and non-empty, and a stated maxHz must not sit below minHz. 0 is the
/// "unset" sentinel for maxHz, so it only contradicts minHz once it names a
/// real, positive frequency below it -- the same rule DelayFinder.cpp always
/// enforced, now enforced once.
inline void validateSpans(std::span<const float> reference, std::span<const float> measurement,
                           double minHz, double maxHz) {
    if (reference.empty() || measurement.empty()) {
        throw std::invalid_argument("delay: spans must not be empty");
    }
    if (reference.size() != measurement.size()) {
        throw std::invalid_argument("delay: reference and measurement lengths differ");
    }
    if (maxHz > 0.0 && maxHz < minHz) {
        throw std::invalid_argument("delay: maxHz is below minHz");
    }
}

/// Signed lag for a circular index i in [0, m): [0, m/2) is non-negative,
/// [m/2, m) wraps to negative -- the standard DFT circular-shift reading.
[[nodiscard]] inline std::ptrdiff_t wrapLag(std::size_t i, std::size_t m) noexcept {
    return (i < m / 2) ? static_cast<std::ptrdiff_t>(i)
                        : static_cast<std::ptrdiff_t>(i) - static_cast<std::ptrdiff_t>(m);
}

[[nodiscard]] inline bool inWindow(std::ptrdiff_t lag, std::ptrdiff_t minLag,
                                    std::ptrdiff_t maxLag) noexcept {
    return lag >= minLag && lag <= maxLag;
}

/// Parabolic vertex on the three samples around index `idx`, fit on SIGNED
/// values -- see DelayFinder.h's DelayEstimate::subSample comment for why the
/// whole triple is negated by one sign (decided by the centre sample) rather
/// than an elementwise abs(). Verbatim port of DelayFinder.cpp's original
/// arithmetic, so every caller of this function reproduces it bit-for-bit.
[[nodiscard]] inline SpectralPeak refine(std::span<const float> correlation, std::size_t idx,
                                          std::size_t m) noexcept {
    SpectralPeak peak;
    peak.lag = wrapLag(idx, m);
    peak.height = static_cast<double>(correlation[idx]);

    const bool inverted = peak.height < 0.0;
    const std::size_t prevIndex = (idx + m - 1) % m;
    const std::size_t nextIndex = (idx + 1) % m;
    const double sign = inverted ? -1.0 : 1.0;
    const double rm = sign * static_cast<double>(correlation[prevIndex]);
    const double rc = sign * static_cast<double>(correlation[idx]);
    const double rp = sign * static_cast<double>(correlation[nextIndex]);

    const double denom = rm - 2.0 * rc + rp;
    if (denom != 0.0) {
        double delta = 0.5 * (rm - rp) / denom;
        delta = std::clamp(delta, -0.5, 0.5);
        peak.subSample = delta;
    }
    return peak;
}

/// Up to `maxCandidates` distinct candidates inside `[minLag, maxLag]`,
/// ranked by |height| descending (ties keep ascending-index order, via
/// std::stable_sort -- the same tie-break a single-pass `a > best` argmax
/// gives the lowest index, which is what makes findDelayPhat's single-best
/// call through this function reproduce its own pre-refactor output
/// bit-for-bit). No amplitude floor here -- that is a policy decision
/// (DelayPolicy.cpp), not a property of "what is a candidate". Two picks
/// closer than `kMinSeparation` samples apart are the same physical peak's
/// shoulder, not two arrivals, so the second is skipped.
[[nodiscard]] inline std::vector<SpectralPeak> pickPeaks(std::span<const float> correlation,
                                                           std::size_t m, std::ptrdiff_t minLag,
                                                           std::ptrdiff_t maxLag,
                                                           int maxCandidates) {
    std::vector<SpectralPeak> picks;
    if (maxCandidates <= 0 || m == 0) return picks;

    struct Candidate {
        std::size_t idx;
        double absHeight;
    };
    std::vector<Candidate> pool;
    pool.reserve(m);
    for (std::size_t i = 0; i < m; ++i) {
        if (!inWindow(wrapLag(i, m), minLag, maxLag)) continue;
        pool.push_back({i, std::abs(static_cast<double>(correlation[i]))});
    }
    if (pool.empty()) return picks;

    std::stable_sort(pool.begin(), pool.end(),
                      [](const Candidate& x, const Candidate& y) { return x.absHeight > y.absHeight; });

    constexpr std::size_t kMinSeparation = 2;
    for (const auto& cand : pool) {
        bool tooClose = false;
        for (const auto& kept : picks) {
            const std::size_t keptIdx = (kept.lag >= 0)
                    ? static_cast<std::size_t>(kept.lag)
                    : static_cast<std::size_t>(kept.lag + static_cast<std::ptrdiff_t>(m));
            std::size_t d = (cand.idx > keptIdx) ? cand.idx - keptIdx : keptIdx - cand.idx;
            d = std::min(d, m - d);
            if (d < kMinSeparation) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;
        picks.push_back(refine(correlation, cand.idx, m));
        if (static_cast<int>(picks.size()) >= maxCandidates) break;
    }
    return picks;
}

/// The whole PHAT pipeline short of peak-picking: zero-pad both spans to a
/// linear (non-circular) correlation length, forward-transform, form
/// G = conj(X)*Y masked to `[minHz, maxHz]`, weight by
/// `1/max(|G|, regularisation*max|G|)`, inverse-transform. Identical
/// arithmetic to DelayFinder.cpp's original single-function body -- see that
/// file's comments (now here) for why each step is shaped as it is.
[[nodiscard]] inline PhatResult computePhatCorrelation(std::span<const float> reference,
                                                         std::span<const float> measurement,
                                                         double sampleRate, double regularisation,
                                                         double minHz, double maxHz) {
    const std::size_t inputLen = reference.size();
    const std::size_t m = std::bit_ceil(std::max<std::size_t>(2 * inputLen, 4));

    std::vector<float> paddedX(m, 0.0f);
    std::vector<float> paddedY(m, 0.0f);
    std::copy(reference.begin(), reference.end(), paddedX.begin());
    std::copy(measurement.begin(), measurement.end(), paddedY.begin());

    RealFft fft(m);
    const std::size_t numBins = fft.numBins();
    std::vector<std::complex<float>> x(numBins);
    std::vector<std::complex<float>> y(numBins);
    fft.forward(paddedX, x);
    fft.forward(paddedY, y);

    const double loMinHz = std::max(minHz, 0.0);
    const double loMaxHz = (maxHz > 0.0) ? maxHz : (sampleRate * 0.5);

    std::vector<std::complex<float>> g(numBins);
    std::vector<double> magG(numBins);
    double maxAbsG = 0.0;
    std::size_t inBandBins = 0;
    for (std::size_t k = 0; k < numBins; ++k) {
        const double freq = static_cast<double>(k) * sampleRate / static_cast<double>(m);
        if (freq < loMinHz || freq > loMaxHz) {
            g[k] = {0.0f, 0.0f};
            magG[k] = 0.0;
            continue;
        }
        ++inBandBins;
        g[k] = std::conj(x[k]) * y[k];
        magG[k] = magnitude(g[k]);
        maxAbsG = std::max(maxAbsG, magG[k]);
    }

    const double floor = regularisation * maxAbsG;

    std::vector<std::complex<float>> weighted(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        if (magG[k] <= 0.0) {
            weighted[k] = {0.0f, 0.0f};
            continue;
        }
        const double denom = std::max(magG[k], floor);
        const float scale = static_cast<float>(1.0 / denom);
        weighted[k] = g[k] * scale;
    }

    PhatResult result;
    result.correlation.resize(m);
    fft.inverse(weighted, result.correlation);
    result.m = m;
    result.inBandBins = inBandBins;
    return result;
}

}  // namespace rta::dsp::detail
