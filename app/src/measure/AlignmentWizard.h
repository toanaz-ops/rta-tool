// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// L7-ALIGN task H (docs/plans/2026-09-15-L7-align-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.2, Sec.7, Sec.9).
//
// THE ONE THING THIS FILE MAY NOT DO. It ASKS the topology and it ASKS
// whether the processor already inverts one output. It never derives either
// from a measurement, and "maximise the measured sum" / "minimise the band
// ripple" IS that derivation wearing a different hat (record Sec.6, owner
// ruling 2026-08-30). It also never reads a topology sign off ANY correlation
// peak, whitened or not: PR #3 measured two correlators disagreeing about
// polarity at two of six orders on ONE unchanged pair of loudspeakers
// (docs/research/2026-09-15-l7-align-order4-probe.md Sec.5, Sec.8), so an
// even-order sign discrepancy is evidence about the SYSTEM and never about a
// convention. The estimator is Sec.4's complex band fit, and its bounded `R`
// collapsing is precisely how it reports "these two are not a matched pair".
//
// Device-free over rta::platform::OutputEngine, the way DelayLocator and
// EqVerify already are, so the whole sequence is provable in ctest with no
// sound card.
#pragma once

#include "measure/CrossoverTopology.h"
#include "measure/OutputPolicy.h"
#include "trace/Trace.h"
#include "trace/VirtualTrace.h"

#include "rta/dsp/CrossoverFit.h"
#include "rta/ir/Deconvolver.h"
#include "rta/ir/Polarity.h"
#include "rta/ir/RelativePolarity.h"
#include "rta/platform/OutputEngine.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

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
};

/// What a polarity signal is allowed to be.
///
/// `Authoritative` exists and is NEVER produced here. That is deliberate: an
/// enum with only Advisory and Refused would make "no time-domain sign is
/// authoritative across a crossover" unfalsifiable, and a future session that
/// promotes rho or the whitened peak would have to reach for this value --
/// where test_alignment_wizard_verdict.cpp is waiting for it.
enum class SignalStanding { Authoritative, Advisory, Refused };

/// The one sentence the UI greys a time-domain sign with, across a crossover.
/// A literal the test asserts by value: a reason a reader cannot find in the
/// source is a reason nobody can check (record Sec.7 table row 4).
[[nodiscard]] inline const char* acrossCrossoverReason() noexcept {
    return "sign undefined across a crossover";
}

enum class PolaritySignalKind {
    FindPolarityHighSide,
    FindPolarityLowSide,
    RelativePolarityRho,
    WhitenedCorrelationPeak,
};

struct PolaritySignal {
    PolaritySignalKind kind = PolaritySignalKind::RelativePolarityRho;
    SignalStanding standing = SignalStanding::Advisory;
    rta::ir::Sign sign = rta::ir::Sign::Unknown;
    rta::ir::Refusal refusal = rta::ir::Refusal::None;

    /// rho for the relative reading, confidenceDb for findPolarity. Shown, not
    /// gated on: record Sec.8 forbids a threshold until two independent grids
    /// agree, and they did not (Wave 3a task F).
    double figure = 0.0;

    /// The literal sentence the UI greys the signal with.
    std::string reason;
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

class AlignmentWizard {
public:
    struct Config {
        /// Named by SOURCE, not by role. Which of the two is the high-pass
        /// side is question (a)'s answer, and that answer is what decides both
        /// the solo order and which capture the fit receives as `hA`
        /// (ALIGN-R14). Naming the channels by role here would have made the
        /// answer decorative.
        int mainOutputChannel = 0;
        int subOutputChannel = 1;
        double sampleRate = 48000.0;

        /// Caller-supplied in full. Record Sec.4 and Sec.12 refuse a window
        /// and a tau grid baked in as constants until they are measured across
        /// the Sec.3 table, so this lane ships them as an option with no
        /// opinion of its own.
        rta::dsp::BandFitOptions fit{};

        /// The bar below which `R` is reported as "not a matched pair". A
        /// caller's number, labelled as one: nothing in this lane measured it.
        double matchedPairAgreement = 0.5;

        double minimumGatedCoherence = 0.0;
    };

    AlignmentWizard(rta::platform::OutputEngine& engine, rta::platform::SourceVariant source,
                    Config config);

    // ---- the four asked questions, and the only writers of those fields ----
    void answerHighPassSide(Source side);
    void answerTopology(Topology topology);
    void answerInversion(ProcessorInversion inversion);
    void answerCrossoverSeed(std::optional<double> seedHz);
    /// Question (d)'s other half, and the cycle integer when no IR pair exists.
    void answerCycleOffsetSamples(std::optional<std::ptrdiff_t> samples);

