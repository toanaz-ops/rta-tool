// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/MinimumPhase.h"

#include "rta/dsp/Fft.h"

#include <cmath>
#include <stdexcept>

namespace rta::dsp {

namespace {

/// True for any power of two, including the degenerate n == 0 the caller
/// must still reject on the size >= 4 branch below.
bool isPowerOfTwo(std::size_t n) noexcept {
    return n != 0 && (n & (n - 1)) == 0;
}

}  // namespace

MinimumPhaseResult minimumPhaseFromMagnitude(std::span<const float> magnitudeFullGrid,
                                              float floorDb) {
    const std::size_t n = magnitudeFullGrid.size();
    if (n < 4 || !isPowerOfTwo(n)) {
        throw std::invalid_argument(
            "minimumPhaseFromMagnitude: size must be a power of two, >= 4");
    }

    // Step 1 (plan Sec.1.1): floor before the log -- log(0) is undefined, and
    // an unmeasured bin should read as "very quiet", not "silent". The floor
    // itself is computed in double so a very negative floorDb (e.g. -200 dB,
    // T5) does not lose precision converting to linear before the compare.
    const double floorLinear = std::pow(10.0, static_cast<double>(floorDb) / 20.0);

    // Step 2 (Sec.1.2): log, NOT halved. scipy's minimum_phase(half=True)
    // returns the SQUARE-ROOT-magnitude system; this project's half=False
    // convention keeps |H_min| == |H| exactly (T2), which both the FIR and EQ
    // records rely on. The log is real-valued, so it loads as the real part
    // of a complex spectrum with a zero imaginary part -- Fft only transforms
    // complex data, and there is no separate real-input transform used here.
    std::vector<std::complex<float>> spectrum(n);
    for (std::size_t k = 0; k < n; ++k) {
        const double a = std::max(static_cast<double>(magnitudeFullGrid[k]), floorLinear);
        spectrum[k] = std::complex<float>(static_cast<float>(std::log(a)), 0.0f);
    }

    // Step 3 (Sec.1.3): the real cepstrum is Re(IFFT(log-magnitude)). Fft's
    // inverse() already carries the 1/N scaling (its own doc comment:
    // inverse(forward(x)) == x), so this is the textbook IFFT, not a
    // half-normalised variant a caller would have to remember to fix up.
    Fft fft(n);
    fft.inverse(spectrum);

    // Step 4 (Sec.1.4): fold the cepstrum onto the causal half. This is the
    // step that actually manufactures "minimum phase": doubling the
    // positive-quefrency samples and discarding the negative-quefrency ones
    // (which for a real cepstrum carry the same information, mirrored) turns
    // a magnitude-only spectrum -- which has no phase information at all --
    // into the specific causal, stable sequence whose zeros all lie inside
    // the unit circle. DC and Nyquist are fixed points of that mirror (each
    // is its own reflection), so they keep weight 1, not 2.
    const std::size_t half = n / 2;
    for (std::size_t k = 0; k < n; ++k) {
        float weight;
        if (k == 0 || k == half) {
            weight = 1.0f;
        } else if (k < half) {
            weight = 2.0f;
        } else {
            weight = 0.0f;
        }
        spectrum[k] = std::complex<float>(spectrum[k].real() * weight, 0.0f);
    }

    // Step 5 (Sec.1.5): H_min = exp(FFT(c_min)). forward() is the
    // unnormalised transform (Fft.h), matching the textbook forward DFT this
    // formula assumes; exp() of a complex number folds the now-real-valued
    // log-magnitude sum back into a magnitude (via its real part) and the
    // reconstructed minimum phase (via its imaginary part) in one step.
    fft.forward(spectrum);
    for (std::size_t k = 0; k < n; ++k) {
        spectrum[k] = std::exp(spectrum[k]);
    }

    return MinimumPhaseResult{ std::move(spectrum) };
}

}  // namespace rta::dsp
