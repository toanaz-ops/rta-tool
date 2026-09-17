// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task F (record docs/dsp/2026-09-16-remote-api.md sec.15 R16).
//
// THE ONLY TRANSLATION UNIT IN THIS REPOSITORY THAT INCLUDES A JSON PARSER.
// `no_json_parser_in_shipped_code` scans core, platform, ui, tools and
// app/src with no permitted file at all, and additionally requires that at
// least one file under app/tests includes the parser -- this one. A parser
// nothing tests with has stopped meaning anything.
//
// This file exists because of the hardest finding against the plan's first
// draft: every OFF assertion up to Task E is a substring match or a
// byte-compare against a file the same serialiser produced, and A STABLE BUT
// MALFORMED DOCUMENT PASSES ALL OF THEM. OFF is the only configuration CI
// runs, so that was the whole proof.

#include <catch2/catch_test_macros.hpp>

// The parser is not ours and is not clean under this project's global /W4,
// so it is included behind a warning barrier rather than by weakening the
// flag for the target. Measured, not assumed -- see the commit message.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <nlohmann/json.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include "ApiFixture.h"

#include "api/ApiPolicy.h"
#include "api/ApiSerialise.h"
#include "api/ApiSettings.h"
#include "api/ApiJson.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using json = nlohmann::json;
using rta::api::ApiSettings;
using rta::api::clampPoints;
using rta::api::etagFor;
using rta::api::Request;
using rta::api::test::makeApiFixture;
using rta::api::test::makeEmptyApiFixture;

namespace {

[[nodiscard]] Request allPoints() {
    Request request;
    request.points = clampPoints(0, ApiSettings{});
    return request;
}

/// Every one of the eight v1 bodies, for a given snapshot. Named rather than
/// listed inline so F1 and F3 walk the same set and cannot drift apart.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> allBodies(
    const rta::measure::Snapshot& snapshot) {
    const Request request = allPoints();
    return {
        {"/status", rta::api::serialiseStatus(snapshot, ApiSettings{})},
        {"/snapshot", rta::api::serialiseSnapshot(snapshot, request)},
        {"/transfer", rta::api::serialiseTransfer(snapshot, request)},
        {"/mtw", rta::api::serialiseMtw(snapshot, request)},
        {"/bands", rta::api::serialiseBands(snapshot)},
        {"/spectrum", rta::api::serialiseSpectrum(snapshot, request)},
        {"/average", rta::api::serialiseAverage(snapshot, request)},
        {"/positions", rta::api::serialisePositions(snapshot)},
    };
}

