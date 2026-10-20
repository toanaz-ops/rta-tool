// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/Rt60EnsembleReadout.h"

#include <cmath>
#include <cstdio>

namespace rta::measure {

namespace {

const char* const kDot = "\xc2\xb7";  // U+00B7, as Rt60Readout.cpp

std::string padLeft(const std::string& text, std::size_t width) {
    return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

std::string oneDecimal(double value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.1f", value);
    return text;
}

// The ensemble band as a single-IR band, so the reason a figure was withheld is
// worded by the same function the single table uses.
Rt60Band asSingleBand(const Rt60EnsembleBand& band) {
    Rt60Band out;
    out.nominalHz = band.nominalHz;
    out.lowHz = band.lowHz;
    out.highHz = band.highHz;
    out.refusal = band.refusal;
    out.t30 = {band.t30.seconds, band.t30.refusal};
    out.t20 = {band.t20.seconds, band.t20.refusal};
    out.edt = {band.edt.seconds, band.edt.refusal};
    out.bandwidthTime = band.bandwidthTime;
    return out;
}

Rt60Result asSingleResult(const Rt60EnsembleResult& result) {
    Rt60Result out;
    out.bandSet = result.bandSet;
    out.sampleRate = result.sampleRate;
    out.tailSeconds = result.tailSeconds;
    out.trustedLowHz = result.trustedLowHz;
    out.trustedHighHz = result.trustedHighHz;
    return out;
}

}  // namespace

std::string rt60SpreadCell(const Rt60Spread& spread) {
    if (!spread.has()) return "--";
    std::string cell = rt60Seconds(spread.seconds);
    if (spread.spreadIsMeaningful()) cell += "  IQR " + std::to_string(std::llround(spread.spreadPercent)) + "%";
    return cell;
}

Rt60Row rt60EnsembleRow(const Rt60EnsembleBand& band, const Rt60EnsembleResult& result) {
    Rt60Row row;
    row.hz = std::to_string(band.nominalHz);
    row.t30 = rt60SpreadCell(band.t30);
    row.t20 = rt60SpreadCell(band.t20);
    row.edt = rt60SpreadCell(band.edt);
    row.range = band.refusal == Rt60Refusal::OutsideTrustedBand
                    ? "--"
                    : std::to_string(band.t30.captures) + " of " + std::to_string(result.captureCount);
    row.note = rt60BandNote(asSingleBand(band), asSingleResult(result));
    return row;
}

std::vector<std::string> rt60EnsembleTableLines(const Rt60EnsembleResult& result) {
    std::vector<std::string> lines;
    lines.push_back(padLeft("Hz", 5) + padLeft("T30", 18) + padLeft("T20", 18) + padLeft("EDT*", 18) +
                    padLeft("captures", 10) + "  note");
    for (const auto& band : result.bands) {
        const Rt60Row row = rt60EnsembleRow(band, result);
        lines.push_back(padLeft(row.hz, 5) + padLeft(row.t30, 18) + padLeft(row.t20, 18) + padLeft(row.edt, 18) +
                        padLeft(row.range, 10) + "  " + row.note);
    }
    return lines;
}

std::vector<std::string> rt60EnsembleHeaderLines(const Rt60EnsembleResult& result,
                                                 const std::vector<std::string>& names) {
    const std::string dot = std::string(" ") + kDot + " ";
    std::string list;
    for (const auto& name : names) list += (list.empty() ? "" : ", ") + name;
    std::vector<std::string> lines;
    lines.push_back("ENSEMBLE " + std::to_string(result.captureCount) + " captures: " + list);
    if (!result.analysed()) return lines;
    const std::string trusted = result.trustedLowHz > 0.0
        ? rt60Hertz(result.trustedLowHz) + " to " + rt60Hertz(result.trustedHighHz)
        : "not stated";
    std::string line = "shortest tail held " + oneDecimal(result.tailSeconds) + " s" + dot + "trusted band " +
                       trusted + dot + "median across captures";
    // The rule, said where the missing number would have been looked for.
    line += result.captureCount >= 3 ? ", IQR in percent of the median"
                                     : ", IQR needs 3 captures (two points give only their gap)";
    char wall[32];
    std::snprintf(wall, sizeof wall, "%.0f", result.wallMs);
    line += dot + "analysed in " + wall + " ms";
    lines.push_back(line);
    return lines;
}

std::string rt60EnsembleRefusalText(const Rt60EnsembleResult& result) {
    if (result.analysed()) return {};
    if (!result.message.empty()) return "ensemble failed: " + result.message;
    if (result.refusal == Rt60Refusal::NoIr) return "ensemble withheld: a selected capture has no IR in memory; sweep again";
    const Rt60Result single = [&result] {
        Rt60Result r;
        r.refusal = result.refusal;
        return r;
    }();
    return "ensemble withheld: " + rt60WholeRefusalText(single, 0);
}

}  // namespace rta::measure
