// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <complex>
#include <span>
#include <vector>

namespace rta::dsp {

/// Same -120 dB as TransferSnapshot::kMagnitudeFloorDb (TransferEstimator.h:70)
/// -- one constant for one physical reason (a measurement's noise floor),
/// deliberately not re-derived for this second consumer (W0-R1 step 1).
inline constexpr float kMinPhaseFloorDb = -120.0f;

/// nFft bins of the minimum-phase complex spectrum: |spectrum[k]| equals the
/// floored input magnitude exactly (T2); arg(spectrum[k]) is the reconstructed
/// minimum phase.
struct MinimumPhaseResult {
    std::vector<std::complex<float>> spectrum;
};

/// The homomorphic real-cepstrum method (plan Sec.1: floor, log NOT halved,
/// real cepstrum, fold, exponentiate), shared verbatim by L7-EQ's excess-phase
/// reconstruction (G24) and L7-FIR's minimum-phase taps (G10) -- W0-R1.
///
/// @param magnitudeFullGrid  |H| on a full nFft-point DFT grid (bin k at
///                           k * fs / nFft, for k = 0 .. nFft-1 -- the whole
///                           circle, not just DC..Nyquist). A caller with an
///                           N/2+1 half-grid mirrors it to nFft first
///                           (conjugate symmetry: magnitude is even, so it is
///                           literally a mirror, no conjugation needed);
///                           a caller with a time-domain FIR takes |FFT(h)|.
/// @param floorDb            magnitude floor in dB before taking the log --
///                           log(0) is undefined, and an unmeasured bin should
///                           read as "very quiet", not "silent".
/// @throws std::invalid_argument if magnitudeFullGrid.size() is not a power of
///         two, or is less than 4 (Fft's own minimum).
[[nodiscard]] MinimumPhaseResult
minimumPhaseFromMagnitude(std::span<const float> magnitudeFullGrid,
                          float floorDb = kMinPhaseFloorDb);

}  // namespace rta::dsp