[[nodiscard]] std::string readGolden() {
    std::ifstream file(std::string(RTA_API_GOLDEN_DIR) + "/api-v1-snapshot.json",
                       std::ios::binary);
    REQUIRE(file.good());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

/// The keys whose values come from a `float` on `Snapshot`. Named rather
/// than inferred, because the document cannot say what C++ type a number was
/// born as -- and getting that wrong in either direction is a test that
/// either passes vacuously or fails on correct code.
///
/// Deliberately NOT here: `sampleRate`, `frequencyHz` and `effectiveAverages`
/// are `double` on `Snapshot` (`Snapshot.h:101`, `:82`, `:55`), and every
/// count and index is an integer.
[[nodiscard]] bool isFloat32Key(std::string_view key) {
    static constexpr std::string_view kKeys[] = {
        "magnitudeDb",    "phaseDeg",  "coherence", "spectrumDb",   "phaseAgreement",
        "weightedCoherence", "centreHz", "lowerHz",  "upperHz",      "levelDb",
        "windowSeconds",  "integrationSeconds",     "seamHz"};
    for (const auto candidate : kKeys) {
        if (key == candidate) {
            return true;
        }
    }
    return false;
}

/// Walks every numeric leaf and reports the first one that is not what the
/// wire says it is. Returns the JSON pointer of the offender, or "".
///
/// **Task F's plan row asked for `static_cast<float>(v) == v` on every
/// numeric leaf, and that assertion cannot hold against correct code** --
/// measured, not assumed. The shortest-round-trip decimal of `25.118864f` is
/// `25.118864`; read back as a `double` that is 25.118864 exactly, while
/// `(double)(float)25.118864` is 25.118864059448242. The two differ by 6e-8
/// BY CONSTRUCTION, because Task A emits the shortest decimal rather than the
/// float's exact double value. A fixed point of float-narrowing is what the
/// plan's wording asks for and what Task A's whole decision refuses.
///
/// The invariant that IS true, and that still catches the defect the row
/// exists for, stated as what the code below actually compares: **the
/// shortest decimal of the parsed value must equal the shortest decimal of
/// its float32 narrowing** -- `number(v) == number((float)v)`. That says the
/// value carries no more precision than a float32 can hold.
///
/// Note what this is NOT, because the first version of this comment claimed
/// it: it is **not** a comparison against the token on the wire. By the time
/// this function runs the document is parsed and the original token is gone;
/// the only things in scope are the parsed `double` and what the emitter
/// would print for it. The two formulations coincide for documents THIS
/// serialiser produces -- a token that is already a shortest form re-emits to
/// itself, which A1 and A2 pin at the primitive level -- but they are
/// different claims, and only the weaker one is checked here.
///
/// It keeps the teeth either way. A value widened to double before printing
/// -- OSM's own mistake (`server.cpp:386-390`, `item.cpp:169-175`) -- parses
/// to a double whose shortest decimal has more digits than its float32
/// narrowing's, and fails immediately. Proven by mutation, not assumed.
[[nodiscard]] std::string firstBadNumber(const json& node, const std::string& path,
                                         std::string_view owningKey) {
    if (node.is_object()) {
        for (const auto& [name, value] : node.items()) {
            const std::string bad = firstBadNumber(value, path + "/" + name, name);
            if (!bad.empty()) {
                return bad;
            }
        }
        return {};
    }
    if (node.is_array()) {
        for (std::size_t i = 0; i < node.size(); ++i) {
            // An array element inherits its array's key: the elements of
            // "magnitudeDb" are magnitudeDb values.
            const std::string bad =
                firstBadNumber(node[i], path + "/" + std::to_string(i), owningKey);
            if (!bad.empty()) {
                return bad;
            }
        }
        return {};
    }
    if (!node.is_number()) {
        return {};
    }
    const double value = node.get<double>();
    // The half of the plan's row that is unambiguously right, and the half
    // A4 covers only at the emitter: nothing non-finite reaches a document.
    if (!std::isfinite(value)) {
        return path + " is not finite";
    }
    if (node.is_number_integer() || node.is_number_unsigned()) {
        return {};   // counts, sizes and sequences are exact integers
    }
    if (!isFloat32Key(owningKey)) {
        return {};   // a genuine double (sampleRate, frequencyHz, effectiveAverages)
    }
    const float narrowed = static_cast<float>(value);
    if (!std::isfinite(narrowed)) {
        return path + " overflows float32";
    }
    if (rta::api::json::number(narrowed) != rta::api::json::number(value)) {
        return path + " carries more precision than its float32 source";
    }
    return {};
}

}  // namespace

TEST_CASE("F1 every endpoint emits WELL-FORMED JSON", "[api][schema]") {
    // The assertion the whole task exists for, and it goes first.
    for (const auto& [name, body] : allBodies(makeApiFixture())) {
        INFO("endpoint: " << name);
        CHECK_NOTHROW(json::parse(body));
    }
    // The empty-optional variants too: absence is a shape this format has to
    // emit correctly, and it is the shape with the dangling-comma hazard.
    for (const auto& [name, body] : allBodies(makeEmptyApiFixture())) {
        INFO("endpoint (no blocks present): " << name);
        CHECK_NOTHROW(json::parse(body));
    }
}

TEST_CASE("F2 the golden file on disk is well-formed too", "[api][schema]") {
    // A regression lock on a malformed document locks in the malformation.
    CHECK_NOTHROW(json::parse(readGolden()));
}

TEST_CASE("F3 every number in every document is a finite float32 value", "[api][schema]") {
    // Task A's A4 covers the emitter PRIMITIVE (a NaN emits null). This
    // covers the DOCUMENT: nothing on the wire that a float32 consumer will
    // silently widen or lose.
    for (const auto& [name, body] : allBodies(makeApiFixture())) {
        INFO("endpoint: " << name);
        CHECK(firstBadNumber(json::parse(body), name, "") == "");
    }
    CHECK(firstBadNumber(json::parse(readGolden()), "golden", "") == "");
}

