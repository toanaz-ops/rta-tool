// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 fix (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E3, review finding H2): the delay of one
// window RELATIVE to another, on a system whose phase is far from linear.
//
// FIXTURE. As test_clock_drift.cpp (a white band-limited signal delayed in the FREQUENCY domain, an
// exact circular fractional delay) with the SYSTEM applied in the frequency domain too: a Linkwitz-
// Riley 4th-order crossover's low-pass + high-pass sum, which is the 2nd-order all-pass with the
// Butterworth Q (Linkwitz & Riley 1976):
//   H(jw) = (w0^2 - w^2 - j w w0 / Q) / (w0^2 - w^2 + j w w0 / Q),   Q = 1 / sqrt 2.
// Its group delay is 2 / (Q w0): 216 samples at 100 Hz, 15 at 1500 Hz. That phase is in BOTH windows,
// so in Hw conj(Ha) it cancels exactly and the answer is the delay DIFFERENCE, 0.37 - 0.13 = 0.24.
//
// BOUND. Same model as test_clock_drift.cpp::modelFit, for a DIFFERENCE of two independent windows:
// per-bin phase variance (1 - g2) / (2 g2 Neff) of each window (floored at 1e-13), added; the weighted
// least-squares slope variance sum w^2 V (f - fw)^2 / (sum w (f - fw)^2)^2 with the weights the fit
// uses (1 / ((1-gw)/gw + (1-ga)/ga)); x4 in sigma for the correlation of adjacent Hann bins; 6 sigma.
// The coherence is the ENGINE's own, so the dispersion the all-pass puts inside a 4096 frame (which is
// what takes gamma^2 below 1 here, unlike the flat fixture) is in the bound, not assumed away.
#include "ClockDriftFixture.h"

#include "rta/dsp/ClockDrift.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using namespace rta::dsp;
using namespace clockfix;

namespace {

/// Delay by `tau` samples then the LR4-sum all-pass at `fc`, both in the frequency domain.
std::vector<double> delayedThroughCrossover(const std::vector<double>& x, double tau, double fc) {
    std::vector<Cd> spec(kM);
    for (std::size_t i = 0; i < kM; ++i) spec[i] = Cd(x[i], 0.0);
    fft(spec, false);
    const double w0 = 2.0 * kPi * fc, q = 1.0 / std::sqrt(2.0);
    for (std::size_t k = 1; k < kM / 2; ++k) {
        const double w = 2.0 * kPi * static_cast<double>(k) * kFs / static_cast<double>(kM);
        const Cd bend(w0 * w0 - w * w, -w * w0 / q);
        spec[k] *= std::polar(1.0, -2.0 * kPi * static_cast<double>(k) * tau / static_cast<double>(kM)) * bend /
                   std::conj(bend);
        spec[kM - k] = std::conj(spec[k]);
    }
    fft(spec, true);
    std::vector<double> y(kM);
    for (std::size_t i = 0; i < kM; ++i) y[i] = spec[i].real();
    return y;
}

/// 6 sigma of the relative delay (samples) for THIS pair of windows (see the file header).
double relativeBound(const WindowSpectrum& window, const WindowSpectrum& anchor) {
    double sw = 0.0, swf = 0.0;
    std::vector<double> f, w, v;
    for (std::size_t k = 1; k + 1 < window.h.size(); ++k) {
        const double fk = static_cast<double>(k) * window.binHz;
        const double gw = window.gammaSquared[k], ga = anchor.gammaSquared[k];
        if (fk > kFs / 4.0 || gw < 0.5 || ga < 0.5) continue;
        const auto varOf = [&](double g, double neff) { return std::max(1.0 - g, 1e-13) / (2.0 * g * neff); };
        const auto relVar = [](double g) { return std::max(1.0 - g, 1e-6) / g; };
        f.push_back(fk);
        w.push_back(1.0 / (relVar(gw) + relVar(ga)));
        v.push_back(varOf(gw, window.effectiveAverages) + varOf(ga, anchor.effectiveAverages));
        sw += w.back();
        swf += w.back() * fk;
    }
    const double fw = swf / sw;
    double num = 0.0, den = 0.0;
    for (std::size_t i = 0; i < f.size(); ++i) {
        const double d = f[i] - fw;
        num += w[i] * w[i] * v[i] * d * d;
        den += w[i] * d * d;
    }
    return 6.0 * 4.0 * std::sqrt(num) / den * kFs / (2.0 * kPi);
}

struct Pair2 {
    WindowSpectrum a, b;  ///< fraction 0.13 and 0.37, both with the same crossover
    std::vector<float> refA, measA;
};

Pair2 build(double fc, unsigned seed) {
    const auto x = makeSignal(1, kM / 2 - 1, seed);
    const auto yA = delayedThroughCrossover(x, 25.13, fc);
    const auto yB = delayedThroughCrossover(x, 25.37, fc);
    Pair2 p;
    p.refA = cut(x, 3000);
    p.measA = cut(yA, 3000);
    const auto a = windowSpectrum(p.refA, p.measA, 25, kFs);
    const auto b = windowSpectrum(cut(x, 24000), cut(yB, 24000), 25, kFs);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    p.a = *a;
    p.b = *b;
    return p;
}

}  // namespace

