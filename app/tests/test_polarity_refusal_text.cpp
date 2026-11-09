// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L4c: the sentences the operator reads for a findPolarity refusal. Owner
// decision (docs/HUMAN-QA-QUEUE.md, L7 polarity): every refusal says where to go
// next and carries the measured number. The expected numbers below are the
// fixture's INPUTS, written before the call, never read back from the output.

#include "measure/AlignmentWizard.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using rta::measure::PolaritySignalKind;
using rta::measure::polarityRefusalText;

namespace {

rta::ir::PolarityResult refused(rta::ir::Refusal why, double lowEdgeHz, double highEdgeHz) {
    rta::ir::PolarityResult result;
    result.refusal = why;
    result.lowEdgeHz = lowEdgeHz;
    result.highEdgeHz = highEdgeHz;
    return result;
}

bool has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

}  // namespace

TEST_CASE("L4c: BandTooLow quotes the low edge and sends the operator to a relative comparison",
          "[polarity_text]") {
    const auto text = polarityRefusalText(refused(rta::ir::Refusal::BandTooLow, 412.4, 9000.0), {},
                                          PolaritySignalKind::FindPolarityHighSide);
    CHECK(has(text, "(low edge 412 Hz)"));
    CHECK(has(text, "Compare relatively against another measurement of this same system."));
    CHECK_FALSE(has(text, "9000"));
}

TEST_CASE("L4c: BandTooHigh quotes the high edge; the sub side says it is expected",
          "[polarity_text]") {
    const auto result = refused(rta::ir::Refusal::BandTooHigh, 40.0, 2750.6);
    const auto highSide = polarityRefusalText(result, {}, PolaritySignalKind::FindPolarityHighSide);
    CHECK(has(highSide, "(high edge 2751 Hz)"));
    CHECK(has(highSide, "Measure again full-range, or compare relatively before/after a change."));

    // A subwoofer gets this refusal by design: it must not be told to measure full-range.
    const auto lowSide = polarityRefusalText(result, {}, PolaritySignalKind::FindPolarityLowSide);
    CHECK(has(lowSide, "(high edge 2751 Hz)"));
    CHECK(has(lowSide, "Expected"));
    CHECK_FALSE(has(lowSide, "full-range"));
}

TEST_CASE("L4c: SweepBandInsufficient blames the sweep and quotes ITS trusted range",
          "[polarity_text]") {
    rta::ir::Deconvolution sweep;
    sweep.trustedLowHz = 120.0;
    sweep.trustedHighHz = 15000.0;
    // The edges of the loudspeaker must not appear: the sweep is at fault.
    const auto text = polarityRefusalText(refused(rta::ir::Refusal::SweepBandInsufficient, 777.0, 888.0),
                                          sweep, PolaritySignalKind::FindPolarityHighSide);
    CHECK(has(text, "This sweep is only trustworthy from 120 to 15000 Hz"));
    CHECK(has(text, "not enough to decide."));
    CHECK_FALSE(has(text, "777"));
    CHECK_FALSE(has(text, "888"));

    sweep.trustedHighHz = 0.0;  // not stated: only the low end is quoted
    CHECK(has(polarityRefusalText(refused(rta::ir::Refusal::SweepBandInsufficient, 0, 0), sweep,
                                  PolaritySignalKind::FindPolarityHighSide),
              "from 120 Hz up"));
}

TEST_CASE("L4c: the remaining refusals each say what to do", "[polarity_text]") {
    const auto kind = PolaritySignalKind::FindPolarityHighSide;
    CHECK(has(polarityRefusalText(refused(rta::ir::Refusal::NoSignal, 0, 0), {}, kind), "20 dB"));
    CHECK(has(polarityRefusalText(refused(rta::ir::Refusal::NoNoiseEstimate, 0, 0), {}, kind), "Measure again"));
    CHECK(has(polarityRefusalText(refused(rta::ir::Refusal::CaptureTooShort, 0, 0), {}, kind), "Lengthen"));
}
