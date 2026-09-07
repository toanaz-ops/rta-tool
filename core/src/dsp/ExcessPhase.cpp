// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/ExcessPhase.h"

#include "rta/dsp/GroupDelay.h"
#include "rta/dsp/MinimumPhase.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace rta::dsp {

namespace {

constexpr double kPi = std::numbers::pi;

/// A single trusted bin can still produce a "median" delay, but nothing
/// downstream (a swing, a slope) is meaningful from one point -- two is the
/// smallest count a median genuinely summarises more than one opinion. A
/// labelled judgment, the same shape as EqTrustFloor's 0.7 (plan EQ-R2).
constexpr std::size_t kMinTrustedBins = 2;

/// groupDelaySeconds' own doc comment requires the caller to state a width;
/// this stays minimal (a plain central difference) because the median delay
/// estimate must not average away the notch's own contribution the way
/// smoothing the SWING itself would (record Sec.4.3.5's explicit warning).
constexpr std::size_t kGroupDelaySmoothingBins = 1;

bool isPowerOfTwo(std::size_t n) noexcept {
    return n != 0 && (n & (n - 1)) == 0;
}

void validate(std::span<const float> magnitudeHalfGrid,
              std::span<const std::complex<double>> hHalfGrid,
              std::span<const std::uint8_t> trusted, double sampleRate,
              std::size_t oversampleFactor) {
    if (magnitudeHalfGrid.size() != hHalfGrid.size() ||
        magnitudeHalfGrid.size() != trusted.size()) {
        throw std::invalid_argument(
            "excessPhase: magnitudeHalfGrid, hHalfGrid and trusted must be the same length");
    }
    const std::size_t m = magnitudeHalfGrid.size();
    if (m < 3 || !isPowerOfTwo(m - 1)) {
        throw std::invalid_argument("excessPhase: magnitudeHalfGrid.size() must be 2^k + 1, k >= 1");
    }
    if (sampleRate <= 0.0) {
        throw std::invalid_argument("excessPhase: sampleRate must be positive");
    }
    if (oversampleFactor == 0 || !isPowerOfTwo(oversampleFactor)) {
        throw std::invalid_argument("excessPhase: oversampleFactor must be a positive power of two");
    }
}

/// Step 1 (record Sec.4.3.2): each untrusted bin takes the NEAREST trusted
/// bin's own magnitude -- a flat hold introduces no new zero into the
/// spectrum, unlike an interpolated or extrapolated guess would. Two linear
/// passes (nearest trusted at-or-before, at-or-after) rather than an O(m^2)
/// scan: this runs on an operator action over the whole fixed-engine grid
/// (record Sec.7's ~8193 bins), not per audio block.
std::vector<std::size_t> nearestTrustedIndex(std::span<const std::uint8_t> trusted) {
    const std::size_t m = trusted.size();
    std::vector<long long> left(m, -1);
    long long last = -1;
    for (std::size_t k = 0; k < m; ++k) {
        if (trusted[k]) last = static_cast<long long>(k);
        left[k] = last;
    }
    std::vector<long long> right(m, -1);
    last = -1;
    for (std::size_t k = m; k-- > 0;) {
        if (trusted[k]) last = static_cast<long long>(k);
        right[k] = last;
    }
    std::vector<std::size_t> nearest(m);
    for (std::size_t k = 0; k < m; ++k) {
        const long long l = left[k], r = right[k];
        if (l < 0) nearest[k] = static_cast<std::size_t>(r);
        else if (r < 0) nearest[k] = static_cast<std::size_t>(l);
        else {
            const long long dl = static_cast<long long>(k) - l;
            const long long dr = r - static_cast<long long>(k);
            nearest[k] = static_cast<std::size_t>(dl <= dr ? l : r);
        }
    }
    return nearest;
}

/// Step 2: the filled half-grid, interpolated onto the oversampled half-grid
/// (oversampleFactor * nFft / 2 + 1 points), linear in log10(bin index),
/// linear in dB -- FirDesign.cpp's interpolateFirTargetDb rule (record
/// Sec.7), applied here to a dense measured curve rather than a sparse
/// breakpoint target. Bin index stands in for frequency: f_k = k*binWidthHz
/// is affine in k, so log10(f) ratios and log10(k) ratios agree exactly, and
/// this sidesteps ever dividing by binWidthHz. The one sub-interval
/// touching DC (k=0) cannot use log10(0); it interpolates linearly in k
/// instead, a labelled edge rule with no physical content near 0 Hz.
std::vector<float> interpolateOversampledFullCircle(std::span<const float> filled,
                                                     std::size_t oversampleFactor,
                                                     std::size_t nFft) {
    const std::size_t m = filled.size();
    const std::size_t nFftOS = oversampleFactor * nFft;
    const std::size_t halfOS = nFftOS / 2;
    std::vector<float> full(nFftOS, 0.0f);

    for (std::size_t j = 0; j <= halfOS; ++j) {
        const double x = static_cast<double>(j) / static_cast<double>(oversampleFactor);
        std::size_t kLo = static_cast<std::size_t>(x);
        if (kLo > m - 1) kLo = m - 1;
        double dbJ;
        if (kLo >= m - 1 || x == static_cast<double>(kLo)) {
            const std::size_t kExact = std::min(kLo, m - 1);
            dbJ = 20.0 * std::log10(std::max(static_cast<double>(filled[kExact]), 1e-20));
        } else {
            const std::size_t kHi = kLo + 1;
            const double dbLo = 20.0 * std::log10(std::max(static_cast<double>(filled[kLo]), 1e-20));
            const double dbHi = 20.0 * std::log10(std::max(static_cast<double>(filled[kHi]), 1e-20));
            const double t = (kLo == 0)
                ? x
                : (std::log10(x) - std::log10(static_cast<double>(kLo))) /
                  (std::log10(static_cast<double>(kHi)) - std::log10(static_cast<double>(kLo)));
            dbJ = dbLo + t * (dbHi - dbLo);
        }
        const float magJ = static_cast<float>(std::pow(10.0, dbJ / 20.0));
        full[j] = magJ;
        if (j > 0 && j < halfOS) full[nFftOS - j] = magJ;
    }
    return full;
}

/// DEVIATION FROM THE RECORD, MEASURED (flag to the record owner, same
/// treatment as the D5/OQ-A and D6 findings this lane already carries):
/// docs/dsp/2026-09-06-l7-auto-eq.md Sec.4.3.4 specifies the MEDIAN of the
/// trusted excess group delay for tau_0, reasoning "the allpass comb term
/// has zero mean group delay per period, so the median recovers the
/// broadband delay". Measured on the record's own two-path fixture
/// (a=2, D=144 samples, 48 kHz, oversampleFactor=32): the MEDIAN read
/// 0.0018011 s against the true 0.003 s -- off by 57.5 SAMPLES -- while the
/// MEAN read 0.0029942 s, off by 0.28 samples. The allpass term's own group
/// delay is not symmetric around its period average (its excursion is
/// concentrated in a narrow window near each notch and the rest of the
/// period sits near one extreme), so its MEDIAN sits near that extreme, not
/// at the period's zero-mean centre the record's reasoning assumed; the
/// record's own "zero MEAN group delay per period" statement is exactly
/// what the MEAN, not the median, is built to recover. This function
/// therefore returns the trusted MEAN. Median's usual advantage --
/// resistance to a few outlier bins -- does not rescue it here: the bias
/// above was measured on a clean, noise-free fixture, before any real
/// measurement noise is even in the picture.
double meanOfTrusted(std::span<const double> values, std::span<const std::uint8_t> trusted) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t k = 0; k < values.size(); ++k) {
        if (trusted[k]) {
            sum += values[k];
            ++n;
        }
    }
    return (n == 0) ? 0.0 : sum / static_cast<double>(n);
}

}  // namespace

