// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RT60-UI lane task T1 (docs/plans/2026-10-20-rt60-ui-lane-plan.md), the "Option
// B" half of its device-free acceptance: SweepIr objects built from closed-form
// decays (Rt60Fixture.h) into `analyseRt60`. JUCE-free.
//
// What these tests may and may not claim. Accuracy of the decay figures is
// core's job (core/tests/test_ir_decay.cpp, +-10 %). The bounds here are
// DISCRIMINATION bounds: half the gap between the truth and the answer of the
// mutant each one exists to catch, derived once in the plan and not tightened.
// Every figure is printed beside its bound (stdout, "RT60-B" lines).
#include <catch2/catch_test_macros.hpp>

#include "Rt60Fixture.h"
#include "measure/Rt60Analysis.h"
#include "measure/SweepIrPipeline.h"
#include "rta/gen/Sweep.h"
#include "rta/ir/Decay.h"

#include <cmath>
#include <cstdio>

using rta::measure::analyseRt60;
using rta::measure::Rt60Refusal;
using rta::measure::Rt60Result;
namespace fx = rt60_fixture;

namespace {

constexpr const char* kTag = "[rt60_analysis]";

// The L the shipped sweep carries, so the lead-in is the production number.
double shippedL() {
    return rta::gen::Sweep(rta::measure::shippedSweepConfig(48000.0)).lengthConstantL();
}

void printBands(const char* label, const Rt60Result& r, double truth, double bound) {
    for (const auto& b : r.bands) {
        std::printf("RT60-B %-12s %5d Hz  T30 %s%.3f  T20 %s%.3f  range %5.1f dB  B*T %6.1f  truth %.2f  bound +-%.2f\n",
                    label, b.nominalHz, b.t30.has() ? "" : "(refused) ", b.t30.seconds, b.t20.has() ? "" : "(refused) ",
                    b.t20.seconds, b.rangeDb, b.bandwidthTime, truth, bound);
    }
    std::fflush(stdout);
}

fx::IrSpec flatSpec(double t60) {
    fx::IrSpec spec;
    spec.harmonicSpacingL = shippedL();
    spec.components = fx::flatRoom(t60);
    return spec;
}

}  // namespace

TEST_CASE("every octave from 125 Hz to 8 kHz reads the room's 0.80 s inside the discrimination bound", kTag) {
    // The x2 span multiplier (record L4b section 1a's double-count) reads 1.60 s
    // here, so |T - 0.80| < 0.40 separates truth from that mutant by half the gap.
    const auto ir = fx::buildIr(flatSpec(0.80));
    const Rt60Result r = analyseRt60(&ir);
    REQUIRE(r.analysed());
    REQUIRE(r.bands.size() == 8);
    printBands("flat-0.80", r, 0.80, 0.40);

    for (std::size_t i = 1; i < r.bands.size(); ++i) {  // 125 .. 8000; index 0 is 63 Hz
        const auto& b = r.bands[i];
        INFO("band " << b.nominalHz << " Hz: T30 " << b.t30.seconds << " T20 " << b.t20.seconds << " range "
                     << b.rangeDb << " B*T " << b.bandwidthTime);
        REQUIRE(b.t30.has());
        REQUIRE(b.t20.has());
        CHECK(std::fabs(b.t30.seconds - 0.80) < 0.40);
        CHECK(std::fabs(b.t20.seconds - 0.80) < 0.40);
        CHECK(b.hasRange);
        CHECK(b.rangeDb >= 35.0);  // a T30 was reported, so the curve fell at least 35 dB
    }
    std::printf("RT60-B analysis wall time %.1f ms for %zu samples, %zu bands\n", r.wallMs, r.analysedSamples,
                r.bands.size());
    CHECK(r.wallMs < 1000.0);  // a sanity ceiling, not a claim: the figure above is the claim
}

