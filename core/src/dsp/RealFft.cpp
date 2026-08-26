// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/RealFft.h"

#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace rta::dsp {

namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

std::size_t validated(const std::size_t size) {
    // At least 4: the method runs an N/2-point complex transform, and Fft
    // itself needs 2 points.
    if (size < 4 || !std::has_single_bit(size)) {
        throw std::invalid_argument("RealFft size must be a power of two and at least 4");
    }
    return size;
}

/// Multiply by i. Written out because `value * std::complex<float>{0, 1}` costs
/// four multiplies and two adds to compute a swap and a sign flip.
constexpr std::complex<float> timesI(const std::complex<float> value) noexcept {
    return { -value.imag(), value.real() };
}

/// Divide by i, i.e. multiply by -i.
constexpr std::complex<float> overI(const std::complex<float> value) noexcept {
    return { value.imag(), -value.real() };
}

}  // namespace

RealFft::RealFft(const std::size_t size) : size_(validated(size)), half_(size / 2) {
    const std::size_t m = size_ / 2;

    // W_N^k for k = 0..m. One past the half-transform's length, because the
    // Nyquist bin is produced from the wrapped-around Z[m] = Z[0].
    unpack_.resize(m + 1);
    for (std::size_t k = 0; k <= m; ++k) {
        const double angle = -kTwoPi * static_cast<double>(k) / static_cast<double>(size_);
        unpack_[k] = { static_cast<float>(std::cos(angle)),
                       static_cast<float>(std::sin(angle)) };
    }

    scratch_.resize(m);
}

void RealFft::forward(std::span<const float> input, std::span<std::complex<float>> spectrum) {
    if (input.size() != size_ || spectrum.size() != numBins()) {
        throw std::invalid_argument("RealFft::forward got a span of the wrong length");
    }

    const std::size_t m = size_ / 2;

    // Pack even samples into the real part and odd samples into the imaginary
    // part. The half-length transform of that sequence contains everything the
    // full transform of the real signal does, interleaved.
    for (std::size_t n = 0; n < m; ++n) {
        scratch_[n] = { input[2 * n], input[2 * n + 1] };
    }

    half_.forward(scratch_);

    // Untangle. Z[k] holds the sum of two spectra: the even-sample transform
    // (conjugate-even in k) and the odd-sample one (conjugate-odd). Splitting
    // Z[k] against conj(Z[m-k]) separates them, and the odd half is then
    // rotated by W_N^k to account for its one-sample offset in time.
    //
    // Indices wrap modulo m because k runs one past the half-transform: Z[m] is
    // Z[0] by periodicity, which is what produces the Nyquist bin.
    for (std::size_t k = 0; k <= m; ++k) {
        const auto zk  = scratch_[k % m];
        const auto zmk = scratch_[(m - k) % m];

        const auto even = (zk + std::conj(zmk)) * 0.5f;
        const auto odd  = overI((zk - std::conj(zmk)) * 0.5f);

        spectrum[k] = even + unpack_[k] * odd;
    }
}

void RealFft::inverse(std::span<const std::complex<float>> spectrum, std::span<float> output) {
    if (spectrum.size() != numBins() || output.size() != size_) {
        throw std::invalid_argument("RealFft::inverse got a span of the wrong length");
    }

    const std::size_t m = size_ / 2;

    // DC and Nyquist are forced real before anything else touches them.
    //
    // The spectrum of a real signal is conjugate-symmetric, and those two bins
    // are their own mirror image -- so an imaginary part there describes a
    // signal that cannot exist. forward() never writes one, which is exactly
    // why round-tripping our own output cannot catch this. But an analyser
    // edits spectra before inverting them: octave smoothing, target curves,
    // trace subtraction. Any of those can leave a residue in those two
    // imaginary parts, and an inverse that believes it returns a signal that
    // was never there, silently. NumPy's irfft discards them; so do we.
    const std::complex<float> dc      { spectrum[0].real(), 0.0f };
    const std::complex<float> nyquist { spectrum[m].real(), 0.0f };

    // The forward untangling, run backwards: recover the even and odd spectra
    // from X[k] against conj(X[m-k]), un-rotate the odd half, and recombine
    // them into the half-length complex spectrum.
    for (std::size_t k = 0; k < m; ++k) {
        // k == 0 is the only iteration that reads either end bin: it pairs
        // X[0] with X[m].
        const auto xk  = (k == 0) ? dc      : spectrum[k];
        const auto xmk = (k == 0) ? nyquist : spectrum[m - k];

        const auto even = (xk + std::conj(xmk)) * 0.5f;
        const auto odd  = (xk - std::conj(xmk)) * 0.5f * std::conj(unpack_[k]);

        scratch_[k] = even + timesI(odd);
    }

    half_.inverse(scratch_);

    for (std::size_t n = 0; n < m; ++n) {
        output[2 * n]     = scratch_[n].real();
        output[2 * n + 1] = scratch_[n].imag();
    }
}

}  // namespace rta::dsp
