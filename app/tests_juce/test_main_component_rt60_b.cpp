// SPDX-License-Identifier: AGPL-3.0-or-later
// RT60-UI lane wave B, tasks T9 and T10 (docs/plans/2026-10-20-rt60-ui-lane-plan.md),
// "Option A" through the real buttons: a real MainComponent in SYNTHETIC whose
// output 0 runs through the feedback comb (T = 3*D / (fs*log10(1/g)) = 0.80 s in
// every band, exactly), a real 1 s log sweep from the RT60 pane's own SWEEP
// button, the real worker deconvolving it.
//
// T10: the comb's Schroeder curve is a staircase of 0.75 dB steps (g^2 per 480
// samples = 20*log10(1/g) = 0.75 dB per trip), so the -5/-35 least-squares line
// cannot be further than about half a step from it; the plan's bound is 1.5 dB,
// twice that. An unintegrated energy-time curve of a pulse train is nothing like
// a line, so the same measurement fails it by tens of dB.
//
// T9: three sweeps with three different noise seeds are three captures of one room.
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
constexpr const char* kTag = "[main_component_rt60_b]";
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

void startRoom(MainComponent& c, std::uint32_t seed) {
    c.setSyntheticMode(true);
    rta::measure::SyntheticInput::Config config;
    config.sampleRate = kFs;
    config.measurementDelaySamples = 4;
    config.measurementNoiseDb = -30.0;
    config.roomCombDelaySamples = 480;
    config.roomCombGain = static_cast<float>(rta::measure::roomCombGainFor(0.80, 480, kFs));
    config.seed = seed;
    Access::restartSyntheticForTest(c, config);
    sweepOf(c).setDurationsForTest(1.0, 3.0);
}

void pressSweep(MainComponent& c, double tailSeconds) {
    auto& pane = paneOf(c);
    Access::rt60ForTest(c).settingsForTest().tailSeconds = tailSeconds;
    REQUIRE(tickUntil(c, [&] { return pane.sweepButtonForTest().isEnabled(); }, 20000));
    pane.sweepButtonForTest().onClick();
    REQUIRE(sweepOf(c).busy());
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 90000));
}

void paintAll(rta::view::Rt60PaneView& pane) {
    if (pane.getWidth() <= 0) {
        pane.setBounds(0, 0, 1000, 640);
        static_cast<juce::Component&>(pane).resized();
    }
    pane.createComponentSnapshot(pane.getLocalBounds(), true);
}

}  // namespace

TEST_CASE("rt60 B: the comb room's decay curve is a straight line to within a staircase step", kTag) {
    MainComponent c;
    startRoom(c, 0x5EEDu);
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    pressSweep(c, 2.0);
    auto& pane = paneOf(c);
    const auto result = Access::rt60ForTest(c).resultForTest();
    REQUIRE(result != nullptr);
    REQUIRE(result->analysed());

    int checked = 0;
    for (std::size_t i = 1; i < result->bands.size(); ++i) {  // 125 .. 8000
        const auto& band = result->bands[i];
        REQUIRE(band.edc.valid());
        REQUIRE(band.edc.t30.valid);
        std::printf("RT60-W comb %5d Hz  T30 %.3f  -5/-35 fit: max residual %.2f dB (bound 1.5, step 0.75)  points %zu\n",
                    band.nominalHz, band.t30.seconds, band.edc.t30.maxResidualDb, band.edc.db.size());
        CHECK(band.edc.t30.maxResidualDb < 1.5);
        ++checked;
    }
    CHECK(checked == 7);
    std::fflush(stdout);

    // The chart the operator sees is that curve: click 1000 Hz and read back what was drawn.
    pane.clickRowForTest(4);
    paintAll(pane);
    CHECK(result->bands[4].nominalHz == 1000);
    CHECK(pane.chartForTest().drawnPointsForTest() == result->bands[4].edc.db);
    CHECK(pane.chartForTest().drawnPointsForTest().front() == 0.0f);
    CHECK(pane.chartForTest().legendForTest().size() == 3);
}

TEST_CASE("rt60 B: three sweeps with three seeds make an ensemble; two make a median with no spread", kTag) {
    MainComponent c;
    c.selectPaneView(rta::view::PaneSelectorButton::Rt60);
    auto& model = Access::rt60ForTest(c);
    const std::uint32_t seeds[] = {0x5EEDu, 0xBEEFu, 0xC0FFEEu};
    for (const auto seed : seeds) {
        startRoom(c, seed);
        pressSweep(c, 2.0);
    }
    auto& pane = paneOf(c);
    REQUIRE(Access::libraryForTest(c).entries().size() == 3);

    pane.chooseEnsembleEntryForTest(0);
    pane.chooseEnsembleEntryForTest(1);
    REQUIRE(tickUntil(c, [&] { return !model.analysingForTest(); }, 600000));
    {
        const auto two = model.ensembleForTest();
        REQUIRE(two != nullptr);
        REQUIRE(two->analysed());
        for (const auto& band : two->bands) {
            if (band.refusal == rta::measure::Rt60Refusal::OutsideTrustedBand) continue;
            CHECK(band.t30.captures == 2);
            CHECK_FALSE(band.t30.spreadIsMeaningful());
            CHECK(band.t30.spreadPercent == 0.0);
        }
        for (const auto& line : pane.tableLinesForTest()) CHECK(line.find("IQR") == std::string::npos);
    }

    pane.chooseEnsembleEntryForTest(2);
    REQUIRE(tickUntil(c, [&] { return !model.analysingForTest(); }, 600000));
    const auto three = model.ensembleForTest();
    REQUIRE(three != nullptr);
    REQUIRE(three->analysed());
    CHECK(three->captureCount == 3);
    std::printf("RT60-W ensemble of 3 real sweeps: %.1f ms on the worker\n", three->wallMs);
    bool anySpread = false;
    for (const auto& band : three->bands) {
        if (band.refusal == rta::measure::Rt60Refusal::OutsideTrustedBand) continue;
        std::printf("RT60-W ens-comb %5d Hz  median T30 %.3f  IQR %.2f %%  captures %zu  truth 0.80  bound +-0.40\n",
                    band.nominalHz, band.t30.seconds, band.t30.spreadPercent, band.t30.captures);
        REQUIRE(band.t30.has());
        CHECK(std::fabs(band.t30.seconds - 0.80) < 0.40);
        CHECK(band.t30.captures == 3);
        CHECK(band.t30.spreadIsMeaningful());
        anySpread = anySpread || band.t30.spreadPercent > 0.0;
    }
    // Three different noise seeds are three different captures: the same IR counted
    // three times would read exactly zero in every band.
    CHECK(anySpread);
    bool shown = false;
    for (const auto& line : pane.tableLinesForTest()) shown = shown || line.find("IQR ") != std::string::npos;
    CHECK(shown);
}
