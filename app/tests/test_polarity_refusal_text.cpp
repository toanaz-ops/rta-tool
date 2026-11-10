// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L4c: the sentences the operator reads for a findPolarity refusal. Owner
// decision (docs/HUMAN-QA-QUEUE.md, L7 polarity): every refusal says where to go
// next and carries the measured number. The expected numbers below are the
// fixture's INPUTS, written before the call, never read back from the output.

#include "measure/AlignmentWizard.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

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
    const auto text = polarityRefusalText(refused(rta::ir::Refusal::BandTooLow, 412.4, 9000.0), {});
    CHECK(has(text, "(low edge 412 Hz)"));
    CHECK(has(text, "Compare relatively against another measurement of this same system."));
    CHECK_FALSE(has(text, "9000"));
}

TEST_CASE("L4c: BandTooHigh quotes the high edge, in the owner's words (the sub case it was written for)",
          "[polarity_text]") {
    const auto result = refused(rta::ir::Refusal::BandTooHigh, 40.0, 2750.6);
    const auto text = polarityRefusalText(result, {});
    CHECK(has(text, "Measured band does not reach high enough (high edge 2751 Hz)."));
    CHECK(has(text, "Measure again full-range, or compare relatively before/after a change."));
}

TEST_CASE("L4c: SweepBandInsufficient blames the sweep and quotes ITS trusted range",
          "[polarity_text]") {
    rta::ir::Deconvolution sweep;
    sweep.trustedLowHz = 120.0;
    sweep.trustedHighHz = 15000.0;
    // The edges of the loudspeaker must not appear: the sweep is at fault.
    const auto text = polarityRefusalText(refused(rta::ir::Refusal::SweepBandInsufficient, 777.0, 888.0),
                                          sweep);
    CHECK(has(text, "This sweep is only trustworthy from 120 to 15000 Hz"));
    CHECK(has(text, "not enough to decide."));
    CHECK_FALSE(has(text, "777"));
    CHECK_FALSE(has(text, "888"));

    sweep.trustedHighHz = 0.0;  // not stated: only the low end is quoted
    CHECK(has(polarityRefusalText(refused(rta::ir::Refusal::SweepBandInsufficient, 0, 0), sweep),
              "from 120 Hz up"));
}

TEST_CASE("L4c: the remaining refusals each say what to do", "[polarity_text]") {
    CHECK(has(polarityRefusalText(refused(rta::ir::Refusal::NoNoiseEstimate, 0, 0), {}), "Measure again"));
    CHECK(has(polarityRefusalText(refused(rta::ir::Refusal::CaptureTooShort, 0, 0), {}), "Lengthen"));
}

TEST_CASE("L4c: NoSignal quotes the MEASURED confidence (one decimal) against the gate", "[polarity_text]") {
    auto result = refused(rta::ir::Refusal::NoSignal, 0, 0);
    result.confidenceDb = 12.34;  // the fixture's input
    const auto text = polarityRefusalText(result, {});
    CHECK(has(text, "Arrival confidence 12.3 dB is below the 20.0 dB needed to decide."));

    // The neighbour: a measured NEGATIVE confidence (noise louder than the arrival) is still a
    // measured number and is quoted.
    result.confidenceDb = -3.0;
    CHECK(has(polarityRefusalText(result, {}), "Arrival confidence -3.0 dB is below the 20.0 dB"));
}

TEST_CASE("H11 L3: NoSignal on a path that never measured a confidence quotes no number",
          "[polarity_text]") {
    // confidenceDb defaults to 0.0 and findPolarity returns early (empty or silent search
    // window) without setting it: the sentence must not say "0.0 dB".
    const auto text = polarityRefusalText(refused(rta::ir::Refusal::NoSignal, 0, 0), {});
    CHECK_FALSE(has(text, "dB"));
    CHECK_FALSE(has(text, "0.0"));
    CHECK(has(text, "Measure again, louder or closer."));
}
