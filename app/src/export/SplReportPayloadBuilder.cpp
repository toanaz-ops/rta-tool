// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/export. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
#include "export/SplReportPayloadBuilder.h"

#include "export/SplCalibrationRecord.h"
#include "export/SplLog.h"
#include "export/SplLogHeaderParse.h"
#include "measure/SplConfig.h"

#include "rta/meter/Block.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <tuple>

namespace rta::splexport {

namespace {

namespace fs = std::filesystem;

std::string readWholeFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Parses "<basename>.gen<G>.seg<S>.csv" -> (G, S), or `std::nullopt` if
/// `name` does not match that exact shape -- so a stray file in the session
/// directory (the calibration record, the session header, another channel's
/// log) is silently skipped rather than misread as a segment.
std::optional<std::pair<std::uint64_t, std::uint64_t>> parseGenSeg(const std::string& name,
                                                                    const std::string& basename) {
    const std::string prefix = basename + ".gen";
    if (name.rfind(prefix, 0) != 0) return std::nullopt;
    std::size_t i = prefix.size();
    const std::size_t genStart = i;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    if (i == genStart) return std::nullopt;
    const std::uint64_t gen = std::stoull(name.substr(genStart, i - genStart));

    const std::string segMarker = ".seg";
    if (name.compare(i, segMarker.size(), segMarker) != 0) return std::nullopt;
    i += segMarker.size();
    const std::size_t segStart = i;
    while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
    if (i == segStart) return std::nullopt;
    const std::uint64_t seg = std::stoull(name.substr(segStart, i - segStart));

    if (name.compare(i, std::string::npos, ".csv") != 0) return std::nullopt;
    return std::make_pair(gen, seg);
}

/// One logged channel's whole recorded history: every block from every
/// generation/segment, oldest first, plus the raw bytes (for the integrity
/// hash) and the LAST segment's own header (settings never change within one
/// generation -- record §10, C4 -- and a calibration-triggered generation
/// bump is a deliberate NEW log with its own header, so the most recent one
/// describes the session's CURRENT calibration state, which is what §9
/// item 3/4 report).
struct ChannelLog {
    std::vector<rta::meter::Block> blocks;
    std::size_t bytesDiscarded = 0;
    std::vector<std::string> segmentRawBytes;
    std::vector<std::string> segmentPaths;
    std::optional<SplLogHeaderParsed> header;
};

ChannelLog readChannelLog(const fs::path& dir, int channel) {
    ChannelLog result;
    const std::string basename = "ch" + std::to_string(channel);

    std::vector<std::tuple<std::uint64_t, std::uint64_t, fs::path>> files;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return result;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (auto gs = parseGenSeg(entry.path().filename().string(), basename)) {
            files.emplace_back(gs->first, gs->second, entry.path());
        }
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
        return std::get<1>(a) < std::get<1>(b);
    });

    for (const auto& entry : files) {
        const fs::path& path = std::get<2>(entry);
        const std::string raw = readWholeFile(path);
        const auto part = readLog(raw);
        result.blocks.insert(result.blocks.end(), part.blocks.begin(), part.blocks.end());
        result.bytesDiscarded += part.bytesDiscarded;
        result.segmentPaths.push_back(path.string());
        if (auto parsedHeader = parseLogHeader(raw)) {
            result.header = parsedHeader;  // last one wins -- see ChannelLog's own comment
        }
        result.segmentRawBytes.push_back(std::move(raw));
    }
    return result;
}

