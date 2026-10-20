// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RT60-UI lane task T2 (docs/plans/2026-10-20-rt60-ui-lane-plan.md, decisions
// 5, 7, 8, 9 and finding F6): the words and numbers the pane shows. Strings are
// asserted whole, so a unit, a separator or a refusal that lost its numbers
// shows up here and not on an operator's screen. JUCE-free.
#include <catch2/catch_test_macros.hpp>

#include "measure/Rt60Readout.h"

#include <string>

using namespace rta::measure;

namespace {

constexpr const char* kTag = "[rt60_readout]";

// U+00B7 as UTF-8: a mis-encoded literal would show a stray A-circumflex first.
const std::string kDot = "\xc2\xb7";

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

Rt60Result analysedResult(double tailSeconds) {
    Rt60Result r;
    r.refusal = Rt60Refusal::None;
    r.sampleRate = 48000.0;
    r.tailSeconds = tailSeconds;
    r.trustedLowHz = 80.0;
    r.trustedHighHz = 19700.0;
    r.wallMs = 12.4;
    return r;
}

Rt60Band goodBand(int hz, double t30, double t20, double edt) {
    Rt60Band b;
    b.nominalHz = hz;
    b.lowHz = hz / 1.4;
    b.highHz = hz * 1.4;
    b.refusal = Rt60Refusal::None;
    b.t30 = {t30, Rt60Refusal::None};
    b.t20 = {t20, Rt60Refusal::None};
    b.edt = {edt, Rt60Refusal::None};
    b.hasRange = true;
    b.rangeDb = 47.24;
    return b;
}

Rt60Band refusedBand(Rt60Refusal why) {
    Rt60Band b = goodBand(1000, 0, 0, 0);
    b.refusal = why;
    b.t30 = b.t20 = b.edt = {0.0, why};
    b.hasRange = false;  // core clears the curve when it refuses a band
    return b;
}

}  // namespace

TEST_CASE("numbers read out in the units CLAUDE.md names", kTag) {
    CHECK(rt60Seconds(0.8) == "0.80 s");           // two decimals, never "0.8 s"
    CHECK(rt60Seconds(0.804) == "0.80 s");
    CHECK(rt60Seconds(0.806) == "0.81 s");
    CHECK(rt60Seconds(12.0) == "12.00 s");
    CHECK(rt60Hertz(80.0) == "80 Hz");             // whole hertz, never "80.0 Hz"
    CHECK(rt60Hertz(19700.4) == "19700 Hz");
    // An octave band is NAMED by its nominal label: the exact centre 125.89 is
    // "125", never "126" -- the exact centre is not what anyone reads off a rack.
    CHECK(rt60NominalOctaveHz(125.89) == 125);
    CHECK(rt60NominalOctaveHz(63.0957) == 63);
    CHECK(rt60NominalOctaveHz(7943.28) == 8000);
    CHECK(rt60Hertz(125.89) == "126 Hz");  // plain rounding is for edges like 80 Hz, not for band names
    CHECK(rt60Db(47.24) == "47.2 dB");             // dB keeps one decimal
}

TEST_CASE("a refusal says what happened with the numbers it was decided on", kTag) {
    const auto result = analysedResult(3.0);

    auto range = refusedBand(Rt60Refusal::RangeTooSmall);
    range.hasRange = true;
    range.rangeDb = 28.4;
    CHECK(rt60BandNote(range, result) == "decay reaches 28.4 dB; T30 needs 35 dB");

    auto narrow = refusedBand(Rt60Refusal::BandwidthTimeTooSmall);
    narrow.bandwidthTime = 5.43;
    CHECK(rt60BandNote(narrow, result) == "band too narrow for a decay this short (B" + kDot + "T 5.4 < 6)");

    const auto noise = refusedBand(Rt60Refusal::NoNoiseEstimate);
    CHECK(rt60BandNote(noise, result) == "no noise floor found in the 3.0 s tail");  // names the tail
    CHECK(contains(rt60BandNote(noise, analysedResult(6.0)), "6.0 s tail"));

    auto low = refusedBand(Rt60Refusal::OutsideTrustedBand);
    low.nominalHz = 63;
    low.lowHz = 44.7;
    low.highHz = 89.1;
    CHECK(rt60BandNote(low, result) == "below this sweep's trusted band (80 Hz)");
    auto high = refusedBand(Rt60Refusal::OutsideTrustedBand);
    high.lowHz = 15000.0;
    high.highHz = 21000.0;
    CHECK(rt60BandNote(high, result) == "above this sweep's trusted band (19700 Hz)");

    CHECK(rt60BandNote(refusedBand(Rt60Refusal::NoDecayFound), result) == "no decreasing slope found");
    CHECK(rt60BandNote(refusedBand(Rt60Refusal::NoLeadIn), result) == "no lead-in before the arrival");
}

