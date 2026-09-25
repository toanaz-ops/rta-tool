// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W2-C (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §10, §13 Q7).
//
// PURE CONTENT, the app/src/export/EqTextExport.h precedent: every function
// below takes and returns strings/values, and none opens a file. The one
// file in this pair that touches disk is SplLogWriter.cpp, whose
// SplLogWriter class (declared at the bottom) calls straight into the
// functions above it.
#pragma once

#include "measure/SplConfig.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Block.h"
#include "rta/meter/Detector.h"
#include "trace/SessionCodecDetail.h"

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rta::splexport {

/// The per-channel facts `rta::measure::SplConfig` itself does not carry --
/// weighting and detector are per-`SplMeter` (one instance per weighting,
/// record §2), and `startedAtUnixMs`/`calibratorLevelDb` are session facts
/// rather than configuration.
struct SplLogHeaderInfo {
    rta::dsp::WeightingType weighting = rta::dsp::WeightingType::A;
    rta::meter::TimeWeighting detector = rta::meter::TimeWeighting::Fast;
    std::uint32_t blockSamples = 0;
    double sampleRate = 0.0;
    std::uint64_t startedAtUnixMs = 0;
    /// Absent until a calibration check has run (SPL-R5's `calibrated` bool
    /// stays the SOURCE of truth; this is the human-readable reference point
    /// beside it). NEW key this lane introduces (C1) -- no precedent exists
    /// for it anywhere in the tree.
    std::optional<double> calibratorLevelDb;
};

/// C1: `# key=value`, one pair per line, through `SessionCodecDetail`'s
/// `writeLine`/`writeNumeric`. `calibrationOffsetDb`/`calibrationUnit` are
/// spelled exactly as `SessionCodec.cpp:30-31` spells them.
[[nodiscard]] inline std::string logHeader(const rta::measure::SplConfig& config,
                                           const SplLogHeaderInfo& info) {
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
    line(out, "weighting", rta::dsp::toString(info.weighting));
    line(out, "detector", rta::meter::toString(info.detector));
    numeric(out, "calibrationOffsetDb", config.referenceOffsetDb);
    line(out, "calibrationUnit", config.calibrated ? "dbspl" : "dbfs");
    if (info.calibratorLevelDb.has_value()) numeric(out, "calibratorLevelDb", *info.calibratorLevelDb);
    numeric(out, "histogramBaseDb", config.histogramBaseDb());
    return out;
}

namespace detail {
// One named literal per column, so each name -- "peakCDb" in particular --
// is its OWN string literal in the source rather than a substring of one
// long comma-joined constant. test_spl_criteria.cpp's E2 scan matches a
// "peak" exemption by whole-literal equality, and `kColumnPeakCDb` is what
// lets that scan see this column's name at all (its own allow-list entry
// names this exact spelling).
inline constexpr std::string_view kColumnBlockIndex = "blockIndex";
inline constexpr std::string_view kColumnBlockSamples = "blockSamples";
inline constexpr std::string_view kColumnDroppedSamples = "droppedSamples";
inline constexpr std::string_view kColumnSumSquares = "sumSquares";
inline constexpr std::string_view kColumnLeqDb = "leqDb";
inline constexpr std::string_view kColumnMaxFastDb = "maxFastDb";
inline constexpr std::string_view kColumnMaxSlowDb = "maxSlowDb";
inline constexpr std::string_view kColumnPeakCDb = "peakCDb";
inline constexpr std::string_view kColumnFlags = "flags";
}  // namespace detail

/// C8: record §10's row plus `blockSamples`/`droppedSamples`, without which a
/// reader cannot form §3's divisor or recover elapsed time across a gap.
[[nodiscard]] inline std::string_view csvHeaderRow() noexcept {
    static const std::string built = std::string(detail::kColumnBlockIndex) + "," +
                                     std::string(detail::kColumnBlockSamples) + "," +
                                     std::string(detail::kColumnDroppedSamples) + "," +
                                     std::string(detail::kColumnSumSquares) + "," +
                                     std::string(detail::kColumnLeqDb) + "," +
                                     std::string(detail::kColumnMaxFastDb) + "," +
                                     std::string(detail::kColumnMaxSlowDb) + "," +
                                     std::string(detail::kColumnPeakCDb) + "," +
                                     std::string(detail::kColumnFlags);
    return built;
}

/// One CSV row. `sumSquares`/`maxFastDb`/`maxSlowDb`/`peakDb` are written RAW
/// (`std::to_chars`, no precision argument -- shortest round-tripping
/// decimal), so `readLog` reconstructs the exact `Block` bit for bit (C2).
/// `leqDb` is a DERIVED, calibrated convenience column kept beside
/// `sumSquares` so a reader can check the two agree (record §10).
[[nodiscard]] inline std::string logRow(const rta::meter::Block& block, double referenceOffsetDb) {
    using rta::trace::detail::toChars;
    std::string out;
    out += toChars(block.blockIndex);
    out += ',';
    out += toChars(block.blockSamples);
    out += ',';
    out += toChars(block.droppedSamples);
    out += ',';
    out += toChars(block.sumSquares);
    out += ',';
    const double leqDb = block.blockSamples > 0
                              ? 10.0 * std::log10(block.sumSquares /
                                                  static_cast<double>(block.blockSamples)) +
                                    referenceOffsetDb
                              : rta::meter::kLevelFloorDb;
    out += toChars(leqDb);
    out += ',';
    out += toChars(block.maxFastDb);
    out += ',';
    out += toChars(block.maxSlowDb);
    out += ',';
    out += toChars(block.peakDb);
    out += ',';
    out += toChars(block.flags);
    out += '\n';
    return out;
}

/// The rows that parsed, and how many trailing bytes did not. NO ATOMICITY
/// CLAIM (none can be made portably) -- tolerance, not a guarantee (C3).
struct LogReadResult {
    std::vector<rta::meter::Block> blocks;
    std::size_t bytesDiscarded = 0;
};

namespace detail {

[[nodiscard]] inline std::optional<rta::meter::Block> parseRow(std::string_view line) {
    using rta::trace::detail::stripTrailingCr;
    using rta::trace::detail::tryParse;
    line = stripTrailingCr(line);

    std::array<std::string_view, 9> fields;
    std::size_t fieldIndex = 0;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        if (i != line.size() && line[i] != ',') continue;
        if (fieldIndex >= fields.size()) return std::nullopt;
        fields[fieldIndex++] = line.substr(start, i - start);
        start = i + 1;
    }
    if (fieldIndex != fields.size()) return std::nullopt;

    rta::meter::Block block;
    double leqDbUnused = 0.0;
    if (!tryParse(fields[0], block.blockIndex)) return std::nullopt;
    if (!tryParse(fields[1], block.blockSamples)) return std::nullopt;
    if (!tryParse(fields[2], block.droppedSamples)) return std::nullopt;
    if (!tryParse(fields[3], block.sumSquares)) return std::nullopt;
    if (!tryParse(fields[4], leqDbUnused)) return std::nullopt;
    if (!tryParse(fields[5], block.maxFastDb)) return std::nullopt;
    if (!tryParse(fields[6], block.maxSlowDb)) return std::nullopt;
    if (!tryParse(fields[7], block.peakDb)) return std::nullopt;
    if (!tryParse(fields[8], block.flags)) return std::nullopt;
    return block;
}

}  // namespace detail

