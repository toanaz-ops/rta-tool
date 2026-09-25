// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
// Lane L6a task W2-E2b part B (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md
// "W2-E -- the wiring nobody was assigned"; record docs/dsp/
// 2026-09-16-spl-pro-l6a.md §10).
//
// Split out of SplLog.h rather than grown into it (that file was already
// within a few lines of the project's 400-line hard cap): the inverse of
// `logHeader()`, needed only by a reader that has nothing but the bytes on
// disk -- SplLogWriter itself never needs its own inverse, only the report
// payload builder (SplReportPayloadBuilder.cpp) does.
#pragma once

#include "export/SplLog.h"
#include "trace/SessionCodecDetail.h"

#include "rta/dsp/Weighting.h"
#include "rta/meter/Detector.h"

#include <optional>
#include <string_view>

namespace rta::splexport {

namespace detail {

/// The inverse of `rta::dsp::toString`/`rta::meter::toString` for exactly the
/// two enums `logHeader` stamps -- local to this file because no production
/// caller needs to parse a weighting/detector name back except a log reader,
/// and neither core header exports a `fromString`.
[[nodiscard]] inline std::optional<rta::dsp::WeightingType> parseWeightingType(
    std::string_view s) noexcept {
    if (s == "A") return rta::dsp::WeightingType::A;
    if (s == "C") return rta::dsp::WeightingType::C;
    if (s == "Z") return rta::dsp::WeightingType::Z;
    return std::nullopt;
}
[[nodiscard]] inline std::optional<rta::meter::TimeWeighting> parseTimeWeighting(
    std::string_view s) noexcept {
    if (s == "Fast") return rta::meter::TimeWeighting::Fast;
    if (s == "Slow") return rta::meter::TimeWeighting::Slow;
    if (s == "Impulse") return rta::meter::TimeWeighting::Impulse;
    return std::nullopt;
}

}  // namespace detail

/// The inverse of `logHeader()`, plus the two facts it stamped from
/// `SplConfig` rather than `SplLogHeaderInfo` (`referenceOffsetDb`,
/// `calibrated`) -- a reader of a log file on disk has no `SplConfig` to hand
/// it, only the bytes `logHeader` wrote into them. Without `referenceOffsetDb`
/// in particular, `rta::meter::combineBlocks` cannot recompute a calibrated
/// Leq from the raw `sumSquares` column: record §10's redundancy ("the log is
/// the evidence") is only real if a reader can recover the offset the row was
/// written under.
struct SplLogHeaderParsed {
    SplLogHeaderInfo info;
    double referenceOffsetDb = 0.0;
    bool calibrated = false;
};

/// NO ATOMICITY CLAIM, the `readLog` precedent: a header missing any of the
/// five keys a caller needs (weighting, detector, blockSamples, sampleRate,
/// calibrationOffsetDb) returns `std::nullopt` rather than a half-filled
/// struct that is silently wrong. Stops at the first line that is not a
/// `# key=value` comment -- the `csvHeaderRow()` column-name row -- exactly
/// where `logHeader`'s own output ends.
[[nodiscard]] inline std::optional<SplLogHeaderParsed> parseLogHeader(std::string_view csvBody) {
    using rta::trace::detail::splitLine;
    using rta::trace::detail::stripTrailingCr;
    using rta::trace::detail::tryParse;

    SplLogHeaderParsed parsed;
    bool sawWeighting = false, sawDetector = false, sawBlockSamples = false, sawSampleRate = false,
        sawOffset = false;
    std::size_t pos = 0;
    while (pos < csvBody.size()) {
        const auto newlinePos = csvBody.find('\n', pos);
        if (newlinePos == std::string_view::npos) break;
        const auto rawLine = csvBody.substr(pos, newlinePos - pos);
        pos = newlinePos + 1;
        const auto line = stripTrailingCr(rawLine);
        if (line.rfind("# ", 0) != 0) break;  // the CSV column-name row ends the header block
        const auto content = line.substr(2);
        std::string_view key, value;
        if (!splitLine(content, key, value)) continue;

        if (key == "startedAtUnixMs") {
            tryParse(value, parsed.info.startedAtUnixMs);
        } else if (key == "sampleRate") {
            if (tryParse(value, parsed.info.sampleRate)) sawSampleRate = true;
        } else if (key == "blockSamples") {
            if (tryParse(value, parsed.info.blockSamples)) sawBlockSamples = true;
        } else if (key == "weighting") {
            if (auto w = detail::parseWeightingType(value)) {
                parsed.info.weighting = *w;
                sawWeighting = true;
            }
        } else if (key == "detector") {
            if (auto d = detail::parseTimeWeighting(value)) {
                parsed.info.detector = *d;
                sawDetector = true;
            }
        } else if (key == "calibrationOffsetDb") {
            if (tryParse(value, parsed.referenceOffsetDb)) sawOffset = true;
        } else if (key == "calibrationUnit") {
            parsed.calibrated = (value == "dbspl");
        } else if (key == "calibratorLevelDb") {
            double v = 0.0;
            if (tryParse(value, v)) parsed.info.calibratorLevelDb = v;
        }
        // "schema" / "histogramBaseDb": not needed by any caller yet, skipped.
    }
    if (!sawWeighting || !sawDetector || !sawBlockSamples || !sawSampleRate || !sawOffset) {
        return std::nullopt;
    }
    return parsed;
}

}  // namespace rta::splexport
