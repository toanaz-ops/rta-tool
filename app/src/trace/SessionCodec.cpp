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

template <typename T>
void writeNumeric(std::string& out, std::string_view key, T value) {
    writeLine(out, key, std::to_string(value));
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

// std::from_chars covers the integer fields. Floating point uses std::stod
// instead, since that is what MSVC 14.51 actually ships for from_chars<double>
// in this toolchain; a malformed numeric field falls back to 0 rather than
// aborting decode -- decodeIndex's Malformed/NewerSchema gate already covers
// the case that matters (a schema line that is missing or unparseable).
template <typename T>
T parseInt(std::string_view v) {
    T result{};
    std::from_chars(v.data(), v.data() + v.size(), result);
    return result;
}

double parseDouble(std::string_view v) {
    try {
        return std::stod(std::string(v));
    } catch (...) {
        return 0.0;
    }
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

    return out;
}

DecodeStatus decodeIndex(std::string_view text, SessionDocument& out) {
    // Split into lines without copying the whole buffer; std::string_view
    // slices reference `text`, which outlives this function.
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto nl = text.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start));
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
    int schemaVersion = parseInt<int>(value);
    if (schemaVersion > kSchemaVersion) return DecodeStatus::NewerSchema;

    SessionDocument doc;
    doc.schemaVersion = schemaVersion;

    enum class Section { None, Capture, Entry };
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
        if (!splitLine(line, key, value)) return DecodeStatus::Malformed;
        std::string v = unescape(value);

        if (section == Section::Capture) {
            if (doc.captures.empty()) return DecodeStatus::Malformed;
            CaptureMeta& m = doc.captures.back();
            if (key == "id") m.id = v;
            else if (key == "capturedAtUnixMs") m.capturedAtUnixMs = parseInt<std::int64_t>(v);
            else if (key == "deviceName") m.deviceName = v;
            else if (key == "channelRoles") m.channelRoles = v;
            else if (key == "sampleRate") m.sampleRate = parseDouble(v);
            else if (key == "fftSize") m.fftSize = parseInt<int>(v);
            else if (key == "window") m.window = v;
            else if (key == "averagingType") m.averagingType = v;
            else if (key == "averagingDepth") m.averagingDepth = parseInt<int>(v);
            else if (key == "effectiveAverages") m.effectiveAverages = parseDouble(v);
            else if (key == "appliedDelaySamples") m.appliedDelaySamples = parseInt<int>(v);
            else if (key == "calibrationOffsetDb") m.calibrationOffsetDb = static_cast<float>(parseDouble(v));
            else if (key == "calibrationUnit") m.calibrationUnit = (v == "dbspl") ? LevelUnit::DbSpl : LevelUnit::DbFs;
            else return DecodeStatus::Malformed;
        } else if (section == Section::Entry) {
            if (doc.entries.empty()) return DecodeStatus::Malformed;
            LibraryEntry& e = doc.entries.back();
            if (key == "traceId") e.traceId = v;
            else if (key == "name") e.name = v;
            else if (key == "group") e.group = v;
            else if (key == "shadeIndex") e.shadeIndex = parseInt<int>(v);
            else if (key == "visible") e.visible = (v == "1");
            else return DecodeStatus::Malformed;
        } else {
            return DecodeStatus::Malformed;
        }
    }

    out = std::move(doc);
    return DecodeStatus::Ok;
}

}  // namespace rta::trace
