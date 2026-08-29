// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>

namespace rta::dsp {

/// The single definition of the one-sided PSD scaling used by every spectral
/// estimator in core. Two divergent copies of this produce a constant dB
/// offset that nobody can find, so there is exactly one -- see
/// docs/dsp/2026-08-28-dual-fft.md §5.
struct PsdScaling {
    /// PSD[k] = scale(fs, sum(w^2)) * |X[k]|^2, before the end-bin factor.
    ///
    /// The 2 turns a two-sided spectrum into a one-sided one (negative
    /// frequencies are folded into their positive mirror, except DC and
    /// Nyquist -- see binFactor()). Dividing by fs * sum(w^2) converts raw
    /// squared-magnitude bins into a density per hertz, normalised for the
    /// energy the window itself removed (Bendat & Piersol; matches
    /// scipy.signal.welch's 'density' scaling).
    [[nodiscard]] static constexpr double scale(double sampleRate,
                                                 double windowSumSquares) noexcept {
        return 2.0 / (sampleRate * windowSumSquares);
    }

    /// DC and Nyquist have no mirror-image partner to fold in, so they do not
    /// get the factor of two that every other bin receives from scale().
    [[nodiscard]] static constexpr double binFactor(std::size_t bin,
                                                      std::size_t numBins) noexcept {
        return (bin == 0 || bin + 1 == numBins) ? 0.5 : 1.0;
    }
};

}  // namespace rta::dsp
