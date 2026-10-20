// SPDX-License-Identifier: AGPL-3.0-or-later
// RT60-UI lane wave B, tasks T9, T10 and T11 (docs/plans/2026-10-20-rt60-ui-lane-plan.md)
// at the pane: the OCTAVE / THIRD toggle, the row click that feeds the decay chart,
// and the ensemble menu. The IRs are Rt60Fixture.h's closed-form ones stored through
// `MainComponentSweep::addIrForTest`; the same controls driven by real sweeps are
// test_main_component_rt60_b.cpp.
//
// The worker thread has no message loop to post to here, so `settle` drives the
// pane's own timer (which polls the controller) until the job is taken. The wait is
// bounded only so a hung worker fails the test; it is not a speed claim.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "Rt60Fixture.h"
#include "trace/Trace.h"
#include "view/Rt60PaneView.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using Access = MainComponentTestAccess;
constexpr const char* kTag = "[main_component_panes_rt60_b]";

rta::view::Rt60PaneView& paneOf(MainComponent& c) {
    auto* pane = dynamic_cast<rta::view::Rt60PaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
    REQUIRE(pane != nullptr);
    return *pane;
}

rta::trace::Trace tinyTrace(const std::string& id) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = 48000.0;
    meta.fftSize = 64;
    auto trace = rta::trace::Trace::make(meta, std::vector<float>(rta::trace::pointCountFor(64), -6.0f));
    REQUIRE(trace.has_value());
    return std::move(*trace);
}

// A SWEEP trace with a closed-form 0.80 s room IR in the store, seeded so that
// every call is a different capture.
std::string addRoom(MainComponent& c, const std::string& name, std::uint32_t seed) {
    rt60_fixture::IrSpec spec;
    spec.components = rt60_fixture::flatRoom(0.80, seed);
    spec.floorSeed = seed + 5;
    const auto id = Access::libraryForTest(c).add(tinyTrace(name), name, "SWEEP");
    Access::sweepForTest(c).addIrForTest(id, std::make_shared<const rta::measure::SweepIr>(rt60_fixture::buildIr(spec)));
    return id;
}

// Ticks the pane until the controller's worker has delivered.
void settle(MainComponent& c, rta::view::Rt60PaneView& pane) {
    const auto deadline = juce::Time::getMillisecondCounter() + 600000u;
    pane.tickForTest();
    while (Access::rt60ForTest(c).analysingForTest() && juce::Time::getMillisecondCounter() < deadline) {
        juce::Thread::sleep(2);
        pane.tickForTest();
    }
    REQUIRE_FALSE(Access::rt60ForTest(c).analysingForTest());
    pane.tickForTest();
}

void paintAll(rta::view::Rt60PaneView& pane) {
    if (pane.getWidth() <= 0) {
        pane.setBounds(0, 0, 1000, 640);
        static_cast<juce::Component&>(pane).resized();
    }
    REQUIRE(pane.getWidth() > 0);
    pane.createComponentSnapshot(pane.getLocalBounds(), true);  // children too: the chart paints inside
}

}  // namespace

TEST_CASE("rt60 pane B: the toggle switches the table to 24 third-octave rows and back; the label follows", kTag) {
    MainComponent c;
    const auto id = addRoom(c, "SWEEP out1 @ 10:00:00", 1000);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& pane = paneOf(c);
    pane.tickForTest();
    pane.irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    REQUIRE(Access::rt60ForTest(c).selectedIdForTest() == id);
    REQUIRE(pane.tableLinesForTest().size() == 9);
    CHECK(pane.bandSetButtonForTest().getButtonText() == "OCTAVE");

    pane.bandSetButtonForTest().onClick();
    CHECK(Access::rt60ForTest(c).analysingForTest());  // thirds run on the worker, not here
    settle(c, pane);
    const auto& lines = pane.tableLinesForTest();
    REQUIRE(lines.size() == 25);  // heading + 24 thirds
    CHECK(pane.bandSetButtonForTest().getButtonText() == "THIRD");
    CHECK(pane.headerForTest().contains("zero-phase third-octave filters"));
    CHECK(lines[1].find("50") != std::string::npos);
    CHECK(lines[1].find("below this sweep's trusted band (80 Hz)") != std::string::npos);
    CHECK(lines[4].find("below this sweep's trusted band (80 Hz)") == std::string::npos);  // the 100 Hz third
    CHECK(lines[24].find("10000") != std::string::npos);
    std::printf("RT60-W pane thirds analysed in %.1f ms on the worker\n", Access::rt60ForTest(c).resultForTest()->wallMs);

    pane.bandSetButtonForTest().onClick();
    settle(c, pane);  // octave of ONE trace is synchronous: nothing to wait for
    CHECK(pane.tableLinesForTest().size() == 9);
    CHECK(pane.bandSetButtonForTest().getButtonText() == "OCTAVE");
    CHECK(pane.headerForTest().contains("zero-phase octave filters"));
}

