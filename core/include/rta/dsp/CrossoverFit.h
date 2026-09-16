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

/// One competing delay, with its own agreement. The fit cannot resolve a delay
/// finer than the band's own width allows, so the competitors are RETURNED
/// rather than hidden behind the winner.
struct DelayCandidateTau {
    double tauSeconds = 0.0;
    double agreement = 0.0;
};

struct BandFit {
    /// + => B (the LP side) arrives LATER than A (the HP side); delay the HP
    /// side by this. Same sign as DelayEstimate::delaySamples.
    double tauSeconds = 0.0;

    /// phi_0 = arg(H_A) - arg(H_B) = arg(H_HP) - arg(H_LP) once tau* is
    /// removed, wrapped to (-pi, pi]. Compared directly against record Sec.3's
    /// expectedOffset for the topology the operator NAMED -- never against one
    /// derived from the measurement.
    double interceptRadians = 0.0;

    /// R in [0, 1] by the triangle inequality. 1 when the band really is "one
    /// delay plus one constant"; R collapsing is precisely how this reports
    /// "these two are not a matched pair" (probe 2026-09-15 Sec.8).
    double agreement = 0.0;

    /// The weight-weighted mean frequency. A residual delay dtau leaves exactly
    /// 2*pi*f_bar*dtau of phase in the intercept, which is what ties the two
    /// residuals into one statement.
    double meanFrequencyHz = 0.0;

    /// Local maxima of |S(tau)| in the searched range, ranked by agreement,
    /// the winner first.
    std::vector<DelayCandidateTau> cycleCandidates;

    CrossoverRefusal refusal = CrossoverRefusal::AllBinsAbsent;
};

struct BandFitOptions {
    double centreHz = 0.0;

    /// Record Sec.4's PROPOSED default. It must be measured across the Sec.3
    /// table before it ships as one; a constant baked into core/ is refused by
    /// the record, so it lives here as a caller-supplied option.
    double octavesEachSide = 1.0;

    double tauRangeSeconds = 0.020;  ///< bounded search, caller-supplied
    double tauGridSeconds = 1.0e-6;  ///< grid step, refined parabolically

    /// Zero means "evaluate at tau = 0 only" -- a caller who has already
    /// removed the delay and wants the intercept alone.
    int maxCycleCandidates = 3;
    double minimumGatedCoherence = 0.0;

    /// D_B - D_A from CaptureMeta::appliedDelaySamples. H_B is pre-rotated by
    /// e^{+j2 pi f (D_B - D_A)/fs} BEFORE the fit (record Sec.1.4, Sec.4).
    /// Core owns it (ALIGN-R2) so the "off by exactly 48/fs without it" case is
    /// a core test, and so the correction is impossible to FORGET: it is a
    /// required field of the options struct, not a step in a caller's recipe.
    double appliedDelayDifferenceSamples = 0.0;
    double sampleRate = 48000.0;
};

/// The complex-domain delay search. hA = the HIGH-PASS side, hB = the LOW-PASS
/// side (the contract above).
///
///     R_k = H_A,k conj(H_B,k) / (|H_A,k| |H_B,k|)
///     w_k = min(gamma^2_A,k, gamma^2_B,k) * |H_A,k| * |H_B,k|
///     tau* = argmax_tau |sum_k w_k R_k e^{-j2 pi f_k tau}|
///     phi_0 = arg(...) at tau*,   R = |...| / sum_k w_k
///
/// NO UNWRAP. A least-squares slope on unwrapped dphi is the textbook
/// alternative and is refused three times over: it needs the unwrap this
/// codebase has ruled out of the engine, a least-squares intercept of angles is
/// not an angle (+179 and -179 average to 0), and it produces no bounded
/// agreement figure.
///
/// The weight is the summation's own cross-term and carries no threshold:
/// |H_A + H_B|^2 has 2|H_A||H_B| as the coefficient of cos(phi_A - phi_B), the
/// only term relative phase can move. That is Smaart's "within about 10 dB"
/// with the 10 dB removed.
[[nodiscard]] BandFit crossoverBandFit(std::span<const std::complex<double>> hA,
                                       std::span<const std::complex<double>> hB,
                                       const std::optional<std::vector<float>>& coherenceA,
                                       const std::optional<std::vector<float>>& coherenceB,
                                       double binWidthHz, const BandFitOptions& options);

}  // namespace rta::dsp
