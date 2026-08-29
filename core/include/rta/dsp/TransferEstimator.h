// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. This file MUST NOT depend on JUCE, Qt, or any
// audio-device API. See docs/specs for the rationale.
#pragma once

#include "rta/dsp/DualFftEngine.h"

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

namespace rta::dsp {

/// Which estimator to divide the accumulators by. See
/// docs/dsp/2026-08-28-dual-fft.md §1 for why all three are kept rather than
/// shipping only the "right" one.
enum class Estimator {
    H1,  ///< Sxy/Sxx. Unbiased when the noise is on the MEASUREMENT channel --
         ///< the live-sound case: an electrical reference tap, and a mic in a
         ///< room full of uncorrelated energy.
    H2,  ///< Syy/conj(Sxy). Unbiased when the noise is on the REFERENCE.
    Hv,  ///< Total least squares; splits the difference between the two.
};

/// H1 = Sxy/Sxx. `{0,0}` when `sxx <= 0`: a reference bin with no energy
/// carries no information about the system, so a zero here reads honestly as
/// the magnitude floor rather than as a divide-by-near-zero spike.
[[nodiscard]] std::complex<double> estimateH1(std::complex<double> sxy, double sxx) noexcept;

/// H2 = Syy/conj(Sxy). `{0,0}` when `|sxy| == 0`, for the same reason as H1.
[[nodiscard]] std::complex<double> estimateH2(std::complex<double> sxy, double syy) noexcept;

/// Total-least-squares Hv, splitting the difference between H1 and H2 rather
/// than favouring a noise assumption on either channel:
///
///     Hv = ((Syy - Sxx) + sqrt((Syy - Sxx)^2 + 4|Sxy|^2)) / (2 * conj(Sxy))
///
/// The quantity under the root is real and non-negative by construction (it
/// is a sum of squares), so the square root itself never needs complex
/// arithmetic even though the surrounding expression does.
[[nodiscard]] std::complex<double> estimateHv(std::complex<double> sxy, double sxx,
                                              double syy) noexcept;

/// gamma^2 = |Sxy|^2 / (Sxx * Syy). MAGNITUDE-SQUARED, as Bendat & Piersol,
/// scipy and MATLAB define it. Clamped to [0, 1] -- rounding can produce
/// 1 + 1e-16, and a coherence above one is a nonsense the view would happily
/// draw -- and 0.0 whenever either denominator term is <= 0.
///
/// Open Sound Meter displays the UN-squared |Grm|/sqrt(Grr*Gmm), so our
/// numbers read lower than theirs on identical data. That is a different
/// quantity, not a bug, and must not be "corrected" -- see decision record §2.
/// The squared form is also the only one for which H1/H2 == gamma^2 holds,
/// which is the free self-test in test_transfer_estimator.cpp.
[[nodiscard]] double magnitudeSquaredCoherence(std::complex<double> sxy, double sxx,
                                               double syy) noexcept;

/// An immutable result. Absence of coherence is absence of a value -- not
/// 0.0, not -1. A sentinel would be plotted.
struct TransferSnapshot {
    Estimator estimator = Estimator::H1;
    double sampleRate = 0.0;
    double binWidthHz = 0.0;
    double effectiveAverages = 0.0;
    std::vector<std::complex<double>> h;
    std::vector<float> magnitudeDb;    ///< 20*log10|H|, floored at kMagnitudeFloorDb
    std::vector<float> phaseRadians;   ///< wrapped to (-pi, pi]; unwrapping is a VIEW job
    std::optional<std::vector<float>> coherence;  ///< empty below the gate

    static constexpr float kMagnitudeFloorDb = -120.0f;
};

/// The ONE place a TransferSnapshot is built, and therefore the one place the
/// coherence gate (decision record §3) can be applied. Guarded by
/// check_coherence_gate.cmake (task 7): nothing else may write the coherence
/// field.
[[nodiscard]] TransferSnapshot makeSnapshot(const DualFftEngine& engine, Estimator estimator);

}  // namespace rta::dsp
