// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/Biquad.h"

#include <complex>
#include <optional>
#include <span>
#include <vector>

namespace rta::dsp {

/// The five G11 virtual-processor operations -- record
/// docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.5.
///
/// Every op is frequency-domain, stateless and allocation-free: spans in, span
/// out. They exist so an operator can see what a delay, a polarity flip, a gain
/// trim or a filter bank WOULD do to a measured response before touching the
/// processor, and so the predicted sum can be drawn beside the measured one.
///
/// ## There is no frequency vector in this codebase
///
/// `TransferSnapshot` carries `binWidthHz` (TransferEstimator.h:63) and no axis
/// array; the engine side spells the same thing `DualFftEngine::binFrequency`
/// (DualFftEngine.h:66). So every op here takes `double binWidthHz` and bin `k`
/// is at `k * binWidthHz`. That is also what keeps them allocation-free: there
/// is no axis to build. (ALIGN-R5.)
///
/// ## Sizes
///
/// Each op writes `min(in.size(), out.size())` bins and touches nothing else.
/// A short `out` is a truncation, not undefined behaviour, and there is no
/// throw on a hot preview path.

/// H'(f) = H(f) * e^{-j2*pi*f*tau}.
///
/// The exponent is NEGATIVE for a positive tau, because a positive tau means
/// this source arrives LATER -- the sign `DelayEstimate::delaySamples` and
/// `DualFftEngine::Config::referenceDelaySamples` already share
/// (memory/dual-fft-conventions.md item 2). Flipping it does not fail to
/// compensate; it compensates the wrong way and doubles the error.
///
/// Exact for fractional tau: the rotation is applied per bin, so there is no
/// interpolation and therefore no interpolation error.
void applyDelay(std::span<const std::complex<double>> in, double binWidthHz, double tauSeconds,
                std::span<std::complex<double>> out) noexcept;

/// H' = -H. Magnitude untouched (IEEE-754 negation is the sign bit alone),
/// phase turned by pi at every bin.
void applyPolarity(std::span<const std::complex<double>> in,
                   std::span<std::complex<double>> out) noexcept;

/// H' = g * H, with g LINEAR (not dB). 20log10|H'| - 20log10|H| = 20log10 g.
void applyGain(std::span<const std::complex<double>> in, double gainLinear,
               std::span<std::complex<double>> out) noexcept;

/// H' = H * prod_i H_i(e^{jw}), w = 2*pi*k*binWidthHz/sampleRate.
///
/// Delegates per bin to rta::dsp::cascadeResponse (BiquadResponse.h:25). The
/// numerator/denominator is NOT re-derived here: a second spelling of one
/// formula is exactly what W0-R3 split BiquadResponse.h out to prevent, and
/// ALIGN-R1 re-states it for this lane.
void applyBiquads(std::span<const std::complex<double>> in, double binWidthHz, double sampleRate,
                  std::span<const Biquad::Coeffs> sections,
                  std::span<std::complex<double>> out);

/// H_Sigma = H_A + H_B, with a per-bin TRUST that is deliberately not a
/// coherence.
///
/// The sum is not an estimate any cross-spectrum defines (record Sec.5; L6b
/// Sec.4 makes the same point for the spatial average), so it has no coherence
/// to carry. What the L5c fade needs is a per-bin trust, and naming that field
/// `coherence` would put a second writer beside TransferEstimator.cpp's
/// makeSnapshot() -- the one place the gate is applied. The guard
/// `coherence_gate_is_not_bypassed` then stays true by construction rather
/// than by exemption.
struct SummedResponse {
    std::vector<std::complex<double>> h;

    /// min(gamma^2_A, gamma^2_B) per bin, or EMPTY when either input had none.
    /// Empty, never zero-filled: a zero-filled placeholder reads as "measured,
    /// and totally untrusted", which is a different claim from "not measured"
    /// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    std::vector<float> summationTrust;

    /// False when either input's coherence was absent or did not span the sum.
    bool trustPresent = false;
};

/// Sums `min(a.size(), b.size())` bins. Trust is produced only when BOTH
/// coherence arguments are engaged and both span the summed length; anything
/// less is absence, and absence is reported as absence.
[[nodiscard]] SummedResponse sumResponses(std::span<const std::complex<double>> a,
                                          std::span<const std::complex<double>> b,
                                          const std::optional<std::vector<float>>& coherenceA,
                                          const std::optional<std::vector<float>>& coherenceB);

}  // namespace rta::dsp
