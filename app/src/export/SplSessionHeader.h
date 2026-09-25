// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// PR #26 fix round, item 7 (record §10, deviation 5 from the original W2-C
// PR body): "one file per logged channel plus one session header". Split out
// of SplLog.h, which was already at its 400-line hard cap's neighbourhood,
// so this stays its own small, pure-content file -- the SplLog.h precedent.
#pragma once

#include "trace/SessionCodecDetail.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rta::splexport {

/// The session-wide facts that would otherwise be repeated identically in
/// every channel's own header (SplLog.h's `logHeader`): schema,
/// startedAtUnixMs, sampleRate, blockSamples. `channelFiles` is the current
/// segment path of every logged channel, in the order the session opened
/// them, so a reader holding only this one file can find every channel's
/// data.
struct SplSessionHeaderInfo {
    std::uint64_t startedAtUnixMs = 0;
    double sampleRate = 0.0;
    std::uint32_t blockSamples = 0;
    std::vector<std::string> channelFiles;
};

/// `# key=value`, one pair per line, through the SAME `writeLine`/
/// `writeNumeric` `SplLog.h::logHeader` uses -- one escaping and round-trip
/// implementation, never a second one. `channelFile0`, `channelFile1`, ...
/// name each logged channel's file, in order; `channelCount` says how many
/// to expect, so a reader can tell a short list from a truncated one.
[[nodiscard]] inline std::string sessionHeader(const SplSessionHeaderInfo& info) {
    using rta::trace::detail::writeLine;
    using rta::trace::detail::writeNumeric;
    auto numeric = [](std::string& out, std::string_view key, auto value) {
        out += "# ";
        writeNumeric(out, key, value);
    };
    auto line = [](std::string& out, std::string_view key, std::string_view value) {
        out += "# ";
        writeLine(out, key, value);
    };

    std::string out;
    numeric(out, "schema", std::uint64_t{ 1 });
    numeric(out, "startedAtUnixMs", info.startedAtUnixMs);
    numeric(out, "sampleRate", info.sampleRate);
    numeric(out, "blockSamples", info.blockSamples);
    numeric(out, "channelCount", info.channelFiles.size());
    for (std::size_t i = 0; i < info.channelFiles.size(); ++i) {
        line(out, "channelFile" + std::to_string(i), info.channelFiles[i]);
    }
    return out;
}

/// Writes `sessionHeader(info)` to `path`, truncating any existing file.
/// Declared here, DEFINED in SplLogWriter.cpp -- the one file in this family
/// that opens a stream (SplLog.h's own header comment states the same rule
/// for SplLogWriter's per-channel files).
void writeSessionHeaderFile(const std::string& path, const SplSessionHeaderInfo& info);

}  // namespace rta::splexport
