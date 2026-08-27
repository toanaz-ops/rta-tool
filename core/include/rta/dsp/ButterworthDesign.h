// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/Biquad.h"

#include <complex>
#include <vector>

namespace rta::dsp {

/// Zeros, poles, gain. The only intermediate representation this module has.
struct Zpk {
    std::vector<std::complex<double>> zeros;
    std::vector<std::complex<double>> poles;
    double gain = 1.0;
};

/// Closed-form Butterworth band-pass design. No scipy at runtime, no
/// transfer-function polynomial at any point -- see
/// docs/dsp/2026-08-27-filterbank.md for why the polynomial form is not a
/// convenience but a wrong answer.
class ButterworthDesign {
public:
    /// Bands whose edge would reach Nyquist are clamped to this fraction of it.
    static constexpr double kNyquistEdgeFraction = 0.995;

    struct Result {
        std::vector<Biquad::Coeffs> sections;   ///< ascending pole radius
        double lowerHz = 0.0;   ///< as DESIGNED, i.e. after any clamp
        double upperHz = 0.0;
        double maxPoleRadius = 0.0;
        bool   nyquistClamped = false;
    };

    /// @param sections  N: the SOS-section count, the analog prototype order,
    ///                  and HALF the band-pass pole count. Six for this bank.
    ///                  Throws std::invalid_argument for N < 1 or N > 12, for
    ///                  a non-positive sample rate, for lower >= upper, and for
    ///                  a lower edge at or below zero.
    [[nodiscard]] static Result bandPass(double lowerHz, double upperHz,
                                         double sampleRate, int sections);

    /// Steps 1-4 of the chain, exposed because a definition nobody can call is
    /// a definition nobody can check -- the same reason BandWeights::responseAt
    /// is public.
    [[nodiscard]] static Zpk bandPassZpk(double lowerHz, double upperHz,
                                         double sampleRate, int sections);

    /// Step 5. Reproduces scipy.signal.zpk2sos(pairing='nearest') exactly,
    /// including the section ordering and the gain landing in section 0.
    [[nodiscard]] static std::vector<Biquad::Coeffs> pairIntoSections(const Zpk&);
};

}  // namespace rta::dsp
