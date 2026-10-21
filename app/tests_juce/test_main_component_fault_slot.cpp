// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Audit prj010-fable-audit-20261021 (medium), the READING half:
// AnalysisThread::lastFault() had no production reader, so an exception that
// killed the analysis thread froze the display on its last snapshot with no
// message. DevicePanel's existing fault slot now shows it. This builds the real
// MainComponent, makes its real analysis thread throw, and reads back what the
// real device panel would paint.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <string>

TEST_CASE("the device panel shows an analysis-thread fault", "[main_component_fault_slot]") {
    MainComponent component;
    CHECK(MainComponentTestAccess::devicePanelTypedForTest(component).faultLineTextForTest().empty());

    MainComponentTestAccess::analysisThread(component).throwOnNextTickForTest();

    const auto deadline = juce::Time::getMillisecondCounter() + 2000u;
    while (MainComponentTestAccess::analysisThread(component).lastFault().sequence == 0 &&
           juce::Time::getMillisecondCounter() < deadline) {
        juce::Thread::sleep(5);
    }
    REQUIRE(MainComponentTestAccess::analysisThread(component).lastFault().sequence == 1);

    // The operator reads this string in the fault slot: it must name the
    // analysis thread (not read as a device problem) and carry the reason.
    const std::string shown = MainComponentTestAccess::devicePanelTypedForTest(component).faultLineTextForTest();
    CHECK(shown == "ANALYSIS STOPPED: injected analysis fault");
}

// LOW from the same audit's follow-up: CaptureBus::resyncAckTimeouts() had no
// reader. A drain that went ahead on the timeout may leave channels misaligned
// for the first block after a device change, and nothing said so.
TEST_CASE("the device panel shows an unacknowledged capture resync", "[main_component_fault_slot]") {
    MainComponent component;
    auto& panel = MainComponentTestAccess::devicePanelTypedForTest(component);
    CHECK(panel.faultLineTextForTest().empty());

    MainComponentTestAccess::forceResyncAckTimeoutForTest(component);

    const auto deadline = juce::Time::getMillisecondCounter() + 2000u;
    while (panel.faultLineTextForTest().empty() && juce::Time::getMillisecondCounter() < deadline) {
        juce::Thread::sleep(5);
    }
    const std::string shown = panel.faultLineTextForTest();
    INFO(shown);
    CHECK(shown.rfind("CAPTURE RESYNC UNACKNOWLEDGED x", 0) == 0);
}

// A recoverable rebuild fault must not read as the terminal "ANALYSIS STOPPED"
// and must disappear when the sweep completes.
TEST_CASE("the device panel shows a rebuild fault as recovering, then clears it", "[main_component_fault_slot]") {
    MainComponent component;
    auto& panel = MainComponentTestAccess::devicePanelTypedForTest(component);
    auto& thread = MainComponentTestAccess::analysisThread(component);
    const auto waitFor = [](const std::function<bool()>& c) {
        const auto deadline = juce::Time::getMillisecondCounter() + 3000u;
        while (!c() && juce::Time::getMillisecondCounter() < deadline) juce::Thread::sleep(5);
        return c();
    };

    thread.failRebuildsForTest(1000000);
    MainComponentTestAccess::prepareBusForTest(component, 44100.0, 2);  // epoch change: a sweep starts and stalls
    REQUIRE(waitFor([&] { return !panel.faultLineTextForTest().empty(); }));
    const std::string shown = panel.faultLineTextForTest();
    INFO(shown);
    CHECK(shown.rfind("ANALYSIS RECOVERING: ", 0) == 0);

    thread.failRebuildsForTest(0);  // memory is back: the sweep completes
    CHECK(waitFor([&] { return panel.faultLineTextForTest().empty(); }));
}
