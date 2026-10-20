// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/Rt60Readout.h"

#include "rta/ir/Decay.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rta::measure {

namespace {

// U+00B7, the separator the other readouts use ("B*T" is written "B" dot "T").
const char* const kDot = "\xc2\xb7";

// What each figure's fit span needs below the start of the decay: T30 fits
// -5 to -35 dB and T20 -5 to -25 dB (record L4b section 1a), so the curve must
// FALL that far. The bar is core's (`fitOne` in Decay.cpp); the app does not
// add a second one (plan decision 5).
constexpr double kT30NeedsDb = 35.0;
constexpr double kT20NeedsDb = 25.0;

// How far past the longest decay the tail should run before the advice stays
// quiet: L4a's bound, restated by plan F6.
constexpr double kTailOverDecay = 1.5;

std::string format(const char* pattern, double value) {
    char text[64];
    std::snprintf(text, sizeof text, pattern, value);
    return text;
}

std::string padLeft(const std::string& text, std::size_t width) {
    return text.size() >= width ? text : std::string(width - text.size(), ' ') + text;
}

std::string bandwidthTimeText(double bandwidthTime) {
    // The bar is core's own default, read rather than copied.
    const double bar = rta::ir::DecayConfig{}.minBandwidthTime;
    char text[96];
    std::snprintf(text, sizeof text, "band too narrow for a decay this short (B%sT %.1f < %.0f)", kDot,
                  bandwidthTime, bar);
    return text;
}

// One refusal, in words, with the numbers it was decided on. `needsDb` is the
// span the figure being explained needs ("T30 needs 35 dB").
std::string reasonText(Rt60Refusal why, const Rt60Band& band, const Rt60Result& result, const char* figure,
                       double needsDb) {
    switch (why) {
        case Rt60Refusal::None:
            return {};
        case Rt60Refusal::BandwidthTimeTooSmall:
            return bandwidthTimeText(band.bandwidthTime);
        case Rt60Refusal::RangeTooSmall: {
            char text[96];
            if (band.hasRange) {
                std::snprintf(text, sizeof text, "decay reaches %.1f dB; %s needs %.0f dB", band.rangeDb, figure,
                              needsDb);
            } else {
                std::snprintf(text, sizeof text, "decay too short for %s (needs %.0f dB)", figure, needsDb);
            }
            return text;
        }
        case Rt60Refusal::NoNoiseEstimate:
            return "no noise floor found in the " + format("%.1f", result.tailSeconds) + " s tail";
        case Rt60Refusal::NoDecayFound:
            return "no decreasing slope found";
        case Rt60Refusal::NoLeadIn:
            return "no lead-in before the arrival";
        case Rt60Refusal::OutsideTrustedBand:
            if (result.trustedLowHz > 0.0 && band.lowHz < result.trustedLowHz) {
                return "below this sweep's trusted band (" + rt60Hertz(result.trustedLowHz) + ")";
            }
            return "above this sweep's trusted band (" + rt60Hertz(result.trustedHighHz) + ")";
        case Rt60Refusal::NoIr:
            return "no IR to analyse";  // rt60WholeRefusalText gives the full sentence
        case Rt60Refusal::NoTail:
            return "this IR does not say how long its capture ran";
    }
    return {};
}

double longestReported(const Rt60Result& result) {
    double longest = 0.0;
    for (const auto& band : result.bands) {
        if (band.t30.has()) longest = std::max(longest, band.t30.seconds);
        else if (band.t20.has()) longest = std::max(longest, band.t20.seconds);
    }
    return longest;
}

}  // namespace

std::string rt60Seconds(double seconds) { return format("%.2f", seconds) + " s"; }

std::string rt60Hertz(double hz) { return std::to_string(std::llround(hz)) + " Hz"; }

std::string rt60Db(double db) { return format("%.1f", db) + " dB"; }

std::string rt60BandNote(const Rt60Band& band, const Rt60Result& result) {
    if (band.refusal == Rt60Refusal::OutsideTrustedBand) {
        return reasonText(band.refusal, band, result, "", 0.0);
    }
    if (!band.t30.has()) return reasonText(band.t30.refusal, band, result, "T30", kT30NeedsDb);
    if (!band.t20.has()) return reasonText(band.t20.refusal, band, result, "T20", kT20NeedsDb);
    if (!band.edt.has()) return reasonText(band.edt.refusal, band, result, "EDT", 10.0);
    return {};
}

