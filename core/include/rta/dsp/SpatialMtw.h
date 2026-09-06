// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/MtwResult.h"
#include "rta/dsp/SpatialAverage.h"

#include <optional>
#include <span>
#include <vector>

namespace rta::dsp {

/// The MTW variant of spatialAverage(): one combine per band, stitched with
/// the SAME mtwBands()/mtwFrequencies() the L3 engine already uses -- no
/// second stitching pass and no flat coherence array in core/, which is
/// exactly why MtwResult carries none of its own (MtwResult.h, record §5's
/// argument against averaging the already-stitched vectors).
struct SpatialMtwResult {
    MtwConfig config;
    std::vector<SpatialAverageResult> bands;  ///< one per band, ascending frequency
    std::vector<double> frequencyHz;          ///< mtwFrequencies(config)
    std::vector<float> magnitudeDb;
    std::vector<float> phaseRadians;
    std::vector<float> phaseAgreement;     ///< R, per stitched point -- see SpatialAverage.h
    std::vector<float> weightedCoherence;  ///< Sum(u*g2)/Sum(u), per stitched point
    std::vector<SpatialBinState> bins;
};

/// Runs spatialAverage() once per band over `positions[i].bandSnapshots[b]`,
/// then stitches with the layout every position's engine already shares --
/// each position's own MTW engine was itself built from one shared
/// MtwConfig (record §5), so `positions` must agree on the geometry that
/// decides the table: `topFftSize`, `octaveCount` and `sampleRate`. Throws
/// std::invalid_argument otherwise, or if `positions` is empty or
/// `u.size() != positions.size()` -- the same grid-mismatch reasoning as
/// spatialAverage()'s own check, one level up.
///
/// Returns std::nullopt only when every band's own combine does -- i.e. no
/// position contributed a weight anywhere in the whole stitched table.
[[nodiscard]] std::optional<SpatialMtwResult> spatialAverageMtw(
    std::span<const MtwResult> positions, std::span<const double> u,
    SpatialMode mode = SpatialMode::Db);

}  // namespace rta::dsp
