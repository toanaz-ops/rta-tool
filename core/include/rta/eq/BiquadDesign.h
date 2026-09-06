// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/Biquad.h"
#include "rta/eq/FilterSpec.h"

namespace rta::eq {

/// RBJ Audio EQ Cookbook synthesis, Q parameterisation (EQ Sec.6) -- the same
/// coefficient forms the Web Audio API biquad filters use, re-derived from
/// the cookbook text rather than transcribed from any one implementation.
///
/// @throws std::invalid_argument if sampleRate <= 0, fcHz <= 0,
///         fcHz >= sampleRate/2, or q <= 0 -- a filter placed at or past
///         Nyquist, or with a non-positive Q or rate, has no coefficients to
///         return.
[[nodiscard]] rta::dsp::Biquad::Coeffs designBiquad(const FilterSpec& spec, double sampleRate);

/// Exact magnitude response in dB (+ = boost, matching gainDb's own sign),
/// i.e. -BiquadCascade::attenuationDb({designBiquad(spec, sampleRate)}, w)
/// with w = 2*pi*hz/sampleRate -- the sign BiquadResponse.h's W0-R3 also
/// corrects, so a caller reading a boost never has to remember which of the
/// two conventions this file uses.
[[nodiscard]] double responseDb(const FilterSpec& spec, double sampleRate, double hz);

}  // namespace rta::eq
