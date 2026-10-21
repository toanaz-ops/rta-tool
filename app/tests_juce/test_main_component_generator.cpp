// SPDX-License-Identifier: AGPL-3.0-or-later
// H7 tasks T3/T4 (docs/plans/2026-10-21-h7-generator-output-plan.md, wave A2):
// the rail's GEN / - / + controller and its mutual exclusion with every
// measurement, against a REAL MainComponent in SYNTHETIC mode -- the engine
// loopback is what renders the generator, so "arms the engine" is read off the
// real engine, not a stand-in. The 2 Hz timer is not pumped offscreen;
// `tickUntil` drives `timerCallback` by hand, bounded.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "view/AlignPaneView.h"

#include <juce_core/juce_core.h>

#include <cstdint>

namespace {

using Access = MainComponentTestAccess;
using rta::measure::GeneratorState;
constexpr const char* kTag = "[main_component_generator]";

template <typename Predicate>
bool tickUntil(MainComponent& c, Predicate&& done, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        Access::timerTickForTest(c);
        if (c.currentPaneView() == rta::view::PaneView::Align) {
            auto* pane = dynamic_cast<rta::view::AlignPaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
            if (pane != nullptr) pane->tickForTest();
        }
        if (done()) return true;
        juce::Thread::sleep(5);
    }
    Access::timerTickForTest(c);
    return done();
}

MainComponentGenerator& genOf(MainComponent& c) { return Access::generatorForTest(c); }
MainComponentSweep& sweepOf(MainComponent& c) { return Access::sweepForTest(c); }

void startSynthetic(MainComponent& c) {
    c.setSyntheticMode(true);
    Access::waitForResyncForTest(c);
    sweepOf(c).setDurationsForTest(2.0, 0.5);
}

/// GEN pressed and, if it was Swapping, settled back to Playing.
void playing(MainComponent& c) {
    genOf(c).genClicked();
    REQUIRE(genOf(c).runnerForTest().state() == GeneratorState::Playing);
}

bool settled(MainComponent& c) { return genOf(c).runnerForTest().state() == GeneratorState::Playing; }

}  // namespace

TEST_CASE("generator: GEN arms the engine on out 1 and the readout says what plays", kTag) {
    MainComponent c;
    startSynthetic(c);
    CHECK(genOf(c).readoutForTest() == "GEN: stopped -- pink -30.0 dBFS RMS, out 1");  // every launch
    CHECK(genOf(c).genButtonTextForTest() == "GEN");
    CHECK(Access::outputSourceQuiescentForTest(c));

    playing(c);
    CHECK(genOf(c).readoutForTest() == "GEN: playing pink -30.0 dBFS RMS on out 1");
    CHECK(genOf(c).genButtonTextForTest() == "STOP");
    CHECK_FALSE(Access::outputSourceQuiescentForTest(c));  // the real engine is armed
    CHECK(rta::platform::OutputRole::Routed == Access::outputRoleForTest(c, 0));
    CHECK(rta::platform::OutputRole::Routed != Access::outputRoleForTest(c, 1));
}

TEST_CASE("generator: + stops at -12.0 (ceiling) and - stops at -60.0", kTag) {
    MainComponent c;
    startSynthetic(c);
    playing(c);
    for (int i = 0; i < 20; ++i) genOf(c).stepClicked(+1);
    CHECK(genOf(c).readoutForTest().contains("-12.0 dBFS RMS on out 1 (ceiling)"));
    CHECK(genOf(c).runnerForTest().settings().levelDb == -12.0);
    REQUIRE(tickUntil(c, [&] { return settled(c); }, 3000));  // the swap landed

    for (int i = 0; i < 60; ++i) genOf(c).stepClicked(-1);
    CHECK(genOf(c).readoutForTest().contains("-60.0 dBFS RMS"));
    CHECK_FALSE(genOf(c).readoutForTest().contains("(ceiling)"));
    CHECK(genOf(c).runnerForTest().settings().levelDb == -60.0);
    REQUIRE(tickUntil(c, [&] { return settled(c); }, 3000));
}

TEST_CASE("generator: the fast poll runs only while the runner waits on the audio callback", kTag) {
    MainComponent c;
    startSynthetic(c);
    CHECK_FALSE(genOf(c).fastPollRunningForTest());
    playing(c);
    CHECK_FALSE(genOf(c).fastPollRunningForTest());  // Playing needs only the 2 Hz tick

    genOf(c).stepClicked(+1);
    REQUIRE(genOf(c).runnerForTest().state() == GeneratorState::Swapping);
    CHECK(genOf(c).fastPollRunningForTest());
    REQUIRE(tickUntil(c, [&] { return settled(c); }, 3000));
    CHECK_FALSE(genOf(c).fastPollRunningForTest());

    genOf(c).genClicked();  // STOP
    REQUIRE(genOf(c).runnerForTest().state() == GeneratorState::Stopping);
    CHECK(genOf(c).fastPollRunningForTest());
    REQUIRE(tickUntil(c, [&] { return genOf(c).runnerForTest().state() == GeneratorState::Idle; }, 3000));
    CHECK_FALSE(genOf(c).fastPollRunningForTest());
    CHECK(genOf(c).readoutForTest() == "GEN: stopped -- pink -29.0 dBFS RMS, out 1");
}

