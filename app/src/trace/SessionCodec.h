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

// 2 adds the [pane] section (PaneSpec, below). decodeIndex refuses only a
// schema NEWER than this constant, so raising it does not stop a schema=1
// file from opening -- it only turns what an older build would see as an
// unrecognised [pane] section (Malformed, "your session is corrupt": a lie)
// into a schema=2 file that build correctly reports as NewerSchema ("this
// needs a newer version": true and actionable). See docs/dsp/2026-08-29-
// display-layer-l5c.md decision 6.
inline constexpr int kSchemaVersion = 2;

/// How the user has organised a capture. Every field editable -- unlike
/// CaptureMeta, which records what the measurement was (spec §2).
struct LibraryEntry {
    std::string traceId;
    std::string name;
    std::string group;
    int shadeIndex = 0;
    bool visible = true;
};

struct SessionDocument {
    int schemaVersion = kSchemaVersion;
    std::vector<CaptureMeta> captures;
    std::vector<LibraryEntry> entries;
    std::vector<PaneSpec> panes;
};

enum class DecodeStatus { Ok, Malformed, NewerSchema };

/// Line-oriented `key=value`, split on the FIRST `=` so values may contain it.
/// Newline and backslash are backslash-escaped; nothing else is.
[[nodiscard]] std::string encodeIndex(const SessionDocument& doc);

/// Refuses a newer schema outright rather than interpreting what it recognises:
/// a partly-read session is a plausible wrong measurement, which is worse than
/// no session at all (spec §3).
[[nodiscard]] DecodeStatus decodeIndex(std::string_view text, SessionDocument& out);

/// float32 little-endian field arrays behind a header carrying only what is
/// needed to read the bytes back: point count, which fields are present, and
/// the trace id as a cross-check. Descriptive metadata lives once, in the
/// index (spec §1.1).
[[nodiscard]] std::vector<std::byte> encodeTraceBlob(const Trace& trace);

[[nodiscard]] DecodeStatus decodeTraceBlob(std::span<const std::byte> blob,
                                           const CaptureMeta& meta,
                                           std::optional<Trace>& out);

}  // namespace rta::trace
