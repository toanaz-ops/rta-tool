// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/TraceLibrary.h"

#include <catch2/catch_test_macros.hpp>

using namespace rta::trace;

namespace {
Trace makeTrace(std::string id) {
    CaptureMeta m;
    m.id = std::move(id);
    m.sampleRate = 48000.0;
    m.fftSize = 8;
    auto t = Trace::make(m, std::vector<float>(5, -20.0f));
    return std::move(*t);
}
}  // namespace

TEST_CASE("every mutation advances the revision", "[library]") {
    TraceLibrary lib;
    const auto start = lib.revision();

    lib.add(makeTrace("a"), "A", "left");
    const auto afterAdd = lib.revision();
    CHECK(afterAdd > start);

    CHECK(lib.rename("a", "A renamed"));
    CHECK(lib.revision() > afterAdd);
    const auto afterRename = lib.revision();

    CHECK(lib.setGroup("a", "right"));
    CHECK(lib.revision() > afterRename);
}

TEST_CASE("a call that changes nothing does NOT advance the revision", "[library]") {
    // The gate in Task 6 rebuilds a cached image whenever this moves. Bumping it
    // for a no-op would rebuild the whole layer on every idle UI tick.
    TraceLibrary lib;
    lib.add(makeTrace("a"), "A", "left");
    const auto before = lib.revision();

    CHECK(lib.setVisible("a", true));   // already visible
    CHECK(lib.rename("a", "A"));        // same name
    CHECK(lib.revision() == before);
}

TEST_CASE("an unknown id is refused and changes nothing", "[library]") {
    TraceLibrary lib;
    lib.add(makeTrace("a"), "A", "left");
    const auto before = lib.revision();

    CHECK_FALSE(lib.rename("nope", "X"));
    CHECK_FALSE(lib.setVisible("nope", false));
    CHECK(lib.revision() == before);
}

TEST_CASE("solo hides everything else and is one revision step", "[library]") {
    TraceLibrary lib;
    lib.add(makeTrace("a"), "A", "g");
    lib.add(makeTrace("b"), "B", "g");
    lib.add(makeTrace("c"), "C", "g");
    const auto before = lib.revision();

    lib.soloOnly("b");
    CHECK(lib.revision() == before + 1);
    CHECK_FALSE(lib.entry("a")->visible);
    CHECK(lib.entry("b")->visible);
    CHECK_FALSE(lib.entry("c")->visible);
}

TEST_CASE("library state is separate from capture metadata", "[library]") {
    TraceLibrary lib;
    lib.add(makeTrace("a"), "A", "left");
    REQUIRE(lib.rename("a", "New name"));
    // Renaming organises; it must not touch what the measurement was.
    CHECK(lib.trace("a")->meta().id == "a");
    CHECK(lib.entry("a")->name == "New name");
}