TEST_CASE("rt60 pane B: the band set and the clicked row survive leaving the pane and coming back", kTag) {
    MainComponent c;
    addRoom(c, "SWEEP out1 @ 10:00:00", 1000);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    {
        auto& pane = paneOf(c);
        pane.tickForTest();
        pane.irComboForTest().setSelectedId(1, juce::sendNotificationSync);
        pane.bandSetButtonForTest().onClick();
        settle(c, pane);
        pane.clickRowForTest(10);
    }
    c.selectPaneView(rta::view::PaneSelectorButton::Rta);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& again = paneOf(c);
    again.tickForTest();
    CHECK(again.tableLinesForTest().size() == 25);
    CHECK(again.bandSetButtonForTest().getButtonText() == "THIRD");
    CHECK(again.chartBandIndexForTest() == 10);
}

TEST_CASE("rt60 pane B: a row click feeds the chart the band's own curve, spans and all", kTag) {
    MainComponent c;
    addRoom(c, "SWEEP out1 @ 10:00:00", 1000);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& pane = paneOf(c);
    pane.tickForTest();
    paintAll(pane);
    CHECK_FALSE(pane.chartForTest().hasCurveForTest());  // nothing picked yet

    pane.irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    const auto result = Access::rt60ForTest(c).resultForTest();
    REQUIRE(result != nullptr);
    REQUIRE(result->analysed());

    // No click yet: the band nearest 1 kHz that has a curve (index 4 of the octaves).
    CHECK(pane.chartBandIndexForTest() == 4);
    CHECK(result->bands[4].nominalHz == 1000);
    paintAll(pane);
    CHECK(pane.chartForTest().drawnPointsForTest() == result->bands[4].edc.db);
    CHECK(pane.chartForTest().captionForTest().find("EDC 1000 Hz") != std::string::npos);
    CHECK(pane.chartForTest().legendForTest().size() == 3);  // EDT, T20, T30 spans drawn
    CHECK(pane.chartForTest().legendForTest().back().find("T30 ") == 0);
    CHECK(pane.chartForTest().legendForTest().back().find("(-5 to -35 dB)") != std::string::npos);

    // A click on 250 Hz moves it: that band's curve, not another's.
    pane.clickRowForTest(2);
    CHECK(pane.chartBandIndexForTest() == 2);
    paintAll(pane);
    CHECK(result->bands[2].nominalHz == 250);
    CHECK(pane.chartForTest().drawnPointsForTest() == result->bands[2].edc.db);
    CHECK(pane.chartForTest().drawnPointsForTest() != result->bands[4].edc.db);

    // The refused 63 Hz row has no curve and says so, rather than keeping the last one.
    pane.clickRowForTest(0);
    paintAll(pane);
    CHECK(pane.chartForTest().drawnPointsForTest().empty());
    CHECK(pane.chartForTest().captionForTest().find("63 Hz: no decay curve") != std::string::npos);
    // The same through a real mouse-down on the painted row: row 3 is 500 Hz, and
    // the heading line above row 0 is not a row.
    const auto mouseDownAt = [&pane](juce::Point<int> where) {
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), where.toFloat(),
                                     juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &pane, &pane,
                                     juce::Time::getCurrentTime(), where.toFloat(), juce::Time::getCurrentTime(), 1,
                                     false);
        static_cast<juce::Component&>(pane).mouseDown(event);
    };
    mouseDownAt(pane.rowCentreForTest(3));
    CHECK(pane.chartBandIndexForTest() == 3);
    CHECK(result->bands[3].nominalHz == 500);
    mouseDownAt(pane.rowCentreForTest(-1));  // the column heading
    CHECK(pane.chartBandIndexForTest() == 3);
    mouseDownAt(pane.rowCentreForTest(7));   // the last row (8000 Hz)
    CHECK(pane.chartBandIndexForTest() == 7);
    pane.clickRowForTest(0);
    // Out-of-range clicks change nothing.
    pane.clickRowForTest(99);
    pane.clickRowForTest(-1);
    CHECK(pane.chartBandIndexForTest() == 0);
}

