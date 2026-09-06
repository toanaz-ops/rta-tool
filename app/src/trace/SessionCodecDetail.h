// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Split out of SessionCodec.cpp (task
// B0, docs/plans/2026-09-06-L6b-impl-plan.md) so the encode helpers
// (SessionCodec.cpp) and the decode helpers (SessionDecode.cpp) share one
// copy each instead of two -- no behaviour change. `inline`, not an
// anonymous namespace: these are included into two translation units, and an
// anonymous-namespace copy unused in one of them (encodeIndex never calls
// tryParse; decodeIndex never calls toChars) would warn as an unreferenced
// internal-linkage function in that TU.
#pragma once

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>

namespace rta::trace::detail {

// Escapes only the two characters that would otherwise break the line-oriented
// format: backslash (so an escaped newline can't be confused with a literal
// one) and newline itself. Nothing else needs it -- '=' is handled by
// splitting at the FIRST occurrence instead, which lets it through unescaped.
inline std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

inline std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out += (s[i] == 'n') ? '\n' : s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

inline void writeLine(std::string& out, std::string_view key, std::string_view value) {
    out += key;
    out += '=';
    out += escape(value);
    out += '\n';
}

// std::to_chars gives the shortest decimal string that round-trips back to
// the exact same bit pattern, and -- unlike sprintf's "%f" underneath
// std::to_string -- it ignores LC_NUMERIC entirely. Both properties matter
// here: a fixed six-decimal %f loses precision on a value like
// 9.523809523809524 (round-trips as 9.52381), and a comma-decimal locale
// would make "%f" write "9,500000", which a C-locale reader then parses as
// the integer 9 with no error at all. 64 bytes is ample for every field this
// format writes (the longest is a full-precision double); to_chars only
// fails on a too-small buffer.
template <typename T>
std::string toChars(T value) {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, res.ptr);
}

template <typename T>
void writeNumeric(std::string& out, std::string_view key, T value) {
    writeLine(out, key, toChars(value));
}

// Splits at the FIRST '=' only, so a value containing '=' survives intact.
// Returns false if the line has no '=' at all.
inline bool splitLine(std::string_view line, std::string_view& key, std::string_view& value) {
    auto pos = line.find('=');
    if (pos == std::string_view::npos) return false;
    key = line.substr(0, pos);
    value = line.substr(pos + 1);
    return true;
}

// A trailing '\r' is what a CRLF-terminated file leaves behind once we split
// on '\n' alone. Stripping it here -- before the line is treated as a
// section marker or a key=value pair -- is what lets a session that crossed
// a Windows/Unix boundary in transit still decode instead of corrupting the
// last character of whatever key or value it lands on (e.g. "dbspl\r"
// failing to match "dbspl" and silently defaulting away from the calibration
// unit the file actually recorded).
inline std::string_view stripTrailingCr(std::string_view line) {
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    return line;
}

// std::from_chars is the mirror of std::to_chars above: exact, and immune to
// locale. Verified directly against this toolchain (MSVC 14.51, VS Build
// Tools 2026) -- floating-point from_chars/to_chars have shipped since
// VS2019 16.4, contrary to an earlier draft of this file that assumed
// otherwise and fell back to std::stod. Requiring the WHOLE value to parse
// (res.ptr reaching the end) is what makes "42xyz" fail instead of silently
// becoming 42: a field that cannot be parsed must fail the whole decode, not
// substitute a plausible-looking wrong number -- the exact failure mode this
// format exists to refuse (spec §3).
template <typename T>
bool tryParse(std::string_view v, T& out) {
    if (v.empty()) return false;
    auto res = std::from_chars(v.data(), v.data() + v.size(), out);
    return res.ec == std::errc() && res.ptr == v.data() + v.size();
}

}  // namespace rta::trace::detail
