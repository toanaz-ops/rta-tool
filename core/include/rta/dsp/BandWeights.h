// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/OctaveBands.h"

#include <cstddef>
#include <span>
#include <vector>

namespace rta::dsp {

/// Maps FFT bins onto fractional-octave bands.
///
/// ## Weighted, not grouped
///
/// The obvious implementation assigns each bin to whichever band contains it
/// and adds. That is what Microstar's technical note describes, and the
/// correction factors it publishes -- plus 0.337 dB for a three-bin group --
/// exist precisely because the assignment is biased. It also makes a tone
/// drifting across a band edge jump between bands.
///
/// Here each bin is multiplied by the design-goal response of the band's filter
/// at that bin's frequency, per IEC 61260-1 / ANSI S1.11:
///
///     1 / |H(f)|^2  =  1 + [ (f/fm - fm/f) / halfSpan ] ^ (2N)
///
/// which is exactly 1 at the mid-band frequency and exactly one half --
/// -3.0103 dB -- at both band edges, for every fraction and every order. A tone
/// crossing an edge fades between bands the way it would through real filters.
/// This is the approach Audio Precision takes.
///
/// ## What it consumes
///
/// **Power spectral density**, not the power spectrum. Summing power-spectrum
/// bins over-counts broadband energy by the analysis window's equivalent noise
/// bandwidth -- 1.76 dB with a Hann window, on every noise or music source.
class BandWeights {
public:
    /// Filter order N, so the band-pass is order 2N = 6. phonometry's
    /// conformance suite finds Butterworth order 6 achieves IEC 61260-1
    /// class 0; Chebyshev II reaches class 1, and Chebyshev I, elliptic and
    /// Bessel all fail the mask.
    static constexpr int kFilterOrder = 3;

    /// A band spanning fewer bins than this cannot be resolved by the transform
    /// and is flagged. Three is the smallest span that has an interior at all.
    static constexpr double kMinBinsPerBand = 3.0;

    /// Weights below this fraction of the peak are dropped. Measured cost of the
    /// truncation: under 0.0001 dB of band energy, against a matrix that stays
    /// between 3 and 21 percent dense.
    static constexpr double kWeightFloor = 1.0e-6;

    struct Band {
        std::size_t firstBin = 0;      ///< transform bin the weights start at
        std::size_t weightOffset = 0;  ///< where they start in the flat store
        std::size_t binCount = 0;      ///< how many consecutive bins
        double      binsSpanned = 0.0; ///< bandwidth / bin width
        bool        underResolved = false;
    };

    BandWeights(OctaveBands bands, std::size_t fftSize, double sampleRate);

    [[nodiscard]] const OctaveBands& bands() const noexcept { return bands_; }
    [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }
    [[nodiscard]] const Band& band(std::size_t i) const { return rows_.at(i); }
    [[nodiscard]] std::span<const float> weights(std::size_t i) const;

    /// bandPower[i] = sum over the band of density[bin] * weight * binWidth.
    ///
    /// @param density   power spectral density, fftSize/2 + 1 long
    /// @param bandPower output, size() long
    void apply(std::span<const float> density, std::span<float> bandPower) const;

    /// The design-goal power response of a band's filter at `frequency`.
    /// Exposed because it is the definition the whole class rests on, and a
    /// definition nobody can call is a definition nobody can check.
    [[nodiscard]] static double responseAt(double frequency, const OctaveBands::Band& band);

private:
    OctaveBands        bands_;
    std::size_t        fftSize_ = 0;
    double             sampleRate_ = 0.0;
    double             binWidthHz_ = 0.0;
    std::vector<Band>  rows_;
    std::vector<float> weights_;
};

}  // namespace rta::dsp
