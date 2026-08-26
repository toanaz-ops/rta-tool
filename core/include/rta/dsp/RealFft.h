// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/Fft.h"

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::dsp {

/// FFT of a real-valued signal, producing the N/2+1 bins from DC to Nyquist.
///
/// A real signal's spectrum is conjugate-symmetric, so the upper half carries
/// no information. Running a full complex transform on it does twice the
/// arithmetic to produce a mirror image nobody reads. This packs the N real
/// samples into an N/2-point complex transform and untangles the result, which
/// halves both the work and the memory traffic -- the difference between
/// comfortable and marginal once several channels are analysed at once.
///
/// Bin `k` sits at `k * sampleRate / size()` hertz. Not thread-safe; one
/// instance per thread.
class RealFft {
public:
    /// @param size number of real samples; must be a power of two and >= 4
    ///             (the method runs an N/2-point complex transform).
    explicit RealFft(std::size_t size);

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// N/2 + 1: DC, the N/2-1 interior bins, and Nyquist.
    [[nodiscard]] std::size_t numBins() const noexcept { return size_ / 2 + 1; }

    /// @param input     `size()` real samples
    /// @param spectrum  `numBins()` complex bins, unnormalised
    void forward(std::span<const float> input, std::span<std::complex<float>> spectrum);

    /// Inverse transform, including the 1/N. The exact inverse of forward().
    ///
    /// The imaginary parts of the DC and Nyquist bins are ignored: a real
    /// signal cannot produce either, so a value there describes something that
    /// does not exist. This matches `numpy.fft.irfft`, and matters for any
    /// caller that edits a spectrum -- smoothing, a target curve, trace
    /// subtraction -- before inverting it.
    void inverse(std::span<const std::complex<float>> spectrum, std::span<float> output);

private:
    std::size_t size_;
    Fft half_;                                    ///< the N/2-point complex transform
    std::vector<std::complex<float>> unpack_;     ///< W_N^k = e^(-2*pi*i*k/N), k <= N/2
    std::vector<std::complex<float>> scratch_;
};

}  // namespace rta::dsp