TEST_CASE("the window ends at origin + tail: nothing past it changes a band, bit for bit", kTag) {
    // F1. The IR past origin + tailSamples is the sweep's artefact. Filling it
    // with a +40 dB decaying ramp must leave EVERY figure identical -- and must
    // not be harmless merely because the fixture is too gentle, so the same
    // window is also analysed UNCROPPED below and must come out wrong.
    auto specA = flatSpec(0.80);
    specA.arrivalSamples = 96;
    auto specB = specA;
    specB.artefactDb = 40.0;
    const auto irA = fx::buildIr(specA);
    const auto irB = fx::buildIr(specB);

    const Rt60Result a = analyseRt60(&irA);
    const Rt60Result b = analyseRt60(&irB);
    REQUIRE(a.analysed());
    REQUIRE(b.analysed());
    // Exact: leadIn + tail - arrival (the window starts `leadIn` before the
    // arrival and ends at origin + tail, and the arrival is 96 after t = 0).
    CHECK(a.leadInSamples == 9600);
    CHECK(a.analysedSamples == 9600 + 144000 - 96);
    CHECK(b.analysedSamples == a.analysedSamples);
    REQUIRE(a.bands.size() == b.bands.size());
    for (std::size_t i = 0; i < a.bands.size(); ++i) {
        INFO("band " << a.bands[i].nominalHz << " Hz");
        CHECK(a.bands[i].t30.seconds == b.bands[i].t30.seconds);
        CHECK(a.bands[i].t20.seconds == b.bands[i].t20.seconds);
        CHECK(a.bands[i].edt.seconds == b.bands[i].edt.seconds);
        CHECK(a.bands[i].rangeDb == b.bands[i].rangeDb);
        CHECK(a.bands[i].refusal == b.bands[i].refusal);
    }

    // Teeth: the SAME IR handed to core without the crop. At 1 kHz the artefact's
    // energy sits in the integral's tail and the figure is wrong or refused.
    const std::size_t arrival = irB.originIndex + 96;
    const std::span<const float> uncropped(irB.samples.data() + (arrival - 9600), irB.samples.size() - (arrival - 9600));
    const auto edges = rta::measure::rt60OctaveBands()[4];  // 1000 Hz
    const auto filtered = rta::ir::bandFilterZeroPhase(uncropped, edges.lowHz, edges.highHz, 48000.0);
    const auto curve = rta::ir::energyDecayCurve(filtered, 9600, 48000.0, edges.highHz - edges.lowHz);
    const auto raw = rta::ir::decayTimes(curve).t30;
    std::printf("RT60-B uncropped 1000 Hz T30: %s%.3f (truth 0.80, cropped %.3f)\n", raw.has() ? "" : "refused ",
                raw.seconds, b.bands[4].t30.seconds);
    CHECK_FALSE((raw.has() && std::fabs(raw.seconds - 0.80) < 0.40));
}

TEST_CASE("the lead-in is half the distance to the 2nd harmonic packet, capped at 200 ms", kTag) {
    // F2. Shipped sweep: half of 1.0 s is 0.5 s, so the 200 ms cap binds: exactly
    // 0.2 * 48000 = 9600 samples.
    const double shipped = shippedL();
    CHECK(rta::measure::rt60LeadInSamples(48000.0, shipped) == 9600);

    // A 1 s sweep: Novak synchronisation rounds f1*L to the integer 3, so L = 3/20
    // = 0.15 s (not the unsynchronised 1/ln(1000) = 0.1448 s the plan estimated),
    // and half the distance to the 2nd harmonic, 0.5 * 0.15 * ln 2 s, is 2495.3
    // samples at 48 kHz, rounded DOWN to 2495 so it cannot cross the packet.
    auto config = rta::measure::shippedSweepConfig(48000.0);
    config.durationSec = 1.0;
    const double shortL = rta::gen::Sweep(config).lengthConstantL();
    std::printf("RT60-B 1 s sweep: L = %.6f s, lead-in = %zu samples\n", shortL,
                rta::measure::rt60LeadInSamples(48000.0, shortL));
    CHECK(std::fabs(shortL - 0.15) < 1e-9);
    CHECK(rta::measure::rt60LeadInSamples(48000.0, shortL) == 2495);

    // Not stated: the cap.
    CHECK(rta::measure::rt60LeadInSamples(48000.0, 0.0) == 9600);

    // And it is what the analysis uses.
    auto spec = flatSpec(0.80);
    spec.harmonicSpacingL = shortL;
    spec.tailSamples = 96000;
    const auto ir = fx::buildIr(spec);
    CHECK(analyseRt60(&ir).leadInSamples == 2495);
}

