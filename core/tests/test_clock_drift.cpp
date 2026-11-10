// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 T2 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E3, section 5):
// phase-slope fractional delay and the Theil-Sen clock line.
//
// FIXTURE (independent of the Kaiser interpolator, T1). A white signal of
// M = 2^16 samples with only the interior bins populated is delayed by tau in
// the FREQUENCY domain, Y_k = X_k e^{-j 2 pi k tau / M}, in double with a local
// radix-2 FFT. That is an exact circular band-limited delay, so a window cut
// from y at the same positions as from x is an exact fractional delay of x.
//
// BOUND, DERIVED (Welch/Bendat-Piersol phase variance). Per bin the phase of
// H1 has variance (1 - g2) / (2 g2 Neff) for coherence g2 and effective
// averages Neff. Here the only decorrelation is
//   (a) the two Hann frames differ by the shift tau: the meas frame is
//       w(n) x(n - tau), the ref frame w(n) x(n); the energy of the difference
//       w(n) - w(n - tau) ~ tau w'(n) relative to w is
//       tau^2 (pi/N)^2 sum sin^2 / sum w^2 = (4/3) pi^2 tau^2 / N^2
//       (N = 4096, Hann w = (1 - cos)/2), an UPPER bound on 1 - g2 because
//       the part in phase with X is coherent; and
//   (b) float32 FFT rounding, ~(3e-7)^2 per channel = 1e-13, negligible.
// For |tau| <= 0.5: 1 - g2 <= 1.96e-7 (so the clamp 1e-6 sets the WEIGHTS but
// not the variance). The fit slope a (rad/Hz) of n independent bins of equal
// variance s^2 spaced df has variance s^2 / sum (f - fbar)^2; adjacent Hann
// bins are correlated (rho = 2/3 at one bin spacing), which inflates the
// variance of a slope by at most ~ (1 + 2 rho + ...) = 5 -> 2.3x in sigma, rounded
// up to 4x. tau_samples = -a fs / (2 pi). The bound is 6 sigma of that.
#include "rta/dsp/AverageCount.h"
#include "rta/dsp/Biquad.h"
#include "rta/dsp/ClockDrift.h"
#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/TransferEstimator.h"
#include "rta/dsp/Window.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <random>
#include <vector>

using namespace rta::dsp;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;
constexpr std::size_t kM = 1u << 16;
constexpr std::size_t kW = 1u << 15;

using Cd = std::complex<double>;

