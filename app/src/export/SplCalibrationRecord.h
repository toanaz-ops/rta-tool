// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W2-E2b part A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md
// "W2-E -- the wiring nobody was assigned"; record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §8, §9 item 3, §15 A2).
//
// PURE CONTENT, the SplLog.h / SplSessionHeader.h precedent: the text builder
// and its inverse parser take and return values; only `writeCalibrationRecordFile`
// touches disk, the one function in this pair that is not provably OFF by a
// string-in-string-out test.
#pragma once

#include "measure/CalibrationSession.h"
#include "trace/SessionCodecDetail.h"

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace rta::splexport {

/// The one fact `CalibrationSession` cannot know (it has no channel and no
/// log): which blocks, in THIS session's own log, the start/end pair
/// brackets. Everything else is `CalibrationSession::reportFields()`'s own
/// struct -- §9 item 3's whole row -- carried straight through rather than
/// re-declared field by field a second time.
struct SplCalibrationRecordInfo {
    rta::measure::CalibrationReportFields fields;
    /// The channel calibration was measured against (kCalibrationRouteIndex
    /// at the composition root) -- the log this range applies to. A report
    /// builder reading a DIFFERENT channel's log must not apply this range.
    int channel = 0;
    /// The bracketed span, inclusive both ends, in the log this record sits
    /// beside. In production this is always [0, N] because starting the
    /// fresh log IS the START check's own action (MainComponentCalibration.cpp:
    /// restartSplLoggingForCalibration runs before recordStartCheck's log
    /// ever writes a block) -- kept as explicit fields rather than a
    /// hardcoded 0 so a reader of the FILE never has to know that convention,
    /// and so a test can probe a non-zero start directly.
    std::uint64_t startBlockIndex = 0;
    std::uint64_t endBlockIndex = 0;
};

namespace detail {
inline constexpr std::string_view kCalKeyPerformed = "calibrationPerformed";
inline constexpr std::string_view kCalKeyChannel = "channel";
inline constexpr std::string_view kCalKeyStartBlockIndex = "startBlockIndex";
inline constexpr std::string_view kCalKeyEndBlockIndex = "endBlockIndex";
inline constexpr std::string_view kCalKeyStartMeasuredLevelDb = "startMeasuredLevelDb";
inline constexpr std::string_view kCalKeyStartNominalLevelDb = "startNominalLevelDb";
inline constexpr std::string_view kCalKeyStartOperatorSupplied = "startOperatorSupplied";
inline constexpr std::string_view kCalKeyStartUnixMs = "startUnixMs";
inline constexpr std::string_view kCalKeyEndMeasuredLevelDb = "endMeasuredLevelDb";
inline constexpr std::string_view kCalKeyEndNominalLevelDb = "endNominalLevelDb";
inline constexpr std::string_view kCalKeyEndOperatorSupplied = "endOperatorSupplied";
inline constexpr std::string_view kCalKeyEndUnixMs = "endUnixMs";
inline constexpr std::string_view kCalKeyDriftDb = "driftDb";
inline constexpr std::string_view kCalKeyVerdict = "verdict";
inline constexpr std::string_view kCalKeyClause = "clause";
}  // namespace detail

/// `# key=value` lines, the SplLog.h / SplSessionHeader.h convention, through
/// the SAME writeLine/writeNumeric this project uses everywhere for this
/// shape -- one escaping and round-trip implementation, never a second one.
/// `fields.performed == false` still writes a one-line record
/// ("calibrationPerformed=0") rather than an empty file, so a reader can tell
/// "no calibration ever completed" from "the file failed to write" -- the
/// same reasoning W3-A A3 already applies to the log itself: an instrument
/// that goes quiet instead of saying why is worse than one that says why the
/// evidence is doubtful.
[[nodiscard]] inline std::string calibrationRecordText(const SplCalibrationRecordInfo& info) {
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
    numeric(out, detail::kCalKeyPerformed, info.fields.performed ? std::uint64_t{1} : std::uint64_t{0});
    if (!info.fields.performed) return out;

    numeric(out, detail::kCalKeyChannel, static_cast<std::int64_t>(info.channel));
    numeric(out, detail::kCalKeyStartBlockIndex, info.startBlockIndex);
    numeric(out, detail::kCalKeyEndBlockIndex, info.endBlockIndex);
    numeric(out, detail::kCalKeyStartMeasuredLevelDb, info.fields.start.measuredLevelDb);
    numeric(out, detail::kCalKeyStartNominalLevelDb, info.fields.start.level.nominalDb);
    numeric(out, detail::kCalKeyStartOperatorSupplied,
           info.fields.start.level.operatorSupplied ? std::uint64_t{1} : std::uint64_t{0});
    numeric(out, detail::kCalKeyStartUnixMs, info.fields.start.unixMs);
    numeric(out, detail::kCalKeyEndMeasuredLevelDb, info.fields.end.measuredLevelDb);
    numeric(out, detail::kCalKeyEndNominalLevelDb, info.fields.end.level.nominalDb);
    numeric(out, detail::kCalKeyEndOperatorSupplied,
           info.fields.end.level.operatorSupplied ? std::uint64_t{1} : std::uint64_t{0});
    numeric(out, detail::kCalKeyEndUnixMs, info.fields.end.unixMs);
    numeric(out, detail::kCalKeyDriftDb, info.fields.driftDb);
    line(out, detail::kCalKeyVerdict,
        (info.fields.verdict.has_value() && *info.fields.verdict == rta::measure::CalibrationVerdict::Pass)
            ? "pass"
            : "fail");
    line(out, detail::kCalKeyClause, info.fields.clause);
    return out;
}

/// The inverse of `calibrationRecordText`: NO ATOMICITY CLAIM, the
/// `SplLog.h::readLog` precedent -- a record this reads back is either
/// well-formed or absent, never partially trusted. Returns `std::nullopt`
/// only when the FIRST required line (`calibrationPerformed`) fails to
/// parse; `fields.performed == false` on a successful parse means "no
/// calibration ran", spelled the same way `CalibrationSession::reportFields()`
/// itself spells it (never a placeholder default --
/// memory/a-placeholder-for-an-absent-result-erases-its-state.md). `clause`
/// is set to `CalibrationSession::kClause` directly rather than parsed back
/// from the file's own `clause=` line: that line exists for a human reading
/// the file, but `fields.clause` is a `string_view` and this project has
/// exactly one clause constant with static storage duration -- reading the
/// file's bytes into it would leave a view over a temporary.
[[nodiscard]] inline std::optional<SplCalibrationRecordInfo> parseCalibrationRecord(
    std::string_view text) {
    using rta::trace::detail::splitLine;
    using rta::trace::detail::stripTrailingCr;
    using rta::trace::detail::tryParse;

    SplCalibrationRecordInfo info;
    bool sawPerformed = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto newlinePos = text.find('\n', pos);
        const auto rawLine = (newlinePos == std::string_view::npos)
                                 ? text.substr(pos)
                                 : text.substr(pos, newlinePos - pos);
        pos = (newlinePos == std::string_view::npos) ? text.size() : newlinePos + 1;

        const auto line = stripTrailingCr(rawLine);
        if (line.rfind("# ", 0) != 0) continue;
        const auto content = line.substr(2);
        std::string_view key, value;
        if (!splitLine(content, key, value)) continue;

        if (key == detail::kCalKeyPerformed) {
            int v = 0;
            if (!tryParse(value, v)) return std::nullopt;
            info.fields.performed = v != 0;
            sawPerformed = true;
        } else if (key == detail::kCalKeyChannel) {
            tryParse(value, info.channel);
        } else if (key == detail::kCalKeyStartBlockIndex) {
            tryParse(value, info.startBlockIndex);
        } else if (key == detail::kCalKeyEndBlockIndex) {
            tryParse(value, info.endBlockIndex);
        } else if (key == detail::kCalKeyStartMeasuredLevelDb) {
            tryParse(value, info.fields.start.measuredLevelDb);
        } else if (key == detail::kCalKeyStartNominalLevelDb) {
            tryParse(value, info.fields.start.level.nominalDb);
        } else if (key == detail::kCalKeyStartOperatorSupplied) {
            int v = 0;
            if (tryParse(value, v)) info.fields.start.level.operatorSupplied = v != 0;
        } else if (key == detail::kCalKeyStartUnixMs) {
            tryParse(value, info.fields.start.unixMs);
        } else if (key == detail::kCalKeyEndMeasuredLevelDb) {
            tryParse(value, info.fields.end.measuredLevelDb);
        } else if (key == detail::kCalKeyEndNominalLevelDb) {
            tryParse(value, info.fields.end.level.nominalDb);
        } else if (key == detail::kCalKeyEndOperatorSupplied) {
            int v = 0;
            if (tryParse(value, v)) info.fields.end.level.operatorSupplied = v != 0;
        } else if (key == detail::kCalKeyEndUnixMs) {
            tryParse(value, info.fields.end.unixMs);
        } else if (key == detail::kCalKeyDriftDb) {
            tryParse(value, info.fields.driftDb);
        } else if (key == detail::kCalKeyVerdict) {
            info.fields.verdict = (value == "pass") ? rta::measure::CalibrationVerdict::Pass
                                                    : rta::measure::CalibrationVerdict::Fail;
        }
        // kCalKeyClause: written for a human reader, deliberately not parsed
        // back -- see this function's own doc comment.
    }
    if (!sawPerformed) return std::nullopt;
    if (info.fields.performed) info.fields.clause = rta::measure::CalibrationSession::kClause;
    return info;
}

/// Writes `calibrationRecordText(info)` to `path`, truncating any existing
/// file -- binary mode, the `SplLogWriter.cpp::writeSessionHeaderFile`
/// precedent, so the bytes on disk match the string exactly with no CRLF
/// translation.
inline void writeCalibrationRecordFile(const std::string& path, const SplCalibrationRecordInfo& info) {
    std::ofstream stream(path, std::ios::out | std::ios::trunc | std::ios::binary);
    stream << calibrationRecordText(info);
}

}  // namespace rta::splexport
