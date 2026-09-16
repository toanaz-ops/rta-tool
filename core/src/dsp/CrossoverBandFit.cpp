// SPDX-License-Identifier: AGPL-3.0-or-later
//
// crossoverBandFit. Task B's spectralCrossover lives in CrossoverFit.cpp; the
// two share one header and nothing else, because together they would push a
// single file past the 400-line cap the acceptance gate enforces.
#include "rta/dsp/CrossoverFit.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace rta::dsp {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;
constexpr double kPi = std::numbers::pi;

/// Wrap to (-pi, pi]. std::arg returns [-pi, pi] and hands back -pi for a
/// negative real, which is the SAME angle as +pi -- reporting it as -pi would
/// make an intercept of 180 degrees compare as 2*pi away from record Sec.3's
/// 180-degree row.
double wrapToPiHalfOpen(double radians) noexcept {
    double wrapped = std::remainder(radians, kTwoPi);
    if (wrapped <= -kPi) wrapped += kTwoPi;
    return wrapped;
}

/// The gated, windowed bins, reduced to what the search actually needs.
struct Band {
    std::vector<double> frequencyHz;
    std::vector<std::complex<double>> unitRelative;  // R_k, on the unit circle
    std::vector<double> weight;                      // w_k >= 0
    double weightSum = 0.0;
    double meanFrequencyHz = 0.0;
    std::size_t binsInWindow = 0;
};

std::complex<double> sumAt(const Band& band, double tau) noexcept {
    std::complex<double> total{ 0.0, 0.0 };
    for (std::size_t i = 0; i < band.frequencyHz.size(); ++i) {
        total += band.weight[i] * band.unitRelative[i]
                 * std::polar(1.0, -kTwoPi * band.frequencyHz[i] * tau);
    }
    return total;
}

/// Vertex of the parabola through three equally spaced samples, as an offset in
/// grid steps, clamped to the sample interval. Returns 0 when the three are
/// collinear or the middle is not the largest.
double parabolicOffset(double left, double centre, double right) noexcept {
    const double denominator = left - 2.0 * centre + right;
    if (denominator == 0.0) return 0.0;
    const double offset = 0.5 * (left - right) / denominator;
    return std::clamp(offset, -1.0, 1.0);
}

}  // namespace

