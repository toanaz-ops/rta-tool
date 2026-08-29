// SPDX-License-Identifier: AGPL-3.0-or-later
#include "trace/SessionCodec.h"
#include "view/PaneRegistry.h"

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

TEST_CASE("an unrecognised visible value is refused, not silently hidden", "[codec]") {
    // Mirrors the calibrationUnit test above: encodeIndex only ever writes
    // "1" or "0" for this field (see writeLine(out, "visible", ...) in
    // SessionCodec.cpp), so any other value is either a hand-edited file or
    // corruption -- either way, defaulting it to false would silently hide a
    // trace with no error to explain why.
    SessionDocument back = sentinelDocument();
    CHECK(decodeIndex("schema=1\n[entry]\ntraceId=x\nvisible=true\n", back) == DecodeStatus::Malformed);
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

TEST_CASE("a workspace round-trips through the index", "[codec]") {
    SessionDocument doc;
    doc.panes.push_back(PaneSpec{"rta", 0.25f});
    doc.panes.push_back(PaneSpec{"transfer", 0.75f});

    const std::string encoded = encodeIndex(doc);
    // Pin the text itself, not just decode(encode(x)) == x -- an encoder that
    // wrote the wrong key name (or a decoder that read a different one) could
    // still round-trip correctly if BOTH sides used the same wrong key, and a
    // round-trip-only assertion would never catch that.
    CHECK(encoded.find("[pane]\n") != std::string::npos);
    CHECK(encoded.find("view=rta\n") != std::string::npos);
    CHECK(encoded.find("view=transfer\n") != std::string::npos);
    // Without this, a matched-pair rename of the weight key in BOTH
    // encodeIndex and decodeIndex (e.g. "weight" -> "w") would still pass
    // every assertion above and every round-trip check below: the two sides
    // agree with each other, just not with the format they're supposed to be.
    CHECK(encoded.find("weight=") != std::string::npos);

    SessionDocument back;
    REQUIRE(decodeIndex(encoded, back) == DecodeStatus::Ok);
    REQUIRE(back.panes.size() == 2u);
    CHECK(back.panes[0].view == "rta");
    CHECK(back.panes[1].view == "transfer");
    CHECK(back.panes[0].weight == 0.25f);
    CHECK(back.panes[1].weight == 0.75f);
}

TEST_CASE("an unknown view name does not refuse the session", "[codec]") {
    // THE DELIBERATE ASYMMETRY with the refuse-newer-schema rule: a guessed
    // MEASUREMENT (calibrationUnit, visible) is a plausible lie about what
    // was captured, so decodeIndex refuses those outright. A guessed LAYOUT
    // is, at worst, a wrong arrangement of data that is still true -- and
    // refusing a whole session of real captures over one unfamiliar layout
    // word would destroy value (every capture, every entry) to protect
    // nothing. So the codec stores the view name verbatim and lets
    // view/PaneRegistry.h fall back to it -- and REPORT the fallback --
    // instead of the codec refusing the session outright.
    SessionDocument back = sentinelDocument();
    REQUIRE(decodeIndex("schema=2\n[pane]\nview=spectrograph\nweight=1\n", back) == DecodeStatus::Ok);
    REQUIRE(back.panes.size() == 1u);
    CHECK(back.panes.front().view == "spectrograph");

    const auto resolved = rta::view::resolvePaneView(back.panes.front().view);
    CHECK(resolved.view == rta::view::PaneView::Rta);
    CHECK(resolved.fellBack);
    CHECK(resolved.requested == "spectrograph");
}

TEST_CASE("a malformed weight still refuses the session", "[codec]") {
    // The tolerance case 6 grants above is for an unknown view NAME, not a
    // licence to guess at NUMBERS -- `weight` still goes through tryParse
    // like every other numeric field, and a value that doesn't parse is
    // Malformed, exactly like a bad fftSize or sampleRate. Without this test,
    // somebody "simplifying" the asymmetry comment above could make the
    // [pane] section accept anything at all, weight included.
    SessionDocument back = sentinelDocument();
    CHECK(decodeIndex("schema=2\n[pane]\nview=rta\nweight=abc\n", back) == DecodeStatus::Malformed);
    REQUIRE(back.captures.size() == 1u);
    CHECK(back.captures.front().id == "SENTINEL");
}

TEST_CASE("a version-1 session still opens", "[codec]") {
    // This is the assertion that makes the kSchemaVersion bump to 2 safe, and
    // it must exist before the bump lands: a v1 file has captures and
    // entries but no [pane] section at all, and decoding it must still
    // succeed with an empty pane list -- which normalisePanes then turns
    // into the single default `rta` pane, not zero panes.
    SessionDocument back;
    const std::string v1 =
        "schema=1\n"
        "[capture]\n"
        "id=abc-123\n"
        "sampleRate=48000\n"
        "fftSize=8\n"
        "[entry]\n"
        "traceId=abc-123\n"
        "name=FOH\n";
    REQUIRE(decodeIndex(v1, back) == DecodeStatus::Ok);
    CHECK(back.schemaVersion == 1);
    REQUIRE(back.captures.size() == 1u);
    REQUIRE(back.entries.size() == 1u);
    CHECK(back.panes.empty());

    auto normalised = normalisePanes(back.panes);
    REQUIRE(normalised.size() == 1u);
    CHECK(normalised.front().view == kDefaultPaneView);
}
