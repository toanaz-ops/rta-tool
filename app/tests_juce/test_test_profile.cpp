// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. DEVCAVE_TEST_PROFILE isolation
// (app/src/TestProfile.h): unset -> real paths, set -> under the profile,
// relative -> refused and never a real-data path.
#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include "TestProfile.h"

namespace tp = rta::app::testprofile;

namespace {
const juce::File realDocs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
const juce::File realAppData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
const juce::File tempRoot = juce::File::getSpecialLocation(juce::File::tempDirectory);
}  // namespace

TEST_CASE("test profile: unset or empty keeps the real paths", "[test_profile]") {
    for (const char* value : {"", "   "}) {
        const auto r = tp::resolve(value);
        CHECK_FALSE(r.active);
        CHECK(r.valid);
        CHECK(tp::documentsDirectory(r) == realDocs);
        CHECK(tp::appDataDirectory(r) == realAppData.getChildFile("RTA Tool"));
        CHECK(tp::windowTitle(r, "RTA Tool") == "RTA Tool");
    }
}

TEST_CASE("test profile: set and absolute redirects under the profile", "[test_profile]") {
    const auto profile = tempRoot.getChildFile("rta-profile-under-test");
    const auto r = tp::resolve(profile.getFullPathName());
    REQUIRE(r.active);
    REQUIRE(r.valid);
    CHECK(tp::documentsDirectory(r) == profile.getChildFile("Documents"));
    CHECK(tp::appDataDirectory(r) == profile.getChildFile("AppData").getChildFile("RTA Tool"));
    CHECK(tp::windowTitle(r, "RTA Tool") == "RTA Tool [TEST PROFILE]");
    // Nothing leaks to the real locations.
    CHECK_FALSE(tp::documentsDirectory(r).isAChildOf(realDocs));
    CHECK_FALSE(tp::appDataDirectory(r).isAChildOf(realAppData));
}

TEST_CASE("test profile: a relative value is rejected, never the real data", "[test_profile]") {
    for (const char* value : {"profile", "..\profile", "./x"}) {
        const auto r = tp::resolve(value);
        CHECK(r.active);
        CHECK_FALSE(r.valid);
        CHECK(r.message.contains("DEVCAVE_TEST_PROFILE"));
        CHECK(r.message.contains("absolute"));
        CHECK(tp::documentsDirectory(r).isAChildOf(tempRoot));
        CHECK(tp::appDataDirectory(r).isAChildOf(tempRoot));
        CHECK_FALSE(tp::documentsDirectory(r).isAChildOf(realDocs));
        CHECK(tp::windowTitle(r, "RTA Tool").endsWith("[TEST PROFILE]"));
    }
}

TEST_CASE("test profile: application name is scoped in test mode, unchanged in real mode", "[test_profile]") {
    // The name keys JUCE's single-instance lock: a test instance must not
    // share it with the owner's real instance.
    CHECK(tp::applicationName(tp::resolve(""), "RTA Tool") == "RTA Tool");
    const auto a = tp::applicationName(tp::resolve(tempRoot.getChildFile("pa").getFullPathName()), "RTA Tool");
    const auto b = tp::applicationName(tp::resolve(tempRoot.getChildFile("pb").getFullPathName()), "RTA Tool");
    CHECK(a != "RTA Tool");
    CHECK(a.startsWith("RTA Tool_testprofile_"));
    CHECK(a != b);
    CHECK(a == tp::applicationName(tp::resolve(tempRoot.getChildFile("pa").getFullPathName()), "RTA Tool"));
}