TEST_CASE("rt60 pane B: the ensemble menu lists IR'd traces; two ticked show the median, three add the spread", kTag) {
    MainComponent c;
    const auto noIr = Access::libraryForTest(c).add(tinyTrace("opened"), "SWEEP opened", "SWEEP");
    (void) noIr;
    const auto a = addRoom(c, "SWEEP A", 1000);
    const auto b = addRoom(c, "SWEEP B", 2000);
    const auto d = addRoom(c, "SWEEP C", 3000);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& pane = paneOf(c);
    pane.tickForTest();
    pane.irComboForTest().setSelectedId(1, juce::sendNotificationSync);  // newest: SWEEP C
    CHECK(pane.ensembleButtonForTest().getButtonText() == "ENSEMBLE");

    // Newest first; the trace with no IR is not offered (the picker carries its reason).
    auto menu = pane.ensembleMenuForTest();
    REQUIRE(menu.size() == 3);
    CHECK(menu[0] == "[ ] SWEEP C");
    CHECK(menu[1] == "[ ] SWEEP B");
    CHECK(menu[2] == "[ ] SWEEP A");

    auto& model = Access::rt60ForTest(c);
    pane.chooseEnsembleEntryForTest(0);
    CHECK(model.ensembleIdsForTest() == std::vector<std::string>{d});
    CHECK_FALSE(pane.showingEnsembleForTest());  // one is not an ensemble
    CHECK(pane.ensembleButtonForTest().getButtonText() == "ENSEMBLE (1)");

    pane.chooseEnsembleEntryForTest(1);
    settle(c, pane);
    CHECK(pane.ensembleButtonForTest().getButtonText() == "ENSEMBLE (2)");
    REQUIRE(pane.showingEnsembleForTest());
    CHECK(pane.headerForTest().contains("ENSEMBLE 2 captures: SWEEP C, SWEEP B"));
    CHECK(pane.headerForTest().contains("IQR needs 3 captures"));
    {
        const auto& lines = pane.tableLinesForTest();
        REQUIRE(lines.size() == 9);
        CHECK(lines[0].find("captures") != std::string::npos);
        CHECK(lines[2].find("2 of 2") != std::string::npos);
        for (const auto& line : lines) CHECK(line.find("IQR") == std::string::npos);  // no spread from two
    }
    menu = pane.ensembleMenuForTest();
    CHECK(menu[0] == "[x] SWEEP C");
    CHECK(menu[2] == "[ ] SWEEP A");
    CHECK(menu.size() == 4);  // + "Clear ensemble"

    pane.chooseEnsembleEntryForTest(2);  // SWEEP A joins: three captures
    settle(c, pane);
    {
        const auto& lines = pane.tableLinesForTest();
        REQUIRE(lines.size() == 9);
        CHECK(lines[2].find("3 of 3") != std::string::npos);
        CHECK(lines[2].find("IQR ") != std::string::npos);
        CHECK(lines[2].find("%") != std::string::npos);
    }
    CHECK(pane.headerForTest().contains("IQR in percent of the median"));
    const auto ensemble = model.ensembleForTest();
    REQUIRE(ensemble != nullptr);
    CHECK(ensemble->captureCount == 3);
    // Three DIFFERENT captures: a spread that is not the zero a counted-three-times IR gives.
    bool anySpread = false;
    for (const auto& band : ensemble->bands) anySpread = anySpread || band.t30.spreadPercent > 0.0;
    CHECK(anySpread);
    std::printf("RT60-W pane ensemble of 3, octave: %.1f ms on the worker\n", ensemble->wallMs);

    // Unticking back to one drops the ensemble and shows the single table again.
    pane.chooseEnsembleEntryForTest(2);
    pane.chooseEnsembleEntryForTest(1);
    settle(c, pane);
    CHECK_FALSE(pane.showingEnsembleForTest());
    CHECK(pane.headerForTest().startsWith("IR: SWEEP C"));
    // "Clear ensemble" is the entry after the three traces while any is ticked.
    REQUIRE(model.ensembleIdsForTest().size() == 1);
    pane.chooseEnsembleEntryForTest(3);
    CHECK(model.ensembleIdsForTest().empty());
    CHECK(pane.ensembleButtonForTest().getButtonText() == "ENSEMBLE");
    (void) b;
    (void) a;
}