void fft(std::vector<Cd>& a, bool inverse) {
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
std::vector<double> makeSignal(std::size_t loBin, std::size_t hiBin, unsigned seed) {
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
std::vector<double> delayed(const std::vector<double>& x, double tau, double theta = 0.0) {
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

std::vector<float> cut(const std::vector<double>& v, std::size_t start) {
    std::vector<float> w(kW);
    for (std::size_t i = 0; i < kW; ++i) w[i] = static_cast<float>(v[start + i]);
    return w;
}

double neffFor(std::ptrdiff_t integerDelay) {
    const Window win(WindowType::Hann, 4096);
    const std::size_t usable = kW - static_cast<std::size_t>(std::abs(integerDelay));
    const std::size_t frames = (usable - 4096) / 1024 + 1;
    return fifoEffectiveAverages(win.coefficients(), 1024, frames);
}

/// 6 sigma of the slope-fit delay, in samples (derivation in the file header).
double delayBound(std::ptrdiff_t integerDelay) {
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

/// Model standard deviation of the fitted delay (samples) for THIS window pair,
/// from the engine's own coherence: per-bin phase variance (1-g2)/(2 g2 Neff)
/// (floored at 1e-13, the float32 FFT noise), weighted-LS slope variance
/// sum w^2 v (f-fw)^2 / (sum w (f-fw)^2)^2 with the same weights the fit uses,
/// times 4 for the correlation of adjacent Hann bins (see the header).
/// RBJ peaking EQ, 4 kHz, Q 1, +6 dB: a fixed, non-linear system phase.
Biquad::Coeffs peaking() {
    const double A = std::pow(10.0, 6.0 / 40.0), w0 = 2.0 * kPi * 4000.0 / kFs;
    const double alpha = std::sin(w0) / 2.0, c = std::cos(w0);
    const double a0 = 1.0 + alpha / A;
    return {(1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0,
            (1.0 - alpha / A) / a0};
}

double peakingPhase(double f) {
    const auto co = peaking();
    const Cd z1 = std::polar(1.0, -2.0 * kPi * f / kFs), z2 = z1 * z1;
    return std::arg((co.b0 + co.b1 * z1 + co.b2 * z2) / (1.0 + co.a1 * z1 + co.a2 * z2));
}

struct Model { double sigma; double predictedFraction; };
Model modelFit(const std::vector<float>& ref, const std::vector<float>& meas, int integerDelay,
               double tauFraction, double (*sysPhase)(double) = nullptr) {
    DualFftEngine::Config cfg;
    cfg.fftSize = 4096; cfg.hopSize = 1024; cfg.sampleRate = kFs;
    cfg.averaging = TransferAveraging::Cumulative;
    cfg.referenceDelaySamples = integerDelay;
    DualFftEngine eng(cfg);
    eng.process(ref, meas);
    const auto snap = makeSnapshot(eng, Estimator::H1);
    REQUIRE(snap.coherence.has_value());
    double sw = 0.0, swf = 0.0;
    std::vector<double> f, w, v;
    for (std::size_t k = 1; k + 1 < snap.h.size(); ++k) {  // no DC, no Nyquist: real bins
        const double fk = eng.binFrequency(k), g2 = (*snap.coherence)[k];
        if (fk > kFs / 4.0 || g2 < 0.5) continue;
        const double om = std::max(1.0 - g2, 1e-13);
        f.push_back(fk);
        w.push_back(g2 / std::max(1.0 - g2, 1e-6));
        v.push_back(om / (2.0 * g2 * snap.effectiveAverages));
        sw += w.back(); swf += w.back() * fk;
    }
    const double fw = swf / sw;
    double num = 0.0, den = 0.0, cross = 0.0;
    for (std::size_t i = 0; i < f.size(); ++i) {
        const double d = f[i] - fw;
        num += w[i] * w[i] * v[i] * d * d;
        den += w[i] * d * d;
        // The true phase the estimator is fitting: -2 pi f tau + system phase.
        const double phase = -2.0 * kPi * f[i] * tauFraction / kFs + (sysPhase ? sysPhase(f[i]) : 0.0);
        cross += w[i] * d * phase;
    }
    // Slope of the weighted fit of the TRUE phase with THIS window's weights
    // (the f in the engine is in Hz and tau in samples, hence / kFs above).
    return {4.0 * std::sqrt(num) / den * kFs / (2.0 * kPi), -cross / den * kFs / (2.0 * kPi)};
}

PhaseSlopeOptions options() {
    PhaseSlopeOptions o;
    o.sampleRate = kFs;
    return o;
}

}  // namespace

TEST_CASE("clock drift: a 2^15 window reaches the coherence gate (Neff >= 8)") {
    // 4096 / 1024: 28 frames after a 25-sample skip. At 16384 it would be 5.
    CHECK(neffFor(25) >= 8.0);
    CHECK(neffFor(0) >= 8.0);
    const Window win(WindowType::Hann, 16384);
    CHECK(fifoEffectiveAverages(win.coefficients(), 1024, (kW - 16384) / 1024 + 1) < 8.0);
}

TEST_CASE("clock drift: pure fractional delays are recovered from the H1 phase slope") {
    const auto x = makeSignal(1, kM / 2 - 1, 11);
    const double fractions[] = {0.0, 0.13, 0.37, -0.42};
    constexpr std::ptrdiff_t integerPart = 25;
    const double bound = delayBound(integerPart);
    for (const double frac : fractions) {
        const auto y = delayed(x, static_cast<double>(integerPart) + frac);
        const auto r = fractionalDelayByPhaseSlope(cut(x, 3000), cut(y, 3000), integerPart, options());
        REQUIRE(r.has_value());
        const double err = std::fabs(r->delaySamples - (static_cast<double>(integerPart) + frac));
        std::printf("[T2] frac=%+.2f  err=%.3e  bound=%.3e  ratio=%.3f  bins=%zu\n", frac, err,
                    bound, err / bound, r->binsUsed);
        CHECK(err <= bound);
        CHECK(r->binsUsed > 900);
    }
    // An integer delay wrong by 2 samples leaves |tau| = 2.37 > 1: the fit's
    // validity domain (header) is exceeded, so the answer is withheld.
    const auto y = delayed(x, 25.37);
    CHECK_FALSE(fractionalDelayByPhaseSlope(cut(x, 3000), cut(y, 3000), 23, options()).has_value());
}

TEST_CASE("clock drift: a fixed biquad's phase lands in the intercept, not the delay difference") {
    const auto x = makeSignal(1, kM / 2 - 1, 23);
    const Biquad::Coeffs co = peaking();
    auto through = [&](double tau) {
        auto y = delayed(x, 25.0 + tau);
        Biquad::State st;
        for (auto& v : y) v = Biquad::processSample(co, st, v);
        return y;
    };
    const auto yA = through(0.13), yB = through(0.37);
    const auto a = fractionalDelayByPhaseSlope(cut(x, 8192), cut(yA, 8192), 25, options());
    const auto b = fractionalDelayByPhaseSlope(cut(x, 24000), cut(yB, 24000), 25, options());
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const double diff = b->delaySamples - a->delaySamples;
    // WHAT IS ASSERTED. The fit is a weighted least-squares line through the
    // TRUE phase, -2 pi f tau + phi_sys(f). phi_sys is not linear, so the
    // fitted slope is tau PLUS a weighted mean of the system's group delay, and
    // the weights (gamma^2 / (1 - gamma^2)) differ a little from window to
    // window with the noise. So the difference of two windows is NOT exactly
    // 0.24: it is 0.24 plus the change of that weighted mean (observed ~0.01
    // sample, the figure record E6 quotes). The test therefore predicts each
    // window from ITS OWN weights applied to the true phase, and bounds the
    // observed delay by 6 sigma of the model noise (per-bin phase variance
    // (1 - g2)/(2 g2 Neff), weighted-LS slope variance, x4 for Hann
    // neighbour correlation) around that prediction.
    const auto mA = modelFit(cut(x, 8192), cut(yA, 8192), 25, 0.13, peakingPhase);
    const auto mB = modelFit(cut(x, 24000), cut(yB, 24000), 25, 0.37, peakingPhase);
    const double errA = std::fabs(a->delaySamples - 25.0 - mA.predictedFraction);
    const double errB = std::fabs(b->delaySamples - 25.0 - mB.predictedFraction);
    std::printf("[T2] biquad  A: err=%.3e bound=%.3e ratio=%.3f  B: err=%.3e bound=%.3e ratio=%.3f"
                "  (diff-0.24=%.3e; weights move the slope by %.3e)\n",
                errA, 6.0 * mA.sigma, errA / (6.0 * mA.sigma), errB, 6.0 * mB.sigma,
                errB / (6.0 * mB.sigma), diff - 0.24, mB.predictedFraction - mA.predictedFraction - 0.24);
    CHECK(errA <= 6.0 * mA.sigma);
    CHECK(errB <= 6.0 * mB.sigma);
    // The system delay itself is NOT zero: the test would pass vacuously if the
    // biquad had no effect on the phase.
    CHECK(std::fabs((a->delaySamples - 25.0) - 0.13) > 0.01);
}

TEST_CASE("clock drift: a constant system phase lands in the intercept and leaves the delay alone") {
    const auto x = makeSignal(1, kM / 2 - 1, 41);
    constexpr double theta = 3.0;  // phase starts near -pi: wraps within a few bins, so the unwrap is exercised
    const auto y = delayed(x, 25.37, theta);
    const auto r = fractionalDelayByPhaseSlope(cut(x, 3000), cut(y, 3000), 25, options());
    REQUIRE(r.has_value());
    const double bound = delayBound(25);
    // Intercept = phase at f = 0 = -theta. Its sigma: sigma_phase^2 / n + (fbar sigma_a)^2,
    // fbar = fs/8; delayBound = 24 sigma_a fs / 2pi, so sigma_a = bound 2pi / (24 fs).
    const double neff = neffFor(25);
    const double sigmaPhase = std::sqrt(((4.0 / 3.0) * kPi * kPi * 0.25 / (4096.0 * 4096.0) + 1e-13) / (2.0 * neff));
    const double sigmaA = bound * 2.0 * kPi / (24.0 * kFs);
    const double fbar = kFs / 8.0;
    const double bound0 = 24.0 * std::sqrt(sigmaPhase * sigmaPhase / 1025.0 + fbar * fbar * sigmaA * sigmaA);
    std::printf("[T2] const phase  delay err=%.3e (bound %.3e)  intercept err=%.3e (bound %.3e)\n",
                std::fabs(r->delaySamples - 25.37), bound, std::fabs(r->interceptRadians + theta), bound0);
    CHECK(std::fabs(r->delaySamples - 25.37) <= bound);
    CHECK(std::fabs(r->interceptRadians + theta) <= bound0);
}

TEST_CASE("clock drift: too few usable bins gives nullopt, with a positive control") {
    // A 100..500 Hz signal (bins 137..683 of the 2^16 fixture FFT) plus an
    // INDEPENDENT white noise floor 60 dB down on each channel, as any real
    // recording has. Control: asking for 150..450 Hz finds ~26 coherent bins.
    // Asking for 5..20 kHz finds fewer than 8 that pass gamma^2 >= 0.5: there
    // the two channels hold independent noise. (Without the noise floor this
    // fails to fail: float32 FFT twiddle error leaks the 100..500 Hz content
    // coherently into every bin at ~-130 dB, gamma^2 reads ~1 there, and a
    // minBins refusal needs a band where the pair is genuinely uncorrelated.)
    const auto sig = makeSignal(137, 683, 31);
    const auto sigDelayed = delayed(sig, 25.37);
    const auto noiseA = makeSignal(1, kM / 2 - 1, 51), noiseB = makeSignal(1, kM / 2 - 1, 52);
    std::vector<double> x(kM), y(kM);
    for (std::size_t i = 0; i < kM; ++i) {
        x[i] = sig[i] + 1e-3 * noiseA[i];
        y[i] = sigDelayed[i] + 1e-3 * noiseB[i];
    }
    PhaseSlopeOptions o = options();
    o.minHz = 150.0;
    o.maxHz = 450.0;
    const auto control = fractionalDelayByPhaseSlope(cut(x, 3000), cut(y, 3000), 25, o);
    REQUIRE(control.has_value());
    CHECK(control->binsUsed >= 25);
    CHECK(std::fabs(control->delaySamples - 25.37) < 0.01);
    std::printf("[T2] control err=%.3e bins=%zu\n", std::fabs(control->delaySamples - 25.37), control->binsUsed);
    o.minHz = 5000.0;
    o.maxHz = 20000.0;
    CHECK_FALSE(fractionalDelayByPhaseSlope(cut(x, 3000), cut(y, 3000), 25, o).has_value());
    // The minBins branch itself: a coherent 4-bin band (150..190 Hz) is refused
    // at the default 8 and accepted when the caller lowers the floor to 3. The
    // 5..20 kHz case above may be refused for finding fewer than 2 bins at all.
    o.minHz = 150.0;
    o.maxHz = 190.0;
    CHECK_FALSE(fractionalDelayByPhaseSlope(cut(x, 3000), cut(y, 3000), 25, o).has_value());
    o.minBins = 3;
    const auto fewBins = fractionalDelayByPhaseSlope(cut(x, 3000), cut(y, 3000), 25, o);
    REQUIRE(fewBins.has_value());
    CHECK(fewBins->binsUsed >= 3);
    CHECK(fewBins->binsUsed < 8);
    o.minBins = 8;
    // Window too short for the gate: no coherence, no answer.
    const auto shortX = cut(x, 3000);
    const auto shortY = cut(y, 3000);
    CHECK_FALSE(fractionalDelayByPhaseSlope(std::span<const float>(shortX).first(6000),
                                            std::span<const float>(shortY).first(6000), 25,
                                            options()).has_value());
    CHECK_THROWS_AS(fractionalDelayByPhaseSlope(std::span<const float>(shortX).first(6000), shortY,
                                                25, options()), std::invalid_argument);
}
