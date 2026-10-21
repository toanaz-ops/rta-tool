// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <vector>

namespace rta::dsp {

/// In-place complex FFT, radix-2, for power-of-two sizes.
///
/// Twiddle factors and the bit-reversal permutation are computed once in the
/// constructor, so a transform allocates nothing. That matters because the
/// analysis thread runs one of these per block, continuously, for the length of
/// a show.
///
/// Not thread-safe: give each thread its own instance. A shared one would need
/// either locking or per-call scratch, and both cost more than the object.
class Fft {
public:
    /// @param size number of complex samples; must be a power of two and >= 2.
    explicit Fft(std::size_t size);

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    /// Unnormalised forward transform, in place.
    void forward(std::span<std::complex<float>> data);

    /// Inverse transform, in place, including the 1/N. `inverse(forward(x))`
    /// returns `x`, which matches NumPy's `ifft` and is what every caller here
    /// wants -- an unnormalised inverse just moves the division to the call
    /// sites, where it gets forgotten.
    void inverse(std::span<std::complex<float>> data);

private:
    void transform(std::span<std::complex<float>> data, bool conjugate);

    std::size_t size_;
    std::vector<std::size_t> reversed_;              ///< bit-reversal permutation
    /// Per-stage twiddle tables, concatenated. The stage with butterfly span
    /// `len` (half = len/2) owns `half` entries starting at offset `half - 1`,
    /// holding e^(-2*pi*i*j/len) for j < half -- the same doubles the old
    /// single N/2 table held at index j*(N/len), but contiguous. N - 1 entries
    /// in total, so twice the old table's N/2.
    std::vector<std::complex<float>> stageTwiddles_;
};

}  // namespace rta::dsp
