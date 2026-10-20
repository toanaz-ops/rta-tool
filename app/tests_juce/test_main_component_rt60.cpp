// SPDX-License-Identifier: AGPL-3.0-or-later
// RT60-UI lane wave A, task T5 (docs/plans/2026-10-20-rt60-ui-lane-plan.md):
// "Option A", the device-free acceptance THROUGH THE REAL BUTTON. A real
// MainComponent in SYNTHETIC, output 0's path a feedback comb (a "room" whose
// decay is T = 3*D / (fs*log10(1/g)) = 0.80 s in every band, exactly), the
// measurement channel's noise on at -30 dB. The RT60 pane's own SWEEP button
// plays a real 1 s log sweep on out 1 with a per-press tail, the real worker
// deconvolves it, the controller analyses it, and the pane's table is read back.
//
// Bounds are DISCRIMINATION bounds (the plan's "Device-free acceptance"): half the
// gap between the truth and the named mutant's answer, never tighter. The x2 span
// multiplier reads 1.60 s, so |T - 0.80| < 0.40. Accuracy is core's job.
//
// HELD TAIL, not requested. The capture keeps 1.0 s of pre-roll and the sweep
// starts part-way into it, so the IR holds the TAIL choice PLUS about 0.9 s of
// slack (measured: 2.0 s held 2.90 s, 0.1 s held 1.04 s). The plan's "tail 1.0 ->
// advice, tail 2.0 -> none" assumed equal; with the slack, 1.5 x 0.80 s = 1.2 s
// fits inside a 1.0 s choice already. The pair below is
// therefore 0.1 s (held ~1.0 s, short of 1.2 s) and 2.0 s (held ~2.9 s); the
// figures are printed.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "dev-support/SyntheticRoomComb.h"
#include "view/Rt60PaneView.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

using Access = MainComponentTestAccess;
using Requester = MainComponentSweep::Requester;
constexpr const char* kTag = "[main_component_rt60]";
constexpr double kFs = 48000.0;

MainComponentSweep& sweepOf(MainComponent& c) { return Access::sweepForTest(c); }

rta::view::Rt60PaneView& paneOf(MainComponent& c) {
    auto* pane = dynamic_cast<rta::view::Rt60PaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
    REQUIRE(pane != nullptr);
    return *pane;
}

template <typename Predicate>
bool tickUntil(MainComponent& c, Predicate&& done, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        Access::timerTickForTest(c);
        if (c.currentPaneView() == rta::view::PaneView::Rt60) paneOf(c).tickForTest();
        if (done()) return true;
        juce::Thread::sleep(5);
    }
    Access::timerTickForTest(c);
    return done();
}

// The comb room: noise off the reference, the room in output 0's path. The path
// delay (4) is SYNTHETIC's own; no band-pass, so the comb IS the whole path.
void startRoom(MainComponent& c, double t60 = 0.80) {
    c.setSyntheticMode(true);
    rta::measure::SyntheticInput::Config config;
    config.sampleRate = kFs;
    config.measurementDelaySamples = 4;
    config.measurementNoiseDb = -30.0;
    config.roomCombDelaySamples = 480;
    config.roomCombGain = static_cast<float>(rta::measure::roomCombGainFor(t60, 480, kFs));
    Access::restartSyntheticForTest(c, config);
    sweepOf(c).setDurationsForTest(1.0, 3.0);  // a 1 s sweep; the RAIL tail stays 3 s
    // The capture timeout runs on the wall clock while the loopback is paced by it
    // too: a saturated box aborts a healthy sweep. Stretch the slack as H5 does.
    sweepOf(c).setTimeoutSlackForTest(30000.0);
}

