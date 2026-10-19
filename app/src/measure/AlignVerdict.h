// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// The wizard's vocabulary a MESSAGE-THREAD header may include: which source is
// the high-pass side, which step a run is in, and the verdict the fit
// produced. Split out of AlignmentWizard.h (ALIGN-UI lane, T2) because that
// header includes rta/ir/*, which no message-thread header may do (the H4
// isolation rule); `AlignRunner.h` and the pane need these types and nothing
// else of the wizard. Declarations moved verbatim -- nothing here changed.
#pragma once

#include "rta/dsp/CrossoverFit.h"

#include <cstddef>
#include <vector>

namespace rta::measure {

/// Every early return sets exactly one, so `lastRefusal()` always describes
/// THIS call and never a stale one. None of them is silent.
enum class WizardRefusal {
    None,
    MissingAnswer,        ///< arm() before all four questions were answered
    EngineNotQuiescent,   ///< OutputEngine::setSource said no; nothing was touched
    WindowNotOpen,        ///< a capture was offered before the ramp had passed
    WrongStep,            ///< this call has no meaning in the current step
    SampleRateMismatch,   ///< the two captures do not share one engine configuration
    FftSizeMismatch,
    ReferenceMismatch,    ///< ALIGN-R7: `channelRoles` string-compared, see below
    TraceNotUsable,       ///< a capture with no phase has no complex form to fit

    /// NOT A REFUSAL. The count, and the reason a new refusal cannot arrive
    /// untested: put a new enumerator ABOVE this line and `Count` moves, which
    /// makes test_alignment_wizard_refusals.cpp's `seen.size() ==
    /// kWizardRefusalCount` go red until the new one is driven and named.
    ///
    /// This replaces a compiler backstop that was claimed and does not exist:
    /// C4062 (an unhandled enumerator in a `switch` with no `default:`) is OFF
    /// by default on MSVC and `/W4` does not turn it on -- measured on 14.51 by
    /// PR #9's round-2 verifier, `-W4` silent, `-W4 -w14062` warning. A count
    /// the test compares needs no warning flag and behaves the same on all
    /// three CI operating systems.
    Count,
};

inline constexpr std::size_t kWizardRefusalCount = static_cast<std::size_t>(WizardRefusal::Count);

/// Question (a): which of the two captures is the high-pass side. There is no
/// default, because there is no measurement that could supply one -- 0 and
/// 180 degrees are symmetric under swapping which side is which, but +-90 is
/// not (record Sec.3, ALIGN-R14).
enum class Source { Main, Sub };

enum class WizardStep {
    Answering,       ///< at least one of the four questions is unanswered
    MeasuringHigh,   ///< the high-pass side is soloed and the window is opening
    MeasuringLow,    ///< the low-pass side is soloed
    AskingCycle,     ///< the fit ran; the cycle integer has no source and no answer
    AskingPolarity,  ///< two eligible signals disagree -- the UI shows both
    Computed,        ///< the fit ran and every outstanding question is answered
    MeasuringSum,    ///< both outputs routed; the MEASURED sum is being captured
    Done,
};

/// The comparison of the fitted intercept against the topology the operator
/// NAMED. No time-domain sign reaches this struct -- H8's invariance is what
/// holds that true, because a grep cannot scope itself to "the verdict path".
struct Verdict {
    double crossoverHz = 0.0;

    /// EVERY crossing the gated bins showed, ascending. A three-way system has
    /// two, and with no seed the wizard asks which one is being aligned rather
    /// than guessing (record Sec.2 computed-(1)).
    std::vector<double> crossingsHz;

    double tauSeconds = 0.0;

    /// The fit's local maxima of |S(tau)|, ranked by agreement, the winner
    /// (`tauSeconds`) first. ALIGN-UI T1: the cycle integer picks among these,
    /// and showing tau* alone would silently pick a cycle for the operator.
    std::vector<double> candidateTausSeconds;
    double interceptRadians = 0.0;
    double agreement = 0.0;
    double meanFrequencyHz = 0.0;

    double expectedRadians = 0.0;
    double deltaRadians = 0.0;  ///< wrapToPi(intercept - expected)

    /// Question (c)'s "unknown" branch: TWO candidate lines and the operator
    /// picks (record Sec.13.3). Not a third value, and nothing here chooses.
    bool expectedAmbiguous = false;
    double alternativeExpectedRadians = 0.0;
    double alternativeDeltaRadians = 0.0;

    /// False when `agreement` is below the caller's stated bar. It says "this
    /// band is not one delay plus one constant" -- the sentence a live show
    /// needs -- and it offers NO topology change, because a low R is evidence
    /// about the pair, not about the answer the operator gave.
    bool matchedPair = true;

    rta::dsp::CrossoverRefusal fitRefusal = rta::dsp::CrossoverRefusal::AllBinsAbsent;
};

}  // namespace rta::measure
