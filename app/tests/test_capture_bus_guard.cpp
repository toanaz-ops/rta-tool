// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests.
//
// Structural guards for the CaptureBus / AnalysisThread seam, written after
// audit prj010-fable-audit-20261021 (high): `CaptureBus::prepare()` called
// `RingBuffer::reset()` on the device or message thread while AnalysisThread
// was draining. `reset()` writes the consumer's read index, so a reset landing
// inside the consumer's `discard()` could leave write=0, read=R+hop and make
// `availableToRead()` wrap to ~2^64: the analysis thread never leaves its drain
// loop. The race is nanoseconds wide and cannot be reproduced by a unit test,
// so the shape that makes it impossible is pinned by scanning the code.
//
// SCOPE, so nobody credits this file with more than it proves: it checks which
// calls appear inside three named function bodies of comment-stripped source.
// It does not prove the absence of the race anywhere else, and a `reset` hidden
// behind a helper called from prepare() is invisible to it -- the behavioural
// half (test_capture_bus_epoch_drain.cpp, tests_juce) covers the consumer side.
#include "CodeLines.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace {

std::filesystem::path repoFile(const char* relative) {
    return std::filesystem::path(RTA_REPO_ROOT) / relative;
}

// Body of the first function whose lowercased, comment-stripped signature
// starts with `signature`: from the first '{' after it to its matching '}'.
// String literals are already emptied by codeText, so a brace inside one cannot
// unbalance the count.
std::string bodyOf(const std::string& code, const std::string& signature) {
    const auto at = code.find(signature);
    REQUIRE(at != std::string::npos);
    const auto open = code.find('{', at);
    REQUIRE(open != std::string::npos);
    int depth = 0;
    for (std::size_t i = open; i < code.size(); ++i) {
        if (code[i] == '{') ++depth;
        if (code[i] == '}' && --depth == 0) return code.substr(open, i - open + 1);
    }
    FAIL("unbalanced braces after " << signature);
    return {};
}

}  // namespace

TEST_CASE("G1 CaptureBus::prepare never resets a ring", "[capture_bus_guard]") {
    const std::string code =
        rta::test::codeText(repoFile("platform/types/src/CaptureBus.cpp"));
    const std::string body = bodyOf(code, "void capturebus::prepare(");

    INFO("prepare() body: " << body);
    CHECK(body.find("reset(") == std::string::npos);
    // The only legitimate way for the producer side to empty a ring is not to:
    // discard() moves the read index too, so it is just as forbidden here.
    CHECK(body.find("discard(") == std::string::npos);
}

TEST_CASE("G2 the consumer services the bus resync every tick", "[capture_bus_guard]") {
    const std::string code =
        rta::test::codeText(repoFile("app/src/measure/AnalysisThreadLoop.cpp"));
    const std::string body = bodyOf(code, "void analysisthread::runbody(");

    // Without this call the previous session's samples stay in the rings and,
    // the producer gate never being lowered, no new sample ever lands.
    INFO("runBody() body: " << body);
    const auto service = body.find("bus_.serviceresyncasconsumer(");
    REQUIRE(service != std::string::npos);
    // And the rings are not READ while the gate is up: they still hold the
    // previous session, which would splice into the fresh Analysers.
    const auto pendingCheck = body.find("bus_.resyncpending(");
    const auto drain = body.find("drain(");
    REQUIRE(pendingCheck != std::string::npos);
    REQUIRE(drain != std::string::npos);
    CHECK(service < pendingCheck);
    CHECK(pendingCheck < drain);
}

TEST_CASE("G4 the consumer lowers the gate only after the drain", "[capture_bus_guard]") {
    const std::string code =
        rta::test::codeText(repoFile("platform/types/src/CaptureBus.cpp"));
    const std::string body = bodyOf(code, "bool capturebus::serviceresyncasconsumer(");

    // Lowered first, the producer writes into rings still being emptied -- the
    // misalignment the gate exists to prevent. One thread cannot observe the
    // order at run time, so the order is pinned here.
    const auto drain = body.find("drainasconsumer(");
    const auto lower = body.find("resyncpending_.store(false");
    INFO("serviceResyncAsConsumer() body: " << body);
    REQUIRE(drain != std::string::npos);
    REQUIRE(lower != std::string::npos);
    CHECK(drain < lower);
}

TEST_CASE("G5 the producer honours the gate before writing any ring", "[capture_bus_guard]") {
    const std::string code =
        rta::test::codeText(repoFile("platform/types/src/CaptureBus.cpp"));
    const std::string body = bodyOf(code, "void capturebus::pushfromcallback(");

    const auto gate = body.find("resyncpending_.load(");
    const auto write = body.find("->write(");
    INFO("pushFromCallback() body: " << body);
    REQUIRE(gate != std::string::npos);
    REQUIRE(write != std::string::npos);
    CHECK(gate < write);
}

TEST_CASE("G3 drainPaired takes its hop budget from pairedHopCount", "[capture_bus_guard]") {
    const std::string code =
        rta::test::codeText(repoFile("app/src/measure/AnalysisThread.cpp"));
    const std::string body =
        bodyOf(code, "void analysisthread::drainpaired(");

    // The arithmetic test_paired_drain.cpp checks must be the arithmetic that
    // ships: a hand-rolled min() here would leave that test guarding nothing.
    INFO("drainPaired() body: " << body);
    CHECK(body.find("pairedhopcount(") != std::string::npos);
    CHECK(body.find("std::min(") == std::string::npos);
    // ...and it must be handed EVERY ring's availability, not a prefix of
    // them: behaviourally a short group is hidden by the peek-all-before-
    // discard guard further down, so no live test can see the difference.
    CHECK(body.find("available.data(), availablecount)") != std::string::npos);
}
