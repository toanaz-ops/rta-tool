// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The expectations here are not "whatever the code printed last time". For a
// periodic generalised-cosine window w[n] = sum_k (-1)^k a_k cos(2*pi*k*n/N),
// every cosine term sums to exactly zero over a full period, which gives closed
// forms that must hold to machine precision:
//
//     sum(w)    = a0 * N
//     sum(w^2)  = (a0^2 + 0.5 * sum_{k>=1} a_k^2) * N
//     ENBW      = (a0^2 + 0.5 * sum_{k>=1} a_k^2) / a0^2
//
// Those are the identities being asserted. They are also cross-checked against
// the values published in Harris (1978), so a typo in a coefficient is caught
// by the second assertion even if it stays self-consistent with the first.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Window.h"

#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::dsp;

namespace {

constexpr std::size_t kN = 4096;

struct CosineSpec {
    WindowType type;
    std::vector<double> a;
    double publishedEnbw;  // Harris (1978), table 1
};

double analyticEnbw(const std::vector<double>& a) {
    double tail = 0.0;
    for (std::size_t k = 1; k < a.size(); ++k) tail += a[k] * a[k];
    const double num = a[0] * a[0] + 0.5 * tail;
    return num / (a[0] * a[0]);
}

}  // namespace

TEST_CASE("Rectangular window is the identity", "[window]") {
    const Window w(WindowType::Rectangular, kN);
    REQUIRE(w.size() == kN);
    CHECK_THAT(w.coherentGain(), WithinRel(1.0, 1e-12));
    CHECK_THAT(w.amplitudeCorrection(), WithinRel(1.0, 1e-12));
    CHECK_THAT(w.energyCorrection(), WithinRel(1.0, 1e-12));
    CHECK_THAT(w.equivalentNoiseBandwidth(), WithinRel(1.0, 1e-12));
}

TEST_CASE("Cosine windows match their closed-form correction factors", "[window]") {
    const std::vector<CosineSpec> specs = {
        {WindowType::Hann,           {0.5, 0.5},                                              1.5000},
        {WindowType::Hamming,        {0.54, 0.46},                                            1.3628},
        {WindowType::BlackmanHarris, {0.35875, 0.48829, 0.14128, 0.01168},                    2.0044},
        {WindowType::FlatTop,        {0.21557895, 0.41663158, 0.277263158,
                                      0.083578947, 0.006947368},                              3.7702},
    };

    for (const auto& spec : specs) {
        const Window w(spec.type, kN);
        CAPTURE(toString(spec.type));

        const double a0 = spec.a[0];
        double tail = 0.0;
        for (std::size_t k = 1; k < spec.a.size(); ++k) tail += spec.a[k] * spec.a[k];

        // Closed forms -- must hold to machine precision.
        CHECK_THAT(w.sum(), WithinRel(a0 * kN, 1e-10));
        CHECK_THAT(w.sumSquares(), WithinRel((a0 * a0 + 0.5 * tail) * kN, 1e-10));
        CHECK_THAT(w.coherentGain(), WithinRel(a0, 1e-10));
        CHECK_THAT(w.amplitudeCorrection(), WithinRel(1.0 / a0, 1e-10));
        CHECK_THAT(w.equivalentNoiseBandwidth(), WithinRel(analyticEnbw(spec.a), 1e-10));

        // Independent cross-check against the published literature value.
        CHECK_THAT(w.equivalentNoiseBandwidth(), WithinAbs(spec.publishedEnbw, 1e-3));
    }
}

TEST_CASE("Hann correction factors have their textbook values", "[window]") {
    const Window w(WindowType::Hann, kN);
    // Amplitude: a sine read through a Hann window is 6.02 dB low -> factor 2.
    CHECK_THAT(w.amplitudeCorrection(), WithinRel(2.0, 1e-10));
    // Energy: noise is only 4.26 dB low -> factor sqrt(8/3).
    CHECK_THAT(w.energyCorrection(), WithinRel(std::sqrt(8.0 / 3.0), 1e-10));
    // The 1.76 dB gap between them is the error you make by confusing the two.
    const double gapDb = 20.0 * std::log10(w.amplitudeCorrection() / w.energyCorrection());
    CHECK_THAT(gapDb, WithinAbs(1.7609, 1e-3));
}

TEST_CASE("Tukey degenerates to its two endpoints", "[window]") {
    const Window rect(WindowType::Rectangular, kN);
    const Window hann(WindowType::Hann, kN);
    const Window tukey0(WindowType::Tukey, kN, 0.0);
    const Window tukey1(WindowType::Tukey, kN, 1.0);

    for (std::size_t n = 0; n < kN; ++n) {
        REQUIRE_THAT(tukey0.coefficients()[n], WithinAbs(rect.coefficients()[n], 1e-6f));
        REQUIRE_THAT(tukey1.coefficients()[n], WithinAbs(hann.coefficients()[n], 1e-6f));
    }
}

TEST_CASE("apply() multiplies sample-wise and tolerates aliasing", "[window]") {
    constexpr std::size_t n = 64;
    const Window w(WindowType::Hann, n);
    std::vector<float> in(n, 1.0f);
    std::vector<float> out(n, 0.0f);

    w.apply(in, out);
    for (std::size_t i = 0; i < n; ++i) {
        CHECK_THAT(out[i], WithinAbs(w.coefficients()[i], 1e-7f));
    }

    // in == out must give the same result.
    std::vector<float> inPlace(n, 1.0f);
    w.apply(inPlace, inPlace);
    for (std::size_t i = 0; i < n; ++i) {
        CHECK_THAT(inPlace[i], WithinAbs(w.coefficients()[i], 1e-7f));
    }
}

TEST_CASE("Zero-length window is rejected", "[window]") {
    CHECK_THROWS_AS(Window(WindowType::Hann, 0), std::invalid_argument);
}
