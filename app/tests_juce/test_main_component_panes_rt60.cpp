// SPDX-License-Identifier: AGPL-3.0-or-later
// RT60-UI lane wave A, tasks T3 and T4 (docs/plans/2026-10-20-rt60-ui-lane-plan.md):
// the seventh selector button reaches the RT60 pane through the real
// MainComponent and the real makePaneFactory; the picker lists every SWEEP trace,
// the ones with no IR WITH the reason; picking a trace fills the table once; and
// leaving the pane and coming back builds a new one over the SAME cached result
// (the model lives in MainComponentRt60, not in the pane).
//
// The IRs here are Rt60Fixture.h's closed-form ones, stored through
// `MainComponentSweep::addIrForTest` -- fast and exact. The same pane driven by a
// real sweep is test_main_component_rt60.cpp.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "Rt60Fixture.h"
#include "trace/Trace.h"
#include "view/Rt60PaneView.h"

#include <string>

namespace {

using Access = MainComponentTestAccess;
constexpr const char* kTag = "[main_component_panes_rt60]";

rta::view::Rt60PaneView* rt60PaneOf(MainComponent& c) {
    return dynamic_cast<rta::view::Rt60PaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
}

// Any trace will do: the RT60 pane lists SWEEP-group entries and reads the IR
// from the store, never the trace's own data.
rta::trace::Trace tinyTrace(const std::string& id) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = 48000.0;
    meta.fftSize = 64;
    auto trace = rta::trace::Trace::make(meta, std::vector<float>(rta::trace::pointCountFor(64), -6.0f));
    REQUIRE(trace.has_value());
    return std::move(*trace);
}

bool anyLineContains(const std::vector<std::string>& lines, const std::string& part) {
    for (const auto& line : lines) {
        if (line.find(part) != std::string::npos) return true;
    }
    return false;
}

const std::string kNoIrText = "no IR: opened from file or dropped from memory (8 kept); sweep again";

}  // namespace

TEST_CASE("rt60 pane: the selector builds an Rt60PaneView, returns to RTA, and a saved rt60 pane reopens", kTag) {
    MainComponent c;
    CHECK(rt60PaneOf(c) == nullptr);

    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    CHECK(c.currentPaneView() == rta::view::PaneView::Rt60);
    REQUIRE(rt60PaneOf(c) != nullptr);
    // What Save would write for this pane is the string OPEN resolves back.
    const auto specs = Access::paneSpecsForTest(c);
    REQUIRE(specs.size() == 1);
    CHECK(specs[0].view == "rt60");

    c.selectPaneView(rta::view::PaneSelectorButton::Rta);
    CHECK(c.currentPaneView() == rta::view::PaneView::Rta);
    CHECK(rt60PaneOf(c) == nullptr);

    // OPEN of a saved session whose pane is "rt60": the same rebuild session
    // Open drives, not only the selector's click path.
    Access::restoreWorkspaceForTest(c, {rta::trace::PaneSpec{"rt60"}});
    CHECK(c.currentPaneView() == rta::view::PaneView::Rt60);
    CHECK(rt60PaneOf(c) != nullptr);
}

