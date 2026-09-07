// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/FirDesign.h"

#include "rta/dsp/RealFft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>

// F2 (minimum-phase mode) is added once tools/gen_fir.py (task F6) has
// MEASURED the cepstral oversampling factor (plan D6) -- this task (F1) only
// implements FirPhase::Linear.

namespace rta::dsp {

namespace {

bool isPowerOfTwo(std::size_t n) noexcept {
    return n != 0 && (n & (n - 1)) == 0;
}

/// Smallest power of two >= n (record Sec.2: "M a power of two, M >= 8N").
std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t m = 1;
    while (m < n) m <<= 1;
    return m;
}

/// Linear interpolation in log10(f) and linear in dB (record Sec.6, plan D2):
/// the rule a banded correction curve implies, and one T10 pins exactly. `f`
/// outside the breakpoint range clamps to the nearest edge value rather than
/// extrapolating -- a target has no opinion past its own edges.
double interpolateTargetDb(const FirTarget& target, double f) {
    const auto& fs = target.frequencyHz;
    const auto& gs = target.gainDb;
    if (f <= fs.front()) return gs.front();
    if (f >= fs.back()) return gs.back();

    // fs is strictly ascending (validated by the caller), so the first
    // element >= f is the upper bracket; std::lower_bound is exact here, not
    // an approximation of the search.
    const auto it = std::lower_bound(fs.begin(), fs.end(), f);
    const std::size_t hi = static_cast<std::size_t>(it - fs.begin());
    const std::size_t lo = hi - 1;
    if (fs[hi] == f) return gs[hi];

    const double logLo = std::log10(fs[lo]);
    const double logHi = std::log10(fs[hi]);
    const double logF = std::log10(f);
    const double t = (logF - logLo) / (logHi - logLo);
    return gs[lo] + t * (gs[hi] - gs[lo]);
}

void validateCommon(double sampleRate, std::size_t taps) {
    if (sampleRate <= 0.0) {
        throw std::invalid_argument("designFir: sampleRate must be > 0");
    }
    if (taps < 8) {
        throw std::invalid_argument("designFir: taps must be >= 8");
    }
}

void validateTarget(const FirTarget& target) {
    if (target.frequencyHz.empty() || target.frequencyHz.size() != target.gainDb.size()) {
        throw std::invalid_argument(
            "designFir: target.frequencyHz and target.gainDb must be non-empty and equal-sized");
    }
    for (std::size_t i = 1; i < target.frequencyHz.size(); ++i) {
        if (!(target.frequencyHz[i] > target.frequencyHz[i - 1])) {
            throw std::invalid_argument("designFir: target.frequencyHz must be strictly ascending");
        }
    }
}

/// Sample a breakpoint target onto the M/2+1 half-grid (bin k at k*fs/M),
/// in dB then converted to linear magnitude -- the interpolation itself
/// (interpolateTargetDb) is what T10 pins; this just walks the grid.
std::vector<float> sampleTargetMagnitude(const FirTarget& target, double sampleRate,
                                          std::size_t m) {
    const std::size_t bins = m / 2 + 1;
    std::vector<float> magnitude(bins);
    for (std::size_t k = 0; k < bins; ++k) {
        const double f = static_cast<double>(k) * sampleRate / static_cast<double>(m);
        const double db = interpolateTargetDb(target, f);
        magnitude[k] = static_cast<float>(std::pow(10.0, db / 20.0));
    }
    return magnitude;
}

/// The frequency-sampling core (record Sec.2): sample the (zero-phase, for
/// linear) magnitude on an M-point grid, IDFT, circularly shift so the
/// zero-phase response centres at (N-1)/2, window with a periodic Window(N),
/// truncate to N. Builds the symmetric half EXPLICITLY and mirrors it, so
/// taps[n]==taps[N-1-n] is bitwise (record Sec.4), not a hope resting on the
/// window's own symmetry surviving float rounding.
std::vector<float> designLinearPhaseCore(std::span<const float> magnitudeHalfGrid,
                                          std::size_t m, std::size_t n, WindowType windowType) {
    RealFft fft(m);
    if (magnitudeHalfGrid.size() != fft.numBins()) {
        throw std::invalid_argument("designFir: magnitude grid size does not match M/2+1");
    }

    std::vector<std::complex<float>> spectrum(fft.numBins());
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        spectrum[k] = std::complex<float>(magnitudeHalfGrid[k], 0.0f);
    }

    std::vector<float> hZero(m);
    fft.inverse(spectrum, hZero);