TEST_CASE("rt60 pane B: eight ticked captures analyse together, in thirds, without blocking the pane", kTag) {
    MainComponent c;
    for (std::uint32_t i = 0; i < 8; ++i) addRoom(c, "SWEEP " + std::to_string(i), 1000 + 1000 * i);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& pane = paneOf(c);
    pane.tickForTest();
    pane.irComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane.bandSetButtonForTest().onClick();
    settle(c, pane);
    for (int i = 0; i < 8; ++i) pane.chooseEnsembleEntryForTest(i);
    CHECK(Access::rt60ForTest(c).analysingForTest());  // the pane returned before the job finished
    settle(c, pane);
    const auto ensemble = Access::rt60ForTest(c).ensembleForTest();
    REQUIRE(ensemble != nullptr);
    CHECK(ensemble->captureCount == 8);
    CHECK(ensemble->bandSet == rta::measure::Rt60BandSet::Third);
    CHECK(pane.tableLinesForTest().size() == 25);
    std::printf("RT60-W pane ensemble of 8, thirds: %.1f ms on the worker\n", ensemble->wallMs);
    const auto menu = pane.ensembleMenuForTest();
    REQUIRE(menu.size() == 9);  // 8 traces + Clear: the store keeps 8 IRs, so a ninth cannot exist
    for (int i = 0; i < 8; ++i) CHECK(menu[static_cast<std::size_t>(i)].startsWith("[x] "));
}

TEST_CASE("rt60 pane B: a ticked capture the store evicts leaves the ensemble, and the survivors keep working", kTag) {
    MainComponent c;
    std::vector<std::string> ids;
    for (std::uint32_t i = 0; i < 8; ++i) ids.push_back(addRoom(c, "SWEEP " + std::to_string(i), 1000 + 1000 * i));
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& pane = paneOf(c);
    auto& model = Access::rt60ForTest(c);
    pane.tickForTest();
    // Menu is newest first: entry 7 is SWEEP 0 (the oldest), 6 is SWEEP 1, 0 is SWEEP 7.
    pane.chooseEnsembleEntryForTest(7);
    pane.chooseEnsembleEntryForTest(6);
    pane.chooseEnsembleEntryForTest(0);
    settle(c, pane);
    REQUIRE(model.ensembleIdsForTest().size() == 3);
    CHECK(model.ensembleForTest()->captureCount == 3);

    // A ninth IR pops the oldest, which is ticked. The pane's next tick drops it.
    addRoom(c, "SWEEP 8", 9000);
    settle(c, pane);
    CHECK(model.ensembleIdsForTest() == std::vector<std::string>{ids[1], ids[7]});
    const auto ensemble = model.ensembleForTest();
    REQUIRE(ensemble != nullptr);
    REQUIRE(ensemble->analysed());  // not the "no IR in memory" refusal
    CHECK(ensemble->captureCount == 2);
    CHECK(pane.showingEnsembleForTest());
    CHECK(pane.ensembleButtonForTest().getButtonText() == "ENSEMBLE (2)");

    // The menu offers the eight IRs that exist, the two survivors ticked, and the evicted one is gone.
    const auto menu = pane.ensembleMenuForTest();
    REQUIRE(menu.size() == 9);  // 8 traces + Clear
    CHECK(menu[0] == "[ ] SWEEP 8");
    for (const auto& line : menu) CHECK_FALSE(line.contains("SWEEP 0"));

    // Tick all eight, add another: the evicted one leaves, so the new id is tickable, not "(full)".
    for (int i = 0; i < 8; ++i) {
        if (menu[static_cast<std::size_t>(i)].startsWith("[ ] ")) pane.chooseEnsembleEntryForTest(i);
    }
    settle(c, pane);
    CHECK(model.ensembleIdsForTest().size() == 8);
    addRoom(c, "SWEEP 9", 10000);
    settle(c, pane);
    CHECK(model.ensembleIdsForTest().size() == 7);  // SWEEP 1 was the oldest left and is gone
    const auto after = pane.ensembleMenuForTest();
    CHECK(after[0] == "[ ] SWEEP 9");  // enabled: no "(full)"
    pane.chooseEnsembleEntryForTest(0);
    CHECK(model.ensembleIdsForTest().size() == 8);
}
