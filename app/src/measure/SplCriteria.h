// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// this file is compiled into rtatool_analysis_tests, which builds on CI even
// when RTA_BUILD_APP is OFF.
#pragma once

#include <array>
#include <string_view>

namespace rta::measure {

// THE THREE 140s ARE NOT THE SAME NUMBER, and record
// docs/dsp/2026-09-16-spl-pro-l6a.md section 7a calls this the most likely
// correctness bug in the whole feature. Three frameworks print "140" and:
//
//   * one of them is not a peak at all, it is an A-weighted LEVEL ceiling;
//   * one of them names NO weighting whatsoever;
//   * one of them applies only to impulsive or impact noise, not to
//     continuous sound.
//
// A single "140 dB peak" readout is therefore silently wrong in at least two
// of the three. This file keeps them as three separate rows, each carrying its
// own quantity, its own weighting-or-absence and its own event scope, so that
// collapsing any two of them is a change somebody has to make on purpose.
//
// WHAT IS NOT SAID HERE. An earlier draft of the record called the OSHA row
// "unweighted peak", which reads as a positive statement that Z weighting is
// required. The regulation says no such thing: it specifies NO weighting. "It
// specifies none" and "it specifies Z" are different facts and only the first
// one is in the CFR, so `weighting` below is an EMPTY string for that row and
// `weightingIsUnspecified` says so in as many words.

/// Which physical quantity a ceiling constrains. Not interchangeable: a peak
/// and a time-weighted level of the same signal differ by many decibels.
enum class CriterionQuantity {
    PeakSoundPressureLevel,  ///< L_peak: the instantaneous maximum
    TimeWeightedLevel,       ///< an exponentially-averaged level (F or S)
};

/// Which kinds of noise a ceiling applies to. Also not interchangeable: a
/// limit scoped to impulsive noise is not a ceiling on continuous sound.
enum class CriterionScope {
    ImpulsiveOrImpactOnly,
    AllNoise,
};

/// One published ceiling, with everything needed to print it honestly.
struct SplCriterion {
    std::string_view framework;   ///< who published it, and where
    double limitDb = 0.0;
    CriterionQuantity quantity = CriterionQuantity::PeakSoundPressureLevel;
    /// "C", "A", or EMPTY when the source names none. Never guessed.
    std::string_view weighting;
    bool weightingIsUnspecified = false;
    CriterionScope scope = CriterionScope::AllNoise;
    /// The label a readout must use for this quantity -- `L_Cpeak`, `L_AFmax`
    /// and so on. Never a bare "peak".
    std::string_view readoutLabel;
    std::string_view citation;
};

/// The three, in the order record section 7a's own table lists them.
inline constexpr std::array<SplCriterion, 3> kPeakAndCeilingCriteria{{
    {
        "OSHA",
        140.0,
        CriterionQuantity::PeakSoundPressureLevel,
        "",     ///< the CFR names NO weighting -- see the note above
        true,
        CriterionScope::ImpulsiveOrImpactOnly,
        "L_Zpeak",  ///< what this project can actually measure and must label
        "29 CFR 1910.95, Table G-16 note: exposure to impulsive or impact "
        "noise should not exceed 140 dB peak sound pressure level",
    },
    {
        "EU 2003/10/EC",
        140.0,
        CriterionQuantity::PeakSoundPressureLevel,
        "C",
        false,
        CriterionScope::AllNoise,
        "L_Cpeak",
        "Directive 2003/10/EC Art. 3: p_peak in its own column at each of "
        "three tiers -- 140 / 137 / 135 dB(C); Art. 2 defines p_peak as the "
        "maximum C-weighted instantaneous noise pressure",
    },
    {
        "NIOSH",
        140.0,
        CriterionQuantity::TimeWeightedLevel,
        "A",
        false,
        CriterionScope::AllNoise,
        "L_AFmax",
        "NIOSH 98-126 cl. 1.1.4 Ceiling Limit, printed p. 4, complete: "
        "exposure to continuous, varying, intermittent, or impulsive noise "
        "shall not exceed 140 dBA",
    },
}};

/// The EU's two lower tiers, which exist only in that framework and are the
/// reason its row cannot be folded into either of the others.
inline constexpr std::array<double, 3> kEuPeakTiersDb{140.0, 137.0, 135.0};

/// The one peak this project can report with a standards reference behind it.
/// IEC 61672-1 cl. 3.9 defines peak sound level for ANY weighting, but
/// ISO 1996-1 cl. 3.1.4 Note 3 records that IEC 61672-1 specifies the ACCURACY
/// of a peak detector only for C weighting (its cl. 5.13 and Table 5). So
/// L_Zpeak and L_Apeak are computable and unspecified, and L_Cpeak is the only
/// one with a reference. IEC 61252 Ed 2 cl. 4.18 independently makes
/// C-weighted peak a MANDATORY indication while leaving dose optional and
/// parametric, which is the clearest available statement that peak is a
/// criterion of its own and not part of a dose.
inline constexpr std::string_view kReportablePeakLabel = "L_Cpeak";

/// SAMPLED, and the readout says so. `Leq::peakDb` is 10*log10(max p^2), the
/// sampled maximum; the 4x-polyphase true-peak meter was deferred by the
/// weighting record and never came, so the inter-sample maximum of a
/// band-limited signal can EXCEED what is logged. Every readout and every
/// report cell that shows a peak uses this string, so the deferral is visible
/// to the operator rather than hidden in section 12 of a document.
inline constexpr std::string_view kSampledPeakLabel = "L_Cpeak (sampled)";

/// True when a label names its quantity. Guards against a bare "peak"
/// reaching a readout: a peak without its weighting is exactly the readout
/// that is silently wrong in two of the three frameworks above.
[[nodiscard]] constexpr bool labelNamesItsQuantity(std::string_view label) noexcept {
    return label == "L_Cpeak" || label == "L_Zpeak" || label == "L_Apeak" ||
           label == "L_AFmax" || label == "L_ASmax" || label == kSampledPeakLabel;
}

}  // namespace rta::measure
