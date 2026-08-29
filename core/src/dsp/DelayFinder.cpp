// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/DelayFinder.h"

#include "rta/dsp/RealFft.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <stdexcept>
#include <vector>

namespace rta::dsp {

namespace {

void validate(std::span<const float> reference, std::span<const float> measurement,
              const PhatOptions& options) {
    if (reference.empty() || measurement.empty()) {
        throw std::invalid_argument("findDelayPhat: spans must not be empty");
    }
    if (reference.size() != measurement.size()) {
        throw std::invalid_argument("findDelayPhat: reference and measurement lengths differ");
    }
    // 0 is the "unset" sentinel for maxHz (meaning Nyquist), so it is only a
    // contradiction once it names a real, positive frequency below minHz.
    if (options.maxHz > 0.0 && options.maxHz < options.minHz) {
        throw std::invalid_argument("findDelayPhat: maxHz is below minHz");
    }
}

/// Magnitude in double, regardless of the storage type: the floor comparison
/// below spans many orders of magnitude (regularisation defaults to 1e-10 of
/// the loudest bin), and float has nowhere near the dynamic range for that
/// comparison to stay meaningful bin to bin.
[[nodiscard]] double magnitude(const std::complex<float>& c) noexcept {
    return std::hypot(static_cast<double>(c.real()), static_cast<double>(c.imag()));
}

}  // namespace

DelayEstimate findDelayPhat(std::span<const float> reference, std::span<const float> measurement,
                             const PhatOptions& options) {
    validate(reference, measurement, options);

    // Linear, not circular, correlation: zero-pad both channels to more than
    // twice the longest one so the true lag never gets folded onto its own
    // mirror image by the FFT's implicit periodicity. bit_ceil for RealFft's
    // power-of-two requirement; floored at 4, RealFft's own minimum.
    const std::size_t inputLen = reference.size();
    const std::size_t m = std::bit_ceil(std::max<std::size_t>(2 * inputLen, 4));

    std::vector<float> paddedX(m, 0.0f);
    std::vector<float> paddedY(m, 0.0f);
    std::copy(reference.begin(), reference.end(), paddedX.begin());
    std::copy(measurement.begin(), measurement.end(), paddedY.begin());

    RealFft fft(m);
    const std::size_t numBins = fft.numBins();
    std::vector<std::complex<float>> x(numBins);
    std::vector<std::complex<float>> y(numBins);
    fft.forward(paddedX, x);
    fft.forward(paddedY, y);

    // G = conj(X) * Y, the same Sxy convention as everywhere else in this
    // codebase (X = reference, Y = measurement). A pure delay y[n] = x[n-D]
    // makes Y[k] = X[k] * exp(-2*pi*i*k*D/M), so G[k] = |X[k]|^2 *
    // exp(-2*pi*i*k*D/M): phase-only information about D, which is exactly
    // what PHAT keeps and everything else throws away.
    const double minHz = std::max(options.minHz, 0.0);
    const double maxHz = (options.maxHz > 0.0) ? options.maxHz : (options.sampleRate * 0.5);

    std::vector<std::complex<float>> g(numBins);
    std::vector<double> magG(numBins);
    double maxAbsG = 0.0;
    for (std::size_t k = 0; k < numBins; ++k) {
        const double freq = static_cast<double>(k) * options.sampleRate / static_cast<double>(m);
        if (freq < minHz || freq > maxHz) {
            // Out of band: PHAT's flat weighting would otherwise amplify bins
            // that carry no real signal here (the decision record's stated
            // weakness of the method), so they are excluded before the floor
            // is even computed rather than weighted down after the fact.
            g[k] = { 0.0f, 0.0f };
            magG[k] = 0.0;
            continue;
        }
        g[k] = std::conj(x[k]) * y[k];
        magG[k] = magnitude(g[k]);
        maxAbsG = std::max(maxAbsG, magG[k]);
    }

    // Regularisation floor RELATIVE to the loudest in-band bin -- see
    // PhatOptions::regularisation. When maxAbsG is 0 (silence, or every bin
    // band-limited away) the floor is 0 too, but that is harmless: the guard
    // below only divides where magG[k] > 0, so a floor of 0 never reaches a
    // 0/0 division.
    const double floor = options.regularisation * maxAbsG;

    std::vector<std::complex<float>> weighted(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        if (magG[k] <= 0.0) {
            weighted[k] = { 0.0f, 0.0f };
            continue;
        }
        const double denom = std::max(magG[k], floor);
        const float scale = static_cast<float>(1.0 / denom);
        weighted[k] = g[k] * scale;
    }

    // RealFft::inverse already includes the 1/M of a standard IDFT (it is the
    // exact inverse of forward()), so with every in-band bin weighted to unit
    // magnitude, a fully-populated, perfectly-matched pair concentrates onto
    // one sample reading close to 1.0 with no further scaling needed.
    std::vector<float> correlation(m);
    fft.inverse(weighted, correlation);

    std::size_t peakIndex = 0;
    double peakAbs = std::abs(static_cast<double>(correlation[0]));
    for (std::size_t i = 1; i < m; ++i) {
        const double a = std::abs(static_cast<double>(correlation[i]));
        if (a > peakAbs) {
            peakAbs = a;
            peakIndex = i;
        }
    }

    DelayEstimate result;
    result.peak = peakAbs;
    result.inverted = correlation[peakIndex] < 0.0f;

    // i in [0, M/2) is a non-negative lag; i in [M/2, M) wraps around to a
    // negative one -- the standard DFT circular-shift reading, valid here
    // because the zero-padding above already made the true peak sit well
    // inside this range rather than at the wraparound boundary.
    result.delaySamples = (peakIndex < m / 2)
                               ? static_cast<std::ptrdiff_t>(peakIndex)
                               : static_cast<std::ptrdiff_t>(peakIndex) - static_cast<std::ptrdiff_t>(m);

    // Parabolic vertex on the three samples around the peak, fit on SIGNED
    // values. A polarity-inverted peak is negated as one whole triple (a
    // single sign flip, decided by the centre sample only) rather than taking
    // |.| of each of the three independently: elementwise abs() can flip only
    // the neighbours that happen to already be positive, distorting the
    // curvature the parabola relies on, whereas one consistent multiplier
    // preserves the dip's shape and only inverts which way it opens.
    //
    // KNOWN GAP -- unverified by this file's own test suite: the ratio
    // (rm-rp)/(rm-2rc+rp) is invariant under negating rm, rc and rp by the
    // SAME factor, so this whole-triple sign flip and an elementwise |.|
    // produce IDENTICAL results whenever the three samples already share one
    // sign, which is every peak a smooth PHAT correlation produces (and
    // therefore every case in test_delay_finder.cpp). They diverge only when
    // a neighbour sample has the opposite sign from the centre -- a genuine
    // fractional delay landing near a zero crossing -- and even then by a
    // fraction of a sample. A fixture built to land exactly there was tried
    // and rejected: at frac = 0.5 the two treatments agree exactly by
    // symmetry, and at frac = 0.3 they differ by only ~0.11 samples, which
    // would need a margin near 0.13 to separate reliably -- a test that
    // narrow would be liable to fail for unrelated reasons later and teach
    // people to ignore red. So: this treatment is signed because that is
    // what a fit to one continuous peak requires, not because a test proves
    // it against the elementwise alternative. See DelayEstimate::subSample.
    const std::size_t prevIndex = (peakIndex + m - 1) % m;
    const std::size_t nextIndex = (peakIndex + 1) % m;
    const double sign = result.inverted ? -1.0 : 1.0;
    const double rm = sign * static_cast<double>(correlation[prevIndex]);
    const double rc = sign * static_cast<double>(correlation[peakIndex]);
    const double rp = sign * static_cast<double>(correlation[nextIndex]);

    const double denom = rm - 2.0 * rc + rp;
    if (denom != 0.0) {
        double delta = 0.5 * (rm - rp) / denom;
        // The three-point fit is only meaningful within its own span; clamp
        // rather than trust a numerically ill-conditioned tail (e.g. a nearly
        // flat correlation, as in the silence case) to stay in range.
        delta = std::clamp(delta, -0.5, 0.5);
        result.subSample = delta;
    }

    return result;
}

}  // namespace rta::dsp
