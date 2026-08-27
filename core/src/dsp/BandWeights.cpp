// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/BandWeights.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rta::dsp {

double BandWeights::responseAt(const double frequency, const OctaveBands::Band& band) {
    if (!(frequency > 0.0)) {
        return 0.0;
    }

    // (f/fm - fm/f) is zero at the mid-band frequency and grows in both
    // directions. Dividing by the band's own edge-to-edge span in that same
    // coordinate is what makes the response exactly one half at the edges --
    // for any fraction and any order, with no per-fraction constant to get
    // wrong. The fraction is therefore not a parameter: it is already in the
    // band's edges.
    const double halfSpan = (band.upper - band.lower) / band.centre;
    const double detune = frequency / band.centre - band.centre / frequency;
    const double normalised = detune / halfSpan;

    return 1.0 / (1.0 + std::pow(normalised, 2 * kFilterOrder));
}

BandWeights::BandWeights(OctaveBands bands, const std::size_t fftSize, const double sampleRate)
    : bands_(std::move(bands)), fftSize_(fftSize), sampleRate_(sampleRate) {
    if (fftSize < 4 || (fftSize & (fftSize - 1)) != 0) {
        throw std::invalid_argument("BandWeights fftSize must be a power of two and at least 4");
    }
    if (!(sampleRate > 0.0)) {
        throw std::invalid_argument("BandWeights sampleRate must be positive");
    }

    binWidthHz_ = sampleRate_ / static_cast<double>(fftSize_);
    const std::size_t binCount = fftSize_ / 2 + 1;

    rows_.reserve(bands_.size());

    for (const auto& band : bands_.bands()) {
        // Walk outward from the band centre until the response falls through
        // the floor. Found by search rather than by inverting the response
        // analytically, because the floor is a policy: a policy that can only
        // be changed by re-deriving an inverse is a policy nobody will change.
        std::size_t first = binCount;
        std::size_t last = 0;
        for (std::size_t bin = 0; bin < binCount; ++bin) {
            const double response = responseAt(static_cast<double>(bin) * binWidthHz_, band);
            if (response > kWeightFloor) {
                first = std::min(first, bin);
                last = std::max(last, bin);
            }
        }

        Band row;
        row.binsSpanned = (band.upper - band.lower) / binWidthHz_;
        row.underResolved = row.binsSpanned < kMinBinsPerBand;

        if (first > last) {
            // The band lies entirely outside what this transform covers. Keep
            // the single nearest bin so every band still has a row and no
            // caller has to special-case an empty one.
            const auto nearest = static_cast<long long>(std::llround(band.centre / binWidthHz_));
            first = static_cast<std::size_t>(
                std::clamp<long long>(nearest, 0, static_cast<long long>(binCount) - 1));
            last = first;
        }

        row.firstBin = first;
        row.weightOffset = weights_.size();
        row.binCount = last - first + 1;

        weights_.reserve(weights_.size() + row.binCount);
        for (std::size_t bin = first; bin <= last; ++bin) {
            const double response = responseAt(static_cast<double>(bin) * binWidthHz_, band);
            weights_.push_back(static_cast<float>(std::max(response, 0.0)));
        }

        rows_.push_back(row);
    }
}

std::span<const float> BandWeights::weights(const std::size_t i) const {
    const auto& row = rows_.at(i);
    return { weights_.data() + row.weightOffset, row.binCount };
}

void BandWeights::apply(std::span<const float> density, std::span<float> bandPower) const {
    if (density.size() != fftSize_ / 2 + 1) {
        throw std::invalid_argument("BandWeights::apply got a density span of the wrong length");
    }
    if (bandPower.size() != rows_.size()) {
        throw std::invalid_argument("BandWeights::apply got a bandPower span of the wrong length");
    }

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& row = rows_[i];

        // Accumulated in double. The summands span the dynamic range of the
        // whole spectrum, and a band at the quiet end of a measurement is
        // exactly where a float accumulator loses the bins that matter.
        double sum = 0.0;
        for (std::size_t j = 0; j < row.binCount; ++j) {
            sum += static_cast<double>(density[row.firstBin + j]) *
                   static_cast<double>(weights_[row.weightOffset + j]);
        }

        bandPower[i] = static_cast<float>(sum * binWidthHz_);
    }
}

}  // namespace rta::dsp
