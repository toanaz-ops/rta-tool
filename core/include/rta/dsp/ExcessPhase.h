// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rta::dsp {

/// The G24 excess-phase kernel's OWN oversampling factor -- NOT
/// FirDesign.cpp's kCepstralOversamplingFactor (8), and not reused from it.
///
/// MEASURED by tools/gen_autoeq.py's sweep (plan
/// docs/plans/2026-09-07-L7-eq-impl-plan.md Task A), against the analytic,
/// UNWINDOWED two-path comb H(theta) = 1 + a*e^{-j theta D} -- the sharpest
/// notch this kernel will ever be asked to resolve, because it feeds RAW
/// measured magnitude, never a windowed target curve the way FIR's caller
/// does (FirDesign.cpp:16-36's own load-bearing comment). Convergence was
/// judged on two quantities the magnitude-identity residual cannot see (that
/// residual is an algebraic tautology, flat at every factor --
/// gen_fir_algo.sweep_oversampling_factor's own finding): the reconstructed
/// excess-phase SWING (within 0.5 deg of the F=256 reference) and the
/// min-phase impulse's TAIL ENERGY past nFft/2 (below -200 dB absolute, 80 dB
/// under kMinPhaseFloorDb -- see gen_autoeq_algo.py's TAIL_CONVERGE_FLOOR_DB
/// for why a reference-relative rule fails once double-precision FFT
/// round-trip noise dominates). The measured minimum across nine (a, D)
/// fixtures was 64x; this constant ships one step of margin above it (the
/// same margin policy FIR's own factor takes), 128x -- SIXTEEN TIMES FIR's
/// 8x, which is exactly the caveat this task existed to either prove or
/// refute (docs/dsp/2026-09-06-l7-auto-eq.md's load-bearing hazard). Do not
/// lower this without re-running the sweep; do not reuse it for a windowed
/// target, where FIR's own, smaller factor already applies.
inline constexpr std::size_t kExcessPhaseOversamplingFactor = 128;

/// The G24 excess-phase reconstruction, shared with L4c's display feature
/// (EQ-R1: placed in rta::dsp so a display-only caller does not have to link
/// rta::eq). Per-bin results over the SAME grid magnitudeHalfGrid/hHalfGrid
/// arrive on; the verdict (boostable or not) is rta::eq::classifyDip's job,
/// not this file's -- this file only reconstructs the phase two hypotheses
/// disagree on (record Sec.4.2).
struct ExcessPhaseResult {
    std::vector<float> excessPhaseRad;     ///< phi_x per bin, broadband delay removed
    std::vector<double> excessGroupDelay;  ///< tau_x per bin, seconds, on h/h_min (|.|==1)
    double broadbandDelaySec = 0.0;        ///< tau_0, the trusted mean removed from phi_x
    std::size_t oversampleFactor = 0;      ///< the factor actually used
    bool valid = false;                    ///< false if too few trusted bins to trust anything
};

/// Reconstructs the excess phase phi_x = arg(h) - arg(h_min) of a measured
/// transfer function, per record Sec.4.3:
///
///   1. Fill: an untrusted bin takes the magnitude of its NEAREST trusted
///      bin (a flat hold introduces no new zeros into the spectrum).
///   2. Interpolate the filled ln|H| onto the oversampled grid
///      (oversampleFactor * nFft points), LINEAR IN log10(f), LINEAR IN dB
///      -- the same rule FirDesign.cpp's interpolateFirTargetDb uses for its
///      breakpoint target, applied here to a dense measured curve instead.
///      This is an EXTRA error source beyond cepstral aliasing (the analytic
///      sweep that chose kExcessPhaseOversamplingFactor has none, since it
///      evaluates the comb exactly at every factor); gen_autoeq.py's golden
///      reports it separately (plan Task A, "interpolation error ... flagged
///      as a separate quantity for the orchestrator").
///   3. Mirror the interpolated half-grid to a full nFft*oversampleFactor
///      circle and reconstruct with minimumPhaseFromMagnitude (the SAME
///      kernel G10's FIR export uses -- W0-R1 / EQ-R1's shared-kernel
///      decision, never a second implementation).
///   4. Read h_min back at the ORIGINAL bin frequencies -- every
///      oversampleFactor-th bin of the oversampled reconstruction lands
///      exactly on an original bin, so this is an exact lookup, not a second
///      interpolation.
///   5. phi_x_raw = arg(h / h_min) per original bin (a single complex
///      division then one arg() call -- never angle subtraction followed by
///      manual unwrap bookkeeping, which risks a spurious 2*pi jump the
///      complex-domain route cannot produce).
///   6. tau_0 = MEAN (over TRUSTED bins) of the group delay of h/h_min
///      (groupDelaySeconds -- its |.|==1 denominator is exactly conditioned,
///      GroupDelay.h's own doc comment). MEASURED DEVIATION from record
///      Sec.4.3.4, which specifies the median: on the record's own two-path
///      fixture (a=2, D=144 samples, 48 kHz) the median read 57.5 SAMPLES
///      off the true delay, while the mean read 0.28 samples off. The
///      allpass term's own group delay is not symmetric about its period
///      average -- its excursion concentrates near each notch, so the
///      MEDIAN sits near the flat part of the period, not at the "zero
///      MEAN group delay per period" value the record's own reasoning
///      names; the MEAN is what actually recovers that value. See
///      ExcessPhase.cpp's meanOfTrusted for the full measurement.
///   7. phi_x = arg((h / h_min) * exp(i * omega * tau_0)) -- the delay is
///      removed by ONE further complex multiply before the final arg(), for
///      the same wrap-safety reason as step 5.
///
/// @param magnitudeHalfGrid |H| on the fixed-engine's linear half-grid, DC to
///                          Nyquist inclusive: size() must be 2^k + 1 (the
///                          same contract FirDesign's magnitudeHalfGrid
///                          overload uses), so nFft = 2*(size()-1) is a power
///                          of two minimumPhaseFromMagnitude can consume.
/// @param hHalfGrid         complex H on the identical grid (TransferSnapshot
///                          convention: phase already delay-compensated
///                          before the FFT, per L2 Sec.4).
/// @param trusted            same length; non-zero marks a bin trusted (L5b's
///                           gate, consumed here as a mask -- this function
///                           owns no threshold).
/// @param sampleRate         > 0.
/// @param oversampleFactor   the factor to use, normally
///                           kExcessPhaseOversamplingFactor; a power of two,
///                           taken as a parameter (not hard-coded) so
///                           gen_autoeq.py's golden and the C++ kernel can be
///                           driven at the SAME swept factors from one call
///                           site in the tests.
/// @throws std::invalid_argument on a size mismatch between the three input
///         spans, an input size that is not 2^k + 1 (k >= 2), a
///         non-positive sampleRate, or an oversampleFactor that is zero or
///         not a power of two.
///
/// valid is false, and every output left at its default (empty vectors,
/// zero delay), whenever fewer than kMinTrustedBins bins are trusted --
/// never a curve built from too little to mean anything
/// (memory/a-fixed-defect-returns-through-the-silent-fallback.md).
[[nodiscard]] ExcessPhaseResult
excessPhase(std::span<const float> magnitudeHalfGrid,
            std::span<const std::complex<double>> hHalfGrid,
            std::span<const std::uint8_t> trusted,
            double sampleRate,
            std::size_t oversampleFactor = kExcessPhaseOversamplingFactor);

}  // namespace rta::dsp
