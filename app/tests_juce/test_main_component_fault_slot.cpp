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
