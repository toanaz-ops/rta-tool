// SPDX-License-Identifier: AGPL-3.0-or-later
//
// ALIGN-UI lane wave A, task T3 (docs/plans/2026-10-20-align-ui-lane-plan.md,
// decisions 6 and 7): what the verdict SAYS. Pure text over a hand-built
// Verdict, so each sentence an operator acts on is proven with no pane.
//
// Visible text that carries a non-ASCII character is asserted with the
// character written as escaped UTF-8 bytes. H4 shipped a mojibake separator
// from a typed one and a test that never looked at it; the assertions below
// include the degree sign, the plus-minus sign and the middle dot, so a
// reader that re-encoded them would go red here.
//
// A hex escape in a C++ literal eats every following hex digit, so "\xb1"
// followed by "1" is one escape, not two characters: each such byte is its
// own literal.

#include "measure/AlignReadout.h"

#include <catch2/catch_test_macros.hpp>

#include <numbers>
#include <string>

using rta::measure::AlignReadoutInput;
using rta::measure::CrossoverFamily;
using rta::measure::ProcessorInversion;
using rta::measure::Source;
using rta::measure::Verdict;

namespace {

constexpr double kFs = 48000.0;
constexpr double kDegree = std::numbers::pi / 180.0;
const std::string kDeg = "\xc2\xb0";
const std::string kDot = "\xc2\xb7";
const std::string kPlusMinus = "\xc2\xb1";

Verdict baseVerdict() {
    Verdict v;
    v.crossoverHz = 100.0;
    v.crossingsHz = { 100.0 };
    v.tauSeconds = 2.0e-3;
    v.candidateTausSeconds = { 2.0e-3 };
    v.agreement = 0.987;
    v.matchedPair = true;
    v.fitRefusal = rta::dsp::CrossoverRefusal::None;
    return v;
}

bool contains(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("T3a: the delay goes to the EARLIER source, named, in ms and samples", "[align_readout]") {
    // tau > 0: the low-pass side arrives later, so the high-pass side is the
    // earlier one and is the one delayed.
    CHECK(rta::measure::delayInstruction(2.0e-3, Source::Main, kFs) == "delay MAIN by 2.00 ms (96 samples)");
    CHECK(rta::measure::delayInstruction(2.0e-3, Source::Sub, kFs) == "delay SUB by 2.00 ms (96 samples)");
    // tau < 0: the high-pass side arrives later, so the OTHER side is delayed.
    CHECK(rta::measure::delayInstruction(-2.0e-3, Source::Main, kFs) == "delay SUB by 2.00 ms (96 samples)");
    CHECK(rta::measure::delayInstruction(-2.0e-3, Source::Sub, kFs) == "delay MAIN by 2.00 ms (96 samples)");
    CHECK(rta::measure::delayInstruction(0.0, Source::Main, kFs) == "no delay needed between MAIN and SUB");
}

TEST_CASE("T3b: the integer picks the candidate nearest it, never silently tau*", "[align_readout]") {
    // The fit's competitors sit one lobe either side of tau* (8.26 ms for a
    // 121-bin window of 1 Hz, C5b). tau* alone is 0.74 ms; the operator's
    // coarse offset is 9 ms, which is tau* + 8.26.
    Verdict v = baseVerdict();
    v.tauSeconds = 0.74e-3;
    v.candidateTausSeconds = { 0.74e-3, 0.74e-3 + 8.26e-3, 0.74e-3 - 8.26e-3 };

    const auto none = rta::measure::resolveCycle(v, std::nullopt, kFs);
    CHECK_FALSE(none.resolvedByInteger);
    CHECK(none.tauSeconds == v.tauSeconds);  // no integer: tau* stands, and says it is unconfirmed

    const auto nine = rta::measure::resolveCycle(v, 432, kFs);  // 9.000 ms
    CHECK(nine.resolvedByInteger);
    CHECK(nine.tauSeconds == v.candidateTausSeconds[1]);

    const auto minusSeven = rta::measure::resolveCycle(v, -336, kFs);  // -7.000 ms
    CHECK(minusSeven.tauSeconds == v.candidateTausSeconds[2]);

    const auto nearTauStar = rta::measure::resolveCycle(v, 48, kFs);  // 1.000 ms
    CHECK(nearTauStar.tauSeconds == v.candidateTausSeconds[0]);

    // And the sentence follows the RESOLVED delay, with tau* mentioned because
    // the operator's offset overrode it.
    AlignReadoutInput input;
    input.verdict = &v;
    input.highPass = Source::Main;
    input.topology = { CrossoverFamily::LinkwitzRiley, 4 };
    input.inversion = ProcessorInversion::No;
    input.sampleRate = kFs;
    input.cycleSamples = 432;
    const auto lines = rta::measure::verdictLines(input);
    CHECK(contains(lines, "delay MAIN by 9.00 ms (432 samples)"));
    CHECK(contains(lines, "fit tau* alone was +0.74 ms"));
}

TEST_CASE("T3c: before the integer is known the candidates are listed and the offset is asked for",
          "[align_readout]") {
    Verdict v = baseVerdict();
    v.tauSeconds = 0.74e-3;
    v.candidateTausSeconds = { 0.74e-3, 9.00e-3, -7.52e-3 };
    AlignReadoutInput input;
    input.verdict = &v;
    input.topology = { CrossoverFamily::LinkwitzRiley, 4 };
    input.sampleRate = kFs;
    const auto lines = rta::measure::verdictLines(input);
    REQUIRE(lines.size() >= 2);
    CHECK(lines[1] == "fit tau* +0.74 ms; candidates (ms): +0.74, +9.00, -7.52 -- type the coarse offset to choose");
    CHECK_FALSE(contains(lines, "delay MAIN"));  // no delay is suggested on a guess
}

TEST_CASE("T3c2: the sentence names who chose the candidate; an unclear IR integer is shown as a hint",
          "[align_readout]") {
    Verdict v = baseVerdict();
    v.tauSeconds = 0.74e-3;
    v.candidateTausSeconds = { 0.74e-3, 9.00e-3, -7.52e-3 };
    AlignReadoutInput input;
    input.verdict = &v;
    input.topology = { CrossoverFamily::LinkwitzRiley, 4 };
    input.sampleRate = kFs;
    input.cycleSamples = 432;  // 9 ms, which overrides tau*

    input.cycleFromIr = true;
    CHECK(contains(rta::measure::verdictLines(input), "the bound IRs chose another candidate"));
    CHECK_FALSE(contains(rta::measure::verdictLines(input), "your offset"));
    input.cycleFromIr = false;
    CHECK(contains(rta::measure::verdictLines(input), "your offset chose another candidate"));

    input.cycleSamples.reset();
    input.irHintSamples = 336;  // 7 ms, between candidates
    const auto asked = rta::measure::verdictLines(input);
    CHECK(contains(asked, "IRs suggest about +7.00 ms but fit no candidate clearly; type the offset"));
    CHECK_FALSE(contains(asked, "delay MAIN"));
}

TEST_CASE("T3d: the inversion sentence needs the intercept NEARER the opposite line, not a 45 degree look",
          "[align_readout]") {
    Verdict v = baseVerdict();
    v.deltaRadians = 100.0 * kDegree;
    CHECK_FALSE(rta::measure::inversionSentence(v).empty());
    v.deltaRadians = -100.0 * kDegree;
    CHECK_FALSE(rta::measure::inversionSentence(v).empty());
    v.deltaRadians = 80.0 * kDegree;
    CHECK(rta::measure::inversionSentence(v).empty());
    v.deltaRadians = 90.0 * kDegree;  // equidistant from both lines: no verdict either way
    CHECK(rta::measure::inversionSentence(v).empty());
    v.deltaRadians = 45.0 * kDegree;  // a looser rule would speak here; this one stays silent
    CHECK(rta::measure::inversionSentence(v).empty());

    // An UNKNOWN answer to question (c) draws two lines and the operator
    // picks: both deltas are shown and no sentence chooses for them.
    v.deltaRadians = 170.0 * kDegree;
    v.expectedAmbiguous = true;
    v.alternativeDeltaRadians = -10.0 * kDegree;
    CHECK(rta::measure::inversionSentence(v).empty());
    AlignReadoutInput input;
    input.verdict = &v;
    input.topology = { CrossoverFamily::LinkwitzRiley, 4 };
    input.inversion = ProcessorInversion::Unknown;
    input.sampleRate = kFs;
    input.cycleSamples = 96;
    const auto lines = rta::measure::verdictLines(input);
    CHECK(contains(lines, "delta vs LR-4 NOT INVERTED +170" + kDeg + " " + kDot + " vs INVERTED -10" + kDeg));
}

TEST_CASE("T3e: the readout, line by line, with every non-ASCII byte spelled out", "[align_readout]") {
    Verdict v = baseVerdict();
    v.deltaRadians = 3.0 * kDegree;
    AlignReadoutInput input;
    input.verdict = &v;
    input.highPass = Source::Main;
    input.topology = { CrossoverFamily::LinkwitzRiley, 4 };
    input.inversion = ProcessorInversion::No;
    input.sampleRate = kFs;
    input.cycleSamples = 96;
    const auto lines = rta::measure::verdictLines(input);
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "crossover 100 Hz");  // whole hertz, no decimal (CLAUDE.md "Reading out numbers")
    CHECK(lines[1] == "delay MAIN by 2.00 ms (96 samples)");
    CHECK(lines[2] == "R 0.99 " + kDot + " fit window " + kPlusMinus + "1 oct, R bar 0.5: unmeasured defaults");
    CHECK(lines[3] == "delta vs LR-4 NOT INVERTED +3" + kDeg);
    // The bytes themselves: U+00B7 and U+00B1 and U+00B0 are two bytes each.
    CHECK(lines[2].find("\xc2\xb7") != std::string::npos);
    CHECK(lines[2].find("\xc2\xb1") != std::string::npos);
    CHECK(lines[3].find("\xc2\xb0") != std::string::npos);
    CHECK(lines[2].find("\xc3\x82") == std::string::npos);  // no mojibake "A-circumflex" lead byte
}

TEST_CASE("T3f: more than one crossing, a low R, and an inverted answer all say so", "[align_readout]") {
    Verdict v = baseVerdict();
    v.crossingsHz = { 100.0, 2000.0 };
    v.agreement = 0.3;
    v.matchedPair = false;
    v.deltaRadians = 100.0 * kDegree;
    AlignReadoutInput input;
    input.verdict = &v;
    input.topology = { CrossoverFamily::Butterworth, 2 };
    input.inversion = ProcessorInversion::Yes;
    input.sampleRate = kFs;
    input.cycleSamples = 96;
    const auto lines = rta::measure::verdictLines(input);
    CHECK(lines[0] == "crossover 100 Hz " + kDot + " 2 crossings seen (a seed picks one)");
    CHECK(contains(lines, "R is below the bar"));
    CHECK(contains(lines, "delta vs BW-2 INVERTED +100" + kDeg));
    CHECK(contains(lines, "OPPOSITE polarity"));
    CHECK(contains(lines, "check whether one output is inverted"));
    CHECK(rta::measure::verdictLines(AlignReadoutInput{}).empty());  // no verdict, no text
}