TEST_CASE("rt60 pane: the picker lists a trace without an IR, with the reason, and picking it shows only that", kTag) {
    MainComponent c;
    auto& library = Access::libraryForTest(c);
    auto& sweep = Access::sweepForTest(c);

    // Oldest first: an opened-from-file SWEEP trace (no IR), then one with an IR.
    const auto noIrId = library.add(tinyTrace("opened"), "SWEEP out1 @ 09:00:00", "SWEEP");
    const auto irId = library.add(tinyTrace("swept"), "SWEEP out1 @ 10:00:00", "SWEEP");
    library.add(tinyTrace("other"), "not a sweep", "default");  // another group is not listed
    rt60_fixture::IrSpec spec;
    spec.components = rt60_fixture::flatRoom(0.80);
    sweep.addIrForTest(irId, std::make_shared<const rta::measure::SweepIr>(rt60_fixture::buildIr(spec)));

    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto* pane = rt60PaneOf(c);
    REQUIRE(pane != nullptr);
    pane->tickForTest();

    // Newest first, both listed; the one without an IR carries the reason.
    const auto items = pane->pickerTextsForTest();
    REQUIRE(items.size() == 2);
    CHECK(items[0] == "SWEEP out1 @ 10:00:00  [IR in memory]");
    CHECK(items[1] == juce::String("SWEEP out1 @ 09:00:00  [") + kNoIrText + "]");

    // Picking the one without an IR: the refusal, whole, and no table of dashes.
    pane->irComboForTest().setSelectedId(2, juce::sendNotificationSync);
    CHECK(Access::rt60ForTest(c).selectedIdForTest() == noIrId);
    CHECK(pane->wholeRefusalForTest() == kNoIrText);
    CHECK(pane->tableLinesForTest().empty());
    CHECK(pane->headerForTest().startsWith("IR: SWEEP out1 @ 09:00:00"));
    CHECK(pane->adviceForTest().isEmpty());

    // Picking the one with an IR fills the table: a heading and one row per octave.
    pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    CHECK(Access::rt60ForTest(c).selectedIdForTest() == irId);
    CHECK(pane->wholeRefusalForTest().empty());
    const auto& lines = pane->tableLinesForTest();
    REQUIRE(lines.size() == 9);
    CHECK(lines[0].find("EDT*") != std::string::npos);
    CHECK(lines[1].find("63") != std::string::npos);
    CHECK(lines[1].find("below this sweep's trusted band (80 Hz)") != std::string::npos);
    CHECK(lines[2].find("125") != std::string::npos);
    CHECK(lines[2].find(" s") != std::string::npos);
    CHECK(pane->footnoteForTest().contains("EDT has no validity check"));
    CHECK(pane->headerForTest().contains("Lundeby truncation, zero-phase octave filters"));
    // One analysis for the one IR picked, however many ticks pass.
    pane->tickForTest();
    pane->tickForTest();
    CHECK(Access::rt60ForTest(c).analysisCountForTest() == 2);  // the refusal, then the IR
}

TEST_CASE("rt60 pane: switching away and back keeps the table, and the TAIL starts at 3 s", kTag) {
    MainComponent c;
    auto& library = Access::libraryForTest(c);
    const auto irId = library.add(tinyTrace("swept"), "SWEEP out1 @ 10:00:00", "SWEEP");
    rt60_fixture::IrSpec spec;
    spec.components = rt60_fixture::flatRoom(0.80);
    Access::sweepForTest(c).addIrForTest(irId, std::make_shared<const rta::measure::SweepIr>(rt60_fixture::buildIr(spec)));

    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto* pane = rt60PaneOf(c);
    REQUIRE(pane != nullptr);
    CHECK(pane->tailComboForTest().getSelectedId() == 1);  // TAIL 3 s
    CHECK(Access::rt60ForTest(c).settingsForTest().tailSeconds == 3.0);
    pane->tickForTest();
    pane->irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    const auto before = pane->tableLinesForTest();
    REQUIRE(before.size() == 9);
    pane->tailComboForTest().setSelectedId(2, juce::sendNotificationSync);  // TAIL 6 s

    c.selectPaneView(rta::view::PaneSelectorButton::Eq);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    pane = rt60PaneOf(c);
    REQUIRE(pane != nullptr);
    CHECK(pane->tableLinesForTest() == before);  // the same cached result, redrawn
    CHECK(pane->irComboForTest().getSelectedId() == 1);
    CHECK(pane->tailComboForTest().getSelectedId() == 2);  // the operator's choice survived the rebuild
    CHECK(Access::rt60ForTest(c).analysisCountForTest() == 1);  // not analysed again
}

TEST_CASE("rt60 pane: SWEEP needs an output: none without a device, out 1 and out 2 in SYNTHETIC", kTag) {
    MainComponent c;
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto* pane = rt60PaneOf(c);
    REQUIRE(pane != nullptr);
    pane->tickForTest();
    CHECK(pane->outputComboForTest().getNumItems() == 0);
    CHECK_FALSE(pane->sweepButtonForTest().isEnabled());

    c.setSyntheticMode(true);
    c.selectPaneView(rta::view::PaneSelectorButton::Rta);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    pane = rt60PaneOf(c);
    REQUIRE(pane != nullptr);
    pane->tickForTest();
    CHECK(pane->outputComboForTest().getNumItems() == 2);
    CHECK(pane->outputComboForTest().getSelectedId() == 1);  // out 1 by default
    CHECK(pane->sweepButtonForTest().isEnabled());
    pane->outputComboForTest().setSelectedId(2, juce::sendNotificationSync);
    CHECK(Access::rt60ForTest(c).settingsForTest().outputChannel == 1);
}
