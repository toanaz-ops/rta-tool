// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L4a Task 5: the absolute polarity verdict, and the systems it must refuse.
// Decision 6b of docs/dsp/2026-08-30-sweep-ir-l4a.md.
//
// Read decision 6b before changing anything here. Decision 6 -- still in the file
// above it, deliberately -- specifies a 2.5-octave bandwidth gate that its own
// survey refuted. Four gate variables were measured; three died. The cases below
// are the ones that killed them, and a suite without them passes an
// implementation that would send an operator to rewire a working loudspeaker.

#include <catch2/catch_test_macros.hpp>

#include "rta/dsp/Biquad.h"
#include "rta/dsp/ButterworthDesign.h"
#include "rta/gen/Sweep.h"
#include "rta/ir/Deconvolver.h"
#include "rta/ir/Polarity.h"

#include <cstddef>
#include <vector>

using rta::gen::Sweep;
using rta::ir::Refusal;
using rta::ir::Sign;

namespace {

constexpr double kSampleRate = 48000.0;

/// 20 Hz to 20 kHz over two seconds: the configuration every figure in decision
/// 6b was measured with. Changing it invalidates the gate constants, which are
/// observed rather than derived -- so if this fixture must change, the survey in
/// tools/probe_polarity_*.py has to be re-run, not the expectations edited.
Sweep::Config testConfig() {
    Sweep::Config cfg;
    cfg.sampleRate  = kSampleRate;
    cfg.startHz     = 20.0;
    cfg.endHz       = 20000.0;
    cfg.durationSec = 2.0;
    return cfg;
}

/// Drive the sweep through a Butterworth band-pass and deconvolve.
///
/// `sections` is the SOS-section count, which is the same number SciPy calls the
/// order -- so `sections = 8` here is `scipy.signal.butter(8, ...)` there, and
/// the survey tables transfer directly.
rta::ir::Deconvolution measureThrough(double lowHz, double highHz, int sections,
                                      float drive) {
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);

    const auto design =
        rta::dsp::ButterworthDesign::bandPass(lowHz, highHz, kSampleRate, sections);
    rta::dsp::BiquadCascade cascade(design.sections);

    std::vector<float> driven(excitation.size());
    for (std::size_t i = 0; i < excitation.size(); ++i)
        driven[i] = static_cast<float>(
            cascade.processSample(static_cast<double>(drive) * excitation[i]));

    rta::ir::DeconvolverConfig cfg;
    cfg.sampleRate       = kSampleRate;
    cfg.harmonicSpacingL = sweep.lengthConstantL();
    // The clamp is the RAW excited band. Clamping to the trusted band instead
    // makes any system below it unreadable -- measured, a 50-71 Hz subwoofer
    // then reports a low edge of 82 Hz, describing a different loudspeaker.
    // The trusted band is carried separately, and is used only to decide
    // whether a sweep like this one can answer at all.
    cfg.excitationLowHz  = testConfig().startHz;
    cfg.excitationHighHz = testConfig().endHz;
    cfg.trustedLowHz     = sweep.validBandLowHz();
    cfg.trustedHighHz    = sweep.validBandHighHz();

    return rta::ir::deconvolve(driven, sweep.buildInverseFilter(), cfg);
}

rta::ir::PolarityResult readPolarity(double lowHz, double highHz, int sections,
                                     float drive) {
    return rta::ir::findPolarity(measureThrough(lowHz, highHz, sections, drive), {});
}

}  // namespace

TEST_CASE("Polarity answers a full-range box, both drive polarities",
          "[ir][polarity]") {
    // 60 Hz - 15 kHz, 4th order: inside the gate. Measured across 256 such boxes
    // (four families x orders 2-8 x eight passbands x both polarities): zero
    // wrong answers, and the reviewing session's independent grid of 144 agreed.
    for (float drive : {+1.0f, -1.0f}) {
        const auto got = readPolarity(60.0, 15000.0, 4, drive);
        CHECK(got.sign == (drive > 0 ? Sign::Positive : Sign::Negative));
        CHECK(got.refusal == Refusal::None);
        CHECK(got.lowEdgeHz <= 100.0);
        CHECK(got.highEdgeHz >= 8000.0);
    }
}

TEST_CASE("Polarity refuses a subwoofer, and says which way to go",
          "[ir][polarity]") {
    // 30-120 Hz: an ordinary subwoofer pass band. Its HIGH edge fails the gate,
    // so the refusal is BandTooHigh -- read that as "the band stops too low".
    // The interface string for it points at the relative comparison, which is
    // sound for the same box measured before and after a change. A refusal that
    // is a dead end is worse than no feature.
    for (float drive : {+1.0f, -1.0f}) {
        const auto got = readPolarity(30.0, 120.0, 4, drive);
        CHECK(got.sign == Sign::Unknown);
        CHECK(got.refusal == Refusal::BandTooHigh);
        CHECK(got.highEdgeHz < 8000.0);
    }
}

