// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cmath>
#include <optional>

namespace rta::meter {

/// One dose accumulator's four settings. All four are DATA: no preset name
/// appears in this file, and no exchange rate is a boolean.
///
/// IEC 61252:2025 cl. 3.9 Formula (7) is the case `q = 10`; its cl. 3.10
/// Formula (8), `10^((L - L_c)*lg2/Q)`, is the case `q = Q/log10(2)` for a
/// declared exchange rate `Q` dB. The allowed-time form
/// `D = 100*sum(C_i/T_i)` with `T_i = T_c*2^((L_c - L_i)/Q)` is algebraically
/// the same thing; this accumulates the energy form because it reads directly
/// off record 3's block energies.
struct DoseSettings {
    double criterionLevelDb = 85.0;      ///< L_c
    double criterionSeconds = 28800.0;   ///< T_c; 8 h
    double q = 10.0;                     ///< the base-10 DENOMINATOR (see below)
    double thresholdDb = 80.0;           ///< levels strictly below this add EXACTLY 0
};

/// The base-10 denominator for a declared exchange rate `Q` in decibels:
/// `q = Q/log10(2)`.
///
/// ALWAYS CALL THIS; never type the decimal. `3/log10(2)` is
/// 9.965784284662087 and the readable form `9.9657843` is 1.53e-08 above it;
/// `5/log10(2)` is 16.609640474436812 against a readable 16.6096404, 7.44e-08
/// below. No numeric acceptance in this lane can distinguish either pair --
/// the literal clears every dose bound by five to eight orders of margin --
/// so the reason for computing it is NOT that the literal is inaccurate. It is
/// that `10^(Q/q)` is then EXACTLY 2.0 bitwise, which makes the one-exchange
/// identities exact comparisons instead of tolerances, and that a derived
/// constant documents where it came from. (SPL-R7, whose original
/// justification was refuted and replaced by exactly this.)
[[nodiscard]] inline double exchangeDenominator(double exchangeRateDb) noexcept {
    return exchangeRateDb / std::log10(2.0);
}

/// Record 7's one formula, accumulated over record 2's blocks:
///
///     D% = (100 / T_c) * sum_i  t_i * 10^((L_i - L_c)/q)
///          terms with L_i < L_threshold contribute EXACTLY 0
///
/// TWO of these run at once in a session, each with its own four settings.
/// That is not a convenience: the Larson Davis 831/LxT defines
/// `NUM_SLM_DOSES = 2` with a per-dose {threshold, exchange rate, criterion
/// time, criterion level}, and it is what makes two side-by-side exposure
/// columns in one log possible at all. A single setting forces a wrong
/// threshold onto whichever convention loses.
///
/// PEAK NEVER ENTERS THIS CLASS. Record 7a: the logged peak is `L_Cpeak`, it
/// is compared OUTSIDE the dose integral, and the three "140" ceilings are
/// three different measurements of three different quantities -- a dose that
/// swallowed a peak could not tell them apart. There is deliberately no
/// member, parameter or overload here carrying one, and a test greps for it.
class Dose {
public:
    explicit Dose(DoseSettings settings) noexcept : settings_(settings) {}

    /// Back to no accumulated dose and no elapsed time; the settings stay.
    void reset() noexcept;

    /// One block's contribution. `blockLeqDb` is the block's Leq in the same
    /// dB reference as `criterionLevelDb`.
    void addBlock(double blockLeqDb, double blockSeconds) noexcept;

    /// D, in per cent. 100 % is the criterion exactly.
    [[nodiscard]] double percent() const noexcept;

    /// `D_now * (T_c / T_elapsed)`.
    ///
    /// A VENDOR CONVENTION, and it is labelled one. Station 1 read the whole
    /// of IEC 61252 Ed 2 cl. 3 and the whole of ANSI S1.25-1991's definitions
    /// and sections 1, 2, 4-7: the word "projected" appears in NEITHER. Three
    /// independent vendors define it identically and none cites a clause. It
    /// ships because operators ask for it, and the word "projected" is not
    /// optional in any label that shows it.
    ///
    /// ABSENT when no time has elapsed: there is nothing to project, and a
    /// 0.0 would read as "projected to zero"
    /// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
    [[nodiscard]] std::optional<double> projectedPercent() const noexcept;

    /// `q*log10(D/100) + L_c`. NIOSH 98-126 cl. 1.1.3's own conversion with
    /// `q = 10`; OSHA App. A I(2)'s `16.61 log10(D/100) + 90` with
    /// `q = 5/log10(2)` and `L_c = 90`.
    ///
    /// ABSENT at zero dose, where the logarithm is -inf. NIOSH's own Table 1-2
    /// starts at 20 %.
    [[nodiscard]] std::optional<double> twaDb() const noexcept;

    /// Time spent strictly below `thresholdDb`. REPORTED, and never folded
    /// into the dose: record 7's threshold term contributes exactly zero, and
    /// the time it represents is a separate fact the report prints separately.
    [[nodiscard]] double secondsBelowThreshold() const noexcept { return secondsBelow_; }

    /// Every second added, contributing or not. This is `T_elapsed` in the
    /// projection.
    [[nodiscard]] double elapsedSeconds() const noexcept { return secondsTotal_; }

    [[nodiscard]] const DoseSettings& settings() const noexcept { return settings_; }

private:
    DoseSettings settings_;
    double accumulated_ = 0.0;  ///< sum t_i * 10^((L_i - L_c)/q), in seconds
    double secondsBelow_ = 0.0;
    double secondsTotal_ = 0.0;
};

/// `L_EX,8h = L_Aeq,T + 10*log10(T / 8h)`. IEC 61252 Ed 2 Formula (5), whose
/// Note 6 says Formula (2) is identical to Directive 2003/10/EC's daily noise
/// exposure level.
///
/// IT DOES NOT TAKE `q`, AND MUST NOT. This is an ENERGY average: the 10 is
/// the definition of the quantity, not a choice of exchange rate. Displayed
/// beside a 5 dB-exchange dose it will not agree, and that is a property of
/// two definitions rather than a bug. A structural test asserts there is no
/// overload taking one.
[[nodiscard]] double exposureLevelDb(double leqDb, double seconds) noexcept;

}  // namespace rta::meter
