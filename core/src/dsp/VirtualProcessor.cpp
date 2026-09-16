// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/VirtualProcessor.h"

#include "rta/dsp/BiquadResponse.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rta::dsp {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

std::size_t commonLength(std::size_t in, std::size_t out) noexcept { return std::min(in, out); }

}  // namespace

void applyDelay(std::span<const std::complex<double>> in, double binWidthHz, double tauSeconds,
                std::span<std::complex<double>> out) noexcept {
    const std::size_t n = commonLength(in.size(), out.size());
    for (std::size_t k = 0; k < n; ++k) {
        const double f = static_cast<double>(k) * binWidthHz;
        // std::polar, not an incremental multiply: a running rotation would
        // accumulate about n*eps of phase error along the band, and the fits
        // downstream assert at 1e-12. One trig pair per bin is the cheap side
        // of that trade.
        out[k] = in[k] * std::polar(1.0, -kTwoPi * f * tauSeconds);
    }
}

void applyPolarity(std::span<const std::complex<double>> in,
                   std::span<std::complex<double>> out) noexcept {
    const std::size_t n = commonLength(in.size(), out.size());
    for (std::size_t k = 0; k < n; ++k) {
        out[k] = -in[k];
    }
}

void applyGain(std::span<const std::complex<double>> in, double gainLinear,
               std::span<std::complex<double>> out) noexcept {
    const std::size_t n = commonLength(in.size(), out.size());
    for (std::size_t k = 0; k < n; ++k) {
        out[k] = gainLinear * in[k];
    }
}

void applyBiquads(std::span<const std::complex<double>> in, double binWidthHz, double sampleRate,
                  std::span<const Biquad::Coeffs> sections,
                  std::span<std::complex<double>> out) {
    const std::size_t n = commonLength(in.size(), out.size());
    for (std::size_t k = 0; k < n; ++k) {
        const double omega = kTwoPi * (static_cast<double>(k) * binWidthHz) / sampleRate;
        out[k] = in[k] * cascadeResponse(sections, omega);
    }
}

SummedResponse sumResponses(std::span<const std::complex<double>> a,
                            std::span<const std::complex<double>> b,
                            const std::optional<std::vector<float>>& coherenceA,
                            const std::optional<std::vector<float>>& coherenceB) {
    SummedResponse result;
    const std::size_t n = std::min(a.size(), b.size());
    result.h.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        result.h[k] = a[k] + b[k];
    }

    const bool haveBoth = coherenceA.has_value() && coherenceB.has_value()
                          && coherenceA->size() >= n && coherenceB->size() >= n;
    if (!haveBoth) {
        // Left EMPTY on purpose. See SummedResponse::summationTrust.
        return result;
    }

    result.trustPresent = true;
    result.summationTrust.resize(n);
    for (std::size_t k = 0; k < n; ++k) {
        result.summationTrust[k] = std::min((*coherenceA)[k], (*coherenceB)[k]);
    }
    return result;
}

}  // namespace rta::dsp
