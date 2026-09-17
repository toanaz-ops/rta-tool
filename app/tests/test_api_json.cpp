// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L-API Task A (docs/plans/2026-09-17-remote-api-impl-plan.md; record
// docs/dsp/2026-09-16-remote-api.md sec.6 "Units on the wire", sec.7).
//
// The one numeric decision in the whole lane, and it is a CLOSED FORM: the
// emitted decimal must read back as the identical float. That is
// std::to_chars's own shortest-round-trip guarantee, which is why A1 asserts
// the ROUND TRIP rather than a digit string -- the digits are the emitter's
// business, the bit-identity is the contract. A2 then pins the record's own
// printed literals, so a disagreement names which of the two documents is
// wrong instead of silently adopting whatever the code produced.

#include <catch2/catch_test_macros.hpp>

#include "api/ApiJson.h"

#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <system_error>
#include <vector>

using rta::api::json::array;
using rta::api::json::number;
using rta::api::json::stringValue;

namespace {

/// Parses `text` back as a float and returns its bit pattern. Deliberately
/// std::from_chars and not std::stof: from_chars is the exact inverse of the
/// to_chars the emitter uses, so this asserts the standard's own guarantee
/// rather than two libraries agreeing by luck.
[[nodiscard]] std::uint32_t roundTripBits(const std::string& text) {
    float parsed = std::numeric_limits<float>::quiet_NaN();
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    REQUIRE(result.ec == std::errc{});
    REQUIRE(result.ptr == text.data() + text.size());
    return std::bit_cast<std::uint32_t>(parsed);
}

}  // namespace

TEST_CASE("A1 the emitted decimal round-trips to bit-identical float32", "[api][json]") {
    // The first six are the record's own sec.6 literals; the last two are the
    // pair std::bit_cast exists here to separate -- -0.0f == 0.0f compares
    // true, so a `==` assertion could not tell a sign-losing emitter apart.
    const std::array<float, 8> values{-3.2145123f, -3.107789f,  12.421333f, 11.901777f,
                                      0.9731445f,  0.9642334f, 0.0f,       -0.0f};
    for (const float value : values) {
        const std::string emitted = number(value);
        INFO("emitted: " << emitted);
        CHECK(roundTripBits(emitted) == std::bit_cast<std::uint32_t>(value));
    }
}

TEST_CASE("A2 the record's own printed values are reproduced exactly", "[api][json]") {
    CHECK(number(-3.2145123f) == "-3.2145123");
    CHECK(number(0.9731445f) == "0.9731445");
    // effectiveAverages is a double on Snapshot, so it crosses the double
    // overload -- the same shortest-round-trip property, a different type.
    CHECK(number(8.5859375) == "8.5859375");
}

TEST_CASE("A3 no display rounding leaks into the transport, including Hz", "[api][json]") {
    CHECK(number(0.9731445f) != "0.97");
    CHECK(number(-3.2145123f) != "-3.2");
    // /mtw's own frequencyHz[1] at the default MtwConfig. CLAUDE.md rounds Hz
    // hardest of the three quantities, so it is the one most likely to be
    // rounded here by a well-meaning implementer; the rounding rule is the
    // VIEWER's (record sec.6 "Units on the wire", sec.12 constraint 2).
    CHECK(number(11.71875) == "11.71875");
    CHECK(number(11.71875) != "12");
}

TEST_CASE("A4 a non-finite value emits the literal null", "[api][json]") {
    // nan and inf are not JSON tokens. Asserted as exact strings because the
    // parser does not exist until Task F -- which re-asserts this at document
    // level, where a "null" in the wrong place is a different defect.
    CHECK(number(std::numeric_limits<float>::quiet_NaN()) == "null");
    CHECK(number(std::numeric_limits<float>::infinity()) == "null");
    CHECK(number(-std::numeric_limits<float>::infinity()) == "null");
    CHECK(number(std::numeric_limits<double>::quiet_NaN()) == "null");
    CHECK(number(std::numeric_limits<double>::infinity()) == "null");
    CHECK(number(-std::numeric_limits<double>::infinity()) == "null");
}

TEST_CASE("A5 strings are escaped, asserted as an exact output", "[api][json]") {
    // PositionSummary::name is a std::string that comes from a device, i.e.
    // from outside this program: every byte it can hold has to survive or be
    // escaped. The multi-byte UTF-8 sequence passes through UNCHANGED --
    // \u-escaping it would be legal JSON and would still be wrong, because
    // the wire is declared UTF-8 and a reader diffing a golden should see the
    // character. Task F round-trips this through the parser.
    const std::string name = "a\"b\\c\nd\x01\xE2\x9C\x93";
    CHECK(stringValue(name) == "\"a\\\"b\\\\c\\nd\\u0001\xE2\x9C\x93\"");
    CHECK(stringValue("") == "\"\"");
    CHECK(stringValue("\t\r\b\f") == "\"\\t\\r\\b\\f\"");
}

TEST_CASE("A6 an array is bounded by the caller's clamp", "[api][json]") {
    const std::vector<float> values{1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    CHECK(array(std::span<const float>(values), 3) == "[1,2,3]");
    CHECK(array(std::span<const float>(values), 5) == "[1,2,3,4,5]");
    // A limit past the end is min(size, limit), not a read past the end.
    CHECK(array(std::span<const float>(values), 99) == "[1,2,3,4,5]");
    CHECK(array(std::span<const float>(values), 0) == "[]");
    CHECK(rta::api::json::emittedCount(values.size(), 3) == 3u);
    CHECK(rta::api::json::emittedCount(values.size(), 99) == 5u);
}
