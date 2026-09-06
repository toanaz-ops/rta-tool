// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/MtwLayout.h"
#include "rta/dsp/TransferEstimator.h"

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

namespace rta::dsp {

/// The stitched MTW output. Conflict C4 (docs/plans/2026-09-05-L3-mtw-impl-
/// plan.md): this struct owns NO coherence array of its own. Coherence lives
/// only inside each band's own `TransferSnapshot`, exactly as `makeSnapshot()`
/// gated it, and is read back out through `coherenceAt()` below. A stitched
/// `coherence` member here -- even a designated initialiser, even under
/// another name -- is either caught by `check_coherence_gate.cmake`'s
/// `(\.|->)coherence[ \t]*=` pattern or, worse, evades it while still
/// duplicating a value the gate already computed once. Keeping the per-band
/// snapshots as the single source avoids both failure modes at once.
struct MtwResult {
    MtwConfig config;
    std::vector<MtwBand> bands;                  ///< ascending in frequency
    std::vector<double> frequencyHz;             ///< mtwPointCount() entries, strictly increasing
    std::vector<std::complex<double>> h;
    std::vector<float> magnitudeDb;              ///< floored at TransferSnapshot::kMagnitudeFloorDb
    std::vector<float> phaseRadians;             ///< (-pi, pi]
    std::vector<TransferSnapshot> bandSnapshots; ///< one per band, straight from makeSnapshot()
};

/// Which band owns stitched index `index`. Throws std::out_of_range if none
/// does -- an out-of-range index here means a caller mis-sized a loop, and a
/// silent wraparound would be worse than a thrown exception.
[[nodiscard]] std::size_t bandForIndex(const MtwResult& result, std::size_t index);

/// Coherence at stitched `index`, read from the OWNING band's own gated
/// snapshot. `std::nullopt` means that band has not yet passed its own
/// `minimumEffectiveAverages` -- which, under frames-uniform averaging
/// (record §5), is a FILL TIME that differs per band, not a permanent
/// exclusion of the low bands.
[[nodiscard]] std::optional<float> coherenceAt(const MtwResult& result, std::size_t index);

}  // namespace rta::dsp
