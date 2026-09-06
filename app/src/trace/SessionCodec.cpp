// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Index (text) codec only -- the trace
// blob (binary) codec lives in TraceBlobCodec.cpp, split out to keep both
// files under the 300-line house limit.
#include "trace/SessionCodec.h"
#include "trace/SessionCodecDetail.h"

namespace rta::trace {

using namespace rta::trace::detail;

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

// decodeIndex() lives in SessionDecode.cpp (task B0 split) -- encode and
// decode are large enough on their own, and schema 3 (B6) grows decode more
// than encode, that keeping both here would push this file past the
// project's line cap before that task even starts.

}  // namespace rta::trace
