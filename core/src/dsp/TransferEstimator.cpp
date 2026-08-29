// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/TransferEstimator.h"

#include <algorithm>
#include <cmath>

namespace rta::dsp {

std::complex<double> estimateH1(std::complex<double> sxy, double sxx) noexcept {
    if (!(sxx > 0.0)) {
        return {0.0, 0.0};
    }
    return sxy / sxx;
}

std::complex<double> estimateH2(std::complex<double> sxy, double syy) noexcept {
    if (std::abs(sxy) == 0.0) {
        return {0.0, 0.0};
    }
    return syy / std::conj(sxy);
}

std::complex<double> estimateHv(std::complex<double> sxy, double sxx, double syy) noexcept {
    if (std::abs(sxy) == 0.0) {
        return {0.0, 0.0};
    }
    // Total least squares: minimises the perpendicular distance to the fit
    // rather than assuming all the noise sits on one channel (that is what
    // makes it split the difference between H1 and H2 -- see decision record
    // §1). The quantity under the root is (Syy-Sxx)^2 + 4|Sxy|^2, a sum of
    // squares, so it is real and non-negative by construction; only the
    // division by conj(Sxy) needs complex arithmetic.
    const double diff = syy - sxx;
    const double radicand = diff * diff + 4.0 * std::norm(sxy);
    const double numeratorReal = diff + std::sqrt(radicand);
    return numeratorReal / (2.0 * std::conj(sxy));
}

double magnitudeSquaredCoherence(std::complex<double> sxy, double sxx, double syy) noexcept {
    if (!(sxx > 0.0) || !(syy > 0.0)) {
        return 0.0;
    }
    const double gamma2 = std::norm(sxy) / (sxx * syy);
    // Rounding can push this fractionally above 1 (norm(sxy) and sxx*syy are
    // computed from independently-rounded accumulators), and a coherence
    // above one is a nonsense the view would happily draw -- clamp it away.
    return std::clamp(gamma2, 0.0, 1.0);
}

namespace {

std::complex<double> estimate(Estimator estimator, std::complex<double> sxy, double sxx,
                               double syy) noexcept {
    switch (estimator) {
        case Estimator::H1:
            return estimateH1(sxy, sxx);
        case Estimator::H2:
            return estimateH2(sxy, syy);
        case Estimator::Hv:
            return estimateHv(sxy, sxx, syy);
    }
    return {0.0, 0.0};  // unreachable, but MSVC /W4 wants every path to return
}

}  // namespace

TransferSnapshot makeSnapshot(const DualFftEngine& engine, Estimator estimator) {
    TransferSnapshot snapshot;
    snapshot.estimator = estimator;
    snapshot.sampleRate = engine.config().sampleRate;
    snapshot.binWidthHz = engine.binWidthHz();
    snapshot.effectiveAverages = engine.effectiveAverages();

    const auto sxx = engine.referencePsd();
    const auto syy = engine.measurementPsd();
    const auto sxy = engine.crossPsd();
    const std::size_t bins = engine.numBins();

    snapshot.h.resize(bins);
    snapshot.magnitudeDb.resize(bins);
    snapshot.phaseRadians.resize(bins);

    // Decision record §3: below the declared minimum, coherence is exactly
    // 1.0 at every bin for a single un-averaged frame (|X*Y|^2 == |X|^2|Y|^2
    // identically), so a completely broken engine would look flawless. The
    // gate is on the EFFECTIVE average count (task 1), never the raw frame
    // count, because overlapped frames are not independent averages.
    const bool coherenceUnlocked =
        snapshot.effectiveAverages >= engine.config().minimumEffectiveAverages;
    if (coherenceUnlocked) {
        snapshot.coherence = std::vector<float>(bins);
    }

    for (std::size_t k = 0; k < bins; ++k) {
        const std::complex<double> h = estimate(estimator, sxy[k], sxx[k], syy[k]);
        snapshot.h[k] = h;

        const double magnitude = std::abs(h);
        const double db = magnitude > 0.0
                               ? 20.0 * std::log10(magnitude)
                               : static_cast<double>(TransferSnapshot::kMagnitudeFloorDb);
        snapshot.magnitudeDb[k] =
            std::max(static_cast<float>(db), TransferSnapshot::kMagnitudeFloorDb);

        // std::atan2 already returns in (-pi, pi], and atan2(0, 0) == 0.0 --
        // finite, not NaN -- so a silent channel (h == {0,0}) still produces a
        // well-defined phase rather than propagating a NaN onto the display.
        snapshot.phaseRadians[k] = static_cast<float>(std::atan2(h.imag(), h.real()));

        if (coherenceUnlocked) {
            (*snapshot.coherence)[k] =
                static_cast<float>(magnitudeSquaredCoherence(sxy[k], sxx[k], syy[k]));
        }
    }

    return snapshot;
}

}  // namespace rta::dsp