TEST_CASE("F4 sequence is monotonic across two polls, and the ETag tracks it",
          "[api][schema]") {
    // Snapshot::sequence is BOTH the ETag source and the ?since= token, so a
    // non-monotonic sequence breaks every conditional-GET client. Nothing
    // asserted this before.
    auto first = makeApiFixture();
    auto second = makeApiFixture();
    second.sequence = first.sequence + 1;

    const auto a = json::parse(rta::api::serialiseStatus(first, ApiSettings{}));
    const auto b = json::parse(rta::api::serialiseStatus(second, ApiSettings{}));
    REQUIRE(a.contains("sequence"));
    REQUIRE(b.contains("sequence"));
    CHECK(b["sequence"].get<std::uint64_t>() == a["sequence"].get<std::uint64_t>() + 1);
    CHECK(etagFor(a["sequence"].get<std::uint64_t>()) != etagFor(b["sequence"].get<std::uint64_t>()));
    CHECK(etagFor(b["sequence"].get<std::uint64_t>()) == "\"" + std::to_string(second.sequence) + "\"");
    // /snapshot and /status must report the SAME sequence for one snapshot,
    // or a client polling one and conditioning on the other loops forever.
    const auto unionDoc = json::parse(rta::api::serialiseSnapshot(second, allPoints()));
    CHECK(unionDoc["sequence"] == b["sequence"]);
}

TEST_CASE("F5 absence is absence at the DOCUMENT level", "[api][schema]") {
    // The structural form of D1, which a substring match can only
    // approximate: `contains` is a fact about the parsed object, not about
    // the bytes.
    auto snapshot = makeApiFixture();
    snapshot.transfer->coherence.reset();
    const auto doc = json::parse(rta::api::serialiseTransfer(snapshot, allPoints()));
    CHECK_FALSE(doc.contains("coherence"));
    CHECK(doc.contains("magnitudeDb"));

    const auto bare = json::parse(rta::api::serialiseSnapshot(makeEmptyApiFixture(), allPoints()));
    CHECK_FALSE(bare.contains("mtw"));
    CHECK_FALSE(bare.contains("transfer"));
    CHECK_FALSE(bare.contains("average"));
    CHECK(bare.contains("bands"));
}

TEST_CASE("F6 the enums are JSON STRINGS, at the document level", "[api][schema]") {
    // E3/E4 asserted the spelling; this asserts the TYPE, which is what a
    // renumbering would change.
    const auto snapshot = makeApiFixture();
    const auto average = json::parse(rta::api::serialiseAverage(snapshot, allPoints()));
    REQUIRE(average["absence"].is_array());
    REQUIRE_FALSE(average["absence"].empty());
    for (const auto& entry : average["absence"]) {
        REQUIRE(entry.is_string());
    }
    const auto positions = json::parse(rta::api::serialisePositions(snapshot));
    REQUIRE(positions["positions"].is_array());
    REQUIRE_FALSE(positions["positions"].empty());
    for (const auto& position : positions["positions"]) {
        REQUIRE(position["membership"].is_string());
        REQUIRE(position["name"].is_string());
    }
}

TEST_CASE("F7 A4 and A5 re-asserted THROUGH the parser", "[api][schema]") {
    // A4: a non-finite value reaches the document as a JSON null, not as the
    // tokens nan/inf, which are not JSON and which a parser rejects outright.
    auto broken = makeApiFixture();
    broken.transfer->magnitudeDb[0] = std::numeric_limits<float>::quiet_NaN();
    broken.transfer->magnitudeDb[1] = std::numeric_limits<float>::infinity();
    const auto doc = json::parse(rta::api::serialiseTransfer(broken, allPoints()));
    CHECK(doc["magnitudeDb"][0].is_null());
    CHECK(doc["magnitudeDb"][1].is_null());

    // A5: a device-supplied name carrying a quote, a backslash, a newline, a
    // control byte and a multi-byte UTF-8 sequence parses back to the
    // IDENTICAL std::string. PositionSummary::name comes from outside this
    // program, so this is the byte range that actually has to survive.
    auto named = makeApiFixture();
    REQUIRE_FALSE(named.positions.empty());
    const std::string hostile = "a\"b\\c\nd\x01\xE2\x9C\x93";
    named.positions[0].name = hostile;
    const auto parsed = json::parse(rta::api::serialisePositions(named));
    CHECK(parsed["positions"][0]["name"].get<std::string>() == hostile);
}
