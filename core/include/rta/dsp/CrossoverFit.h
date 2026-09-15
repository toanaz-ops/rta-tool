// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <complex>
#include <optional>
#include <span>
#include <vector>

namespace rta::dsp {

/// Why no number is returned. There is no sentinel value and no silent
/// fallback: an answer that could not be computed is reported as an absence
/// with a reason, never as a default the caller cannot tell from a
/// measurement (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
enum class CrossoverRefusal {
    None,
    AllBinsAbsent,  ///< either coherence was std::nullopt, or no bin passed the gate
    NoCrossing,     ///< the gated bins never change the sign of |H_A| - |H_B|
    TooFewBins,     ///< fewer gated bins than the estimate needs
    RangeEmpty,     ///< the requested window contains no bin at all
};

struct SpectralCrossover {
    /// The selected crossing, interpolated between two adjacent gated bins by
    /// the same two-point form core/src/ir/Polarity.cpp:39-48 uses. Taking the
    /// bin index itself would charge up to a full bin of quantisation to a
    /// figure the operator reads in hertz.
    double frequencyHz = 0.0;

    /// EVERY crossing, ascending. A three-way system has two, and the wizard
    /// asks which one is being aligned rather than guessing -- so the list is
    /// the honest return value and `frequencyHz` is only the seed's pick.
    std::vector<double> allCrossingsHz;

    CrossoverRefusal refusal = CrossoverRefusal::AllBinsAbsent;
};

/// ARGUMENT CONTRACT, load-bearing (ALIGN-R14): **A is the HIGH-PASS side, B is
/// the LOW-PASS side**, for this function and for crossoverBandFit. It is what
/// makes record Sec.3's arg(H_HP) - arg(H_LP) and record Sec.4's
/// R_k = H_A conj(H_B) the same quantity. Transposing the two silently flips
/// both tau's sign and phi_0's comparison against the Sec.3 table.
///
/// Where the two magnitudes cross, on the bins both sides were measured on.
///
/// A bin takes part only when BOTH coherences are present and both are at or
/// above `minimumGatedCoherence`, and a crossing is only ever read between two
/// ADJACENT gated bins: reading one across an ungated gap would report a
/// crossover frequency out of a stretch nobody measured.
///
/// `seedHz` selects among the crossings by nearest LOG distance -- frequency is
/// a log quantity, and a linear "nearest" hands a seed sitting midway between
/// 100 Hz and 2 kHz to the 2 kHz side every time. With no seed the first
/// crossing is returned, with the full list beside it.
[[nodiscard]] SpectralCrossover spectralCrossover(std::span<const float> magnitudeDbA,
                                                  std::span<const float> magnitudeDbB,
                                                  const std::optional<std::vector<float>>& coherenceA,
                                                  const std::optional<std::vector<float>>& coherenceB,
                                                  double binWidthHz, double minimumGatedCoherence,
                                                  std::optional<double> seedHz);

}  // namespace rta::dsp