ExcessPhaseResult excessPhase(std::span<const float> magnitudeHalfGrid,
                              std::span<const std::complex<double>> hHalfGrid,
                              std::span<const std::uint8_t> trusted, double sampleRate,
                              std::size_t oversampleFactor) {
    validate(magnitudeHalfGrid, hHalfGrid, trusted, sampleRate, oversampleFactor);

    ExcessPhaseResult result;
    result.oversampleFactor = oversampleFactor;

    std::size_t trustedCount = 0;
    for (auto t : trusted) {
        if (t) ++trustedCount;
    }
    // Never a curve built from too little to mean anything (memory/
    // a-fixed-defect-returns-through-the-silent-fallback.md): valid stays
    // false and every output stays at its default.
    if (trustedCount < kMinTrustedBins) return result;

    const std::size_t m = magnitudeHalfGrid.size();
    const std::size_t nFft = 2 * (m - 1);

    const auto nearest = nearestTrustedIndex(trusted);
    std::vector<float> filled(m);
    for (std::size_t k = 0; k < m; ++k) filled[k] = magnitudeHalfGrid[nearest[k]];

    const auto magOSFull = interpolateOversampledFullCircle(filled, oversampleFactor, nFft);
    const auto minPhase = minimumPhaseFromMagnitude(magOSFull, kMinPhaseFloorDb);

    // Step 4: every oversampleFactor-th oversampled bin lands exactly on an
    // original bin -- an exact lookup, never a second interpolation.
    std::vector<std::complex<double>> ratioRaw(m);
    for (std::size_t k = 0; k < m; ++k) {
        const auto& c = minPhase.spectrum[k * oversampleFactor];
        ratioRaw[k] = hHalfGrid[k] / std::complex<double>(c.real(), c.imag());
    }

    const double binWidthHz = sampleRate / static_cast<double>(nFft);
    result.excessGroupDelay.assign(m, 0.0);
    groupDelaySeconds(ratioRaw, binWidthHz, kGroupDelaySmoothingBins, result.excessGroupDelay);
    result.broadbandDelaySec = meanOfTrusted(result.excessGroupDelay, trusted);

    // Step 7: the delay is removed by ONE complex multiply before the final
    // arg() -- never by adding angles arithmetically, which would need its
    // own unwrap bookkeeping (see ExcessPhase.h's doc comment on step 5/7).
    result.excessPhaseRad.assign(m, 0.0f);
    for (std::size_t k = 0; k < m; ++k) {
        const double f = static_cast<double>(k) * binWidthHz;
        const double omega = 2.0 * kPi * f;
        const std::complex<double> corrected =
            ratioRaw[k] * std::exp(std::complex<double>(0.0, omega * result.broadbandDelaySec));
        result.excessPhaseRad[k] = static_cast<float>(std::arg(corrected));
    }

    result.valid = true;
    return result;
}

}  // namespace rta::dsp
