// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Record §9, task B6, §11 T13: schema 3 adds [tf] and [average] -- editable
// configuration, never touching CaptureMeta's immutable history -- and
// [routing], whose `bound` fact is computed at decode time against the
// CURRENT device, never read from the file.

#include "trace/SessionCodec.h"

#include <catch2/catch_test_macros.hpp>

using namespace rta::trace;

TEST_CASE("A schema-3 document round-trips every [tf] and [average] field", "[presets]") {
    SessionDocument doc;

    TransferFunctionSpec a;
    a.name = "FOH=left\nmain";  // '=' and a newline, both must survive escaping
    a.measurementChannel = 3;
    a.referenceChannel = 0;
    a.delaySamples = 137;
    // Needs full to_chars precision to round-trip exactly.
    a.trimDb = 1.0 / 3.0;
    a.polarityInverted = true;
    a.memberOfAverage = true;
    a.averagingMode = AveragingMode::Pinned;
    a.fifoDepth = 24;
    doc.transferFunctions.push_back(a);

    TransferFunctionSpec b;
    b.name = "FOH right";
    b.measurementChannel = 4;
    b.referenceChannel = 0;
    b.delaySamples = 0;
    b.trimDb = -3.0;
    b.polarityInverted = false;
    b.memberOfAverage = true;
    b.averagingMode = AveragingMode::Global;
    b.fifoDepth = 16;
    doc.transferFunctions.push_back(b);

    AverageSpec average;
    average.mode = AverageModeName::Power;
    average.members = {a.name, b.name};
    doc.average = average;

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);

    REQUIRE(back.transferFunctions.size() == 2);
    const auto& backA = back.transferFunctions[0];
    CHECK(backA.name == a.name);
    CHECK(backA.measurementChannel == a.measurementChannel);
    CHECK(backA.referenceChannel == a.referenceChannel);
    CHECK(backA.delaySamples == a.delaySamples);
    CHECK(backA.trimDb == a.trimDb);  // exact: std::to_chars round-trips bit-for-bit
    CHECK(backA.polarityInverted == a.polarityInverted);
    CHECK(backA.memberOfAverage == a.memberOfAverage);
    CHECK(backA.averagingMode == AveragingMode::Pinned);
    CHECK(backA.fifoDepth == a.fifoDepth);

    const auto& backB = back.transferFunctions[1];
    CHECK(backB.name == b.name);
    CHECK(backB.averagingMode == AveragingMode::Global);
    CHECK(backB.trimDb == b.trimDb);

    REQUIRE(back.average.has_value());
    CHECK(back.average->mode == AverageModeName::Power);
    REQUIRE(back.average->members.size() == 2);
    CHECK(back.average->members[0] == a.name);
    CHECK(back.average->members[1] == b.name);
}

TEST_CASE("A schema-2 document decodes Ok with zero transfer functions and zero average members",
          "[presets]") {
    // A hand-written schema-2 document -- no [tf], no [average], exactly
    // what a session saved before this task existed looks like.
    const std::string schema2Text =
        "schema=2\n"
        "[pane]\n"
        "view=rta\n"
        "weight=1\n";

    SessionDocument doc;
    REQUIRE(decodeIndex(schema2Text, doc) == DecodeStatus::Ok);
    CHECK(doc.schemaVersion == 2);
    CHECK(doc.transferFunctions.empty());
    CHECK_FALSE(doc.average.has_value());
    CHECK_FALSE(doc.routing.has_value());
    REQUIRE(doc.panes.size() == 1);
    CHECK(doc.panes[0].view == "rta");
}

TEST_CASE("Routing binds only when the current device matches BOTH name and channel count",
          "[presets]") {
    SessionDocument doc;
    RoutingSpec routing;
    routing.deviceName = "Scarlett 2i2";
    routing.inputChannelCount = 2;
    doc.routing = routing;
    const std::string text = encodeIndex(doc);

    SECTION("no current device at all: unbound, the conservative default") {
        SessionDocument back;
        REQUIRE(decodeIndex(text, back) == DecodeStatus::Ok);
        REQUIRE(back.routing.has_value());
        CHECK_FALSE(back.routing->bound);
        CHECK(back.routing->deviceName == "Scarlett 2i2");
        CHECK(back.routing->inputChannelCount == 2);
    }

    SECTION("same name, different channel count: unbound") {
        const CurrentDevice current{"Scarlett 2i2", 4};
        SessionDocument back;
        REQUIRE(decodeIndex(text, back, &current) == DecodeStatus::Ok);
        REQUIRE(back.routing.has_value());
        CHECK_FALSE(back.routing->bound);
    }

    SECTION("different name, same channel count: unbound") {
        const CurrentDevice current{"Some Other Interface", 2};
        SessionDocument back;
        REQUIRE(decodeIndex(text, back, &current) == DecodeStatus::Ok);
        REQUIRE(back.routing.has_value());
        CHECK_FALSE(back.routing->bound);
    }

    SECTION("both match: bound") {
        const CurrentDevice current{"Scarlett 2i2", 2};
        SessionDocument back;
        REQUIRE(decodeIndex(text, back, &current) == DecodeStatus::Ok);
        REQUIRE(back.routing.has_value());
        CHECK(back.routing->bound);
    }
}

TEST_CASE("A schema-4 document still returns NewerSchema", "[presets]") {
    SessionDocument doc;
    REQUIRE(decodeIndex("schema=4\n", doc) == DecodeStatus::NewerSchema);
}