void printTable(const char* label, const rta::measure::Rt60Result& r) {
    std::printf("RT60-A %s: tail held %.2f s, lead-in %zu, analysed %zu samples, wall %.1f ms\n", label, r.tailSeconds,
                r.leadInSamples, r.analysedSamples, r.wallMs);
    for (const auto& b : r.bands) {
        std::printf("RT60-A %s %5d Hz  T30 %s%.3f  T20 %s%.3f  range %5.1f dB  B*T %6.1f  truth 0.80  bound +-0.40\n",
                    label, b.nominalHz, b.t30.has() ? "" : "(refused) ", b.t30.seconds, b.t20.has() ? "" : "(refused) ",
                    b.t20.seconds, b.rangeDb, b.bandwidthTime);
    }
    std::fflush(stdout);
}

// Presses the pane's own SWEEP button with `tailSeconds` and waits for the sweep
// and its analysis to finish.
void pressSweep(MainComponent& c, double tailSeconds) {
    auto& pane = paneOf(c);
    Access::rt60ForTest(c).settingsForTest().tailSeconds = tailSeconds;
    REQUIRE(tickUntil(c, [&] { return pane.sweepButtonForTest().isEnabled(); }, 600000));
    pane.sweepButtonForTest().onClick();
    REQUIRE(sweepOf(c).busy());
    REQUIRE(sweepOf(c).armedTailSecondsForTest() == tailSeconds);  // the override reached the armed request
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 600000));
}

}  // namespace

TEST_CASE("rt60: the pane's SWEEP button fills the table with the room's 0.80 s, inside the bound", kTag) {
    MainComponent c;
    startRoom(c);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& pane = paneOf(c);
    auto& model = Access::rt60ForTest(c);
    auto& library = Access::libraryForTest(c);

    pressSweep(c, 2.0);
    INFO("sweep readout: " << sweepOf(c).readoutForTest());
    REQUIRE(library.entries().size() == 1);
    const auto& entry = library.entries()[0];
    CHECK(entry.name.rfind("SWEEP out1 @ ", 0) == 0);  // a named-output press
    CHECK(entry.group == "SWEEP");

    // The RT60 press's own result became the selection, analysed exactly once.
    CHECK(model.selectedIdForTest() == entry.traceId);
    CHECK(model.analysisCountForTest() == 1);
    const auto result = model.resultForTest();
    REQUIRE(result != nullptr);
    REQUIRE(result->analysed());
    printTable("pane-sweep", *result);
    CHECK(result->wallMs < 1000.0);  // a sanity ceiling; the figure above is the claim

    REQUIRE(result->bands.size() == 8);
    for (std::size_t i = 1; i < result->bands.size(); ++i) {  // 125 .. 8000
        const auto& b = result->bands[i];
        INFO("band " << b.nominalHz << " Hz: T30 " << b.t30.seconds << " T20 " << b.t20.seconds << " range "
                     << b.rangeDb << " B*T " << b.bandwidthTime);
        REQUIRE(b.t30.has());
        REQUIRE(b.t20.has());
        CHECK(std::fabs(b.t30.seconds - 0.80) < 0.40);
        CHECK(std::fabs(b.t20.seconds - 0.80) < 0.40);
    }

    // The sweep never reaches 63 Hz (80 Hz trusted): refused WITH the reason, in
    // the table the operator reads.
    CHECK(result->bands[0].nominalHz == 63);
    CHECK(result->bands[0].refusal == rta::measure::Rt60Refusal::OutsideTrustedBand);
    const auto& lines = pane.tableLinesForTest();
    REQUIRE(lines.size() == 9);
    CHECK(lines[1].find("below this sweep's trusted band (80 Hz)") != std::string::npos);
    CHECK(pane.headerForTest().startsWith("IR: SWEEP out1 @ "));

    // The crop is what makes the figures a room's: the window is the lead-in plus
    // the tail the IR held, not the whole deconvolution.
    CHECK(result->leadInSamples == 2495);  // 0.5 * 0.15 s * ln 2 at 48 kHz, rounded down
    const auto ir = sweepOf(c).irStoreForTest().find(entry.traceId);
    REQUIRE(ir != nullptr);
    CHECK(static_cast<long>(result->analysedSamples) ==
          static_cast<long>(result->leadInSamples) + static_cast<long>(ir->tailSamples) - ir->arrivalSamples);
    CHECK(ir->samples.size() > result->analysedSamples + 10000);  // there WAS an artefact region to leave out

    // Held 2.0 s + slack: 1.5 x 0.80 s needs 1.2 s, so no advice.
    CHECK(pane.adviceForTest().isEmpty());

    // The rail's SWEEP afterwards is still 3 s: the override was per press.
    sweepOf(c).clicked();
    CHECK(sweepOf(c).armedTailSecondsForTest() == 3.0);
    CHECK(model.selectedIdForTest() == entry.traceId);  // a rail press does not move the selection

    // The IR is evicted by later sweeps: the result stays shown, and says so; the
    // picker now reads the trace as having no IR.
    Access::timerTickForTest(c);
    sweepOf(c).fillIrStoreForTest(8);
    paneOf(c).tickForTest();
    CHECK(paneOf(c).headerForTest().contains("IR no longer in memory"));
    CHECK(paneOf(c).tableLinesForTest().size() == 9);
    const auto items = paneOf(c).pickerTextsForTest();
    REQUIRE(items.size() == 1);
    CHECK(items[0].contains("no IR: opened from file or dropped from memory (8 kept); sweep again"));
}