TEST_CASE("every refusal has words, and an accepted band has none", kTag) {
    const auto result = analysedResult(3.0);
    for (const auto why : {Rt60Refusal::BandwidthTimeTooSmall, Rt60Refusal::NoDecayFound, Rt60Refusal::RangeTooSmall,
                           Rt60Refusal::NoNoiseEstimate, Rt60Refusal::NoLeadIn, Rt60Refusal::OutsideTrustedBand,
                           Rt60Refusal::NoIr, Rt60Refusal::NoTail}) {
        INFO("refusal " << static_cast<int>(why));
        CHECK_FALSE(rt60BandNote(refusedBand(why), result).empty());
    }
    CHECK(rt60BandNote(goodBand(1000, 0.8, 0.8, 0.8), result).empty());

    // The whole-analysis refusals, as the pane's table area shows them.
    Rt60Result none;
    none.refusal = Rt60Refusal::NoIr;
    CHECK(rt60WholeRefusalText(none, 8) == "no IR: opened from file or dropped from memory (8 kept); sweep again");
    none.refusal = Rt60Refusal::NoTail;
    CHECK_FALSE(rt60WholeRefusalText(none, 8).empty());
    none.refusal = Rt60Refusal::NoLeadIn;
    CHECK_FALSE(rt60WholeRefusalText(none, 8).empty());
    none.refusal = Rt60Refusal::NoDecayFound;
    none.message = "bandFilterZeroPhase: need 0 < lowHz < highHz < fs/2";
    CHECK(contains(rt60WholeRefusalText(none, 8), "analysis failed: bandFilterZeroPhase"));
    CHECK(rt60WholeRefusalText(analysedResult(3.0), 8).empty());
    CHECK(rt60NoIrText(8) == "no IR: opened from file or dropped from memory (8 kept); sweep again");
}

TEST_CASE("a row shows the figures, refuses with dashes, and keeps the EDT footnote", kTag) {
    auto result = analysedResult(3.0);
    auto ok = goodBand(125, 0.80, 0.78, 0.95);
    const Rt60Row row = rt60Row(ok, result);
    CHECK(row.hz == "125");
    CHECK(row.t30 == "0.80 s");
    CHECK(row.t20 == "0.78 s");
    CHECK(row.edt == "0.95 s");
    CHECK(row.range == "47.2 dB");
    CHECK(row.note.empty());

    // T30 refused for range, T20 reported: the figure that answered stays, the
    // one that could not is a dash with its reason -- never one substituted.
    auto partial = goodBand(250, 0.0, 0.70, 0.9);
    partial.t30 = {0.0, Rt60Refusal::RangeTooSmall};
    partial.rangeDb = 28.4;
    const Rt60Row p = rt60Row(partial, result);
    CHECK(p.t30 == "--");
    CHECK(p.t20 == "0.70 s");
    CHECK(p.note == "decay reaches 28.4 dB; T30 needs 35 dB");

    const Rt60Row refused = rt60Row(refusedBand(Rt60Refusal::NoNoiseEstimate), result);
    CHECK(refused.t30 == "--");
    CHECK(refused.t20 == "--");
    CHECK(refused.edt == "--");
    CHECK(refused.range == "--");

    result.bands = {ok, partial};
    const auto lines = rt60TableLines(result);
    REQUIRE(lines.size() == 3);
    CHECK(contains(lines[0], "EDT*"));
    CHECK(contains(lines[0], "T30"));
    CHECK(contains(lines[1], "0.80 s"));
    CHECK(contains(lines[2], "decay reaches 28.4 dB"));
    CHECK(contains(rt60Footnote(), "EDT has no validity check"));
}

TEST_CASE("the header names the IR, the tail, the trusted band and the method", kTag) {
    const auto result = analysedResult(3.0);
    const auto lines = rt60HeaderLines(result, "SWEEP out1 @ 12:00:00", true);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "IR: SWEEP out1 @ 12:00:00");
    const std::string sep = " " + kDot + " ";
    CHECK(lines[1] == "tail held 3.0 s" + sep + "trusted band 80 Hz to 19700 Hz" + sep +
                          "Lundeby truncation, zero-phase octave filters" + sep + "analysed in 12 ms");

    const auto evicted = rt60HeaderLines(result, "SWEEP out1 @ 12:00:00", false);
    CHECK(evicted[0] == "IR: SWEEP out1 @ 12:00:00 -- IR no longer in memory");

    Rt60Result refused;
    refused.refusal = Rt60Refusal::NoIr;
    CHECK(rt60HeaderLines(refused, "x", true).size() == 1);  // no tail to name
}

