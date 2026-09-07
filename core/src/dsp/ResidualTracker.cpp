// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/ResidualTracker.h"

#include "PhatCorrelation.h"

#include <cmath>
#include <stdexcept>

namespace rta::dsp {

ResidualDelayTracker::ResidualDelayTracker(std::size_t fftSize)
    : fft_(fftSize), weighted_(fft_.numBins()), correlation_(fftSize) {}

std::optional<DelayTrack> ResidualDelayTracker::update(const DualFftEngine& engine,
                                                         const TransferSnapshot& snapshot) {
    // Absence is the whole contract below the gate (record sec.5, sec.8b):
    // nothing is computed, nothing is shown, and -- critically -- nothing
    // from a PRIOR call survives here. Checked first, before touching either
    // scratch buffer, so a closed gate costs nothing but the check itself.
    if (!snapshot.coherence.has_value()) {
        return std::nullopt;
    }
    // No named alias to snapshot.coherence -- check_coherence_gate.cmake
    // deliberately flags exactly that shape (a reference bound straight to
    // the field), so every access below goes through snapshot.coherence's
    // own operator[] / size(), read-only, at the call site.

    const auto sxy = engine.crossPsd();
    const std::size_t numBins = weighted_.size();
    if (sxy.size() != numBins || snapshot.coherence->size() != numBins) {
        throw std::invalid_argument(
                "ResidualDelayTracker::update: engine/snapshot bin count does not match "
                "the fftSize this tracker was constructed for");
    }

    // psi_k = gamma^2_k / |Sxy_k| (record sec.5): bounded, needs no floor --
    // unlike flat PHAT on gated bins (would need its own coherence
    // threshold) or the Hannan-Thomson weight gamma^2/(1-gamma^2) (explodes
    // near 1, needs a floor L6b sec.3 already argued against). A zero-energy
    // bin contributes zero weight rather than a divide-by-zero spike, for
    // the same reason DelayPolicy.cpp masks an out-of-band bin to zero
    // instead of weighting it up.
    double coherenceSum = 0.0;
    for (std::size_t k = 0; k < numBins; ++k) {
        const double gamma2 = static_cast<double>((*snapshot.coherence)[k]);
        coherenceSum += gamma2;

        const double magSxy = std::hypot(sxy[k].real(), sxy[k].imag());
        if (magSxy <= 0.0) {
            weighted_[k] = {0.0f, 0.0f};
            continue;
        }
        const double scale = gamma2 / magSxy;
        weighted_[k] = std::complex<float>(static_cast<float>(sxy[k].real() * scale),
                                            static_cast<float>(sxy[k].imag() * scale));
    }
    const double meanCoherence = (numBins > 0) ? (coherenceSum / static_cast<double>(numBins)) : 0.0;

    // The engine's own averaged Sxy is CIRCULAR (frames are windowed and the
    // cross-spectrum is periodic in fftSize), unlike the one-shot's linear,
    // zero-padded correlation -- so no window restriction applies here.
    // detail::pickBestPeak (not pickPeaks): the tracker runs once per
    // publish and must allocate nothing (record sec.11); pickPeaks'
    // candidate ranking allocates a pool by design, pickBestPeak does not. A
    // residual beyond fftSize/2 aliases by construction (record sec.5's own
    // documented bound, proven by C4, not guarded against here).
    fft_.inverse(weighted_, correlation_);
    const auto peak = detail::pickBestPeak(correlation_, fft_.size());

    DelayTrack track;
    track.residualSamples = peak.lag;
    track.subSample = peak.subSample;
    track.peak = std::abs(peak.height);
    track.inverted = peak.height < 0.0;
    track.meanCoherence = meanCoherence;
    return track;
}

}  // namespace rta::dsp
