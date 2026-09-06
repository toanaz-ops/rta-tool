// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/trace. No JUCE: enforced by the
// measure_has_no_framework_deps ctest. Pure encode/decode with no filesystem,
// so the format is testable without touching disk (spec §3, §6).
#pragma once

#include "trace/Trace.h"
#include "trace/Workspace.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rta::trace {

// 2 added the [pane] section (PaneSpec, below). 3 adds [tf] and [average]
// (task B6, record §9): N transfer functions and the spatial average that
// combines them -- editable configuration, unlike CaptureMeta below, which
// is immutable history and is NOT touched by this schema bump. decodeIndex
// refuses only a schema NEWER than this constant, so raising it does not
// stop an older file from opening -- it only turns what an older BUILD would
// see as an unrecognised section (Malformed, "your session is corrupt": a
// lie) into a newer-schema file that build correctly reports as
// NewerSchema ("this needs a newer version": true and actionable). See
// docs/dsp/2026-08-29-display-layer-l5c.md decision 6 (schema 2's own
// reasoning, unchanged by this bump).
inline constexpr int kSchemaVersion = 3;

/// How the user has organised a capture. Every field editable -- unlike
/// CaptureMeta, which records what the measurement was (spec §2).
struct LibraryEntry {
    std::string traceId;
    std::string name;
    std::string group;
    int shadeIndex = 0;
    bool visible = true;
};

/// Smaart's "Use Global / pinned" pattern (record §9, Part A): a member's
/// own averaging settings either track whatever the group currently uses
/// (`Global`), or stay fixed at what was persisted (`Pinned`) even if the
/// group's own settings change later.
enum class AveragingMode { Global, Pinned };

/// One `[tf]` section -- schema 3, record §9. Persists what B1's
/// `ChannelConfig` and B2's routing hold at RUNTIME, so a session can
/// restore the exact routing it was saved with.
struct TransferFunctionSpec {
    std::string name;
    int measurementChannel = -1;
    int referenceChannel = -1;
    int delaySamples = 0;
    double trimDb = 0.0;
    bool polarityInverted = false;
    bool memberOfAverage = false;
    AveragingMode averagingMode = AveragingMode::Global;
    int fifoDepth = 16;
};

/// `rta::dsp::SpatialMode` (record §2) restated locally rather than
/// included from core: this file is JUCE-free but has never linked
/// rta_core, and a preset's own vocabulary ("db"/"power" on disk) is a
/// smaller, more stable surface than the enum core happens to use today.
enum class AverageModeName { Db, Power };

/// The `[average]` section -- at most one per document (record §9: "the
/// average", singular, is the one group this schema persists). `members`
/// names `TransferFunctionSpec::name` entries, in file order, and may be
/// empty (a group with nobody in it yet is still a real group).
struct AverageSpec {
    AverageModeName mode = AverageModeName::Db;
    std::vector<std::string> members;
};

/// The `[routing]` section: which device and how many input channels this
/// session's routing was saved against. `bound` is NEVER persisted -- it is
/// computed at decode time by comparing against the CURRENT device
/// (`decodeIndex`'s `currentDevice` argument), so a session opened on a
/// different machine, or after a device swap, loads its routing visibly
/// UNBOUND rather than silently rebinding to whatever now answers to the
/// same name (Part B's `deviceIdByName` trap; record §9).
struct RoutingSpec {
    std::string deviceName;
    int inputChannelCount = 0;
    bool bound = false;
};

struct SessionDocument {
    int schemaVersion = kSchemaVersion;
    std::vector<CaptureMeta> captures;
    std::vector<LibraryEntry> entries;
    std::vector<PaneSpec> panes;
    std::vector<TransferFunctionSpec> transferFunctions;
    std::optional<AverageSpec> average;
    std::optional<RoutingSpec> routing;
};

enum class DecodeStatus { Ok, Malformed, NewerSchema };

/// Line-oriented `key=value`, split on the FIRST `=` so values may contain it.
/// Newline and backslash are backslash-escaped; nothing else is.
[[nodiscard]] std::string encodeIndex(const SessionDocument& doc);

/// What `decodeIndex` compares a `[routing]` section against to decide
/// `RoutingSpec::bound` -- the device the CALLER currently has open, not
/// anything read from the file itself.
struct CurrentDevice {
    std::string_view name;
    int inputChannelCount = 0;
};

/// Refuses a newer schema outright rather than interpreting what it recognises:
/// a partly-read session is a plausible wrong measurement, which is worse than
/// no session at all (spec §3).
///
/// `currentDevice`: `nullptr` (the default, and every pre-B6 call site) means
/// "no device to compare against" -- a decoded `[routing]` section's `bound`
/// stays `false`, the conservative answer, never a guessed `true`.
[[nodiscard]] DecodeStatus decodeIndex(std::string_view text, SessionDocument& out,
                                       const CurrentDevice* currentDevice = nullptr);

/// float32 little-endian field arrays behind a header carrying only what is
/// needed to read the bytes back: point count, which fields are present, and
/// the trace id as a cross-check. Descriptive metadata lives once, in the
/// index (spec §1.1).
[[nodiscard]] std::vector<std::byte> encodeTraceBlob(const Trace& trace);

[[nodiscard]] DecodeStatus decodeTraceBlob(std::span<const std::byte> blob,
                                           const CaptureMeta& meta,
                                           std::optional<Trace>& out);

}  // namespace rta::trace
