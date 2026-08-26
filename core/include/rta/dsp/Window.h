// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. This file MUST NOT depend on JUCE, Qt, or any
// audio-device API. See docs/specs for the rationale.
#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace rta::dsp {

/// Window shapes available for spectral analysis.
///
/// All windows are generated in their **periodic** (DFT-even) form,
/// `w[n] = f(2*pi*n / N)` for `n = 0..N-1`, *not* the symmetric form that
/// divides by `N-1`. The periodic form is the correct choice for FFT analysis:
/// it makes the window seamless when the block is treated as one period of a
/// periodic signal, which is exactly the assumption the DFT makes. It also
/// yields exact correction factors (periodic Hann has sum(w) = N/2 exactly),
/// which is what lets the unit tests assert to 1e-12 instead of "about right".
enum class WindowType {
    Rectangular,
    Hann,
    Hamming,
    BlackmanHarris,  ///< 4-term, -92 dB sidelobes
    FlatTop,         ///< 5-term, for accurate amplitude of coherent tones
    Tukey,           ///< tapered cosine; alpha = taper fraction
};

std::string_view toString(WindowType type) noexcept;

/// A precomputed analysis window plus the scaling factors that make a spectrum
/// mean something physical.
///
/// ## Why two different correction factors
///
/// Multiplying a block by a window removes energy from it, so a raw FFT of a
/// windowed block always reads low. The amount you must add back depends on
/// **what kind of signal you are measuring**, and this is the single most common
/// silent error in home-grown analysers:
///
///   * A **coherent** component (a sine) has all its energy concentrated in one
///     bin. Its amplitude is scaled by the window's mean value, so you divide by
///     `sum(w)/N`. That is `amplitudeCorrection()`.
///   * **Random / broadband** content (noise, music) spreads across bins and its
///     *power* adds incoherently, so the relevant quantity is `sum(w^2)`, not
///     `sum(w)`. That is `energyCorrection()`.
///
/// For a Hann window the two differ by about 1.76 dB. Applying the amplitude
/// factor to noise (or vice versa) produces a plot that looks perfectly
/// plausible and is wrong by that amount at every frequency.
class Window {
public:
    /// @param type   window shape
    /// @param size   block length in samples; must be > 0
    /// @param param  shape parameter; only used by Tukey (taper fraction, clamped to [0,1])
    Window(WindowType type, std::size_t size, double param = 0.5);

    [[nodiscard]] WindowType type() const noexcept { return type_; }
    [[nodiscard]] std::size_t size() const noexcept { return coefficients_.size(); }
    [[nodiscard]] std::span<const float> coefficients() const noexcept { return coefficients_; }

    /// sum(w[n])
    [[nodiscard]] double sum() const noexcept { return sum_; }
    /// sum(w[n]^2)
    [[nodiscard]] double sumSquares() const noexcept { return sumSquares_; }

    /// Mean window value, sum(w)/N. Also called coherent gain. 1.0 for rectangular.
    [[nodiscard]] double coherentGain() const noexcept;

    /// Multiply a magnitude spectrum by this to recover the true amplitude of a
    /// **sinusoidal** component: `N / sum(w)`.
    [[nodiscard]] double amplitudeCorrection() const noexcept;

    /// Multiply a magnitude spectrum by this to preserve the total power of
    /// **random / broadband** content: `sqrt(N / sum(w^2))`.
    [[nodiscard]] double energyCorrection() const noexcept;

    /// Equivalent Noise Bandwidth, expressed in FFT bins:
    /// `N * sum(w^2) / sum(w)^2`. This is the width of the ideal rectangular
    /// filter that would pass the same noise power as this window's mainlobe.
    /// Needed to convert a bin value into a power spectral density, and to sum
    /// bins into fractional-octave bands without double-counting overlap.
    [[nodiscard]] double equivalentNoiseBandwidth() const noexcept;

    /// out[n] = in[n] * w[n]. `in` and `out` must both be `size()` long;
    /// aliasing (in == out) is allowed.
    void apply(std::span<const float> in, std::span<float> out) const;

private:
    WindowType type_;
    std::vector<float> coefficients_;
    double sum_ = 0.0;
    double sumSquares_ = 0.0;
};

}  // namespace rta::dsp