TEST_CASE("relative delay: two windows through an LR4 crossover differ by their known fraction") {
    for (const double fc : {100.0, 1500.0}) {
        const Pair2 p = build(fc, 7);
        const auto r = relativeFractionalDelay(p.b, p.a, options());
        REQUIRE(r.has_value());
        const double bound = relativeBound(p.b, p.a);
        std::printf("[relative] crossover %4.0f Hz: difference %.5f (true 0.24)  err %.3e  bound %.3e\n", fc, *r,
                    std::fabs(*r - 0.24), bound);
        CHECK(std::fabs(*r - 0.24) <= bound);
        // The same pair the other way round is the negative difference.
        const auto back = relativeFractionalDelay(p.a, p.b, options());
        REQUIRE(back.has_value());
        CHECK(std::fabs(*back + 0.24) <= bound);
        // A window against itself is exactly zero.
        const auto self = relativeFractionalDelay(p.a, p.a, options());
        REQUIRE(self.has_value());
        CHECK(std::fabs(*self) < 1e-12);
    }
}

TEST_CASE("relative delay: the absolute fit carries the system's group delay, the relative one does not") {
    // 100 Hz crossover: a group delay of 216 samples at the bottom of the band. The absolute fit
    // (a weighted least-squares slope over 0..fs/4) takes a share of it: measured +0.096 sample here,
    // on a fixture whose every bin is almost perfectly coherent (so the weights favour the top of the
    // band, where the group delay is ~0). On real pink noise through the pair fixture the share is
    // larger than the fit's one-sample validity and the window is dropped (test_wav_pair_lr4.cpp).
    // The relative fit is exact to 1e-5 on the same pair.
    const Pair2 p = build(100.0, 7);
    const auto absolute = fractionalDelayOf(p.a, 25, options());
    const double bound = relativeBound(p.b, p.a);
    const double biasOfAbsolute = absolute ? std::fabs(absolute->delaySamples - 25.13) : 1e9;
    std::printf("[relative] absolute fit at 100 Hz: %s, error %.3f sample; relative bound %.1e\n",
                absolute ? "answered" : "refused", biasOfAbsolute, bound);
    CHECK(biasOfAbsolute > 100.0 * bound);
}

TEST_CASE("relative delay: a bin the ANCHOR's coherence rejects is left out, whatever the window says") {
    // Above 3 kHz the anchor's phase is garbage and its coherence 0.1: those bins must not steer the
    // fit. A fit that looked at the window's coherence only would unwrap through random phase.
    Pair2 p = build(1500.0, 9);
    for (std::size_t k = 1; k + 1 < p.a.h.size(); ++k) {
        if (static_cast<double>(k) * p.a.binHz <= 3000.0) continue;
        p.a.gammaSquared[k] = 0.1f;
        p.a.h[k] = std::polar(1.0, 1.7 * static_cast<double>(k));
    }
    const auto r = relativeFractionalDelay(p.b, p.a, options());
    REQUIRE(r.has_value());
    const double bound = relativeBound(p.b, p.a);
    std::printf("[relative] garbage anchor bins: err %.3e  bound %.3e\n", std::fabs(*r - 0.24), bound);
    CHECK(std::fabs(*r - 0.24) <= bound);
}

TEST_CASE("relative delay: refusals -- a difference past one sample, too few bins, mismatched grids") {
    const auto x = makeSignal(1, kM / 2 - 1, 11);
    const auto yA = delayedThroughCrossover(x, 25.13, 1500.0);
    const auto yFar = delayedThroughCrossover(x, 26.43, 1500.0);  // 1.30 past A: the unwrap's validity is gone
    const auto a = windowSpectrum(cut(x, 3000), cut(yA, 3000), 25, kFs);
    const auto far = windowSpectrum(cut(x, 24000), cut(yFar, 24000), 25, kFs);
    REQUIRE(a.has_value());
    REQUIRE(far.has_value());
    CHECK_FALSE(relativeFractionalDelay(*far, *a, options()).has_value());
    // Just inside: 0.93 past A is answered (the neighbour of the refusal above).
    const auto yNear = delayedThroughCrossover(x, 26.06, 1500.0);
    const auto near = windowSpectrum(cut(x, 24000), cut(yNear, 24000), 25, kFs);
    REQUIRE(near.has_value());
    const auto answered = relativeFractionalDelay(*near, *a, options());
    REQUIRE(answered.has_value());
    CHECK(std::fabs(*answered - 0.93) <= relativeBound(*near, *a));

    PhaseSlopeOptions narrow = options();
    narrow.minHz = 100.0;
    narrow.maxHz = 100.0 + 3.0 * a->binHz;  // fewer than minBins (8) bins
    CHECK_FALSE(relativeFractionalDelay(*near, *a, narrow).has_value());

    WindowSpectrum other = *a;
    other.binHz *= 2.0;
    CHECK_THROWS_AS(relativeFractionalDelay(*near, other, options()), std::invalid_argument);
    other = *a;
    other.h.pop_back();
    CHECK_THROWS_AS(relativeFractionalDelay(*near, other, options()), std::invalid_argument);
}