TEST_CASE("generator: GEN refuses in LIVE with no device running", kTag) {
    MainComponent c;  // LIVE, nothing opened
    genOf(c).genClicked();
    CHECK(genOf(c).runnerForTest().state() == GeneratorState::Idle);
    CHECK(genOf(c).readoutForTest().contains("refused (no audio device running)"));
    CHECK(Access::outputSourceQuiescentForTest(c));
}

TEST_CASE("generator: while GEN plays every measurement refuses naming GEN, and captureBusy is unchanged", kTag) {
    MainComponent c;
    startSynthetic(c);
    playing(c);
    CHECK(Access::outputBusyForTest(c));
    CHECK_FALSE(Access::captureBusyForTest(c));  // GEN joins outputBusy(), not captureBusy()

    sweepOf(c).clicked();
    CHECK(sweepOf(c).stateForTest() == MainComponentSweep::State::Idle);
    CHECK(sweepOf(c).readoutForTest() == "sweep: refused -- GEN is playing; press STOP");

    Access::locateClickedForTest(c);
    CHECK_FALSE(Access::locateActiveForTest(c));
    CHECK(Access::delayReadoutForTest(c) == "delay: refused -- GEN is playing; press STOP");

    Access::calibrationStartClickedForTest(c);
    CHECK_FALSE(Access::calibrationArmedForTest(c));
    CHECK(Access::calibrationReadoutForTest(c).contains("GEN is playing"));
    Access::calibrationEndClickedForTest(c);
    CHECK_FALSE(Access::calibrationArmedForTest(c));
    CHECK(Access::calibrationReadoutForTest(c).contains("GEN is playing"));

    CHECK(genOf(c).runnerForTest().state() == GeneratorState::Playing);  // none of them disturbed it
}

TEST_CASE("generator: ALIGN MEASURE refuses naming GEN", kTag) {
    MainComponent c;
    startSynthetic(c);
    c.selectPaneView(rta::view::PaneSelectorButton::Align);
    auto& pane = *dynamic_cast<rta::view::AlignPaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
    pane.highPassComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane.topologyComboForTest().setSelectedId(2, juce::sendNotificationSync);
    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane.seedComboForTest().setSelectedId(1, juce::sendNotificationSync);
    REQUIRE(tickUntil(c, [&] { return pane.measureButtonForTest().isEnabled(); }, 20000));

    playing(c);
    Access::timerTickForTest(c);
    pane.tickForTest();
    CHECK_FALSE(pane.measureButtonForTest().isEnabled());
    CHECK(pane.blockerTextForTest().contains("GEN"));
    CHECK_FALSE(Access::alignForTest(c).runnerForTest().press());
    CHECK_FALSE(Access::alignForTest(c).runnerForTest().busy());
}

TEST_CASE("generator: a running SWEEP refuses GEN by name; after STOP the SWEEP arms", kTag) {
    MainComponent c;
    startSynthetic(c);
    sweepOf(c).clicked();
    REQUIRE(sweepOf(c).busy());
    genOf(c).genClicked();
    CHECK(genOf(c).runnerForTest().state() == GeneratorState::Idle);
    CHECK(genOf(c).readoutForTest().contains("refused (SWEEP is running)"));
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 30000));
    // The sweep's own tail may still be ramping out; GEN would say so and refuse.
    REQUIRE(tickUntil(c, [&] { return Access::outputSourceQuiescentForTest(c); }, 3000));

    playing(c);
    genOf(c).genClicked();  // STOP
    REQUIRE(tickUntil(c, [&] { return !genOf(c).active(); }, 3000));
    sweepOf(c).clicked();
    CHECK(sweepOf(c).busy());  // the output is free again
    REQUIRE(tickUntil(c, [&] { return !sweepOf(c).busy(); }, 30000));
}

TEST_CASE("generator: leaving SYNTHETIC stops GEN with a reason and it stays stopped on re-entry", kTag) {
    MainComponent c;
    startSynthetic(c);
    playing(c);
    c.setSyntheticMode(false);
    CHECK(genOf(c).runnerForTest().state() == GeneratorState::Idle);
    CHECK(genOf(c).readoutForTest().contains("input mode changed"));
    CHECK(Access::outputSourceQuiescentForTest(c));

    c.setSyntheticMode(true);
    Access::waitForResyncForTest(c);
    for (int i = 0; i < 20; ++i) {
        Access::timerTickForTest(c);
        juce::Thread::sleep(5);
    }
    CHECK(genOf(c).runnerForTest().state() == GeneratorState::Idle);  // no GEN press, no sound
    CHECK(Access::outputSourceQuiescentForTest(c));
}
