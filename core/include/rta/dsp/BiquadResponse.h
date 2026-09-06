// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

// Biquad.h avoids <complex> on purpose (Biquad.h:107): a two-term real
// numerator/denominator does not need it, and the hot audio-callback path
// should not pay for a header a real-time section never uses. This is the
// header that adds it, for a caller who needs phase, not just |H| -- so
// nobody re-derives the numerator/denominator a second time (W0-R3).
#include "rta/dsp/Biquad.h"

#include <complex>
#include <span>

namespace rta::dsp {

/// H(e^{jw}) = (b0 + b1 z^-1 + b2 z^-2) / (1 + a1 z^-1 + a2 z^-2), z = e^{jw}.
/// Same DF2T transfer function BiquadCascade::attenuationDb evaluates
/// (Biquad.h:75); this is the complex form of it.
[[nodiscard]] std::complex<double>
biquadResponse(const Biquad::Coeffs& c, double omegaRadiansPerSample) noexcept;

/// Product of the section responses in cascade order -- the G11
/// biquad-cascade virtual-processor op (ALIGN Sec.5).
[[nodiscard]] std::complex<double>
cascadeResponse(std::span<const Biquad::Coeffs> sections, double omegaRadiansPerSample) noexcept;

}  // namespace rta::dsp
