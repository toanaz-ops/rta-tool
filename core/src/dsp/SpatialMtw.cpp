// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/SpatialMtw.h"

#include <stdexcept>
#include <utility>

namespace rta::dsp {

namespace {

void validateInputs(std::span<const MtwResult> positions, std::span<const double> u) {
    if (positions.empty()) {
        throw std::invalid_argument("spatialAverageMtw: positions must not be empty");
    }
    if (u.size() != positions.size()) {
        throw std::invalid_argument("spatialAverageMtw: u.size() must equal positions.size()");
    }
    const auto& first = positions.front().config;
    for (const auto& position : positions) {
        // Every position's MTW engine is built from one shared MtwConfig in
        // this app (record §5), so a difference in the table GEOMETRY is a
        // programming error -- exact `!=`, same reasoning as
        // spatialAverage()'s own grid check.
        if (position.config.topFftSize != first.topFftSize ||
            position.config.octaveCount != first.octaveCount ||
            position.config.sampleRate != first.sampleRate) {
            throw std::invalid_argument(
                "spatialAverageMtw: positions disagree on topFftSize, octaveCount or sampleRate");
        }
    }
}

}  // namespace

std::optional<SpatialMtwResult> spatialAverageMtw(std::span<const MtwResult> positions,
                                                   std::span<const double> u, SpatialMode mode) {
    validateInputs(positions, u);

    SpatialMtwResult result;
    result.config = positions.front().config;
    result.frequencyHz = mtwFrequencies(result.config);
    const auto bandLayout = mtwBands(result.config);

    const std::size_t total = result.frequencyHz.size();
    result.magnitudeDb.assign(total, 0.0f);
    result.phaseRadians.assign(total, 0.0f);
    result.phaseAgreement.assign(total, 0.0f);
    result.weightedCoherence.assign(total, 0.0f);
    result.bins.assign(total, SpatialBinState{});
    result.bands.reserve(bandLayout.size());

    bool anyPresent = false;
    std::vector<TransferSnapshot> bandPositions(positions.size());

    for (std::size_t b = 0; b < bandLayout.size(); ++b) {
        // Record §5: the same function runs once per band over the per-band
        // TransferSnapshots MtwResult::bandSnapshots already holds -- no
        // second stitching pass, and no combine logic duplicated here.
        for (std::size_t i = 0; i < positions.size(); ++i) {
            bandPositions[i] = positions[i].bandSnapshots[b];
        }

        // spatialAverageBins(), never spatialAverage(): the always-returning
        // form carries this band's REAL per-bin SpatialBinState even when
        // every bin of THIS band is absent -- a bin with contributors but
        // zero weight (NoWeight) must not collapse to the same
        // {NoContributor, 0} a truly unmeasured bin would report. The
        // all-absent -> nullopt rule is applied once, below, over the whole
        // stitched result (memory/a-fixed-defect-returns-through-the-silent-
        // fallback.md).
        auto bandResult = spatialAverageBins(bandPositions, u, mode);

        const auto& band = bandLayout[b];
        for (std::size_t bin = band.firstBin; bin <= band.lastBin; ++bin) {
            const std::size_t index = band.firstIndex + (bin - band.firstBin);
            result.magnitudeDb[index] = bandResult.magnitudeDb[bin];
            result.phaseRadians[index] = bandResult.phaseRadians[bin];
            result.phaseAgreement[index] = bandResult.phaseAgreement[bin];
            result.weightedCoherence[index] = bandResult.weightedCoherence[bin];
            result.bins[index] = bandResult.bins[bin];
            if (bandResult.bins[bin].absence == SpatialAbsence::Present) {
                anyPresent = true;
            }
        }
        result.bands.push_back(std::move(bandResult));
    }

    if (!anyPresent) {
        return std::nullopt;
    }
    return result;
}

}  // namespace rta::dsp