    // Circular shift: h_zero[0] is the centre of the (even, zero-phase)
    // impulse response; sample n of the final N-tap filter is h_zero at
    // circular offset (n - (N-1)/2) mod M. Building only n <= (N-1)/2 and
    // mirroring the WINDOWED value (not the raw sample -- see below) makes
    // the symmetry bitwise instead of trusting that two independently
    // computed halves land on identical floats.
    //
    // A PERIODIC window's own coefficients (Window.h: w[k] = f(2*pi*k/N)) are
    // symmetric about k=N/2, not about k=(N-1)/2 -- those two axes coincide
    // only in the limit, never at integer k, so applying coefficients_[k] to
    // an already-mirrored sample index-for-index (coefficients_[i] on one
    // side, coefficients_[N-1-i] on the other) multiplies the two mirrored
    // taps by two DIFFERENT numbers and breaks bitwise symmetry. Computing
    // the windowed value once per source sample (i in [0, half], using
    // coefficients_[i] only) and mirroring THAT value sidesteps the mismatch
    // entirely: the periodic window's true asymmetry never gets a chance to
    // show up in the taps, at the cost of the window's own literal peak
    // sample (index N/2) never being read for even N -- coefficients_[half]
    // (index (N-2)/2, the nearest sample below the true half-integer centre)
    // stands in for it on both sides, which is what "the symmetric half,
    // mirrored" (record Sec.4, plan F1) means construction-wise.
    const std::size_t half = (n - 1) / 2;   // floor; correct for both odd and even N
    Window window(windowType, n);
    const std::span<const float> coeffs = window.coefficients();
    std::vector<float> taps(n);
    for (std::size_t i = 0; i <= half; ++i) {
        const long long offset = static_cast<long long>(i) - static_cast<long long>(half);
        const long long idx = ((offset % static_cast<long long>(m)) + static_cast<long long>(m)) %
                               static_cast<long long>(m);
        const float windowedValue = hZero[static_cast<std::size_t>(idx)] * coeffs[i];
        taps[i] = windowedValue;
        taps[n - 1 - i] = windowedValue;
    }
    // For even N the loop fills [0..half] and mirrors to [half+1..N-1], which
    // covers every index exactly once (half == n/2 - 1 there); for odd N
    // index `half` is written twice with the identical value from the same
    // source sample and the same coefficient, which is a no-op, not a bug.
    return taps;
}

struct DesignMetrics {
    double peakGainDb = 0.0;
    double coefficientPeak = 0.0;
};

/// peakGainDb: max 20*log10|H| over the SAME design grid the target was
/// sampled on (record Sec.6 Result doc) -- evaluated directly from the taps
/// via a real-to-half-spectrum forward transform padded to the same M, which
/// is exact for a linear-phase (zero-phase-derived) filter's magnitude and,
/// for minimum phase, is the identity the design already proved (F2 T6)
/// before truncation cost anything.
DesignMetrics measureDesign(std::span<const float> taps, std::size_t m) {
    std::vector<float> padded(m, 0.0f);
    std::copy(taps.begin(), taps.end(), padded.begin());

    RealFft fft(m);
    std::vector<std::complex<float>> spectrum(fft.numBins());
    fft.forward(padded, spectrum);

    DesignMetrics metrics;
    double peakMag = 0.0;
    for (const auto& bin : spectrum) peakMag = std::max(peakMag, static_cast<double>(std::abs(bin)));
    metrics.peakGainDb = 20.0 * std::log10(std::max(peakMag, 1e-12));

    for (float t : taps) metrics.coefficientPeak = std::max(metrics.coefficientPeak,
                                                              std::abs(static_cast<double>(t)));
    return metrics;
}

FirResult designFirCore(std::span<const float> magnitudeHalfGrid, std::size_t m, double sampleRate,
                        std::size_t taps, FirPhase phase, WindowType window) {
    validateCommon(sampleRate, taps);
    if (magnitudeHalfGrid.size() != m / 2 + 1) {
        throw std::invalid_argument("designFir: magnitude grid size does not match M/2+1");
    }
    if (taps > m / 2) {
        throw std::invalid_argument("designFir: taps exceeds the design grid (record Sec.10)");
    }

    FirResult result;
    result.sampleRate = sampleRate;
    result.phase = phase;
    result.method = FirMethod::FrequencySampling;
    result.window = window;
    result.designFftSize = m;

    if (phase == FirPhase::Linear) {
        result.taps = designLinearPhaseCore(magnitudeHalfGrid, m, taps, window);
        result.groupDelaySamples = taps / 2;   // N/2 for even N (half-sample delay, record Sec.4);
                                                // (N-1)/2 for odd N since integer division floors.
    } else {
        // FirPhase::Minimum is added in task F2, once F6 has measured the
        // cepstral oversampling factor (plan D6, order F1 -> F6 -> F2).
        throw std::invalid_argument("designFir: FirPhase::Minimum is not yet implemented (task F2)");
    }

    const DesignMetrics metrics = measureDesign(result.taps, m);
    result.peakGainDb = metrics.peakGainDb;
    result.coefficientPeak = metrics.coefficientPeak;
    return result;
}

}  // namespace

FirResult designFir(const FirTarget& target, double sampleRate, std::size_t taps, FirPhase phase,
                    WindowType window, FirMethod method) {
    validateCommon(sampleRate, taps);
    validateTarget(target);
    if (method != FirMethod::FrequencySampling) {
        throw std::invalid_argument("designFir: only FrequencySampling is implemented in v1");
    }

    const std::size_t m = nextPowerOfTwo(8 * taps);
    const std::vector<float> magnitude = sampleTargetMagnitude(target, sampleRate, m);
    return designFirCore(magnitude, m, sampleRate, taps, phase, window);
}

FirResult designFir(std::span<const float> magnitudeHalfGrid, double sampleRate, std::size_t taps,
                    FirPhase phase, WindowType window, FirMethod method) {
    validateCommon(sampleRate, taps);
    if (method != FirMethod::FrequencySampling) {
        throw std::invalid_argument("designFir: only FrequencySampling is implemented in v1");
    }
    const std::size_t bins = magnitudeHalfGrid.size();
    if (bins < 5 || !isPowerOfTwo(bins - 1)) {
        throw std::invalid_argument("designFir: magnitudeHalfGrid.size() must be 2^k + 1, k >= 3");
    }
    const std::size_t m = 2 * (bins - 1);
    return designFirCore(magnitudeHalfGrid, m, sampleRate, taps, phase, window);
}

}  // namespace rta::dsp
