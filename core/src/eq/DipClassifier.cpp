// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/eq/DipClassifier.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::eq {

namespace {

/// r_D = (10^(D/20) - 1) / (10^(D/20) + 1) (record Sec.4.3.6). D is clamped
/// to >= 0 first: a negative "depth" means fStar was not actually a local
/// minimum of residualDb relative to its flanks, which has no r_D reading --
/// clamped to 0 gives r_D = 0, S* = 0, so a malformed region reads as "any
/// swing at all is enough to call it NotMinimumPhase", the conservative
/// direction (never silently Boostable on bad input).
double reflectionCoefficient(double depthDb) noexcept {
    const double d = std::max(depthDb, 0.0);
    const double ratio = std::pow(10.0, d / 20.0);
    return (ratio - 1.0) / (ratio + 1.0);
}

}  // namespace

DipClassification classifyDip(const dsp::ExcessPhaseResult& xp, std::span<const float> hz,
                              std::size_t fL, std::size_t fStar, std::size_t fR,
                              std::span<const float> residualDb, bool isBoost) {
    // isBoost does not enter this function's own math -- see DipClassifier.h's
    // doc comment (B3/B4: the classifier reports, EqAllocator gates).
    (void)isBoost;

    if (hz.size() != residualDb.size()) {
        throw std::invalid_argument("classifyDip: hz and residualDb must be the same length");
    }
    if (!(fL <= fStar && fStar <= fR) || fR >= hz.size()) {
        throw std::invalid_argument("classifyDip: require fL <= fStar <= fR < hz.size()");
    }

    // xp.valid == false is checked BEFORE xp.excessPhaseRad's own length: an
    // invalid excessPhase() result carries empty arrays by design
    // (ExcessPhase.h's "never a curve" contract), so this is the FIRST
    // check that can legitimately see a length mismatch against hz.
    if (!xp.valid) {
        return DipClassification{ DipVerdict::Untrusted, 0.0, 0.0, 0.0 };
    }
    if (xp.excessPhaseRad.size() != hz.size()) {
        throw std::invalid_argument("classifyDip: xp.excessPhaseRad must match hz's length");
    }

    const double depthDb =
        std::min(static_cast<double>(residualDb[fL]), static_cast<double>(residualDb[fR])) -
        static_cast<double>(residualDb[fStar]);

    double swingMax = xp.excessPhaseRad[fL];
    double swingMin = xp.excessPhaseRad[fL];
    for (std::size_t k = fL; k <= fR; ++k) {
        swingMax = std::max(swingMax, static_cast<double>(xp.excessPhaseRad[k]));
        swingMin = std::min(swingMin, static_cast<double>(xp.excessPhaseRad[k]));
    }
    const double swingRad = swingMax - swingMin;

    const double r = reflectionCoefficient(depthDb);
    const double thresholdRad = 2.0 * std::asin(std::clamp(r, 0.0, 1.0));

    const DipVerdict verdict = (swingRad < thresholdRad) ? DipVerdict::Boostable
                                                          : DipVerdict::NotMinimumPhase;
    return DipClassification{ verdict, depthDb, swingRad, thresholdRad };
}

}  // namespace rta::eq
