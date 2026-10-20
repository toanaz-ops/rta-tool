// SPDX-License-Identifier: AGPL-3.0-or-later
// ALIGN-UI lane wave A: what a human sees when they press ALIGN in SYNTHETIC with
// the SHIPPED defaults (no fixture swapped in). test_main_component_align.cpp
// swaps in the LR4 high-pass / LR4 low-pass pair the plan derives its bounds
// from; an operator cannot do that, so this drives the real defaults through
// the same combos and buttons.
//
// THE DEFAULTS (H4 ch 0, decision 10 of the plan keeps it): output 0 is the
// 40 Hz - 16 kHz band-pass after 4 samples, output 1 the LR4 low-pass at 100 Hz
// after 100. Output 0 is NOT an LR4 high-pass, so the pair is not an LR-4
// crossover and the readout says things about THAT pair:
//   - the two magnitudes cross where the band-pass's 40 Hz high-pass skirt
//     rises through the low-pass's 100 Hz skirt, i.e. between the two corners;
//   - the low-pass side arrives later (the 96-sample offset plus its own group
//     delay), so the delay goes to MAIN;
//   - a 2nd-order skirt against a 4th-order one is not N*90 degrees apart, so
//     the intercept sits more than 90 degrees from the LR-4 line and the
//     opposite-polarity sentence is shown.
// This is a BEHAVIOUR LOCK on those structural facts, not an accuracy claim:
// the numbers are not asserted.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "view/AlignPaneView.h"

#include <juce_core/juce_core.h>

#include <cstdint>

namespace {

using Access = MainComponentTestAccess;

template <typename Predicate>
bool tickUntil(MainComponent& c, rta::view::AlignPaneView& pane, Predicate&& done, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        Access::timerTickForTest(c);
        pane.tickForTest();
        if (done()) return true;
        juce::Thread::sleep(5);
    }
    Access::timerTickForTest(c);
    pane.tickForTest();
    return done();
}

}  // namespace

TEST_CASE("align with the shipped SYNTHETIC defaults: what the readout says (behaviour lock)",
          "[main_component_align_defaults]") {
    MainComponent c;
    c.setSyntheticMode(true);  // NOT restartSyntheticForTest: the shipped paths
    Access::waitForResyncForTest(c);  // the resync gate is a startup window, not under test
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto* pane = dynamic_cast<rta::view::AlignPaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
    REQUIRE(pane != nullptr);
    pane->highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);   // MAIN is high-pass
    pane->topologyComboForTest().setSelectedId(2, juce::sendNotificationSync);   // LR-4
    pane->inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);  // NOT INVERTED
    pane->seedComboForTest().setSelectedId(1, juce::sendNotificationSync);       // NO SEED

    REQUIRE(tickUntil(c, *pane, [&] { return pane->measureButtonForTest().isEnabled(); }, 20000));
    pane->measureButtonForTest().onClick();
    auto& runner = Access::alignForTest(c).runnerForTest();
    REQUIRE(runner.busy());
    REQUIRE(tickUntil(c, *pane, [&] { return !runner.busy(); }, 40000));
    REQUIRE(runner.verdict().has_value());
    const auto& verdict = *runner.verdict();
    INFO("status: " << runner.status() << " readout: " << pane->readoutForTest());
    REQUIRE(verdict.fitRefusal == rta::dsp::CrossoverRefusal::None);

    // The crossing lies between the band-pass's 40 Hz corner and the low-pass's 100 Hz corner.
    WARN("defaults: crossover " << verdict.crossoverHz << " Hz, tau " << verdict.tauSeconds * 1e3
                                << " ms, delta " << verdict.deltaRadians * 180.0 / 3.14159265358979 << " deg");
    CHECK(verdict.crossoverHz > 40.0);
    CHECK(verdict.crossoverHz < 100.0);
    CHECK(verdict.tauSeconds > 0.0);  // the low-pass side is later
    CHECK(std::abs(verdict.deltaRadians) > 3.14159265358979 / 2.0);

    // The delay line needs the operator's coarse offset first; type the
    // low-pass offset the plan's operator would (the 96-sample difference).
    pane->offsetEditorForTest().setText("2.0", juce::dontSendNotification);
    pane->offsetButtonForTest().onClick();
    const juce::String readout = pane->readoutForTest();
    CHECK(readout.contains("delay MAIN by "));
    CHECK(readout.contains("OPPOSITE polarity"));
    CHECK(readout.contains("crossover "));
}