TEST_CASE("63 Hz is refused as outside the sweep's trusted band and 125 Hz to 8 kHz are not", kTag) {
    // F3. The 63 Hz octave spans 44.7 to 89.1 Hz; the shipped sweep's trusted
    // band starts at 80 Hz. 125 Hz spans 89.1 to 177 Hz (core's OctaveBands is base-ten: 125.893 / 10^(3/20)) and 8 kHz ends at 11.2 kHz,
    // both inside [80, 19700].
    const auto ir = fx::buildIr(flatSpec(0.80));
    const Rt60Result r = analyseRt60(&ir);
    REQUIRE(r.bands.size() == 8);
    CHECK(r.bands[0].nominalHz == 63);
    CHECK(r.bands[0].refusal == Rt60Refusal::OutsideTrustedBand);
    CHECK(r.bands[0].t30.refusal == Rt60Refusal::OutsideTrustedBand);
    CHECK_FALSE(r.bands[0].hasRange);
    for (std::size_t i = 1; i < r.bands.size(); ++i) {
        CHECK(r.bands[i].refusal != Rt60Refusal::OutsideTrustedBand);
    }
    CHECK(r.trustedLowHz == 80.0);

    // A band whose upper edge is past the trusted high edge refuses too.
    auto spec = flatSpec(0.80);
    spec.trustedHighHz = 5000.0;
    const auto irHigh = fx::buildIr(spec);
    const Rt60Result high = analyseRt60(&irHigh);
    REQUIRE(high.bands.size() == 8);
    CHECK(high.bands[7].nominalHz == 8000);
    CHECK(high.bands[7].refusal == Rt60Refusal::OutsideTrustedBand);
    CHECK(high.bands[5].refusal != Rt60Refusal::OutsideTrustedBand);  // 2 kHz, edge 2.5 kHz

    // "Not stated" (zero) clamps nothing: 63 Hz is then analysed, not refused.
    auto open = flatSpec(0.80);
    open.trustedLowHz = 0.0;
    open.trustedHighHz = 0.0;
    const auto irOpen = fx::buildIr(open);
    CHECK(analyseRt60(&irOpen).bands[0].refusal != Rt60Refusal::OutsideTrustedBand);
}

TEST_CASE("two octaves with different decays are reported under their own labels", kTag) {
    // A label shift (bands reversed, or one band's edges filtered under another's
    // name) would swap these: 1.20 s at 125 Hz, 0.40 s at 2 kHz, so a swap reads
    // 0.40 and 1.20 and is 0.80 s away from the truth; half is the 0.40 bound.
    // Leakage makes the adjacent octaves read the component's decay too (250 Hz
    // reads ~1.13 s), so a one-octave edge shift cannot be seen in the two
    // component bands alone. The 500 Hz and 8 kHz octaves are two octaves from
    // BOTH components and hold only the noise floor: they must refuse, and a
    // band filtered with a neighbour's edges would read a leaked decay there.
    // Edges come from the grid itself, never typed.
    const auto grid = rta::measure::rt60OctaveBands();
    fx::IrSpec spec;
    spec.harmonicSpacingL = shippedL();
    spec.components = {{grid[1].lowHz, grid[1].highHz, 1.20, 11}, {grid[5].lowHz, grid[5].highHz, 0.40, 12}};
    const auto ir = fx::buildIr(spec);
    const Rt60Result r = analyseRt60(&ir);
    REQUIRE(r.analysed());
    REQUIRE(r.bands.size() == 8);
    REQUIRE(r.bands[1].nominalHz == 125);
    REQUIRE(r.bands[5].nominalHz == 2000);
    printBands("label-shift", r, 0.0, 0.40);
    REQUIRE(r.bands[1].t30.has());
    REQUIRE(r.bands[5].t30.has());
    CHECK(std::fabs(r.bands[1].t30.seconds - 1.20) < 0.40);
    CHECK(std::fabs(r.bands[5].t30.seconds - 0.40) < 0.40);
    REQUIRE(r.bands[3].nominalHz == 500);
    REQUIRE(r.bands[7].nominalHz == 8000);
    CHECK_FALSE(r.bands[3].t30.has());  // no decay two octaves from either component
    CHECK_FALSE(r.bands[7].t30.has());
}

TEST_CASE("no IR, no tail and no lead-in are named, and never throw", kTag) {
    CHECK(analyseRt60(nullptr).refusal == Rt60Refusal::NoIr);
    CHECK(analyseRt60(nullptr).bands.empty());

    rta::measure::SweepIr empty;  // what IrStore holds for a test filler
    CHECK(analyseRt60(&empty).refusal == Rt60Refusal::NoIr);

    auto spec = flatSpec(0.80);
    auto ir = fx::buildIr(spec);
    ir.sampleRate = 0.0;
    CHECK(analyseRt60(&ir).refusal == Rt60Refusal::NoIr);

    ir = fx::buildIr(spec);
    ir.tailSamples = 0;  // an IR that does not say how long its capture ran
    CHECK(analyseRt60(&ir).refusal == Rt60Refusal::NoTail);

    // A tail that ends before the direct sound arrives leaves nothing to analyse.
    ir = fx::buildIr(spec);
    ir.arrivalSamples = static_cast<long>(ir.tailSamples) + 10;
    CHECK(analyseRt60(&ir).refusal == Rt60Refusal::NoTail);

    // An arrival nearer the start of the samples than the lead-in.
    ir = fx::buildIr(spec);
    ir.originIndex = 100;
    ir.arrivalSamples = 0;
    ir.tailSamples = 1000;
    CHECK(analyseRt60(&ir).refusal == Rt60Refusal::NoLeadIn);
}

