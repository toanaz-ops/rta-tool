// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shared fixture of test_clock_drift*.cpp (H12 T2, H12 fix): the circular
// band-limited fractional delay built in the FREQUENCY domain, independent of any
// interpolator, and the bound helpers. Derivation of the bounds: test_clock_drift.cpp.
#pragma once

#include "rta/dsp/AverageCount.h"
#include "rta/dsp/Biquad.h"
#include "rta/dsp/ClockDrift.h"
#include "rta/dsp/Window.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <optional>
#include <span>
#include <random>
#include <vector>

namespace clockfix {

using namespace rta::dsp;

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;
constexpr std::size_t kM = 1u << 16;
constexpr std::size_t kW = 1u << 15;

using Cd = std::complex<double>;

inline void fft(std::vector<Cd>& a, bool inverse) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2.0 * kPi / static_cast<double>(len) * (inverse ? 1.0 : -1.0);
        const Cd wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            Cd w(1.0, 0.0);
            for (std::size_t k = 0; k < len / 2; ++k) {
                const Cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto& v : a) v /= static_cast<double>(n);
}

/// Real signal with Gaussian-ish bins in [loBin, hiBin], zero elsewhere (no DC,
/// no Nyquist), rms 0.25.
inline std::vector<double> makeSignal(std::size_t loBin, std::size_t hiBin, unsigned seed) {
    std::mt19937 gen(seed);
    auto uni = [&] { return (static_cast<double>(gen()) + 1.0) / 4294967297.0; };
    std::vector<Cd> spec(kM, Cd{0.0, 0.0});
    for (std::size_t k = loBin; k <= hiBin; ++k) {
        const double r = std::sqrt(-2.0 * std::log(uni()));
        const double t = 2.0 * kPi * uni();
        spec[k] = Cd(r * std::cos(t), r * std::sin(t));
        spec[kM - k] = std::conj(spec[k]);
    }
    fft(spec, true);
    std::vector<double> x(kM);
    double e = 0.0;
    for (std::size_t i = 0; i < kM; ++i) { x[i] = spec[i].real(); e += x[i] * x[i]; }
    const double g = 0.25 / std::sqrt(e / static_cast<double>(kM));
    for (auto& v : x) v *= g;
    return x;
}

/// y(n) = x(n - tau), circular, exact for a band-limited periodic x; theta is a
/// constant phase rotation e^{-j theta} on every positive frequency (a system
/// with a flat phase of -theta, the cleanest 'system phase' there is).
inline std::vector<double> delayed(const std::vector<double>& x, double tau, double theta = 0.0) {
    std::vector<Cd> spec(kM);
    for (std::size_t i = 0; i < kM; ++i) spec[i] = Cd(x[i], 0.0);
    fft(spec, false);
    for (std::size_t k = 1; k < kM / 2; ++k) {
        const Cd rot = std::polar(1.0, -2.0 * kPi * static_cast<double>(k) * tau / static_cast<double>(kM) - theta);
        spec[k] *= rot;
        spec[kM - k] = std::conj(spec[k]);
    }
    fft(spec, true);
    std::vector<double> y(kM);
    for (std::size_t i = 0; i < kM; ++i) y[i] = spec[i].real();
    return y;
}

inline std::vector<float> cut(const std::vector<double>& v, std::size_t start) {
    std::vector<float> w(kW);
    for (std::size_t i = 0; i < kW; ++i) w[i] = static_cast<float>(v[start + i]);
    return w;
}

inline double neffFor(std::ptrdiff_t integerDelay) {
    const Window win(WindowType::Hann, 4096);
    const std::size_t usable = kW - static_cast<std::size_t>(std::abs(integerDelay));
    const std::size_t frames = (usable - 4096) / 1024 + 1;
    return fifoEffectiveAverages(win.coefficients(), 1024, frames);
}

/// 6 sigma of the slope-fit delay, in samples (derivation in the file header).
inline double delayBound(std::ptrdiff_t integerDelay) {
    const double neff = neffFor(integerDelay);
    const double oneMinusG2 = (4.0 / 3.0) * kPi * kPi * 0.25 / (4096.0 * 4096.0) + 1e-13;
    const double sigmaPhase = std::sqrt(oneMinusG2 / (2.0 * neff));
    const double df = kFs / 4096.0;
    const double nBins = (kFs / 4.0) / df;
    const double sumSq = nBins * (kFs / 4.0) * (kFs / 4.0) / 12.0;  // sum (f - fbar)^2, uniform
    (void)df;
    const double sigmaSlope = sigmaPhase / std::sqrt(sumSq);
    return 6.0 * 4.0 * sigmaSlope * kFs / (2.0 * kPi);
}


/// RBJ peaking EQ, 4 kHz, Q 1, +6 dB: a fixed, non-linear system phase.
/// RBJ peaking EQ, 4 kHz, Q 1, +6 dB: a fixed, non-linear system phase.
inline Biquad::Coeffs peaking() {
    const double A = std::pow(10.0, 6.0 / 40.0), w0 = 2.0 * kPi * 4000.0 / kFs;
    const double alpha = std::sin(w0) / 2.0, c = std::cos(w0);
    const double a0 = 1.0 + alpha / A;
    return {(1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0,
            (1.0 - alpha / A) / a0};
}

inline double peakingPhase(double f) {
    const auto co = peaking();
    const Cd z1 = std::polar(1.0, -2.0 * kPi * f / kFs), z2 = z1 * z1;
    return std::arg((co.b0 + co.b1 * z1 + co.b2 * z2) / (1.0 + co.a1 * z1 + co.a2 * z2));
}

inline PhaseSlopeOptions options() {
    PhaseSlopeOptions o;
    o.sampleRate = kFs;
    return o;
}

/// The absolute phase-slope delay of one window pair (what the tests of the T2 fit call).
inline std::optional<FractionalDelay> fractionalDelayByPhaseSlope(std::span<const float> reference,
                                                                  std::span<const float> measurement,
                                                                  std::ptrdiff_t integerDelay,
                                                                  const PhaseSlopeOptions& options) {
    const auto spectrum = windowSpectrum(reference, measurement, integerDelay, options.sampleRate);
    if (!spectrum) return std::nullopt;
    return fractionalDelayOf(*spectrum, integerDelay, options);
}

}  // namespace clockfix
