// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 T1 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E5, section 5): the
// Kaiser-windowed-sinc interpolator, proved against closed forms only.
//
// BOUND, DERIVED (Kaiser 1974). The kernel is designed for A = 100 dB, so the
// ripple of its continuous frequency response is delta = 10^(-A/20) = 1e-5 in
// the pass band (|f| <= 0.45) and in the stop band (|f| >= 0.55 cycles/sample).
// For a sampled sinusoid x[i] = sin(w i + phi) the reconstruction at position t
// is, by the Poisson sum formula,
//     out(t) = Re{ e^{j(w t + phi)} * sum_m Hhat(f + m) e^{j 2 pi m t} },
// where Hhat is the kernel's continuous response. The m = 0 term is within
// delta of 1 for f <= 0.45; the images m != 0 have |f + m| >= 0.55 and are in
// the stop band. The nearest two carry the stop-band ripple delta each; further
// images fall off with the window's side-lobe decay, so 3 delta bound the sum
// (one pass-band term + two nearest images) with margin. Added to that:
//   * the float32 rounding of each input sample, <= 2^-24 (|x| <= 1), weighted
//     by sum|h|, computed here from the kernel (not assumed), and
//   * the float32 rounding of the output, <= 2^-24.
//   bound = 3 delta + 2^-24 (sum|h| + 1).
#include "rta/dsp/KaiserSincInterpolator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

using rta::dsp::KaiserSincInterpolator;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDelta = 1e-5;                         // 10^(-100/20)
constexpr double kHalfUlp24 = 5.9604644775390625e-08;   // 2^-24

double sumAbsKernel(double position) {
    const double base = std::floor(position);
    double s = 0.0;
    for (int i = -31; i <= 32; ++i) {
        s += std::fabs(KaiserSincInterpolator::kernel(position - (base + i)));
    }
    return s;
}

}  // namespace

TEST_CASE("kaiser sinc: integer positions at step 1 copy the input bit for bit") {
    // h(0) = 1 and h(n) = 0 for every other integer whatever the window, so the
    // sum collapses to input[i] * 1.0 and the double -> float round trip is
    // exact. A kernel that evaluated sin(pi n)/(pi n) literally would leave a
    // ~1e-16 smear and fail the memcmp.
    std::mt19937 gen(7);
    std::vector<float> in(400);
    for (auto& v : in) v = static_cast<float>(static_cast<double>(gen()) / 2147483648.0 - 1.0);
    std::vector<float> out(300);
    KaiserSincInterpolator{}.resample(in, 40.0, 1.0, out);
    for (std::size_t k = 0; k < out.size(); ++k) {
        REQUIRE(std::memcmp(&out[k], &in[40 + k], sizeof(float)) == 0);
    }
    REQUIRE(KaiserSincInterpolator::kernel(0.0) == 1.0);
    REQUIRE(KaiserSincInterpolator::kernel(3.0) == 0.0);
    REQUIRE(KaiserSincInterpolator::kernel(-17.0) == 0.0);
    REQUIRE(KaiserSincInterpolator::kernel(32.0) == 0.0);
    REQUIRE(KaiserSincInterpolator::kernel(31.999) != 0.0);
}

TEST_CASE("kaiser sinc: sinusoids to 0.45 fs at step 1 +- 150 ppm, fractional start") {
    const double freqs[] = {0.05, 0.25, 0.45};
    const double steps[] = {1.0 + 150e-6, 1.0 - 150e-6};
    constexpr double t0 = 40.37;
    constexpr double phi = 0.6;
    constexpr std::size_t count = 2000;
    for (const double f : freqs) {
        for (const double step : steps) {
            std::vector<float> in(2200);
            for (std::size_t i = 0; i < in.size(); ++i) {
                in[i] = static_cast<float>(std::sin(2.0 * kPi * f * static_cast<double>(i) + phi));
            }
            std::vector<float> out(count);
            KaiserSincInterpolator{}.resample(in, t0, step, out);
            double worst = 0.0, worstSum = 0.0;
            for (std::size_t k = 0; k < count; ++k) {
                const double t = t0 + static_cast<double>(k) * step;
                worst = std::max(worst, std::fabs(static_cast<double>(out[k]) -
                                                  std::sin(2.0 * kPi * f * t + phi)));
                worstSum = std::max(worstSum, sumAbsKernel(t));
            }
            const double bound = 3.0 * kDelta + kHalfUlp24 * (worstSum + 1.0);
            std::printf("[T1] f=%.2f fs step=%+.0f ppm  worst=%.3e  bound=%.3e  ratio=%.3f  sum|h|=%.3f\n",
                        f, (step - 1.0) * 1e6, worst, bound, worst / bound, worstSum);
            CHECK(worst <= bound);
        }
    }
}

