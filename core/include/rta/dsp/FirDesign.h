// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/Window.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace rta::dsp {

/// Linear phase keeps the whole filter length as latency ((N-1)/(2*fs));
/// minimum phase pays no fixed latency but does not correct the room's
/// measured phase (record docs/dsp/2026-09-06-l7-fir-export.md Sec.4). There
/// is no default: the caller states which trade it wants.
enum class FirPhase { Linear, Minimum };

/// LeastSquares is reserved vocabulary for the firls-shaped method the
/// record decides in principle (Sec.2) but does not build in v1 -- naming it
/// here now means the enum does not need an ABI-breaking extension later,
/// while designFir() throws if it is ever passed (nothing implements it yet).
enum class FirMethod { FrequencySampling, LeastSquares };

/// A magnitude-vs-frequency target as ascending breakpoints. designFir()
/// interpolates linearly in log10(f) and linearly in dB between them (record
/// Sec.6) -- the rule a banded correction curve (OctaveBands/BandWeights)
/// implies, and the one a closed-form test can pin exactly (record Sec.7.5).
struct FirTarget {
    std::vector<double> frequencyHz;   // strictly ascending, size >= 2
    std::vector<double> gainDb;        // same size as frequencyHz
};

struct FirResult {
    std::vector<float> taps;                  // as designed; normalisation is the writer's job
    double      sampleRate = 0.0;
    FirPhase    phase  = FirPhase::Linear;
    FirMethod   method = FirMethod::FrequencySampling;
    WindowType  window = WindowType::Hann;
    std::size_t groupDelaySamples = 0;        // (N-1)/2 for linear; 0 reported for minimum
    double      peakGainDb = 0.0;             // max 20*log10|H| over the design grid
    double      coefficientPeak = 0.0;        // max |taps[n]|

    /// Minimum phase only. A fraction of the reconstructed impulse response's
    /// energy that fell at or past sample N when truncating the (longer)
    /// cepstral reconstruction down to the requested tap count -- reported,
    /// not hidden (record Sec.4, Sec.7 item 3):
    ///
    ///   truncationLossDb = 10*log10( sum_{n>=N} h_min[n]^2 / sum_all h_min[n]^2 )
    ///
    /// This is a ratio of energies that is <= 1, so the value is <= 0 dB: 0 dB
    /// means all the energy landed inside the kept taps, and a more negative
    /// number means less was thrown away. It is NOT "how much was lost" read
    /// as a positive number -- the sign is the log of the lost FRACTION
    /// itself, and the golden pins the exact value per fixture (Shape-A
    /// regression lock, plan F2 T9).
    std::optional<double> truncationLossDb;

    std::size_t designFftSize = 0;             // M (freq sampling); nFft (minimum phase)
};

/// @throws std::invalid_argument if sampleRate <= 0; taps < 8 or taps > M/2
///         (M is the design FFT size, record Sec.10 -- the window would be
///         wider than the circularly shifted response is long); target.frequencyHz
///         is empty, not the same size as target.gainDb, or not strictly
///         ascending; or the requested method is not FrequencySampling (only
///         method implemented in v1).
[[nodiscard]] FirResult designFir(const FirTarget& target, double sampleRate,
                                  std::size_t taps, FirPhase phase,
                                  WindowType window = WindowType::Hann,
                                  FirMethod method = FirMethod::FrequencySampling);

/// D2 overload: the target as a per-bin magnitude half-grid (DC..Nyquist)
/// rather than breakpoints -- the shape an auto-EQ solver may hand over
/// directly instead of a sparse curve (record Sec.6 amendment, Sec.11).
///
/// @throws std::invalid_argument as above, plus: magnitudeHalfGrid.size() is
///         not (2^k + 1) for some k >= 3 (the size a power-of-two IDFT needs:
///         M = 2*(size-1) bins forward, M/2+1 = size back).
[[nodiscard]] FirResult designFir(std::span<const float> magnitudeHalfGrid, double sampleRate,
                                  std::size_t taps, FirPhase phase,
                                  WindowType window = WindowType::Hann,
                                  FirMethod method = FirMethod::FrequencySampling);

}  // namespace rta::dsp
