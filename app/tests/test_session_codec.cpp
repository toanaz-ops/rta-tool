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

// A recognisable, never-legitimately-produced sentinel: if a failing
// decodeIndex call clears or overwrites `out` instead of leaving it alone,
// this id disappears and the test catches it. A default-constructed
// SessionDocument cannot make that distinction -- "still empty" and
// "wrongly cleared to empty" look identical.
SessionDocument sentinelDocument() {
    SessionDocument doc;
    CaptureMeta m = sampleMeta();
    m.id = "SENTINEL";
    doc.captures.push_back(m);
    return doc;
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

TEST_CASE("a double that six decimals would truncate round-trips bit-exactly", "[codec]") {
    // 9.523809523809524 has no exact 6-decimal representation (it prints as
    // 9.523810 or reads back as 9.52381 through sprintf/strtod's "%f" path).
    // std::to_chars/from_chars round-trip the exact bit pattern instead.
    CaptureMeta m = sampleMeta();
    m.effectiveAverages = 9.523809523809524;
    SessionDocument doc;
    doc.captures.push_back(m);

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().effectiveAverages == 9.523809523809524);
}

TEST_CASE("a newer schema is refused, never partly read", "[codec]") {
    SessionDocument doc;
    doc.schemaVersion = kSchemaVersion + 1;
    SessionDocument back = sentinelDocument();
    CHECK(decodeIndex(encodeIndex(doc), back) == DecodeStatus::NewerSchema);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "SENTINEL");
}

TEST_CASE("a name may contain = and newlines", "[codec]") {
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "gain = +3\nrow two", "g", 0, true});

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);
    CHECK(back.entries.front().name == "gain = +3\nrow two");
}

TEST_CASE("a lone backslash round-trips", "[codec]") {
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "\\", "g", 0, true});

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);
    CHECK(back.entries.front().name == "\\");
}

TEST_CASE("a value ending in a backslash round-trips", "[codec]") {
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "trailing\\", "g", 0, true});

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);
    CHECK(back.entries.front().name == "trailing\\");
}

TEST_CASE("a literal backslash-n pair round-trips, distinct from a real newline", "[codec]") {
    // "esc\\ntest" in source is the 9-byte sequence e s c \ n t e s t -- a
    // literal backslash followed by the letter n, NOT the one-byte newline
    // 0x0A that "\n" in a C++ string literal would produce. escape() must
    // double the backslash without touching the unrelated 'n' that follows
    // it, and unescape() must undo exactly that, or this value would drift
    // into (or be confused with) an actual embedded newline.
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "esc\\ntest", "g", 0, true});

    SessionDocument back;
    REQUIRE(decodeIndex(encodeIndex(doc), back) == DecodeStatus::Ok);
    CHECK(back.entries.front().name == "esc\\ntest");
}

TEST_CASE("garbage decodes to Malformed, not to a half-read session", "[codec]") {
    SessionDocument back = sentinelDocument();
    CHECK(decodeIndex("not an index at all", back) == DecodeStatus::Malformed);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "SENTINEL");

    CHECK(decodeIndex("", back) == DecodeStatus::Malformed);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "SENTINEL");
}

TEST_CASE("a malformed numeric field after a good schema line is refused, never partly read", "[codec]") {
    SessionDocument back = sentinelDocument();
    CHECK(decodeIndex("schema=1\n[capture]\nfftSize=xyz\n", back) == DecodeStatus::Malformed);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "SENTINEL");

    // Trailing garbage after otherwise-valid digits must fail too -- "42xyz"
    // is not 42.
    CHECK(decodeIndex("schema=1\n[capture]\nsampleRate=48000xyz\n", back) == DecodeStatus::Malformed);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "SENTINEL");
}

TEST_CASE("an unrecognised calibrationUnit is refused, not silently read as dBFS", "[codec]") {
    SessionDocument back = sentinelDocument();
    CHECK(decodeIndex("schema=1\n[capture]\ncalibrationUnit=lux\n", back) == DecodeStatus::Malformed);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "SENTINEL");
}

TEST_CASE("a CRLF-terminated index decodes the same as an LF one", "[codec]") {
    SessionDocument doc;
    doc.captures.push_back(sampleMeta());
    doc.entries.push_back(LibraryEntry{"abc-123", "FOH left", "positions", 2, false});

    std::string lf = encodeIndex(doc);
    std::string crlf;
    crlf.reserve(lf.size() + 8);
    for (char c : lf) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    REQUIRE(crlf != lf);  // sanity: the test actually exercises CRLF, not LF.

    SessionDocument back;
    REQUIRE(decodeIndex(crlf, back) == DecodeStatus::Ok);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().calibrationUnit == LevelUnit::DbSpl);
    REQUIRE(back.entries.size() == 1u);
    CHECK(back.entries.front().name == "FOH left");
    CHECK_FALSE(back.entries.front().visible);
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

    // Pre-populate `back` with a real decode (same fftSize=8, so the same
    // 5-point magnitude length sampleMeta() requires) so a wrongly-cleared
    // `out` is distinguishable from a correctly-untouched one.
    std::optional<Trace> back = Trace::make(sampleMeta(), std::vector<float>(5, 9.f));
    REQUIRE(back.has_value());

    CHECK(decodeTraceBlob(encodeTraceBlob(*t), other, back) == DecodeStatus::Malformed);
    REQUIRE(back.has_value());
    CHECK(back->field(Field::Magnitude)[0] == 9.f);
}

TEST_CASE("a blob with trailing bytes after its last field is refused", "[codec]") {
    auto t = Trace::make(sampleMeta(), std::vector<float>{1.f, 2.f, 3.f, 4.f, 5.f});
    REQUIRE(t.has_value());

    auto blob = encodeTraceBlob(*t);
    blob.push_back(std::byte{0xAB});  // one byte this decoder cannot attribute to any field.

    std::optional<Trace> back = Trace::make(sampleMeta(), std::vector<float>(5, 9.f));
    REQUIRE(back.has_value());

    CHECK(decodeTraceBlob(blob, sampleMeta(), back) == DecodeStatus::Malformed);
    REQUIRE(back.has_value());
    CHECK(back->field(Field::Magnitude)[0] == 9.f);
}
