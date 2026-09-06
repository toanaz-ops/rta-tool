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

    for (const auto& tf : doc.transferFunctions) {
        out += "[tf]\n";
        writeLine(out, "name", tf.name);
        writeNumeric(out, "measurementChannel", tf.measurementChannel);
        writeNumeric(out, "referenceChannel", tf.referenceChannel);
        writeNumeric(out, "delaySamples", tf.delaySamples);
        writeNumeric(out, "trimDb", tf.trimDb);
        writeLine(out, "polarity", tf.polarityInverted ? "1" : "0");
        writeLine(out, "memberOfAverage", tf.memberOfAverage ? "1" : "0");
        writeLine(out, "averagingMode", tf.averagingMode == AveragingMode::Pinned ? "pinned" : "global");
        writeNumeric(out, "fifoDepth", tf.fifoDepth);
    }

    if (doc.average.has_value()) {
        out += "[average]\n";
        writeLine(out, "mode", doc.average->mode == AverageModeName::Power ? "power" : "db");
        // Repeated key, one line per member -- decodeIndex APPENDS on each
        // "member=" it sees inside this section rather than overwriting,
        // the one place this format's usual "last write wins" per-key rule
        // does not apply (see SessionDecode.cpp's own comment there).
        for (const auto& member : doc.average->members) {
            writeLine(out, "member", member);
        }
    }

    if (doc.routing.has_value()) {
        out += "[routing]\n";
        // `bound` is NEVER written -- it is a decode-time fact about the
        // CURRENT machine, not something the file itself states (see
        // RoutingSpec's own comment).
        writeLine(out, "deviceName", doc.routing->deviceName);
        writeNumeric(out, "inputChannelCount", doc.routing->inputChannelCount);
    }

    return out;
}

// decodeIndex() lives in SessionDecode.cpp (task B0 split) -- encode and
// decode are large enough on their own, and schema 3 (B6) grows decode more
// than encode, that keeping both here would push this file past the
// project's line cap before that task even starts.

}  // namespace rta::trace