Rt60Row rt60Row(const Rt60Band& band, const Rt60Result& result) {
    const auto cell = [](const Rt60Figure& figure) { return figure.has() ? rt60Seconds(figure.seconds) : "--"; };
    Rt60Row row;
    row.hz = std::to_string(band.nominalHz);
    row.t30 = cell(band.t30);
    row.t20 = cell(band.t20);
    row.edt = cell(band.edt);
    row.range = band.hasRange ? rt60Db(band.rangeDb) : "--";
    row.note = rt60BandNote(band, result);
    return row;
}

std::string rt60NoIrText(std::size_t kept) {
    return "no IR: opened from file or dropped from memory (" + std::to_string(kept) + " kept); sweep again";
}

std::string rt60WholeRefusalText(const Rt60Result& result, std::size_t irKept) {
    if (result.analysed()) return {};
    if (result.refusal == Rt60Refusal::NoIr && result.message.empty()) return rt60NoIrText(irKept);
    if (!result.message.empty()) return "analysis failed: " + result.message;
    if (result.refusal == Rt60Refusal::NoLeadIn) {
        return "the arrival is too close to the start of the IR for the band filters; sweep again";
    }
    Rt60Band none;
    return reasonText(result.refusal, none, result, "", 0.0);
}

std::vector<std::string> rt60HeaderLines(const Rt60Result& result, const std::string& irName,
                                         bool irStillInMemory) {
    std::vector<std::string> lines;
    lines.push_back("IR: " + irName + (irStillInMemory ? "" : " -- IR no longer in memory"));
    if (!result.analysed()) return lines;
    const std::string trusted = result.trustedLowHz > 0.0
        ? rt60Hertz(result.trustedLowHz) + " to " + rt60Hertz(result.trustedHighHz)
        : "not stated";
    const std::string dot = std::string(" ") + kDot + " ";
    // "held", not "requested": the capture also keeps what is left of the 1.0 s
    // pre-roll after the loop's arrival, so the IR holds the TAIL choice plus
    // that slack, and the knob and this number differ by about 0.9 s.
    lines.push_back("tail held " + format("%.1f", result.tailSeconds) + " s" + dot + "trusted band " + trusted + dot +
                    "Lundeby truncation, zero-phase " +
                    (result.bandSet == Rt60BandSet::Third ? "third-octave" : "octave") + " filters" + dot +
                    "analysed in " +
                    format("%.0f", result.wallMs) + " ms");
    return lines;
}

std::vector<std::string> rt60TableLines(const Rt60Result& result) {
    std::vector<std::string> lines;
    lines.push_back(padLeft("Hz", 5) + padLeft("T30", 10) + padLeft("T20", 10) + padLeft("EDT*", 10) +
                    padLeft("range", 10) + "  note");
    for (const auto& band : result.bands) {
        const Rt60Row row = rt60Row(band, result);
        lines.push_back(padLeft(row.hz, 5) + padLeft(row.t30, 10) + padLeft(row.t20, 10) + padLeft(row.edt, 10) +
                        padLeft(row.range, 10) + "  " + row.note);
    }
    return lines;
}

std::string rt60Footnote() { return "* EDT has no validity check (record sec. 9); read it as indicative"; }

std::string rt60Advice(const Rt60Result& result) {
    if (!result.analysed()) return {};
    bool missingNoise = false;
    for (const auto& band : result.bands) {
        if (band.refusal != Rt60Refusal::OutsideTrustedBand && band.t30.refusal == Rt60Refusal::NoNoiseEstimate) {
            missingNoise = true;
        }
    }
    const double longest = longestReported(result);
    const double needed = kTailOverDecay * longest;
    const bool tooShort = needed > result.tailSeconds;
    if (!tooShort && !missingNoise) return {};

    // Without a length to reach (no noise floor), "longer than the tail just
    // measured" is the whole requirement, so the target is zero.
    std::string why;
    double target = 0.0;
    if (tooShort) {
        why = "longest decay " + rt60Seconds(longest) + " needs a tail of at least " + rt60Seconds(needed);
        target = needed;
    } else {
        why = "a band found no noise floor in the " + format("%.1f", result.tailSeconds) + " s tail";
    }
    // The shortest offered tail that both covers the need and is longer than
    // the one that was just measured -- offering the same tail again is not advice.
    for (const double choice : kRt60TailChoicesSeconds) {
        if (choice >= target && choice > result.tailSeconds) {
            return why + ": choose TAIL " + format("%.0f", choice) + " s and sweep again";
        }
    }
    return why + "; no TAIL choice is long enough (the longest is " +
           format("%.0f", kRt60TailChoicesSeconds.back()) + " s)";
}

}  // namespace rta::measure
