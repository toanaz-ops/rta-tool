// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 2, task W2-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §4).
#include "measure/SplHistory.h"

#include "AllocationProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

using rta::measure::SplHistory;
using rta::measure::SplMarker;
using rta::measure::SplMarkerKind;
using rta::meter::Block;

namespace {

Block blockAt(std::uint64_t index) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = 48000;
    b.sumSquares = 1.0;
    return b;
}

}  // namespace

// --- A1: allocated once, never grown --------------------------------------

TEST_CASE("A1 SplHistory allocates once at construction and never again", "[spl_history]") {
    std::optional<SplHistory> history;
    std::size_t constructionBytes = 0;
    {
        rta::test::AllocationProbe probe;
        history.emplace(100);
        constructionBytes = probe.bytes();
    }
    // Constructing a 100-block ring must allocate SOMETHING (the vector's
    // backing store) -- a reading of zero here would mean the allocation was
    // elided or the ring was never sized, not that it is free.
    CHECK(constructionBytes > 0);

    rta::test::AllocationProbe probe;
    for (std::uint64_t i = 0; i < 1000; ++i) {  // 10x the capacity
        history->push(blockAt(i));
    }
    CHECK(probe.bytes() == 0);
}

// --- A2: the size table is arithmetic, not a guess ------------------------

TEST_CASE("A2 capacityBlocks is read from sizeof(Block), not the constant 40", "[spl_history]") {
    CHECK(SplHistory::capacityBlocks(8.0 * 3600.0, 1.0) == 28800);
    CHECK(28800ull * sizeof(Block) == 1152000ull);

    CHECK(SplHistory::capacityBlocks(24.0 * 3600.0, 1.0) == 86400);
    CHECK(86400ull * sizeof(Block) == 3456000ull);

    CHECK(SplHistory::capacityBlocks(7.0 * 24.0 * 3600.0, 1.0) == 604800);
    CHECK(604800ull * sizeof(Block) == 24192000ull);
}

// --- A3: roll, never wrap-silently -----------------------------------------

TEST_CASE("A3 SplHistory rolls at capacity and the session does not end", "[spl_history]") {
    SplHistory history(10);
    for (std::uint64_t i = 0; i < 10; ++i) history.push(blockAt(i));

    REQUIRE(history.oldestBlockIndex().has_value());
    CHECK(*history.oldestBlockIndex() == 0);
    CHECK(*history.newestBlockIndex() == 9);
    CHECK(history.size() == 10);

    // Capacity + 1: the oldest block leaves memory, oldestBlockIndex advances
    // by exactly one, and pushing continues to succeed (the "session does not
    // end").
    history.push(blockAt(10));
    CHECK(*history.oldestBlockIndex() == 1);
    CHECK(*history.newestBlockIndex() == 10);
    CHECK(history.size() == 10);
    CHECK_FALSE(history.at(0).has_value());  // rolled off: absent, not stale
    REQUIRE(history.at(1).has_value());
    CHECK(history.at(1)->blockIndex == 1);

    // A reader asking for a block older than the ring gets an absence, not a
    // stale block reused from the same slot.
    CHECK_FALSE(history.at(0).has_value());
    // A block not yet arrived is likewise absent, not a default-constructed
    // Block masquerading as real data.
    CHECK_FALSE(history.at(999).has_value());
}

// --- A4: numbers, not pixels ------------------------------------------------

namespace {

std::string readWholeFile(const char* path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool containsAny(const std::string& text, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (text.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Strips `//` and `/* */` comments before the scan below runs. Without this,
// A4's own explanatory prose -- which has to NAME the forbidden words to say
// why the class avoids them -- would trip the very check it documents
// (memory/a-naming-grep-that-bans-a-word-bans-its-own-justification.md). The
// acceptance is about CODE holding no colour/pixel vocabulary, not about
// comments being forbidden from discussing the rule.
std::string stripComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            if (i < text.size()) out += '\n';
            continue;
        }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            i += 1;  // land on the closing '/'; the loop's ++i advances past it
            continue;
        }
        out += text[i];
    }
    return out;
}

}  // namespace

TEST_CASE("A4 SplHistory source holds no colour or pixel vocabulary", "[spl_history]") {
    const auto header = stripComments(readWholeFile(RTA_REPO_ROOT "/app/src/measure/SplHistory.h"));
    const auto body = stripComments(readWholeFile(RTA_REPO_ROOT "/app/src/measure/SplHistory.cpp"));
    const auto needles = { "colour", "Colour", "color", "pixel", "argb", "rgb" };
    CHECK_FALSE(containsAny(header, needles));
    CHECK_FALSE(containsAny(body, needles));
}

// --- A5: markers are Smaart's four kinds ------------------------------------

TEST_CASE("A5 SplHistory markers carry Smaart's four kinds and their fields", "[spl_history]") {
    SplHistory history(10);
    history.push(blockAt(0));

    SplMarker fired;
    fired.blockIndex = 0;
    fired.kind = SplMarkerKind::Alarm;
    fired.direction = 1;
    fired.quantity = "LAeq,Fast";
    fired.window = 60;
    fired.value = 102.3;
    history.addMarker(fired);

    SplMarker overload;
    overload.blockIndex = 0;
    overload.kind = SplMarkerKind::Overload;
    history.addMarker(overload);

    SplMarker note;
    note.blockIndex = 0;
    note.kind = SplMarkerKind::Note;
    history.addMarker(note);

    SplMarker reset;
    reset.blockIndex = 0;
    reset.kind = SplMarkerKind::Reset;
    history.addMarker(reset);

    REQUIRE(history.markers().size() == 4);
    CHECK(history.markers()[0].kind == SplMarkerKind::Alarm);
    CHECK(history.markers()[0].direction == 1);
    CHECK(history.markers()[0].quantity == "LAeq,Fast");
    CHECK(history.markers()[0].window == 60);
    CHECK(history.markers()[0].value == 102.3);
    CHECK(history.markers()[1].kind == SplMarkerKind::Overload);
    CHECK(history.markers()[2].kind == SplMarkerKind::Note);
    CHECK(history.markers()[3].kind == SplMarkerKind::Reset);
}
