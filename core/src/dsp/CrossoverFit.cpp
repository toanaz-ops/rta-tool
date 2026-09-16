// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/CrossoverFit.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rta::dsp {
namespace {

/// Is bin k measured on both sides and above the gate?
///
/// An absent coherence is ABSENT, not unity. std::nullopt means the estimator
/// was below minimumEffectiveAverages, which is a statement about how little is
/// known, not about how coherent the bin is; treating it as gamma^2 == 1 is the
/// silent fallback that quietly restores a fixed defect.
bool gated(const std::optional<std::vector<float>>& coherenceA,
           const std::optional<std::vector<float>>& coherenceB, std::size_t k, double floorValue) {
    if (!coherenceA.has_value() || !coherenceB.has_value()) return false;
    if (k >= coherenceA->size() || k >= coherenceB->size()) return false;
    return static_cast<double>((*coherenceA)[k]) >= floorValue
           && static_cast<double>((*coherenceB)[k]) >= floorValue;
}

}  // namespace

SpectralCrossover spectralCrossover(std::span<const float> magnitudeDbA,
                                    std::span<const float> magnitudeDbB,
                                    const std::optional<std::vector<float>>& coherenceA,
                                    const std::optional<std::vector<float>>& coherenceB,
                                    double binWidthHz, double minimumGatedCoherence,
                                    std::optional<double> seedHz) {
    SpectralCrossover result;

    const std::size_t n = std::min(magnitudeDbA.size(), magnitudeDbB.size());
    if (n < 2) {
        result.refusal = CrossoverRefusal::TooFewBins;
        return result;
    }

    std::size_t gatedBins = 0;
    for (std::size_t k = 0; k < n; ++k) {
        if (gated(coherenceA, coherenceB, k, minimumGatedCoherence)) ++gatedBins;
    }
    if (gatedBins == 0) {
        result.refusal = CrossoverRefusal::AllBinsAbsent;
        return result;
    }
    if (gatedBins < 2) {
        result.refusal = CrossoverRefusal::TooFewBins;
        return result;
    }

    // Only ADJACENT gated pairs. Interpolating across an ungated gap would
    // invent a crossover frequency out of bins nobody measured, which is the
    // one thing a gate exists to prevent.
    for (std::size_t k = 0; k + 1 < n; ++k) {
        if (!gated(coherenceA, coherenceB, k, minimumGatedCoherence)
            || !gated(coherenceA, coherenceB, k + 1, minimumGatedCoherence)) {
            continue;
        }
        const double d0 = static_cast<double>(magnitudeDbA[k]) - static_cast<double>(magnitudeDbB[k]);
        const double d1 =
            static_cast<double>(magnitudeDbA[k + 1]) - static_cast<double>(magnitudeDbB[k + 1]);

        if (d0 == 0.0) {
            result.allCrossingsHz.push_back(static_cast<double>(k) * binWidthHz);
            continue;
        }
        if ((d0 < 0.0) == (d1 < 0.0) || d1 == 0.0) continue;

        // Linear in dB against linear in frequency -- the same two-point form
        // core/src/ir/Polarity.cpp:39-48 uses, and with a strict sign change
        // t lies in the OPEN interval (0, 1), so there is nothing to clamp.
        const double t = -d0 / (d1 - d0);
        result.allCrossingsHz.push_back((static_cast<double>(k) + t) * binWidthHz);
    }

    if (result.allCrossingsHz.empty()) {
        result.refusal = CrossoverRefusal::NoCrossing;
        return result;
    }

    result.refusal = CrossoverRefusal::None;
    result.frequencyHz = result.allCrossingsHz.front();
    if (seedHz.has_value() && *seedHz > 0.0) {
        double best = std::numeric_limits<double>::infinity();
        for (const double f : result.allCrossingsHz) {
            if (f <= 0.0) continue;
            const double distance = std::abs(std::log(f / *seedHz));
            if (distance < best) {
                best = distance;
                result.frequencyHz = f;
            }
        }
    }
    return result;
}

}  // namespace rta::dsp