TEST_CASE("Polarity refuses a horn even though it spans four octaves",
          "[ir][polarity]") {
    // THE case with teeth. 1000-16000 Hz at order 8 is FOUR OCTAVES WIDE, so
    // every bandwidth-in-octaves gate ever proposed for this class would admit
    // it -- the superseded 2.5-octave gate measured it at 4.18 octaves. And it
    // answers BACKWARDS: at arrivalFraction 0.5 the first arrival reads index 4
    // with the wrong sign, because at high filter order the second excursion is
    // larger than the first and the threshold steps over the real arrival.
    //
    // What refuses it is the LOW edge: a horn has no bottom, and the sign of a
    // first arrival needs one. This single case is why the gate is a pair of
    // band edges and not a width.
    for (float drive : {+1.0f, -1.0f}) {
        const auto got = readPolarity(1000.0, 16000.0, 8, drive);
        CHECK(got.sign == Sign::Unknown);
        CHECK(got.refusal == Refusal::BandTooLow);
        CHECK(got.lowEdgeHz > 100.0);
    }
}

TEST_CASE("The low gate is load-bearing: widen it and the horn answers wrongly",
          "[ir][polarity]") {
    // A gate is only proven by showing what goes wrong without it. A gate that
    // refuses everything also produces a green suite, so the previous case alone
    // proves nothing. Open the low edge and the same measurement must produce a
    // CONFIDENT WRONG SIGN -- that is the failure the gate exists to prevent,
    // and this is the only case in the suite that demonstrates it.
    rta::ir::PolarityConfig ungated;
    ungated.gateLowHz = 20000.0;   // admit anything
    for (float drive : {+1.0f, -1.0f}) {
        const auto got =
            rta::ir::findPolarity(measureThrough(1000.0, 16000.0, 8, drive), ungated);
        CHECK(got.refusal == Refusal::None);
        CHECK(got.sign == (drive > 0 ? Sign::Negative : Sign::Positive));  // BACKWARDS
    }
}

TEST_CASE("Polarity survives an inverted reflection louder than the direct sound",
          "[ir][polarity]") {
    // A boundary bounce, or a second box wired backwards: 3 ms later at 1.5x the
    // direct level, through a full-range pass band. The 50% threshold latches
    // onto the direct arrival before the reflection lands. Measured correct at
    // every threshold from 0.2 to 0.7 on THIS system -- unlike the order-8 horn
    // above, where 0.2 and 0.5 disagree, which is why that case exists too.
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);

    const auto delay = static_cast<std::size_t>(0.003 * kSampleRate);
    std::vector<float> summed(excitation.size(), 0.0f);
    for (std::size_t i = 0; i < excitation.size(); ++i) {
        summed[i] += excitation[i];
        if (i + delay < summed.size()) summed[i + delay] += -1.5f * excitation[i];
    }

    const auto design =
        rta::dsp::ButterworthDesign::bandPass(60.0, 15000.0, kSampleRate, 4);
    rta::dsp::BiquadCascade cascade(design.sections);
    for (auto& s : summed)
        s = static_cast<float>(cascade.processSample(static_cast<double>(s)));

    rta::ir::DeconvolverConfig cfg;
    cfg.sampleRate       = kSampleRate;
    cfg.harmonicSpacingL = sweep.lengthConstantL();
    cfg.excitationLowHz  = testConfig().startHz;
    cfg.excitationHighHz = testConfig().endHz;
    cfg.trustedLowHz     = sweep.validBandLowHz();
    cfg.trustedHighHz    = sweep.validBandHighHz();

    const auto got = rta::ir::findPolarity(
        rta::ir::deconvolve(summed, sweep.buildInverseFilter(), cfg), {});
    CHECK(got.sign == Sign::Positive);
    CHECK(got.refusal == Refusal::None);
}