TEST_CASE("rt60: a tail too short for 1.5 x the decay gets an advice line, and a long one does not", kTag) {
    // See the file comment: HELD tail = the choice plus ~0.9 s of pre-roll slack.
    MainComponent c;
    startRoom(c);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);

    pressSweep(c, 0.1);
    const auto shortResult = Access::rt60ForTest(c).resultForTest();
    REQUIRE(shortResult != nullptr);
    REQUIRE(shortResult->analysed());
    printTable("tail-0.1", *shortResult);
    std::printf("RT60-A tail-0.1 advice: \"%s\"\n", paneOf(c).adviceForTest().toRawUTF8());
    CHECK(shortResult->tailSeconds < 1.2);  // short of 1.5 x 0.80 s: the premise of the advice
    CHECK(paneOf(c).adviceForTest().contains("TAIL"));
    CHECK(paneOf(c).adviceForTest().contains("and sweep again"));
}

TEST_CASE("rt60: only a press the RT60 pane made is auto-selected; ALIGN's and the rail's are not", kTag) {
    MainComponent c;
    startRoom(c);
    sweepOf(c).setDurationsForTest(1.0, 0.5);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& model = Access::rt60ForTest(c);
    auto& library = Access::libraryForTest(c);

    // A sweep ALIGN asked for (the default requester of a named press).
    REQUIRE(sweepOf(c).clickedOnOutput(0));
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 600000));
    REQUIRE(library.entries().size() == 1);
    CHECK(model.selectedIdForTest().empty());
    CHECK(model.analysisCountForTest() == 0);

    // The rail's own button.
    sweepOf(c).clicked();
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 600000));
    REQUIRE(library.entries().size() == 2);
    CHECK(model.selectedIdForTest().empty());
    CHECK(model.analysisCountForTest() == 0);

    // The same call tagged RT60: selected and analysed.
    REQUIRE(sweepOf(c).clickedOnOutput(0, Requester::Rt60, 0.5));
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 600000));
    REQUIRE(library.entries().size() == 3);
    CHECK(model.selectedIdForTest() == library.entries()[2].traceId);
    CHECK(model.analysisCountForTest() == 1);
}

TEST_CASE("rt60: the pane's SWEEP refuses while LOCATE holds the output, and adds nothing", kTag) {
    MainComponent c;
    startRoom(c);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    Access::locateClickedForTest(c);
    REQUIRE(Access::locateActiveForTest(c));
    auto& pane = paneOf(c);
    pane.tickForTest();

    pane.sweepButtonForTest().onClick();
    CHECK(sweepOf(c).stateForTest() == MainComponentSweep::State::Idle);
    CHECK(sweepOf(c).readoutForTest().contains("refused"));
    CHECK(Access::libraryForTest(c).entries().empty());
    pane.tickForTest();
    CHECK(pane.statusForTest().contains("refused"));  // the pane shows the sweep's own reason
}
