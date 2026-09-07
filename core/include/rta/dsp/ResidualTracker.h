// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
//
// L7-DELAY task C (docs/plans/2026-09-07-L7-delay-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-auto-delay.md sec.5, sec.8). The live
// residual: coherence-weighted PHAT on the engine's ALREADY-AVERAGED
// cross-spectrum, one real IFFT per publish, reusing the same windowed pick
// and parabolic refinement as the one-shot (PhatCorrelation.h, DEL-R1) --
// not a phase-slope fit (record sec.5 rejects that on three independent
// grounds) and not a second correlator (record sec.2).
#pragma once

#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/RealFft.h"
#include "rta/dsp/TransferEstimator.h"

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

namespace rta::dsp {

/// One published residual reading. `residualSamples` is what remains AFTER
/// `DualFftEngine::Config::referenceDelaySamples` -- the tracker never sees
/// or reports the total; combining the two is app-layer presentation (record
/// sec.5, sec.11.4).
struct DelayTrack {
    std::ptrdiff_t residualSamples = 0;
    double subSample = 0.0;

    /// Normalised |correlation| at the peak. Bounded by `meanCoherence`
    /// (record sec.5's identity: equality when the residual phase is exactly
    /// linear across the band) -- NOT independently thresholded here; the
    /// bound itself is what a caller reads to judge the reading.
    double peak = 0.0;

    double meanCoherence = 0.0;  ///< mean gated gamma^2 over the band
    bool inverted = false;
};

/// Owns one `RealFft(fftSize)` and its scratch, so `update()` never
/// allocates (record sec.11) -- constructed once per live transfer function,
/// matching `DualFftEngine`'s own one-instance-per-position lifetime.
class ResidualDelayTracker {
public:
    explicit ResidualDelayTracker(std::size_t fftSize);

    /// `nullopt` whenever `snapshot.coherence` is (record sec.5, sec.8b) --
    /// no fallback to the last good reading
    /// (memory/a-fixed-defect-returns-through-the-silent-fallback.md).
    /// Reads `engine.crossPsd()` and `snapshot.coherence`; writes neither
    /// `referenceDelaySamples` nor `coherence` (record sec.8: an Apply is an
    /// explicit, separate `app/` action).
    ///
    /// @throws std::invalid_argument if `engine`'s bin count does not match
    ///         the `fftSize` this tracker was constructed for, or if
    ///         `snapshot.coherence`'s length disagrees with it.
    [[nodiscard]] std::optional<DelayTrack> update(const DualFftEngine& engine,
                                                     const TransferSnapshot& snapshot);

private:
    RealFft fft_;
    std::vector<std::complex<float>> weighted_;    ///< ctor-sized scratch, numBins()
    std::vector<float> correlation_;               ///< ctor-sized scratch, fftSize
};

}  // namespace rta::dsp
