// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/dsp/BiquadResponse.h"

namespace rta::dsp {

std::complex<double> biquadResponse(const Biquad::Coeffs& c,
                                     double omegaRadiansPerSample) noexcept {
    // z^-1 = e^{-jw}; z^-2 is just that squared, so both powers come from one
    // trig evaluation instead of two (std::polar for each) -- the same
    // "avoid re-deriving it" reasoning as the header split itself.
    const std::complex<double> zInv{ std::polar(1.0, -omegaRadiansPerSample) };
    const std::complex<double> zInv2 = zInv * zInv;

    const std::complex<double> numerator = c.b0 + c.b1 * zInv + c.b2 * zInv2;
    const std::complex<double> denominator = 1.0 + c.a1 * zInv + c.a2 * zInv2;
    return numerator / denominator;
}

std::complex<double> cascadeResponse(std::span<const Biquad::Coeffs> sections,
                                      double omegaRadiansPerSample) noexcept {
    std::complex<double> total{ 1.0, 0.0 };
    for (const auto& c : sections) {
        total *= biquadResponse(c, omegaRadiansPerSample);
    }
    return total;
}

}  // namespace rta::dsp