    [[nodiscard]] std::optional<Source> highPassSide() const noexcept { return highPassSide_; }
    [[nodiscard]] std::optional<Topology> topology() const noexcept { return topology_; }
    [[nodiscard]] std::optional<ProcessorInversion> inversionAnswer() const noexcept {
        return inversion_;
    }
    [[nodiscard]] std::optional<double> crossoverSeedHz() const noexcept { return seedHz_; }

    // ---- witnesses the wizard is GIVEN, never ones it computes -------------
    void supplyImpulseResponses(rta::ir::Deconvolution highSide, rta::ir::Deconvolution lowSide);
    /// The sign of a PHAT-whitened correlation peak, if the caller has one.
    /// The wizard does not compute it: PHAT on a band-limited source relocates
    /// the ambiguity rather than resolving it (record Sec.4), and PR #3 Sec.5
    /// measured it reading the mirror of the un-whitened rule at order 8.
    void supplyWhitenedPeakSign(rta::ir::Sign sign);

    // ---- the L7-OUT Sec.6 sequence ----------------------------------------
    /// Answering -> MeasuringHigh: setSource, STRICT solo of the high-pass
    /// side (ALIGN-R6, owner decision 4 -- a SEQUENCE is strict), armSource.
    void arm();

    /// Opens the capture window once the excitation has rendered past the
    /// 10 ms that are not stationary, counted from the moment THIS step's solo
    /// was applied (L7-OUT Sec.11). Each solo re-ramps a different output
    /// gate, so each step waits again.
    void poll();
    [[nodiscard]] bool captureWindowOpen() const noexcept { return windowOpen_; }

    void submitCapture(rta::trace::Trace trace);

    /// Both outputs routed -- one signal on two outputs, which is what L7-OUT
    /// Sec.11 promises -- for the MEASURED sum.
    void beginMeasuredSum();
    void submitMeasuredSum(rta::trace::Trace trace);

    // ---- what came out ----------------------------------------------------
    [[nodiscard]] WizardStep step() const noexcept { return step_; }
    [[nodiscard]] WizardRefusal lastRefusal() const noexcept { return refusal_; }
    [[nodiscard]] const std::optional<Verdict>& verdict() const noexcept { return verdict_; }
    [[nodiscard]] const std::vector<PolaritySignal>& polaritySignals() const noexcept {
        return signals_;
    }
    [[nodiscard]] std::optional<std::ptrdiff_t> cycleOffsetSamples() const noexcept {
        return cycleOffset_;
    }
    /// The options the fit was actually handed, so a test can assert the
    /// appliedDelaySamples reconciliation was passed once and correctly
    /// (ALIGN-R2, H5) instead of trusting that it was.
    [[nodiscard]] const rta::dsp::BandFitOptions& lastFitOptions() const noexcept {
        return lastFit_;
    }
    /// Kept by SOURCE. Which one lands in the fit's `hA` follows from question
    /// (a), so flipping that answer really does transpose the fit rather than
    /// relabelling it.
    [[nodiscard]] const std::optional<rta::trace::Trace>& mainCapture() const noexcept {
        return mainCapture_;
    }
    [[nodiscard]] const std::optional<rta::trace::Trace>& subCapture() const noexcept {
        return subCapture_;
    }
    [[nodiscard]] const std::optional<rta::trace::Trace>& measuredSum() const noexcept {
        return measuredSum_;
    }

private:
    void soloStep(int channel);
    void compute();
    void buildPolaritySignals();
    [[nodiscard]] int highSideChannel() const noexcept;
    [[nodiscard]] int lowSideChannel() const noexcept;

    rta::platform::OutputEngine& engine_;
    rta::platform::SourceVariant source_;
    Config config_;

    std::optional<Source> highPassSide_;
    std::optional<Topology> topology_;
    std::optional<ProcessorInversion> inversion_;
    std::optional<double> seedHz_;
    /// Question (d) may legitimately be answered "no seed", which is
    /// `nullopt`. That is indistinguishable from never having been asked
    /// unless the ASKING is recorded separately -- so it is.
    bool seedAnswered_ = false;
    std::optional<std::ptrdiff_t> cycleAnswer_;

    std::optional<rta::ir::Deconvolution> irHigh_;
    std::optional<rta::ir::Deconvolution> irLow_;
    std::optional<rta::ir::Sign> whitenedSign_;

    std::optional<rta::trace::Trace> mainCapture_;
    std::optional<rta::trace::Trace> subCapture_;
    std::optional<rta::trace::Trace> measuredSum_;

    WizardStep step_ = WizardStep::Answering;
    WizardRefusal refusal_ = WizardRefusal::None;
    bool windowOpen_ = false;
    std::uint64_t stepStartSamples_ = 0;

    std::optional<Verdict> verdict_;
    std::vector<PolaritySignal> signals_;
    std::optional<std::ptrdiff_t> cycleOffset_;
    rta::dsp::BandFitOptions lastFit_{};
};

}  // namespace rta::measure
