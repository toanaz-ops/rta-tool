// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests_juce. D7 (docs/HUMAN-QA-QUEUE.md, PR #43 r4
// item 14): `MainComponent::exportReportClicked()`'s try/catch "EXPORT
// FAILED" branch (MainComponentSpl.cpp) had no test at all -- nothing in
// this tree could make `buildReportPayload`/`utf8Path` throw through the
// public API. `MainComponentTestAccess::forceExportThrowForTest` is the
// seam this file exercises: it makes the function throw before doing any
// real work, so this test needs no SPL session, no device and no live
// snapshot.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"

TEST_CASE("EXPORT REPORT's catch branch reports a forced failure", "[main_component_export]") {
    MainComponent component;

    MainComponentTestAccess::forceExportThrowForTest(component, true);
    MainComponentTestAccess::exportReportClickedForTest(component);

    // The mutant this proves against (D7's own "drop the catch"): with the
    // catch removed, the forced `std::runtime_error` propagates out of
    // exportReportClicked() uncaught -- `JUCE_CATCH_UNHANDLED_EXCEPTIONS` is
    // 0 in this project (project CLAUDE.md's own build), so that is
    // `std::terminate`, not a caught exception this CHECK could see: the
    // test process itself aborts, which ctest reports as this test failing.
    const auto readout = MainComponentTestAccess::exportReportReadoutForTest(component);
    CHECK(readout.startsWith("EXPORT FAILED"));
    // fromUTF8, not mojibake (F4/D7's own fix round note on
    // `juce::String::fromUTF8(e.what())`): the forced message is plain
    // ASCII, so this also guards against a regression to the narrow
    // `juce::String(const char*)` constructor silently reappearing.
    CHECK(readout.contains("forced by MainComponentTestAccess::forceExportThrowForTest"));
}

TEST_CASE("EXPORT REPORT without a forced throw and with no session logged refuses normally",
         "[main_component_export]") {
    // Guards the seam itself: forceExportThrowForTest_ defaults to false, so
    // a plain click with nothing logged still takes the ORIGINAL "no
    // session" refusal, never the forced-throw path.
    MainComponent component;
    MainComponentTestAccess::exportReportClickedForTest(component);
    const auto readout = MainComponentTestAccess::exportReportReadoutForTest(component);
    CHECK(readout.startsWith("export: no SPL session logged yet"));
}