/// Task W2-E2b fix round (MEDIUM finding): record §9 item 7's time history
/// and markers, filled from the log's own per-block rows -- neither was
/// wired at all before this fix, and the PR that shipped without them did
/// not say so.
///
/// One history point per block NOT excluded from compliance windows (record
/// §3/§15 A2's own membership rule) -- a session export must not plot a
/// `CalibrationInvalid` block as if its level were trustworthy, the same
/// reasoning that already excludes it from the recomputed Leq. Overload and
/// Gap markers, in contrast, are drawn from EVERY block regardless of
/// calibration validity: they are facts about the raw signal (a clipped
/// waveform, samples the bus lost), not about whether the calibration held.
/// Alarm transition markers are NOT derived here -- `SplAlarms`' fired/
/// cleared state lives only in memory (record §9's own validity-section
/// honesty sentence, `renderValidity`), and inventing one from the log alone
/// would be a marker this format cannot actually support.
void appendHistoryAndMarkers(int channel, const std::vector<rta::meter::Block>& blocks,
                             double referenceOffsetDb, ReportPayload& payload) {
    ReportHistorySeries series;
    series.metricId = "ch" + std::to_string(channel);
    series.points.reserve(blocks.size());

    for (const auto& block : blocks) {
        if (!rta::meter::hasFlag(block.flags, rta::meter::BlockFlag::CalibrationInvalid)) {
            ReportHistoryPoint point;
            point.blockIndex = block.blockIndex;
            // The SAME per-block formula SplLog.h::logRow stamps into the
            // log's own `leqDb` column (record §10) -- recomputed here from
            // the raw columns rather than re-parsed from that derived one, so
            // a bracket-excluded block (flagged only in this function's own
            // in-memory copy, never on disk) cannot disagree with it.
            point.valueDb = block.blockSamples > 0
                                ? 10.0 * std::log10(block.sumSquares /
                                                    static_cast<double>(block.blockSamples)) +
                                      referenceOffsetDb
                                : rta::meter::kLevelFloorDb;
            series.points.push_back(point);
        }

        if (rta::meter::hasFlag(block.flags, rta::meter::BlockFlag::Overload)) {
            rta::measure::SplMarker marker;
            marker.blockIndex = block.blockIndex;
            marker.kind = rta::measure::SplMarkerKind::Overload;
            marker.quantity = series.metricId;
            payload.markers.push_back(marker);
        }
        if (rta::meter::hasFlag(block.flags, rta::meter::BlockFlag::Gap)) {
            rta::measure::SplMarker marker;
            marker.blockIndex = block.blockIndex;
            marker.kind = rta::measure::SplMarkerKind::Gap;
            marker.quantity = series.metricId;
            payload.markers.push_back(marker);
        }
    }

    if (!series.points.empty()) payload.history.push_back(std::move(series));
}

}  // namespace

