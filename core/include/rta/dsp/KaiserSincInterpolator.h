// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <span>

namespace rta::dsp {

/// Band-limited fractional-position interpolation by a Kaiser-windowed sinc
/// (record docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E5; J.O. Smith,
/// "Digital Audio Resampling Home Page", CCRMA; Kaiser, "Nonrecursive digital
/// filter design using the I0-sinh window function", ISCAS 1974).
///
/// Kernel: h(x) = sinc(x) * K(x / 32) for |x| < 32, else 0, where sinc is the
/// NORMALISED sinc sin(pi x)/(pi x) and K(u) = I0(beta sqrt(1 - u^2)) / I0(beta).
///
/// Why 32 and 10.06 (Kaiser 1974): for stop-band attenuation A = 100 dB,
/// beta = 0.1102 (A - 8.7) = 10.06, and for a transition band 0.45..0.55 fs
/// (d_omega = 0.2 pi) the length is N ~ (A - 8) / (2.285 d_omega) = 64.07,
/// i.e. +-32 taps. The ripple is delta = 10^(-A/20) = 1e-5 in both bands, so a
/// sinusoid at f <= 0.45 fs is reproduced to a few 1e-5 (the passband error
/// plus the nearest spectral images, each below delta in the stop band).
///
/// Not a decimator: the kernel is not scaled by the step, so a `step` far
/// from 1 would alias. The caller (clock-drift correction) uses |step - 1|
/// <= 1e-3, where this is exact in the sense above.
class KaiserSincInterpolator {
public:
    static constexpr int kHalfLength = 32;
    static constexpr double kBeta = 10.06;

    /// First and last input index (inclusive) that `resample` reads.
    struct InputSpan {
        std::ptrdiff_t first;
        std::ptrdiff_t last;
    };

    /// h(x). Exactly 1 at x = 0 and exactly 0 at every other integer, so
    /// integer positions reproduce the input bit for bit.
    [[nodiscard]] static double kernel(double x) noexcept;

    /// The input indices needed for `count` outputs at positions
    /// firstPosition + k * step, k = 0..count-1 (step > 0 so the first output
    /// needs the lowest tap and the last output the highest).
    /// @throws std::invalid_argument if count == 0, step <= 0 or a value is not finite.
    [[nodiscard]] static InputSpan requiredInputSpan(double firstPosition, double step,
                                                     std::size_t count);

    /// output[k] = sum_i input[i] * h(position_k - i), position_k =
    /// firstPosition + k * step, measured from input[0]. position_k is computed
    /// from k, never accumulated, so the error does not grow with k. Double
    /// accumulation; the caller supplies a 32-sample margin each side.
    /// @throws std::invalid_argument if any needed tap lies outside `input`,
    ///         or step <= 0 / not finite.
    void resample(std::span<const float> input, double firstPosition, double step,
                  std::span<float> output) const;
};

}  // namespace rta::dsp