/// C3: an interrupted append costs at most one line. Every well-formed,
/// newline-terminated row before the break parses; the FINAL, possibly
/// truncated line is dropped and its byte length reported, never guessed at.
///
/// A REAL file (`SplLogWriter`'s own output) is not just rows: it opens with
/// `logHeader`'s `# key=value` lines and one `csvHeaderRow()` column-name
/// line. Neither is a 9-field numeric row, so both are skipped here BEFORE
/// `detail::parseRow` ever sees them -- an intact header must never be
/// misread as corrupted data and folded into `bytesDiscarded` (PR #26 fix
/// round item 2: the verifier's probe read a perfectly intact 478 B / 5-row
/// file and got `bytesDiscarded=283`, which was every header byte).
[[nodiscard]] inline LogReadResult readLog(std::string_view csvBody) {
    using rta::trace::detail::stripTrailingCr;
    const std::string_view header = csvHeaderRow();

    LogReadResult result;
    std::size_t pos = 0;
    while (pos < csvBody.size()) {
        const auto newlinePos = csvBody.find('\n', pos);
        if (newlinePos == std::string_view::npos) {
            result.bytesDiscarded += csvBody.size() - pos;
            break;
        }
        const auto line = csvBody.substr(pos, newlinePos - pos);
        pos = newlinePos + 1;

        const auto content = stripTrailingCr(line);
        if (content.rfind("# ", 0) == 0 || content == header) continue;  // header, not data

        if (auto block = detail::parseRow(line)) {
            result.blocks.push_back(*block);
        } else {
            result.bytesDiscarded += line.size() + 1;
        }
    }
    return result;
}

