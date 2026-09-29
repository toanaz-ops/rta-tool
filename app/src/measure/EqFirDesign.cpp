// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/EqFirDesign.h"

#include "rta/eq/BiquadDesign.h"

#include <cmath>
#include <stdexcept>

namespace rta::measure {

std::size_t eqFirGridSize(std::size_t taps) {
    std::size_t m = 1;
    while (m < 8 * taps) m <<= 1;
    return m;
}

std::vector<float> eqFirMagnitudeHalfGrid(std::span<const CommittedFilter> filters,
                                          double sampleRate, std::size_t taps) {
    const std::size_t m = eqFirGridSize(taps);
    const std::size_t bins = m / 2 + 1;
    std::vector<float> magnitude(bins, 1.0f);
    for (std::size_t k = 0; k < bins; ++k) {
        const double hz = static_cast<double>(k) * sampleRate / static_cast<double>(m);
        double sumDb = 0.0;
        for (const auto& filter : filters) {
            if (filter.applied) continue;  // already in the rig (D11)
            sumDb += rta::eq::responseDb(filter.spec, sampleRate, hz);
        }
        // Skip the pow for the empty / all-applied case so "no correction"
        // is EXACTLY 1.0f, not 10^0 rounded through a float conversion.
        if (sumDb != 0.0) magnitude[k] = static_cast<float>(std::pow(10.0, sumDb / 20.0));
    }
    return magnitude;
}

rta::dsp::FirResult designEqFir(std::span<const CommittedFilter> filters, double sampleRate,
                                std::size_t taps, rta::dsp::FirPhase phase) {
    if (taps % 2 == 0) {
        throw std::invalid_argument(
            "designEqFir: even tap counts are refused -- FirDesign realises an even-length "
            "filter with the wrong gain (see EqFirDesign.h)");
    }
    const auto grid = eqFirMagnitudeHalfGrid(filters, sampleRate, taps);
    return rta::dsp::designFir(grid, sampleRate, taps, phase);
}

}  // namespace rta::measure
