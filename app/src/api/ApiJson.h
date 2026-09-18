// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/api. No JUCE, no Qt, no audio-device API, and
// no server library: this half of the lane is proven in RTA_BUILD_APP=OFF,
// which is the only configuration CI runs (record sec.15 R15).
// See docs/dsp/2026-09-16-remote-api.md sec.6 "Units on the wire", sec.7.
#pragma once

#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace rta::api::json {

/// std::to_chars with NO precision argument is the whole decision: the
/// standard guarantees it produces the SHORTEST decimal that reads back as
/// the identical value. That is why no printf format string appears anywhere
/// in this lane -- a format string picks a digit count, and any digit count
/// either loses bits or invents them.
///
/// The display rule (CLAUDE.md "Reading out numbers": whole hertz, one
/// decimal of dB, two of coherence) is the VIEWER's, not the transport's.
/// Rounding here would be a lossy transform nobody asked for, applied to
/// numbers a client may want to re-analyse.
///
/// Non-finite emits the literal `null`: `nan` and `inf` are not JSON tokens,
/// and a document carrying them is not a document a conforming parser reads.
template <std::floating_point T>
[[nodiscard]] inline std::string number(T value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    // 64 bytes is past the longest shortest-round-trip form of a double
    // (17 significant digits, sign, point and a three-digit exponent).
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    return std::string(buffer, result.ptr);
}

/// Integers never round-trip through the float path: sequence is a uint64
/// whose value above 2^53 a double cannot hold, and it is the ETag source.
template <std::integral T>
[[nodiscard]] inline std::string number(T value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    return std::string(buffer, result.ptr);
}

/// A JSON string literal, quotes included and contents escaped. Every string
/// on this wire is either a compile-time name from this file or
/// `PositionSummary::name`, which comes from a device -- i.e. from outside
/// this program -- so every byte it can hold has to survive or be escaped.
///
/// Multi-byte UTF-8 passes through UNCHANGED. \u-escaping it would be legal
/// JSON and would still be wrong: the wire is declared UTF-8, and a reader
/// diffing a golden file should see the character rather than its code point.
[[nodiscard]] inline std::string stringValue(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char raw : text) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (byte) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20u) {
                    // RFC 8259 requires a \u escape for any other C0 control;
                    // a raw 0x01 in a string is a malformed document.
                    static constexpr char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(kHex[(byte >> 4) & 0x0Fu]);
                    out.push_back(kHex[byte & 0x0Fu]);
                } else {
                    out.push_back(raw);
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

/// How many elements an array of `size` emits under `limit`. Separate from
/// `array` because the count travels in the object BESIDE the array
/// (`axis.pointCount`), and a count computed twice by two different
/// expressions is a count that can disagree with its own array.
[[nodiscard]] inline std::size_t emittedCount(std::size_t size, std::size_t limit) noexcept {
    return size < limit ? size : limit;
}

/// `min(values.size(), limit)` elements. The clamp is a real-time-safety
/// control, not hygiene: an uncapped `?points=` is one of the two ways a
/// remote caller can make this program do unbounded work during a show.
template <typename T>
[[nodiscard]] inline std::string array(std::span<const T> values, std::size_t limit) {
    const std::size_t count = emittedCount(values.size(), limit);
    std::string out;
    out.reserve(count * 12u + 2u);
    out.push_back('[');
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        out += number(values[i]);
    }
    out.push_back(']');
    return out;
}

}  // namespace rta::api::json