TEST_CASE("Polarity refuses rather than throwing when the harmonic spacing is unknown",
          "[ir][polarity]") {
    // harmonicSpacingL == 0 is a LEGAL output of deconvolve(): it validates the
    // sample rate and the lengths and never touches L. Throwing here would kill
    // polarity on a valid input from its own pipeline.
    //
    // And confidenceDb must read 0.0, not a sentinel. The superseded design
    // returned 200.0 dB when the noise window was empty, which made its
    // narrowband test pass its `confidenceDb > 20.0` assertion for entirely the
    // wrong reason -- green because of a constant, not because of a measurement.
    Sweep sweep(testConfig());
    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);

    rta::ir::DeconvolverConfig cfg;
    cfg.sampleRate = kSampleRate;          // harmonicSpacingL deliberately left 0
    cfg.excitationLowHz  = testConfig().startHz;
    cfg.excitationHighHz = testConfig().endHz;
    cfg.trustedLowHz     = sweep.validBandLowHz();
    cfg.trustedHighHz    = sweep.validBandHighHz();

    const auto got = rta::ir::findPolarity(
        rta::ir::deconvolve(excitation, sweep.buildInverseFilter(), cfg), {});
    CHECK(got.sign == Sign::Unknown);
    CHECK(got.refusal == Refusal::NoNoiseEstimate);
    CHECK(got.confidenceDb == 0.0);
}

TEST_CASE("Polarity reports a subwoofer's real band, not the sweep's",
          "[ir][polarity]") {
    // A 50-71 Hz subwoofer measured with a sweep whose fade-in finishes at 80 Hz
    // sits ENTIRELY below the sweep's "valid" band. If the estimator clamps its
    // search to that valid band, the -10 dB threshold is taken against a peak
    // made of skirt, and the sub is reported as a system near 100 Hz -- so the
    // refusal reason prints a number that is a lie about the loudspeaker.
    //
    // The clamp exists to exclude bins the sweep never EXCITED (below f1, above
    // f2), which is a different and wider band than the one where the result is
    // most trustworthy. Reported edges must land on the real passband.
    for (float drive : {+1.0f, -1.0f}) {
        const auto got = readPolarity(50.0, 71.0, 4, drive);
        CHECK(got.sign == Sign::Unknown);
        CHECK(got.refusal == Refusal::BandTooHigh);
        CHECK(got.lowEdgeHz < 80.0);    // its real low edge, not the fade edge
        CHECK(got.highEdgeHz < 200.0);  // nowhere near the 8 kHz gate
    }
}

TEST_CASE("Polarity blames the sweep when the sweep is what cannot answer",
          "[ir][polarity]") {
    // A sweep starting at 30 Hz with the default two-octave fade-in is only
    // trustworthy from 120 Hz up -- above the 100 Hz gate. Every loudspeaker
    // measured with it would then report a low edge at or above 120 Hz, fail the
    // gate, and receive "low edge 120 Hz, gate 100 Hz": a refusal that sends the
    // operator to inspect a perfectly good box when the fault is the excitation.
    //
    // A 30 Hz sweep is not exotic; it is a common default. So the precondition
    // runs before any measurement and names the real culprit.
    auto cfg = testConfig();
    cfg.startHz = 30.0;
    Sweep sweep(cfg);
    REQUIRE(sweep.validBandLowHz() > 100.0);   // the premise of this test

    std::vector<float> excitation(sweep.lengthSamples());
    sweep.process(excitation);

    const auto design =
        rta::dsp::ButterworthDesign::bandPass(60.0, 15000.0, kSampleRate, 4);
    rta::dsp::BiquadCascade cascade(design.sections);
    std::vector<float> driven(excitation.size());
    for (std::size_t i = 0; i < excitation.size(); ++i)
        driven[i] = static_cast<float>(
            cascade.processSample(static_cast<double>(excitation[i])));

    rta::ir::DeconvolverConfig dc;
    dc.sampleRate       = kSampleRate;
    dc.harmonicSpacingL = sweep.lengthConstantL();
    dc.excitationLowHz  = cfg.startHz;
    dc.excitationHighHz = cfg.endHz;
    dc.trustedLowHz     = sweep.validBandLowHz();
    dc.trustedHighHz    = sweep.validBandHighHz();

    const auto got = rta::ir::findPolarity(
        rta::ir::deconvolve(driven, sweep.buildInverseFilter(), dc), {});
    CHECK(got.sign == Sign::Unknown);
    CHECK(got.refusal == Refusal::SweepBandInsufficient);
}

TEST_CASE("sign and refusal cannot disagree", "[ir][polarity]") {
    // The invariant, asserted in BOTH directions so that neither field can drift
    // into meaning something the other does not. A result carrying a sign and a
    // reason for having none is a bug no single-direction check would catch.
    const rta::ir::PolarityResult results[] = {
        readPolarity(60.0, 15000.0, 4, +1.0f),   // answers
        readPolarity(30.0, 120.0, 4, +1.0f),     // BandTooHigh
        readPolarity(1000.0, 16000.0, 8, +1.0f), // BandTooLow
    };
    for (const auto& got : results)
        CHECK((got.sign != Sign::Unknown) == (got.refusal == Refusal::None));
}