BandFit crossoverBandFit(std::span<const std::complex<double>> hA,
                         std::span<const std::complex<double>> hB,
                         const std::optional<std::vector<float>>& coherenceA,
                         const std::optional<std::vector<float>>& coherenceB, double binWidthHz,
                         const BandFitOptions& options) {
    BandFit fit;

    const std::size_t n = std::min(hA.size(), hB.size());
    const double spread = std::pow(2.0, options.octavesEachSide);
    const double lowHz = options.centreHz / spread;
    const double highHz = options.centreHz * spread;

    // A relative epsilon on the edges, so a window whose edges land exactly on
    // a bin (40 and 160 at 1 Hz bins, say) contains 121 bins and not 119. The
    // count is load-bearing: the agreement floor is a function of N.
    constexpr double kEdgeEpsilon = 1e-12;

    Band band;
    const double rotationPerHz =
        options.sampleRate > 0.0
            ? kTwoPi * options.appliedDelayDifferenceSamples / options.sampleRate
            : 0.0;

    for (std::size_t k = 0; k < n; ++k) {
        const double f = static_cast<double>(k) * binWidthHz;
        if (f < lowHz * (1.0 - kEdgeEpsilon) || f > highHz * (1.0 + kEdgeEpsilon)) continue;
        ++band.binsInWindow;

        if (!coherenceA.has_value() || !coherenceB.has_value()) continue;
        if (k >= coherenceA->size() || k >= coherenceB->size()) continue;
        const double gammaA = static_cast<double>((*coherenceA)[k]);
        const double gammaB = static_cast<double>((*coherenceB)[k]);
        if (gammaA < options.minimumGatedCoherence || gammaB < options.minimumGatedCoherence) {
            continue;
        }

        // Undo the difference in appliedDelaySamples BEFORE forming R. A
        // capture delayed by D samples carries e^{-j2 pi f D/fs}, so the
        // correction is the conjugate of that difference on the B side.
        const std::complex<double> hBCorrected = hB[k] * std::polar(1.0, rotationPerHz * f);
        const double magA = std::abs(hA[k]);
        const double magB = std::abs(hBCorrected);
        if (magA <= 0.0 || magB <= 0.0) continue;  // a null bin has no relative phase

        band.frequencyHz.push_back(f);
        band.unitRelative.push_back(hA[k] * std::conj(hBCorrected) / (magA * magB));
        // The summation cross-term, gated. No constant is read off any grid.
        band.weight.push_back(std::min(gammaA, gammaB) * magA * magB);
    }

    if (band.binsInWindow == 0) {
        fit.refusal = CrossoverRefusal::RangeEmpty;
        return fit;
    }
    if (band.frequencyHz.empty()) {
        fit.refusal = CrossoverRefusal::AllBinsAbsent;
        return fit;
    }
    if (band.frequencyHz.size() < 2) {
        // A fit needs two distinct frequencies to separate a delay from a
        // constant. One bin would read agreement == 1 for every tau, which is
        // a statement about arithmetic and not about the loudspeakers.
        fit.refusal = CrossoverRefusal::TooFewBins;
        return fit;
    }

    for (std::size_t i = 0; i < band.weight.size(); ++i) {
        band.weightSum += band.weight[i];
        band.meanFrequencyHz += band.weight[i] * band.frequencyHz[i];
    }
    if (band.weightSum <= 0.0) {
        fit.refusal = CrossoverRefusal::AllBinsAbsent;
        return fit;
    }
    band.meanFrequencyHz /= band.weightSum;
    fit.meanFrequencyHz = band.meanFrequencyHz;

    const double grid = options.tauGridSeconds > 0.0 ? options.tauGridSeconds : 1.0e-6;
    const std::ptrdiff_t half =
        options.tauRangeSeconds > 0.0
            ? static_cast<std::ptrdiff_t>(std::llround(options.tauRangeSeconds / grid))
            : 0;

    std::vector<double> magnitude(static_cast<std::size_t>(2 * half + 1));
    for (std::ptrdiff_t i = -half; i <= half; ++i) {
        magnitude[static_cast<std::size_t>(i + half)] =
            std::abs(sumAt(band, static_cast<double>(i) * grid));
    }

    // Every local maximum is a candidate, the global one included. Ranking them
    // and returning the top few is the honest form: the winner alone would hide
    // that a competitor was almost as good.
    std::vector<DelayCandidateTau> candidates;
    const std::size_t points = magnitude.size();
    // INTERIOR maxima only. The two ends of a bounded search are not local
    // maxima of |S| -- they are where the caller stopped looking, and offering
    // one as a competing delay would report the search range as a measurement.
    for (std::size_t i = 1; i + 1 < points; ++i) {
        if (magnitude[i - 1] >= magnitude[i] || magnitude[i + 1] >= magnitude[i]) continue;
        const double tau = (static_cast<double>(i) - static_cast<double>(half)) * grid
                           + parabolicOffset(magnitude[i - 1], magnitude[i], magnitude[i + 1])
                                 * grid;
        candidates.push_back({ tau, std::abs(sumAt(band, tau)) / band.weightSum });
    }
    if (candidates.empty()) {
        // No interior maximum: a single grid point (tauRangeSeconds == 0), a
        // perfectly flat |S|, or a peak pinned against the search bound. Report
        // the largest sample as-is rather than inventing a refinement for it.
        const auto best = std::max_element(magnitude.begin(), magnitude.end());
        const auto index = static_cast<double>(std::distance(magnitude.begin(), best));
        const double tau = (index - static_cast<double>(half)) * grid;
        candidates.push_back({ tau, std::abs(sumAt(band, tau)) / band.weightSum });
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const DelayCandidateTau& x, const DelayCandidateTau& y) {
                         return x.agreement > y.agreement;
                     });

    fit.tauSeconds = candidates.front().tauSeconds;
    fit.agreement = candidates.front().agreement;
    fit.interceptRadians = wrapToPiHalfOpen(std::arg(sumAt(band, fit.tauSeconds)));

    const std::size_t keep = options.maxCycleCandidates > 0
                                 ? static_cast<std::size_t>(options.maxCycleCandidates)
                                 : std::size_t{ 1 };
    if (candidates.size() > keep) candidates.resize(keep);
    fit.cycleCandidates = std::move(candidates);
    fit.refusal = CrossoverRefusal::None;
    return fit;
}

}  // namespace rta::dsp