TEST_CASE("kaiser sinc: the kernel's continuous response is in the stop band above 0.55") {
    // A baseband interpolator cannot attenuate a TONE at 0.6 fs -- as a sampled
    // sequence it IS the 0.4 fs tone (sin(2 pi 0.6 n) = -sin(2 pi 0.4 n)) and
    // is reconstructed as such. What the stop band protects is the images in
    // the sum above: Hhat(f) for f >= 0.55. Measure Hhat by quadrature of the
    // kernel itself (trapezoid, dx = 1/256; h(+-32) = 0 so the ends vanish).
    // Bound: 2 delta, delta = 1e-5 being Kaiser's design ripple; the 2x covers
    // that N ~ 64.07 is an empirical fit, not an identity.
    constexpr int perUnit = 256;
    auto response = [&](double f) {
        std::complex<double> acc{0.0, 0.0};
        for (int n = -32 * perUnit + 1; n < 32 * perUnit; ++n) {
            const double x = static_cast<double>(n) / perUnit;
            acc += KaiserSincInterpolator::kernel(x) * std::polar(1.0, -2.0 * kPi * f * x);
        }
        return std::abs(acc) / perUnit;
    };
    double worst = 0.0;
    for (double f = 0.55; f <= 3.0; f += 0.01) worst = std::max(worst, response(f));
    std::printf("[T1] stop band worst |Hhat| = %.3e  bound=%.3e  ratio=%.3f\n", worst,
                2.0 * kDelta, worst / (2.0 * kDelta));
    CHECK(worst <= 2.0 * kDelta);
    // The pass band edge is flat to the same ripple.
    const double edge = std::fabs(response(0.45) - 1.0);
    std::printf("[T1] pass band edge |Hhat(0.45)-1| = %.3e  bound=%.3e\n", edge, 2.0 * kDelta);
    CHECK(edge <= 2.0 * kDelta);
}

TEST_CASE("kaiser sinc: out-of-range taps and a non-positive step throw") {
    std::vector<float> in(300, 0.5f);
    std::vector<float> out(10);
    const KaiserSincInterpolator interp;
    // Integer position 31 needs indices 0..62: exactly in range.
    CHECK_NOTHROW(interp.resample(in, 31.0, 1.0, out));
    // 30.99 needs floor 30 - 31 = -1.
    CHECK_THROWS_AS(interp.resample(in, 30.99, 1.0, out), std::invalid_argument);
    // The high end: last output at 259.5 + 9 needs index ceil(268.5) + 31 = 300.
    CHECK_THROWS_AS(interp.resample(in, 259.5, 1.0, out), std::invalid_argument);
    CHECK_NOTHROW(interp.resample(in, 258.5, 1.0, out));
    CHECK_THROWS_AS(interp.resample(in, 100.0, 0.0, out), std::invalid_argument);
    CHECK_THROWS_AS(interp.resample(in, 100.0, -1.0, out), std::invalid_argument);
    CHECK_THROWS_AS(interp.resample(in, 100.0, std::nan(""), out), std::invalid_argument);
    const auto span = KaiserSincInterpolator::requiredInputSpan(40.37, 1.0001, 100);
    CHECK(span.first == 9);
    CHECK(span.last == static_cast<std::ptrdiff_t>(std::ceil(40.37 + 99 * 1.0001)) + 31);
}