TEST_CASE("a refusal from core arrives under the mirrored name", kTag) {
    // No noise floor and no decay to find: a pure decay with the floor off ends
    // in a record with no region to estimate noise from (NoNoiseEstimate) or a
    // curve that never reaches the T30 bar (RangeTooSmall); either way the band
    // refuses with a name and no figure rather than a number.
    auto spec = flatSpec(5.0);  // a 5 s room in a 1 s record: the decay never ends
    spec.tailSamples = 48000;
    spec.floorDb = -200.0;
    const auto ir = fx::buildIr(spec);
    const Rt60Result r = analyseRt60(&ir);
    REQUIRE(r.analysed());
    bool anyRefused = false;
    for (std::size_t i = 1; i < r.bands.size(); ++i) {
        if (!r.bands[i].t30.has()) {
            anyRefused = true;
            CHECK(r.bands[i].t30.refusal != Rt60Refusal::None);
            CHECK(r.bands[i].t30.refusal != Rt60Refusal::OutsideTrustedBand);
        }
    }
    CHECK(anyRefused);
}

TEST_CASE("a band carries the provenance the pane and the record quote", kTag) {
    // Mutation-closing: every field analyseBand and analyseRt60 copy or derive.
    auto spec = flatSpec(0.80);
    spec.arrivalSamples = 96;
    const auto ir = fx::buildIr(spec);
    const Rt60Result r = analyseRt60(&ir);
    REQUIRE(r.analysed());
    CHECK(r.sampleRate == 48000.0);
    CHECK(r.trustedLowHz == ir.trustedLowHz);
    CHECK(r.trustedHighHz == ir.trustedHighHz);
    CHECK(r.trustedHighHz > 0.0);
    CHECK(r.arrivalSamples == 96);
    CHECK(r.tailSeconds == static_cast<double>(ir.tailSamples) / 48000.0);

    const auto grid = rta::measure::rt60OctaveBands();
    REQUIRE(r.bands.size() == grid.size());
    for (std::size_t i = 0; i < r.bands.size(); ++i) {
        CHECK(r.bands[i].nominalHz == grid[i].nominalHz);
        CHECK(r.bands[i].lowHz == grid[i].lowHz);
        CHECK(r.bands[i].highHz == grid[i].highHz);
    }
    const auto& b = r.bands[4];  // 1000 Hz, accepted
    REQUIRE(b.t30.has());
    CHECK(b.refusal == Rt60Refusal::None);  // the curve's own verdict, mirrored
    CHECK(b.edt.has());
    // B*T is the width times the late decay the gate read, so the 1 kHz octave
    // (707 Hz wide) of a 0.8 s decay is far above the bar of 6.
    CHECK(b.bandwidthTime > 6.0);
    CHECK(b.crossingSec > 0.0);  // Lundeby found the noise floor, after the arrival
    CHECK(b.crossingSec < r.tailSeconds);
}

TEST_CASE("an empty window refuses with no bands, and a DSP failure is a named refusal", kTag) {
    auto spec = flatSpec(0.80);
    auto ir = fx::buildIr(spec);
    ir.arrivalSamples = static_cast<long>(ir.tailSamples) + 10;
    const Rt60Result none = analyseRt60(&ir);
    CHECK(none.refusal == Rt60Refusal::NoTail);
    CHECK(none.bands.empty());

    // At 16 kHz the 8 kHz octave's edges pass Nyquist and core's band filter
    // throws; the analysis must come back with the text, never an exception.
    ir = fx::buildIr(spec);
    ir.sampleRate = 16000.0;
    ir.trustedLowHz = 0.0;
    ir.trustedHighHz = 0.0;
    Rt60Result thrown;
    REQUIRE_NOTHROW(thrown = analyseRt60(&ir));
    CHECK(thrown.refusal == Rt60Refusal::NoDecayFound);
    CHECK_FALSE(thrown.message.empty());
    CHECK(thrown.bands.empty());
    CHECK(thrown.wallMs > 0.0);
}