SplReportBuildResult buildReportPayload(const SplReportBuildRequest& request) {
    SplReportBuildResult out;
    const fs::path dir(request.sessionDir);

    std::optional<SplCalibrationRecordInfo> calibrationRecord;
    {
        std::error_code ec;
        const fs::path calPath = dir / "calibration.txt";
        if (fs::exists(calPath, ec)) {
            calibrationRecord = parseCalibrationRecord(readWholeFile(calPath));
        }
    }

    ReportPayload payload;
    payload.venue = request.venue;
    payload.event = request.event;
    payload.operatorName = request.operatorName;
    payload.company = request.company;
    payload.engineer = request.engineer;
    payload.productionCompany = request.productionCompany;
    payload.notes = request.notes;
    payload.appName = request.appName;
    payload.appVersion = request.appVersion;
    payload.buildId = request.buildId;
    payload.device = request.device;

    // Wave 3 cut fallback (plan W3-C): a record that was never written, or
    // one whose own `performed` is false, leaves `payload.calibration`
    // default-constructed, which is exactly `performed == false` --
    // `renderReport` already prints "calibration check not performed" for
    // that value with no branch needed here.
    if (calibrationRecord.has_value()) {
        payload.calibration = calibrationRecord->fields;
    }

    std::vector<std::string> allSegmentBytes;
    std::optional<SplLogHeaderParsed> firstHeader;
    bool any = false;

    for (const int channel : request.channels) {
        ChannelLog log = readChannelLog(dir, channel);
        if (log.segmentPaths.empty()) {
            out.channelsWithNoLog.push_back(channel);
            continue;
        }
        any = true;
        if (!firstHeader.has_value() && log.header.has_value()) firstHeader = log.header;

        // Record §15 A2 / task acceptance: a calibration record measured on
        // THIS channel, whose drift FAILED (0.5 dB > cl. 5.2), marks its own
        // bracketed range CalibrationInvalid here, in the copy this function
        // just read into memory -- the file on disk is never rewritten
        // (W3-A A3, record §10). A PASSING drift brackets nothing: the pair
        // certifies the blocks between it, it does not exclude them.
        const bool bracketApplies = calibrationRecord.has_value() && calibrationRecord->fields.performed &&
                                    calibrationRecord->channel == channel &&
                                    calibrationRecord->fields.verdict.has_value() &&
                                    *calibrationRecord->fields.verdict ==
                                        rta::measure::CalibrationVerdict::Fail;
        if (bracketApplies) {
            for (auto& block : log.blocks) {
                if (block.blockIndex >= calibrationRecord->startBlockIndex &&
                    block.blockIndex <= calibrationRecord->endBlockIndex) {
                    block.flags |= rta::meter::flagMask(rta::meter::BlockFlag::CalibrationInvalid);
                }
            }
        }

        payload.validity.segmentPaths.insert(payload.validity.segmentPaths.end(),
                                             log.segmentPaths.begin(), log.segmentPaths.end());
        payload.validity.bytesDiscarded += log.bytesDiscarded;
        payload.validity.totalBlocks += log.blocks.size();
        allSegmentBytes.insert(allSegmentBytes.end(),
                               std::make_move_iterator(log.segmentRawBytes.begin()),
                               std::make_move_iterator(log.segmentRawBytes.end()));

        const double referenceOffsetDb = log.header.has_value() ? log.header->referenceOffsetDb : 0.0;
        const double sampleRate = log.header.has_value() ? log.header->info.sampleRate : 0.0;
        // Record §3: the WHOLE session, recomputed from every block just
        // read -- never a running subtraction. `windowBlocks ==
        // blocks.size()` asks combineBlocks for exactly the membership
        // already in `log.blocks`, nothing sliding.
        const auto whole = rta::meter::combineBlocks(
            log.blocks, sampleRate, referenceOffsetDb, static_cast<std::uint64_t>(log.blocks.size()));

        payload.validity.excludedBlocks += whole.excludedBlocks;
        payload.validity.overloadBlocks += whole.overloadBlocks;
        payload.validity.underRangeBlocks += whole.underRangeBlocks;
        payload.validity.droppedBlocks += whole.droppedBlocks;
        payload.validity.gapBlocks += whole.gapBlocks;
        payload.validity.droppedSamplesTotal += whole.droppedSamplesTotal;

        appendHistoryAndMarkers(channel, log.blocks, referenceOffsetDb, payload);

        ReportMetricResult metric;
        metric.id = "ch" + std::to_string(channel);
        if (log.header.has_value()) {
            metric.weighting = log.header->info.weighting;
            metric.detector = log.header->info.detector;
        }
        metric.intervalSeconds = whole.seconds;
        metric.leqDb = whole.leqDb;
        metric.lmaxDb = whole.maxFastDb;
        metric.lpeakDb = whole.peakDb;  // L_Cpeak, SAMPLED -- ReportMetricResult's own doc comment
        // SEL = Leq + 10*log10(T/1s): ISO 1996-1's own definition of sound
        // exposure level, a closed-form function of the Leq this function
        // just recomputed and the measured duration `combineBlocks` already
        // reports -- not a second aggregation over the blocks.
        if (whole.leqDb.has_value() && whole.seconds > 0.0) {
            metric.selDb = *whole.leqDb + 10.0 * std::log10(whole.seconds);
        }
        // lminDb: no running minimum exists anywhere in the log or the live
        // state (only maxima and a sampled peak are tracked -- record §2),
        // so it stays absent rather than a fabricated value.
        payload.metrics.push_back(std::move(metric));
    }

    if (firstHeader.has_value()) {
        payload.config.referenceOffsetDb = firstHeader->referenceOffsetDb;
        payload.config.calibrated = firstHeader->calibrated;
        payload.sampleRate = firstHeader->info.sampleRate;
    }

    // Task part B's own instruction: Ln/dose/alarm are NOT in the log --
    // read from the live view the composition root supplied, and the
    // validity section says whether one was available at all.
    payload.validity.lnDoseAlarmFromLiveSession = request.liveView.has_value();
    if (request.liveView.has_value()) {
        const auto& live = *request.liveView;
        payload.validity.refusedMetrics = live.refusedMetrics;
        for (const auto& alarm : live.alarms) {
            ReportAlarmResult row;
            row.metricId = alarm.metricId;
            row.limitDb = alarm.limitDb;
            row.windowBlocks = alarm.windowBlocks;
            row.state = alarm.state;
            row.headroomDb = alarm.headroomDb;
            payload.alarms.push_back(std::move(row));
        }
        for (std::size_t i = 0; i < payload.dose.size() && i < live.dosePercent.size(); ++i) {
            payload.dose[i].percent = live.dosePercent[i];
            payload.dose[i].projectedPercent = live.doseProjected[i];
        }
        // The live view carries ONE Ln set, for the channel it published --
        // attached to metric row 0 when one exists, matching
        // AnalysisPublish.cpp's own "Wave 0 publishes the first logged
        // channel" convention. `lnPercents` has no per-session source yet
        // (the running pipeline logs with `SplConfig{}` defaults --
        // MainComponentSpl.cpp's own comment), so it is read from that same
        // default rather than invented here.
        if (!payload.metrics.empty()) {
            payload.metrics.front().lnDb = live.lnDb;
            payload.metrics.front().lnPercents = rta::measure::SplConfig{}.lnPercents;
        }
    }

    payload.logSegments = std::move(allSegmentBytes);

    if (any) out.payload = std::move(payload);
    return out;
}

}  // namespace rta::splexport
