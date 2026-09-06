// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Split out of SessionCodec.cpp's
// decodeIndex (task B0, docs/plans/2026-09-06-L6b-impl-plan.md) so encode and
// decode each have room to grow toward schema 3 without either file
// approaching the project's line cap. No behaviour change from the code this
// function had at SessionCodec.cpp:158-285 before the split.
#include "trace/SessionCodec.h"
#include "trace/SessionCodecDetail.h"

#include <vector>

namespace rta::trace {

using namespace rta::trace::detail;

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
