// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Index (text) codec only -- the trace
// blob (binary) codec lives in TraceBlobCodec.cpp, split out to keep both
// files under the 300-line house limit.
#include "trace/SessionCodec.h"

#include <charconv>
#include <cstdint>

namespace rta::trace {
namespace {

// Escapes only the two characters that would otherwise break the line-oriented
// format: backslash (so an escaped newline can't be confused with a literal
// one) and newline itself. Nothing else needs it -- '=' is handled by
// splitting at the FIRST occurrence instead, which lets it through unescaped.
std::string escape(std::string_view s) {
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

std::string unescape(std::string_view s) {
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

void writeLine(std::string& out, std::string_view key, std::string_view value) {
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
bool splitLine(std::string_view line, std::string_view& key, std::string_view& value) {
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
std::string_view stripTrailingCr(std::string_view line) {
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

}  // namespace

std::string encodeIndex(const SessionDocument& doc) {
    std::string out;
    writeNumeric(out, "schema", doc.schemaVersion);

    for (const auto& m : doc.captures) {
        out += "[capture]\n";
        writeLine(out, "id", m.id);
        writeNumeric(out, "capturedAtUnixMs", m.capturedAtUnixMs);
        writeLine(out, "deviceName", m.deviceName);
        writeLine(out, "channelRoles", m.channelRoles);
        writeNumeric(out, "sampleRate", m.sampleRate);
        writeNumeric(out, "fftSize", m.fftSize);
        writeLine(out, "window", m.window);
        writeLine(out, "averagingType", m.averagingType);
        writeNumeric(out, "averagingDepth", m.averagingDepth);
        writeNumeric(out, "effectiveAverages", m.effectiveAverages);
        writeNumeric(out, "appliedDelaySamples", m.appliedDelaySamples);
        writeNumeric(out, "calibrationOffsetDb", m.calibrationOffsetDb);
        writeLine(out, "calibrationUnit", m.calibrationUnit == LevelUnit::DbSpl ? "dbspl" : "dbfs");
    }

    for (const auto& e : doc.entries) {
        out += "[entry]\n";
        writeLine(out, "traceId", e.traceId);
        writeLine(out, "name", e.name);
        writeLine(out, "group", e.group);
        writeNumeric(out, "shadeIndex", e.shadeIndex);
        writeLine(out, "visible", e.visible ? "1" : "0");
    }

    for (const auto& p : doc.panes) {
        out += "[pane]\n";
        // `view` is written verbatim, whatever string PaneSpec holds -- the
        // codec does not know or care what a valid pane view name is (see
        // Workspace.h). `weight` is a plain number and goes through the same
        // to_chars path as every other numeric field.
        writeLine(out, "view", p.view);
        writeNumeric(out, "weight", p.weight);
    }

    return out;
}

DecodeStatus decodeIndex(std::string_view text, SessionDocument& out) {
    // Split into lines without copying the whole buffer; std::string_view
    // slices reference `text`, which outlives this function. `out` is never
    // touched until the single assignment at the very end of this function,
    // on the Ok path only -- every Malformed/NewerSchema return above that
    // point leaves the caller's `out` exactly as they passed it in.
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back(stripTrailingCr(text.substr(start)));
            break;
        }
        lines.push_back(stripTrailingCr(text.substr(start, nl - start)));
        start = nl + 1;
    }

    if (lines.empty()) return DecodeStatus::Malformed;

    // schema= must be the very first line -- decodeIndex reads it before
    // anything else so a newer file is refused before any of its content is
    // interpreted (spec §3).
    std::string_view key, value;
    if (!splitLine(lines[0], key, value) || key != "schema") {
        return DecodeStatus::Malformed;
    }
    int schemaVersion = 0;
    if (!tryParse(value, schemaVersion)) return DecodeStatus::Malformed;
    if (schemaVersion > kSchemaVersion) return DecodeStatus::NewerSchema;

    SessionDocument doc;
    doc.schemaVersion = schemaVersion;

    enum class Section { None, Capture, Entry, Pane };
    Section section = Section::None;

    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string_view line = lines[i];
        if (line.empty()) continue;
        if (line == "[capture]") {
            doc.captures.emplace_back();
            section = Section::Capture;
            continue;
        }
        if (line == "[entry]") {
            doc.entries.emplace_back();
            section = Section::Entry;
            continue;
        }
        if (line == "[pane]") {
            doc.panes.emplace_back();
            section = Section::Pane;
            continue;
        }
        if (!splitLine(line, key, value)) return DecodeStatus::Malformed;
        std::string v = unescape(value);

        if (section == Section::Capture) {
            if (doc.captures.empty()) return DecodeStatus::Malformed;
            CaptureMeta& m = doc.captures.back();
            if (key == "id") m.id = v;
            else if (key == "capturedAtUnixMs") { if (!tryParse(v, m.capturedAtUnixMs)) return DecodeStatus::Malformed; }
            else if (key == "deviceName") m.deviceName = v;
            else if (key == "channelRoles") m.channelRoles = v;
            else if (key == "sampleRate") { if (!tryParse(v, m.sampleRate)) return DecodeStatus::Malformed; }
            else if (key == "fftSize") { if (!tryParse(v, m.fftSize)) return DecodeStatus::Malformed; }
            else if (key == "window") m.window = v;
            else if (key == "averagingType") m.averagingType = v;
            else if (key == "averagingDepth") { if (!tryParse(v, m.averagingDepth)) return DecodeStatus::Malformed; }
            else if (key == "effectiveAverages") { if (!tryParse(v, m.effectiveAverages)) return DecodeStatus::Malformed; }
            else if (key == "appliedDelaySamples") { if (!tryParse(v, m.appliedDelaySamples)) return DecodeStatus::Malformed; }
            else if (key == "calibrationOffsetDb") { if (!tryParse(v, m.calibrationOffsetDb)) return DecodeStatus::Malformed; }
            else if (key == "calibrationUnit") {
                // Unknown KEYS fall through to Malformed below; an unknown
                // VALUE here must too -- silently defaulting to dBFS is the
                // "94 dB lie" the format exists to refuse, no different from
                // getting the number itself wrong.
                if (v == "dbspl") m.calibrationUnit = LevelUnit::DbSpl;
                else if (v == "dbfs") m.calibrationUnit = LevelUnit::DbFs;
                else return DecodeStatus::Malformed;
            }
            else return DecodeStatus::Malformed;
        } else if (section == Section::Entry) {
            if (doc.entries.empty()) return DecodeStatus::Malformed;
            LibraryEntry& e = doc.entries.back();
            if (key == "traceId") e.traceId = v;
            else if (key == "name") e.name = v;
            else if (key == "group") e.group = v;
            else if (key == "shadeIndex") { if (!tryParse(v, e.shadeIndex)) return DecodeStatus::Malformed; }
            else if (key == "visible") {
                // Same policy as calibrationUnit above, for the same reason:
                // encodeIndex only ever writes "1" or "0" (see writeLine(out,
                // "visible", ...) below), so any other value is an
                // unrecognised one from a hand-edited or corrupted file.
                // Treating it as "0" would silently hide a trace with no
                // error to explain why it vanished.
                if (v == "1") e.visible = true;
                else if (v == "0") e.visible = false;
                else return DecodeStatus::Malformed;
            }
            else return DecodeStatus::Malformed;
        } else if (section == Section::Pane) {
            if (doc.panes.empty()) return DecodeStatus::Malformed;
            PaneSpec& p = doc.panes.back();
            // THE ASYMMETRY: `view` is a layout word, not a measurement, so it
            // is stored verbatim with NO validation here -- an unrecognised
            // name is resolved (and reported) by view/PaneRegistry.h at read
            // time, never refused by the codec. Do not "tidy" this to match
            // calibrationUnit/visible above: those guard against a guessed
            // NUMBER masquerading as a real measurement, which is the one
            // thing this format exists to refuse; a pane layout carries no
            // such risk, and refusing a whole session of real captures over
            // an unfamiliar layout word would destroy value to protect
            // nothing. `weight`, by contrast, IS a number, so it keeps the
            // same tryParse-or-Malformed treatment as every other numeric
            // field -- the tolerance above is for the view name only.
            if (key == "view") p.view = v;
            else if (key == "weight") { if (!tryParse(v, p.weight)) return DecodeStatus::Malformed; }
            else return DecodeStatus::Malformed;
        } else {
            return DecodeStatus::Malformed;
        }
    }

    out = std::move(doc);
    return DecodeStatus::Ok;
}

}  // namespace rta::trace
