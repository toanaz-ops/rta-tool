// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/meter/Detector.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::meter {

namespace {

/// Seconds, per the decision record (docs/dsp/2026-08-27-weighting-and-meters.md):
/// Fast/Slow are IEC 61672-1 clause 5; Impulse is the two-tau approximation
/// of the historical circuit, not an IEC-normative value.
double riseSeconds(TimeWeighting w) noexcept {
    switch (w) {
        case TimeWeighting::Fast: return 0.125;
        case TimeWeighting::Slow: return 1.0;
        case TimeWeighting::Impulse: return 0.035;
    }
    return 0.125;  // unreachable; keeps MSVC quiet about a missing return
}

double decaySeconds(TimeWeighting w) noexcept {
    switch (w) {
        case TimeWeighting::Fast: return 0.125;
        case TimeWeighting::Slow: return 1.0;
        case TimeWeighting::Impulse: return 1.5;
    }
    return 0.125;
}

/// alpha = 1 - exp(-1/(fs*tau)), computed in double from the exact
/// exponential -- NOT the first-order approximation T/tau. The plan measured
/// the two diverging in the 4th digit at Fast/8 kHz, and this code has no
/// business caring what sample rate it is at.
double computeAlpha(double sampleRate, double tau) noexcept {
    return 1.0 - std::exp(-1.0 / (sampleRate * tau));
}

}  // namespace

std::string_view toString(TimeWeighting weighting) noexcept {
    switch (weighting) {
        case TimeWeighting::Fast: return "Fast";
        case TimeWeighting::Slow: return "Slow";
        case TimeWeighting::Impulse: return "Impulse";
    }
    return "Fast";
}

Detector::Detector(TimeWeighting weighting, double sampleRate)
    : weighting_(weighting), sampleRate_(sampleRate) {
    if (sampleRate <= 0.0) throw std::invalid_argument("Detector: sampleRate must be > 0");
    alphaRise_ = computeAlpha(sampleRate_, riseSeconds(weighting_));
    alphaDecay_ = computeAlpha(sampleRate_, decaySeconds(weighting_));
}

void Detector::reset(double initialMeanSquare) noexcept { meanSquare_ = initialMeanSquare; }

double Detector::processSample(float x) noexcept {
    const double squared = static_cast<double>(x) * static_cast<double>(x);
    // Fast/Slow have alphaRise_ == alphaDecay_, so the branch is a no-op for
    // them; Impulse is the only weighting where it selects a different tau.
    const double alpha = (squared > meanSquare_) ? alphaRise_ : alphaDecay_;
    meanSquare_ += alpha * (squared - meanSquare_);
    return meanSquare_;
}

double Detector::process(std::span<const float> in) noexcept {
    for (float x : in) processSample(x);
    return meanSquare_;
}

void Detector::process(std::span<const float> in, std::span<double> outMeanSquare) noexcept {
    const std::size_t n = std::min(in.size(), outMeanSquare.size());
    for (std::size_t i = 0; i < n; ++i) outMeanSquare[i] = processSample(in[i]);
}

double Detector::meanSquare() const noexcept { return meanSquare_; }

double Detector::levelDb(double referenceOffsetDb) const noexcept {
    if (meanSquare_ <= 0.0) return kLevelFloorDb + referenceOffsetDb;
    const double db = 10.0 * std::log10(meanSquare_);
    return (db < kLevelFloorDb) ? (kLevelFloorDb + referenceOffsetDb) : (db + referenceOffsetDb);
}

double Detector::riseTimeConstant(TimeWeighting weighting) noexcept {
    return riseSeconds(weighting);
}

double Detector::decayTimeConstant(TimeWeighting weighting) noexcept {
    return decaySeconds(weighting);
}

double Detector::sampleRate() const noexcept { return sampleRate_; }

TimeWeighting Detector::weighting() const noexcept { return weighting_; }

}  // namespace rta::meter
