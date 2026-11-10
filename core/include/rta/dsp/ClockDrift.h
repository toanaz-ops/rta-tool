// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace rta::dsp {

/// Options for `fractionalDelayByPhaseSlope`.
struct PhaseSlopeOptions {
    double sampleRate = 48000.0;
    double minHz = 0.0;
    double maxHz = 0.0;            ///< 0 = fs / 4
    double minCoherence = 0.5;     ///< bins below this gamma^2 are left out of the fit
    std::size_t minBins = 8;       ///< fewer usable bins -> no answer
};

struct FractionalDelay {
    /// Total delay, samples. POSITIVE = the measurement lags the reference --
    /// the sign `DelayFinder.h` (`DelayEstimate::delaySamples`) and
    /// `DualFftEngine::Config::referenceDelaySamples` use, so it composes with
    /// both without a negation.
    double delaySamples = 0.0;
    /// The fit's intercept c: the time-invariant part of the system's phase
    /// (and any constant phase). It is NOT a delay and never enters
    /// `delaySamples`.
    double interceptRadians = 0.0;
    std::size_t binsUsed = 0;
};

/// Sub-sample delay by weighted least squares on the H1 phase after integer
/// alignment (record docs/dsp/2026-11-10-g22b-two-file-wav-drift.md section 2;
/// no interpolation bias, unlike a 3-point parabola on a GCC-PHAT peak).
///
/// The measurement is first advanced by `integerDelay` samples by
/// `DualFftEngine` (fftSize 4096, hop 1024, Hann, Cumulative), whose H1 phase
/// is then -2 pi f tau + c with tau the RESIDUAL delay. Over bins with
/// gamma^2 >= minCoherence and minHz <= f <= maxHz (default fs/4) a weighted
/// line fit phi = a f + c gives tau = -a / (2 pi). Weights gamma^2 / (1 -
/// gamma^2): the variance of a cross-spectrum phase estimate is proportional
/// to (1 - gamma^2) / (2 gamma^2 Neff), so this is the inverse variance.
///
/// Validity: the phase is wrapped, so it is unwrapped along frequency over the
/// selected bins first. That is unambiguous only while |tau| stays within about
/// one sample (an integer lag from GCC-PHAT leaves |tau| <= 0.5); a fit whose
/// residual exceeds one sample returns nullopt rather than a number the unwrap
/// may have got wrong.
///
/// Coherence comes only from `makeSnapshot` (the coherence gate), so the window
/// must be long enough to reach 8 effective averages: at 4096/1024 a 2^15-sample
/// window gives 29 frames (Neff ~ 10.7); a 16384 FFT would give 5 and be
/// withheld.
///
/// @return nullopt when coherence is withheld, fewer than `minBins` bins
///         qualify, or |residual| > 1 sample.
/// @throws std::invalid_argument for mismatched lengths, a non-positive rate,
///         or maxHz > 0 below minHz.
[[nodiscard]] std::optional<FractionalDelay> fractionalDelayByPhaseSlope(
    std::span<const float> reference, std::span<const float> measurement,
    std::ptrdiff_t integerDelay, const PhaseSlopeOptions& options);

/// One delay measurement: where (reference-timeline sample position of the
/// window centre) and what (total delay, samples).
struct DelayObservation {
    double positionSamples;
    double delaySamples;
};

/// delay = intercept + slope * position, with the residuals about that line.
struct DriftFit {
    double intercept;
    double slope;
    double residualRms;
    double maxAbsResidual;
    std::size_t used;
};

/// Theil-Sen line (Theil 1950; Sen 1968): slope = median of all pairwise
/// slopes, intercept = median of (delay - slope * position). Breakdown point
/// ~29 % (a pair is contaminated if EITHER end is an outlier, so 29 % bad
/// points spoil 50 % of the pairs), which is why it fits a clock line through
/// windows that a reverberant or silent stretch has corrupted, where least
/// squares would follow the outlier.
/// @return nullopt for fewer than 2 points or all positions equal.
[[nodiscard]] std::optional<DriftFit> fitDriftTheilSen(std::span<const DelayObservation> points);

}  // namespace rta::dsp
