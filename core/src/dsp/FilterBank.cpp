// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/FilterBank.h"

#include "rta/dsp/ButterworthDesign.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::dsp {

FilterBank::FilterBank(const Config& config) : config_(config) {
    if (!(config.sampleRate > 0.0)) {
        throw std::invalid_argument("FilterBank sampleRate must be positive");
    }
    if (!(config.timeConstantSeconds > 0.0)) {
        throw std::invalid_argument("FilterBank timeConstantSeconds must be positive");
    }

    const OctaveBands table(config.fraction, config.lowestHz, config.highestHz, config.base);

    // Section 2 of the plan: a band whose CENTRE has reached Nyquist cannot be
    // measured at all and is not constructed. A band whose upper edge alone
    // reaches it is still built -- ButterworthDesign::bandPass applies the
    // matching clamp internally and reports it via Result::nyquistClamped.
    const double edgeLimit = ButterworthDesign::kNyquistEdgeFraction * (config.sampleRate / 2.0);

    for (const auto& tb : table.bands()) {
        if (tb.centre >= edgeLimit) continue;

        const auto result = ButterworthDesign::bandPass(tb.lower, tb.upper, config.sampleRate,
                                                          config.sections);
        bands_.push_back({ tb.index, tb.centre, result.lowerHz, result.upperHz,
                            result.maxPoleRadius, result.nyquistClamped });
        cascades_.emplace_back(result.sections);
    }

    // An empty bank makes every downstream assertion vacuously true, so it is
    // refused rather than silently returned.
    if (bands_.empty()) {
        throw std::invalid_argument("FilterBank produced an empty bank (no band fits below Nyquist)");
    }

    meanSquareAccum_.assign(bands_.size(), 0.0);
    smoothedAccum_.assign(bands_.size(), 0.0);
    meanSquare_.assign(bands_.size(), 0.0f);
    smoothed_.assign(bands_.size(), 0.0f);

    alpha_ = detectorAlpha(config.timeConstantSeconds, config.sampleRate);
}

void FilterBank::process(std::span<const float> samples) noexcept {
    // Outer over samples, inner over bands: the running mean's divisor
    // (sampleCount_) advances once per sample, not once per band, so it stays
    // unambiguous regardless of how many bands the bank has.
    for (const float x : samples) {
        ++sampleCount_;
        const double n = static_cast<double>(sampleCount_);

        for (std::size_t i = 0; i < cascades_.size(); ++i) {
            const double y = cascades_[i].processSample(static_cast<double>(x));
            const double p = y * y;

            meanSquareAccum_[i] += (p - meanSquareAccum_[i]) / n;
            smoothedAccum_[i] += alpha_ * (p - smoothedAccum_[i]);

            meanSquare_[i] = static_cast<float>(meanSquareAccum_[i]);
            smoothed_[i] = static_cast<float>(smoothedAccum_[i]);
        }
    }
}

void FilterBank::reset() noexcept {
    // Contract (why the cascades are left alone): FilterBank.h, reset().
    std::fill(meanSquareAccum_.begin(), meanSquareAccum_.end(), 0.0);
    std::fill(smoothedAccum_.begin(), smoothedAccum_.end(), 0.0);
    std::fill(meanSquare_.begin(), meanSquare_.end(), 0.0f);
    std::fill(smoothed_.begin(), smoothed_.end(), 0.0f);
    sampleCount_ = 0;
}

double FilterBank::detectorAlpha(double tau, double sampleRate) {
    // One-pole exponential smoothing at the sample rate: each sample advances
    // the clock by 1/fs seconds, so that -- not any block size -- is what the
    // time constant is measured against. IEC 61672-1 clause 5.
    return 1.0 - std::exp(-1.0 / (sampleRate * tau));
}

}  // namespace rta::dsp