TEST_CASE("the advice line names the next TAIL choice, and only when it would help", kTag) {
    // 1.5 x the longest reported decay must fit in the tail (L4a's bound).
    auto result = analysedResult(3.0);
    result.bands = {goodBand(1000, 2.40, 2.40, 2.5)};
    CHECK(rt60Advice(result) ==
          "longest decay 2.40 s needs a tail of at least 3.60 s: choose TAIL 6 s and sweep again");

    // Exactly on the bound is enough (the comparison is strict): 1.5 x 2.00 = 3.00.
    result.bands = {goodBand(1000, 2.00, 2.00, 2.0)};
    CHECK(rt60Advice(result).empty());

    // A T20 stands in when T30 refused; a band with neither adds nothing.
    auto t20only = goodBand(1000, 0.0, 2.40, 2.4);
    t20only.t30 = {0.0, Rt60Refusal::RangeTooSmall};
    result.bands = {t20only, refusedBand(Rt60Refusal::NoNoiseEstimate)};
    CHECK(contains(rt60Advice(result), "longest decay 2.40 s"));

    // The plan's device-free pair at T = 0.80 s.
    auto shortTail = analysedResult(1.0);
    shortTail.bands = {goodBand(1000, 0.80, 0.80, 0.8)};
    CHECK(contains(rt60Advice(shortTail), "choose TAIL 3 s"));  // 1.2 s needed > 1.0 s held
    auto longTail = analysedResult(2.0);
    longTail.bands = {goodBand(1000, 0.80, 0.80, 0.8)};
    CHECK(rt60Advice(longTail).empty());                         // 1.2 s needed < 2.0 s held

    // No noise floor found: the next longer tail, whatever the figures say.
    auto noFloor = analysedResult(3.0);
    noFloor.bands = {goodBand(500, 0.5, 0.5, 0.5), refusedBand(Rt60Refusal::NoNoiseEstimate)};
    noFloor.bands[1].t30 = {0.0, Rt60Refusal::NoNoiseEstimate};
    CHECK(rt60Advice(noFloor) ==
          "a band found no noise floor in the 3.0 s tail: choose TAIL 6 s and sweep again");
    // ... but a 63 Hz band that was never analysed is not "no noise floor".
    noFloor.bands[1] = refusedBand(Rt60Refusal::OutsideTrustedBand);
    CHECK(rt60Advice(noFloor).empty());

    // Past the longest choice there is nothing to suggest but the fact.
    auto huge = analysedResult(10.0);
    huge.bands = {goodBand(1000, 9.0, 9.0, 9.0)};
    CHECK(contains(rt60Advice(huge), "no TAIL choice is long enough (the longest is 10 s)"));

    // An analysis that was withheld has no advice to give.
    Rt60Result withheld;
    withheld.refusal = Rt60Refusal::NoIr;
    CHECK(rt60Advice(withheld).empty());
}

TEST_CASE("each figure that refused is the one the note explains", kTag) {
    // Mutation-closing cases: the T20 and EDT branches, the band-level trusted
    // refusal winning over per-figure ones, and the exact whole-analysis words.
    const auto result = analysedResult(3.0);

    auto t20 = goodBand(1000, 0.8, 0.0, 0.8);
    t20.t20 = {0.0, Rt60Refusal::RangeTooSmall};
    t20.rangeDb = 31.0;
    CHECK(rt60BandNote(t20, result) == "decay reaches 31.0 dB; T20 needs 25 dB");

    auto edt = goodBand(1000, 0.8, 0.8, 0.0);
    edt.edt = {0.0, Rt60Refusal::RangeTooSmall};
    edt.rangeDb = 8.0;
    CHECK(rt60BandNote(edt, result) == "decay reaches 8.0 dB; EDT needs 10 dB");

    // No curve survived (core clears it on a B*T refusal), so no range to quote.
    auto noRange = refusedBand(Rt60Refusal::RangeTooSmall);
    CHECK(rt60BandNote(noRange, result) == "decay too short for T30 (needs 35 dB)");

    // The band-level refusal explains the band even when no figure carries it.
    auto outside = goodBand(63, 0.0, 0.0, 0.0);
    outside.lowHz = 44.7;
    outside.refusal = Rt60Refusal::OutsideTrustedBand;
    outside.t30 = outside.t20 = outside.edt = {0.0, Rt60Refusal::NoDecayFound};
    CHECK(rt60BandNote(outside, result) == "below this sweep's trusted band (80 Hz)");

    CHECK(rt60BandNote(refusedBand(Rt60Refusal::NoIr), result) == "no IR to analyse");
    Rt60Result lead;
    lead.refusal = Rt60Refusal::NoLeadIn;
    CHECK(rt60WholeRefusalText(lead, 8) ==
          "the arrival is too close to the start of the IR for the band filters; sweep again");
}

TEST_CASE("the advice names a tail that covers the need, not merely a longer one", kTag) {
    // Tail 1.0 s held, longest decay 2.40 s: 3 s is longer than the tail but
    // 1.5 x 2.40 = 3.60 s needs the 6 s choice.
    auto result = analysedResult(1.0);
    result.bands = {goodBand(1000, 2.40, 2.40, 2.5)};
    CHECK(rt60Advice(result) ==
          "longest decay 2.40 s needs a tail of at least 3.60 s: choose TAIL 6 s and sweep again");

    // A withheld analysis gives no advice even if it somehow carries bands.
    Rt60Result withheld;
    withheld.refusal = Rt60Refusal::NoIr;
    withheld.bands = {goodBand(1000, 2.40, 2.40, 2.5)};
    CHECK(rt60Advice(withheld).empty());
}
