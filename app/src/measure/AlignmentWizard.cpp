// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/AlignmentWizard.h"

#include <cmath>
#include <numbers>
#include <utility>

namespace rta::measure {
namespace {

constexpr double kPi = std::numbers::pi;

/// Output-path record Sec.11, the settle DelayLocator.cpp and EqVerify.cpp
/// already wait out: the first 10 ms of an excitation are not stationary.
constexpr double kSettleSamplesAt48k = 480.0;
constexpr double kSettleReferenceRate = 48000.0;


double wrapToPiHalfOpen(double radians) noexcept {
    double wrapped = std::remainder(radians, 2.0 * kPi);
    if (wrapped <= -kPi) wrapped += 2.0 * kPi;
    return wrapped;
}

std::optional<std::vector<float>> coherenceOf(const rta::trace::Trace& trace) {
    const auto field = trace.field(rta::trace::Field::Coherence);
    if (field.empty()) return std::nullopt;
    return std::vector<float>(field.begin(), field.end());
}

rta::ir::Sign productOf(rta::ir::Sign a, rta::ir::Sign b) noexcept {
    if (a == rta::ir::Sign::Unknown || b == rta::ir::Sign::Unknown) return rta::ir::Sign::Unknown;
    return (a == b) ? rta::ir::Sign::Positive : rta::ir::Sign::Negative;
}

}  // namespace

AlignmentWizard::AlignmentWizard(rta::platform::OutputEngine& engine,
                                 rta::platform::SourceVariant source, Config config)
    : engine_(engine), source_(std::move(source)), config_(config) {}

// --- the four asked questions. THE ONLY WRITERS of these four fields. -------
// H1 is a structural scan over this file asserting exactly that: nothing in
// compute(), in buildPolaritySignals() or anywhere on the capture path may
// assign to them. A wizard that set the inversion answer from the sign of its
// own fitted intercept would have made the forbidden move, and that mutation
// is what the scan is there to catch.

void AlignmentWizard::answerHighPassSide(Source side) { highPassSide_ = side; }

void AlignmentWizard::answerTopology(Topology answer) { topology_ = answer; }

void AlignmentWizard::answerInversion(ProcessorInversion answer) { inversion_ = answer; }

void AlignmentWizard::answerCrossoverSeed(std::optional<double> hz) {
    seedHz_ = hz;
    seedAnswered_ = true;
}

void AlignmentWizard::answerCycleOffsetSamples(std::optional<std::ptrdiff_t> samples) {
    cycleAnswer_ = samples;
    if (samples.has_value() && step_ == WizardStep::AskingCycle) {
        cycleOffset_ = samples;
        step_ = WizardStep::Computed;
    }
}

void AlignmentWizard::supplyImpulseResponses(rta::ir::Deconvolution highSide,
                                             rta::ir::Deconvolution lowSide) {
    irHigh_ = std::move(highSide);
    irLow_ = std::move(lowSide);
}

void AlignmentWizard::supplyWhitenedPeakSign(rta::ir::Sign sign) { whitenedSign_ = sign; }

int AlignmentWizard::highSideChannel() const noexcept {
    return (highPassSide_ == Source::Sub) ? config_.subOutputChannel : config_.mainOutputChannel;
}

int AlignmentWizard::lowSideChannel() const noexcept {
    return (highPassSide_ == Source::Sub) ? config_.mainOutputChannel : config_.subOutputChannel;
}

void AlignmentWizard::soloStep(int channel) {
    // STRICT solo: a sequence takes the owner's 2026-09-06 decision 4, the one
    // OutputPolicy.h:16-19 records (ALIGN-R6). One loop over routeOutput, and
    // no app code outside OutputPolicy touches a gate (record Sec.9).
    soloOutput(engine_, channel);
    windowOpen_ = false;
    stepStartSamples_ = engine_.renderedSamples();
}

void AlignmentWizard::arm() {
    refusal_ = WizardRefusal::None;
    if (step_ != WizardStep::Answering) {
        refusal_ = WizardRefusal::WrongStep;
        return;
    }
    if (!highPassSide_.has_value() || !topology_.has_value() || !inversion_.has_value()
        || !seedAnswered_) {
        // All four, before anything touches the engine. Arming with a question
        // outstanding would mean choosing its answer by default, which is the
        // one thing this lane exists to refuse.
        refusal_ = WizardRefusal::MissingAnswer;
        return;
    }
    if (!engine_.setSource(rta::platform::SourceVariant(source_))) {
        // Nothing has been touched: not the routing, not the slot. Arming over
        // somebody else's running excitation would report a measurement of the
        // wrong signal without ever saying so.
        refusal_ = WizardRefusal::EngineNotQuiescent;
        return;
    }
    soloStep(highSideChannel());
    engine_.armSource();
    step_ = WizardStep::MeasuringHigh;
}

void AlignmentWizard::poll() {
    if (step_ != WizardStep::MeasuringHigh && step_ != WizardStep::MeasuringLow
        && step_ != WizardStep::MeasuringSum) {
        return;
    }
    const double settle = kSettleSamplesAt48k * config_.sampleRate / kSettleReferenceRate;
    const auto needed = static_cast<std::uint64_t>(settle);
    // Counted from THIS step's solo, not from the arm: each solo re-ramps a
    // different output gate, so each step has its own ramp to wait out.
    if (engine_.renderedSamples() - stepStartSamples_ >= needed) windowOpen_ = true;
}

void AlignmentWizard::submitCapture(rta::trace::Trace trace) {
    refusal_ = WizardRefusal::None;
    if (step_ != WizardStep::MeasuringHigh && step_ != WizardStep::MeasuringLow) {
        refusal_ = WizardRefusal::WrongStep;
        return;
    }
    if (!windowOpen_) {
        refusal_ = WizardRefusal::WindowNotOpen;
        return;
    }

    const bool measuringHigh = step_ == WizardStep::MeasuringHigh;
    const bool intoMain = (highPassSide_ == Source::Main) == measuringHigh;
    if (intoMain) {
        mainCapture_ = std::move(trace);
    } else {
        subCapture_ = std::move(trace);
    }

    if (measuringHigh) {
        soloStep(lowSideChannel());
        step_ = WizardStep::MeasuringLow;
        return;
    }

    // The excitation stops the instant its job is done -- the same rule the
    // Locate sequence follows (output-path record Sec.8).
    engine_.disarmSource();
    compute();
}

void AlignmentWizard::beginMeasuredSum() {
    refusal_ = WizardRefusal::None;
    if (step_ != WizardStep::Computed && step_ != WizardStep::AskingCycle
        && step_ != WizardStep::AskingPolarity) {
        refusal_ = WizardRefusal::WrongStep;
        return;
    }
    // One signal on two outputs, which is what L7-OUT Sec.11 promises. Built
    // from the policy primitives rather than two raw routeOutput calls, so a
    // third output left over from an earlier solo cannot survive into the sum.
    soloOutput(engine_, config_.mainOutputChannel);
    applyToggle(engine_, config_.subOutputChannel, true, SoloMode::Additive);
    engine_.armSource();
    windowOpen_ = false;
    stepStartSamples_ = engine_.renderedSamples();
    step_ = WizardStep::MeasuringSum;
}

void AlignmentWizard::submitMeasuredSum(rta::trace::Trace trace) {
    refusal_ = WizardRefusal::None;
    if (step_ != WizardStep::MeasuringSum) {
        refusal_ = WizardRefusal::WrongStep;
        return;
    }
    if (!windowOpen_) {
        refusal_ = WizardRefusal::WindowNotOpen;
        return;
    }
    measuredSum_ = std::move(trace);
    engine_.disarmSource();
    step_ = WizardStep::Done;
}

void AlignmentWizard::compute() {
    if (!mainCapture_.has_value() || !subCapture_.has_value()) {
        refusal_ = WizardRefusal::WrongStep;
        return;
    }
    const rta::trace::Trace& a =
        (highPassSide_ == Source::Main) ? *mainCapture_ : *subCapture_;  // the HIGH-PASS side
    const rta::trace::Trace& b =
        (highPassSide_ == Source::Main) ? *subCapture_ : *mainCapture_;  // the LOW-PASS side

    // L6b Sec.5's "positions must share one engine configuration", applied to a
    // pair. ALIGN-R7: CaptureMeta has no reference-channel field, so the proxy
    // for "same reference" is the `channelRoles` string. It is honest and weak;
    // making it structural is a CaptureMeta field and a session-schema bump,
    // which this lane does not do.
    if (a.meta().sampleRate != b.meta().sampleRate) {
        refusal_ = WizardRefusal::SampleRateMismatch;
        return;
    }
    if (a.meta().fftSize != b.meta().fftSize) {
        refusal_ = WizardRefusal::FftSizeMismatch;
        return;
    }
    if (a.meta().channelRoles != b.meta().channelRoles) {
        refusal_ = WizardRefusal::ReferenceMismatch;
        return;
    }

    // THE conversion goes through VirtualTrace and nowhere else (task G). This
    // file applies no rotation of its own: the appliedDelaySamples correction
    // is a required field of BandFitOptions, so core owns it (ALIGN-R2) and it
    // is impossible to forget here.
    const auto virtualA = rta::trace::VirtualTrace::fromTrace(a);
    const auto virtualB = rta::trace::VirtualTrace::fromTrace(b);
    if (!virtualA.has_value() || !virtualB.has_value()) {
        refusal_ = WizardRefusal::TraceNotUsable;
        return;
    }
    const auto renderA = virtualA->render();
    const auto renderB = virtualB->render();

    const auto coherenceA = coherenceOf(a);
    const auto coherenceB = coherenceOf(b);
    const double binWidthHz = a.binHz();

    const auto crossing = rta::dsp::spectralCrossover(
        a.field(rta::trace::Field::Magnitude), b.field(rta::trace::Field::Magnitude), coherenceA,
        coherenceB, binWidthHz, config_.minimumGatedCoherence, seedHz_);

    lastFit_ = config_.fit;
    lastFit_.centreHz = crossing.frequencyHz;
    lastFit_.minimumGatedCoherence = config_.minimumGatedCoherence;
    lastFit_.sampleRate = a.meta().sampleRate;
    // D_B - D_A, B the LOW-PASS side, A the HIGH-PASS side. Passed ONCE.
    lastFit_.appliedDelayDifferenceSamples =
        static_cast<double>(b.meta().appliedDelaySamples - a.meta().appliedDelaySamples);

    const auto fit = rta::dsp::crossoverBandFit(renderA.h, renderB.h, coherenceA, coherenceB,
                                                binWidthHz, lastFit_);
    const auto expected = expectedOffset(*topology_, *inversion_);

    Verdict result;
    result.crossoverHz = crossing.frequencyHz;
    result.crossingsHz = crossing.allCrossingsHz;
    result.tauSeconds = fit.tauSeconds;
    result.interceptRadians = fit.interceptRadians;
    result.agreement = fit.agreement;
    result.meanFrequencyHz = fit.meanFrequencyHz;
    result.fitRefusal = fit.refusal;
    result.expectedRadians = expected.radians;
    result.deltaRadians = wrapToPiHalfOpen(fit.interceptRadians - expected.radians);
    result.expectedAmbiguous = expected.ambiguous;
    result.alternativeExpectedRadians = expected.alternativeRadians;
    result.alternativeDeltaRadians =
        wrapToPiHalfOpen(fit.interceptRadians - expected.alternativeRadians);
    // A low R says "this band is not one delay plus one constant". It offers no
    // change to the answer the operator gave: an even-order sign discrepancy is
    // evidence about the SYSTEM, never about a convention (PR #3 Sec.8).
    result.matchedPair = fit.agreement >= config_.matchedPairAgreement;
    verdict_ = result;

    buildPolaritySignals();

    // The cycle integer comes from the IR pair or from the operator. Never from
    // a whitened correlator in this file -- PHAT on a band-limited source
    // relocates the ambiguity, it does not resolve it (record Sec.4) -- and a
    // scan over this file asserts no such call appears here.
    if (irHigh_.has_value() && irLow_.has_value()) {
        cycleOffset_ = static_cast<std::ptrdiff_t>(irLow_->originIndex)
                       - static_cast<std::ptrdiff_t>(irHigh_->originIndex);
    } else {
        cycleOffset_ = cycleAnswer_;
    }

    if (!cycleOffset_.has_value()) {
        step_ = WizardStep::AskingCycle;
        return;
    }

    // The two signals that answer the SAME question -- "is this pair relatively
    // inverted" -- are rho's sign and the product of the two findPolarity
    // readings. Looked up by kind, never by position: a list that grew a fourth
    // entry would silently re-point an index.
    const auto eligibleSign = [this](PolaritySignalKind kind) {
        for (const auto& signal : signals_) {
            if (signal.kind != kind) continue;
            if (signal.standing == SignalStanding::Refused) return rta::ir::Sign::Unknown;
            return signal.sign;
        }
        return rta::ir::Sign::Unknown;
    };
    const rta::ir::Sign findPolaritySides =
        productOf(eligibleSign(PolaritySignalKind::FindPolarityHighSide),
                  eligibleSign(PolaritySignalKind::FindPolarityLowSide));
    const rta::ir::Sign rhoSign = eligibleSign(PolaritySignalKind::RelativePolarityRho);
    const bool bothEligible =
        findPolaritySides != rta::ir::Sign::Unknown && rhoSign != rta::ir::Sign::Unknown;
    // When two eligible signals disagree the UI shows both with their reasons
    // and asks. It never picks silently (research D7).
    step_ = (bothEligible && findPolaritySides != rhoSign) ? WizardStep::AskingPolarity
                                                           : WizardStep::Computed;
}

}  // namespace rta::measure
