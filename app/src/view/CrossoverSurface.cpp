// SPDX-License-Identifier: AGPL-3.0-or-later
#include "view/CrossoverSurface.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rta::view {
namespace {

/// Two coherent sources at the same level, in phase: |1 + 1| = 2.
const double kCoherentSumDb = 20.0 * std::log10(2.0);

/// sqrt(2), the in-phase sum of an even-order Butterworth pair at fc.
const double kQuadratureSumDb = 20.0 * std::log10(std::sqrt(2.0));

/// What the ASKED topology is designed to sum to at fc -- a MARK, not a
/// criterion.
///
///   Linkwitz-Riley, any N: 0 dB. LR-N is the BW-(N/2) cascaded with itself,
///     so |L + H| == 1 at every frequency (Linkwitz 1976; locked by the plan's
///     D3 / core/tests/test_align_order4_identity.cpp case 3).
///   Butterworth, N odd: 0 dB. The odd-order pair sums flat in EITHER
///     polarity, which is exactly what makes "maximise the sum" a FLAT
///     objective on this row (record Sec.6.2, plan D4).
///   Butterworth, N even: +3.0103 dB. At order 2 that is |L - H| at fc, the
///     row station-1 research had backwards (record Sec.1.1, plan D2); at
///     order 4 it is the in-phase sum PR #3 measured as the expansion
///     `1.41421356237309537 < 1e-15` when the L4a claim was asserted instead
///     (probe 2026-09-15 Sec.7).
double designedSumDbFor(rta::measure::Topology topology) noexcept {
    if (topology.family == rta::measure::CrossoverFamily::LinkwitzRiley) return 0.0;
    return (topology.order % 2 == 0) ? kQuadratureSumDb : 0.0;
}

}  // namespace

void CrossoverSurface::setSources(rta::trace::VirtualTrace highSide,
                                 rta::trace::VirtualTrace lowSide) {
    highSource_ = std::move(highSide);
    lowSource_ = std::move(lowSide);
    binWidthHz_ = highSource_->binWidthHz();

    // The ghost is taken HERE, from the sources exactly as handed in, before
    // any pending op exists. Recomputing it later would quietly move the line
    // the operator is trying to improve on -- and then every change would look
    // like no change at all.
    ghostDb_ = rta::trace::sumOf(*highSource_, *lowSource_).magnitudeDb;

    recomputePrediction();
}

void CrossoverSurface::setAskedTopology(rta::measure::Topology topology,
                                        rta::measure::ProcessorInversion inversion) {
    askedTopology_ = topology;
    askedInversion_ = inversion;
    target_ = rta::measure::expectedOffset(topology, inversion);
    marks_.coherentSumDb = kCoherentSumDb;
    marks_.designedSumDb = designedSumDbFor(topology);
}

void CrossoverSurface::setWindow(PhaseWindow window) {
    window_ = window;
    recomputePrediction();
}

void CrossoverSurface::setPendingOps(std::vector<rta::trace::VirtualOp> highSideChain,
                                     std::vector<rta::trace::VirtualOp> lowSideChain) {
    if (!highSource_.has_value() || !lowSource_.has_value()) return;
    highSource_->setChain(std::move(highSideChain));
    lowSource_->setChain(std::move(lowSideChain));
    recomputePrediction();
}

void CrossoverSurface::setMeasuredSum(std::vector<float> magnitudeDb) {
    measuredDb_ = std::move(magnitudeDb);
    recomputeGap();
}

void CrossoverSurface::recomputePrediction() {
    if (!highSource_.has_value() || !lowSource_.has_value()) return;

    const auto high = highSource_->render();
    const auto low = lowSource_->render();
    highDb_ = high.magnitudeDb;
    lowDb_ = low.magnitudeDb;

    // The sum goes through rta::dsp::sumResponses by way of rta::trace::sumOf
    // -- one summation in the codebase, and its per-bin trust is a
    // summationTrust and not a coherence (record Sec.5).
    const auto summed = rta::trace::sumOf(*highSource_, *lowSource_);
    predictedDb_ = summed.magnitudeDb;

    relativePhase_.clear();
    const std::size_t bins = std::min(high.h.size(), low.h.size());
    if (window_.centreHz > 0.0 && window_.octavesEachSide > 0.0 && binWidthHz_ > 0.0) {
        const double lowEdgeHz = window_.centreHz * std::pow(2.0, -window_.octavesEachSide);
        const double highEdgeHz = window_.centreHz * std::pow(2.0, +window_.octavesEachSide);
        for (std::size_t k = 0; k < bins; ++k) {
            const double hz = static_cast<double>(k) * binWidthHz_;
            if (hz < lowEdgeHz || hz > highEdgeHz) continue;
            // arg(H_A conj H_B) = arg(H_A) - arg(H_B), wrapped to (-pi, pi] by
            // std::arg. NO unwrap: the same refusal crossoverBandFit makes, for
            // the same reason -- an unwrap is a choice about cycles that the
            // measurement does not contain.
            relativePhase_.push_back({ hz, std::arg(high.h[k] * std::conj(low.h[k])) });
        }
    }

    recomputeGap();
}

void CrossoverSurface::recomputeGap() {
    if (measuredDb_.empty() || predictedDb_.empty()) {
        // Absent, not zeroed. An evidence-free surface must not render
        // bit-identical to a flawless one.
        gap_.reset();
        return;
    }

    SumGap gap;
    const std::size_t bins = std::min(measuredDb_.size(), predictedDb_.size());
    gap.perBinDb.resize(bins);
    double sumOfSquares = 0.0;
    for (std::size_t k = 0; k < bins; ++k) {
        const double delta =
            static_cast<double>(measuredDb_[k]) - static_cast<double>(predictedDb_[k]);
        gap.perBinDb[k] = static_cast<float>(delta);
        sumOfSquares += delta * delta;
        gap.maxAbsDb = std::max(gap.maxAbsDb, std::abs(delta));
    }
    gap.bins = bins;
    gap.rmsDb = bins > 0 ? std::sqrt(sumOfSquares / static_cast<double>(bins)) : 0.0;
    gap_ = std::move(gap);
}

}  // namespace rta::view
