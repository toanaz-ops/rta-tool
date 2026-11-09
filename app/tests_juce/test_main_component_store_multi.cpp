// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests_juce. HW-176 T4 (QA queue Z6): STORE freezes
// ONE trace per distinct storable kind the workspace shows. It used to branch
// on the first pane alone, so an [rta, transfer] layout froze one of the two
// curves on screen. Against a REAL MainComponent and live synthetic feed, same
// fixture shape as test_main_component_store.cpp.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Workspace.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <string>

namespace {

bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (predicate()) return true;
        juce::Thread::sleep(5);
    }
    return predicate();
}

// Same three conditions test_main_component_store.cpp waits for: a reference,
// the fixed-FFT transfer block and the MTW block all published.
void enableSyntheticWithReference(MainComponent& component) {
    component.setSyntheticMode(true);
    REQUIRE(waitUntil(
        [&] {
            const auto snapshot = MainComponentTestAccess::analysisThread(component).latest();
            return snapshot && snapshot->hasReference && snapshot->transfer.has_value() &&
                   snapshot->mtw.has_value();
        },
        15000));
}

std::string groupsStored(MainComponent& component, std::size_t from) {
    auto& library = MainComponentTestAccess::libraryForTest(component);
    std::string out;
    for (std::size_t i = from; i < library.entries().size(); ++i) {
        out += (out.empty() ? "" : ",") + library.entries()[i].group;
    }
    return out;
}

}  // namespace

TEST_CASE("STORE on [rta, transfer] freezes one trace of EACH kind", "[main_component_store_multi]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    MainComponentTestAccess::restoreWorkspaceForTest(
        component, {rta::trace::PaneSpec{"rta"}, rta::trace::PaneSpec{"transfer"}});

    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto before = library.entries().size();
    MainComponentTestAccess::storeClickedForTest(component);

    INFO(MainComponentTestAccess::storeReadoutForTest(component).toStdString());
    REQUIRE(library.entries().size() == before + 2);
    // Both groups present, RTA first (pane order), and exactly the TRANSFER one carries phase.
    CHECK(groupsStored(component, before) == "RTA,TRANSFER");
    CHECK_FALSE(library.trace(library.entries()[before].traceId)->has(rta::trace::Field::Phase));
    CHECK(library.trace(library.entries()[before + 1].traceId)->has(rta::trace::Field::Phase));
    // The readout counts what landed: two names, not one.
    const auto readout = MainComponentTestAccess::storeReadoutForTest(component);
    CHECK(readout.contains("RTA @"));
    CHECK(readout.contains("TRANSFER @"));
}

TEST_CASE("STORE on two TRANSFER panes freezes exactly one trace", "[main_component_store_multi]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    MainComponentTestAccess::restoreWorkspaceForTest(
        component, {rta::trace::PaneSpec{"transfer"}, rta::trace::PaneSpec{"transfer"}});

    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto before = library.entries().size();
    MainComponentTestAccess::storeClickedForTest(component);
    CHECK(library.entries().size() == before + 1);
    CHECK(groupsStored(component, before) == "TRANSFER");
}

TEST_CASE("STORE on [xover, transfer] freezes the TRANSFER trace and the button is enabled (Z6)",
         "[main_component_store_multi]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    MainComponentTestAccess::restoreWorkspaceForTest(
        component, {rta::trace::PaneSpec{"xover"}, rta::trace::PaneSpec{"transfer"}});
    REQUIRE(MainComponentTestAccess::storeButtonEnabledForTest(component));

    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto before = library.entries().size();
    MainComponentTestAccess::storeClickedForTest(component);
    CHECK(library.entries().size() == before + 1);
    CHECK(groupsStored(component, before) == "TRANSFER");
}

TEST_CASE("STORE on a layout with no RTA or TRANSFER pane stores nothing and says so",
         "[main_component_store_multi]") {
    MainComponent component;
    enableSyntheticWithReference(component);
    MainComponentTestAccess::restoreWorkspaceForTest(
        component, {rta::trace::PaneSpec{"xover"}, rta::trace::PaneSpec{"spl"}});

    auto& library = MainComponentTestAccess::libraryForTest(component);
    const auto before = library.entries().size();
    MainComponentTestAccess::storeClickedForTest(component);
    CHECK(library.entries().size() == before);
    CHECK(MainComponentTestAccess::storeReadoutForTest(component).contains("nothing to freeze"));
}