/// C7: the IDENTICAL field names as `csvHeaderRow()`, JSON-encoded -- one
/// schema, two encodings. Same raw/derived split as `logRow`.
[[nodiscard]] inline std::string logJson(const rta::meter::Block& block, double referenceOffsetDb) {
    using rta::trace::detail::toChars;
    const double leqDb = block.blockSamples > 0
                              ? 10.0 * std::log10(block.sumSquares /
                                                  static_cast<double>(block.blockSamples)) +
                                    referenceOffsetDb
                              : rta::meter::kLevelFloorDb;
    // Built from the SAME named literals `csvHeaderRow()` uses, key by key --
    // "one schema, two encodings" means one place each name is spelled, not
    // two copies that could drift.
    auto field = [](std::string_view key, const std::string& valueChars) {
        return "\"" + std::string(key) + "\":" + valueChars;
    };
    std::string out = "{";
    out += field(detail::kColumnBlockIndex, toChars(block.blockIndex)) + ",";
    out += field(detail::kColumnBlockSamples, toChars(block.blockSamples)) + ",";
    out += field(detail::kColumnDroppedSamples, toChars(block.droppedSamples)) + ",";
    out += field(detail::kColumnSumSquares, toChars(block.sumSquares)) + ",";
    out += field(detail::kColumnLeqDb, toChars(leqDb)) + ",";
    out += field(detail::kColumnMaxFastDb, toChars(block.maxFastDb)) + ",";
    out += field(detail::kColumnMaxSlowDb, toChars(block.maxSlowDb)) + ",";
    out += field(detail::kColumnPeakCDb, toChars(block.peakDb)) + ",";
    out += field(detail::kColumnFlags, toChars(block.flags));
    out += "}";
    return out;
}

/// C9: a RECONSTRUCTED instant, never a wall-clock reading (SPL-R2 as
/// amended): `startedAtUnixMs + elapsedSamples/sampleRate*1000`, where
/// `elapsedSamples` is the caller's own running
/// `Sigma(blockSamples_i + droppedSamples_i)` -- immune to a gap, unlike
/// `startedAtUnixMs + blockIndex*blockSamples/fs`, which is early by the
/// gap's own duration for the rest of the session.
[[nodiscard]] inline std::uint64_t reconstructedElapsedUnixMs(std::uint64_t startedAtUnixMs,
                                                              double sampleRate,
                                                              std::uint64_t elapsedSamples) noexcept {
    if (!(sampleRate > 0.0)) return startedAtUnixMs;
    const double elapsedMs = static_cast<double>(elapsedSamples) / sampleRate * 1000.0;
    return startedAtUnixMs + static_cast<std::uint64_t>(elapsedMs + 0.5);
}

/// The append-only, disk-touching half (C4, C5, C6). One instance per logged
/// channel: it owns one growing segment file at a time, rotates at
/// `segmentBlocks` and NEVER deletes (asserted structurally: no unlink/remove
/// call anywhere in SplLogWriter.cpp). Implemented in SplLogWriter.cpp, the
/// FirTextWriter.cpp precedent -- the only file here that opens a stream.
class SplLogWriter {
public:
    /// @param basePath      directory + filename stem, no extension; segment
    ///                      N is `basePath + "." + N + ".csv"`.
    /// @param segmentBlocks record §13 Q7's default of one hour of blocks.
    SplLogWriter(std::string basePath, rta::measure::SplConfig config, SplLogHeaderInfo info,
                std::uint64_t segmentBlocks);

    /// Writes one block's row, rotating to a new segment at
    /// `segmentBlocks`-aligned boundaries (C5) -- never deleting anything.
    void write(const rta::meter::Block& block);

    /// Changes the weighting/detector this writer stamps into its header. A
    /// no-op if both already match. Otherwise closes the current file and
    /// opens a BRAND NEW log (a fresh generation suffix, segment 0) rather
    /// than writing a mixed file -- C4's own wording: "starts a new log
    /// rather than writing a mixed file", made true by never appending a
    /// changed header to an existing one.
    void reconfigure(rta::dsp::WeightingType weighting, rta::meter::TimeWeighting detector);

    [[nodiscard]] const std::vector<std::string>& segmentPaths() const noexcept {
        return segmentPaths_;
    }

    /// Station-4 fix round (PR #31, verifier finding 6, MEDIUM): true once
    /// any `openSegment()` call -- the constructor's own first call, or a
    /// later rotation/reconfigure -- failed to open its file (an unwritable
    /// or missing directory, most often). STICKY: once true, always true,
    /// because a segment that failed once means this session's log already
    /// has a gap no later success can back-fill, and "still logging, and
    /// nothing looked wrong for the last five minutes" is a worse thing to
    /// report than a false alarm on a log that recovered.
    [[nodiscard]] bool openFailed() const noexcept { return openFailed_; }

private:
    void openSegment();

    std::string basePath_;
    rta::measure::SplConfig config_;
    SplLogHeaderInfo info_;
    std::uint64_t segmentBlocks_;

    std::uint64_t segmentIndex_ = 0;
    std::uint64_t logGeneration_ = 0;
    std::uint64_t blocksInSegment_ = 0;
    std::ofstream stream_;
    std::vector<std::string> segmentPaths_;
    bool openFailed_ = false;
};

}  // namespace rta::splexport
