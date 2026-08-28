// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/SessionCodec.h"

#include <catch2/catch_test_macros.hpp>

using namespace rta::trace;

namespace {
CaptureMeta sampleMeta() {
    CaptureMeta m;
    m.id = "abc-123";
    m.capturedAtUnixMs = 1756400000000;
    m.deviceName = "Scarlett 2i2";
    m.sampleRate = 48000.0;
    m.fftSize = 8;
    m.window = "hann";
    m.averagingType = "fifo";
    m.averagingDepth = 16;
    m.effectiveAverages = 9.5;
    m.appliedDelaySamples = 137;
    m.calibrationOffsetDb = 94.0f;
    m.calibrationUnit = LevelUnit::DbSpl;
    return m;
}
}  // namespace

TEST_CASE("an index round-trips every metadata item", "[codec]") {
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "FOH left", "positions", 2, false});

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);

    REQUIRE(back.captures.size() == 1u);
    const auto& m = back.captures.front();
    CHECK(m.id == "abc-123");
    CHECK(m.capturedAtUnixMs == 1756400000000);
    CHECK(m.deviceName == "Scarlett 2i2");
    CHECK(m.sampleRate == 48000.0);
    CHECK(m.fftSize == 8);
    CHECK(m.averagingDepth == 16);
    CHECK(m.effectiveAverages == 9.5);
    CHECK(m.appliedDelaySamples == 137);
    CHECK(m.calibrationOffsetDb == 94.0f);
    // The one that turns a reload into a silent 94 dB lie if it is dropped.
    CHECK(m.calibrationUnit == LevelUnit::DbSpl);

    REQUIRE(back.entries.size() == 1u);
    CHECK(back.entries.front().name == "FOH left");
    CHECK(back.entries.front().group == "positions");
    CHECK(back.entries.front().shadeIndex == 2);
    CHECK_FALSE(back.entries.front().visible);
}

TEST_CASE("a newer schema is refused, never partly read", "[codec]") {
    SessionDocument doc;
    doc.schemaVersion = kSchemaVersion + 1;
    SessionDocument back;
    CHECK(decodeIndex(encodeIndex(doc), back) == DecodeStatus::NewerSchema);
    CHECK(back.captures.empty());
}

TEST_CASE("a name may contain = and newlines", "[codec]") {
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "gain = +3\nrow two", "g", 0, true});

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);
    CHECK(back.entries.front().name == "gain = +3\nrow two");
}

TEST_CASE("garbage decodes to Malformed, not to a half-read session", "[codec]") {
    SessionDocument back;
    CHECK(decodeIndex("not an index at all", back) == DecodeStatus::Malformed);
    CHECK(decodeIndex("", back) == DecodeStatus::Malformed);
}

TEST_CASE("a blob round-trips presence, not just numbers", "[codec]") {
    auto t = Trace::make(sampleMeta(), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f});
    REQUIRE(t.has_value());
    REQUIRE(t->setCoherence(std::vector<float>{0.1f, 0.2f, 0.3f, 0.4f, 0.5f}));

    std::optional<Trace> back;
    REQUIRE(decodeTraceBlob(encodeTraceBlob(*t), sampleMeta(), back) == DecodeStatus::Ok);
    REQUIRE(back.has_value());
    CHECK(back->has(Field::Magnitude));
    CHECK(back->has(Field::Coherence));
    // Absent on the way in must be absent on the way out -- not zeros.
    CHECK_FALSE(back->has(Field::Phase));
    CHECK(back->field(Field::Coherence)[4] == 0.5f);
}

TEST_CASE("a blob whose id does not match its metadata is refused", "[codec]") {
    auto t = Trace::make(sampleMeta(), std::vector<float>(5, 0.0f));
    REQUIRE(t.has_value());
    CaptureMeta other = sampleMeta();
    other.id = "different-id";

    std::optional<Trace> back;
    CHECK(decodeTraceBlob(encodeTraceBlob(*t), other, back) == DecodeStatus::Malformed);
    CHECK_FALSE(back.has_value());
}
