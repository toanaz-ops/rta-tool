// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// RT60-UI lane (docs/plans/2026-10-20-rt60-ui-lane-plan.md) task T2, decisions
// 5, 7, 8 and 9: the words and numbers of the RT60 pane, as plain strings so a
// test with no window can read them.
//
// Units (CLAUDE.md "Reading out numbers" and decision 8): seconds two decimals
// ("0.80 s" -- an RT difference becomes audible near 5 %, 15 ms at 0.30 s, so
// 0.01 s resolves it and a third decimal is far below the measured spread);
// frequency a whole number of hertz, the nominal label an operator reads off a
// rack (63, 125 ... 8000); dB one decimal; B*T one decimal; tail one decimal.
//
// Non-ASCII text is written as escaped UTF-8 bytes (the middle dot U+00B7 is
// "\xc2\xb7"); a test asserts the separator is visible, not a stray A-circumflex.
#pragma once

#include "measure/Rt60Analysis.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace rta::measure {

/// The tails the pane offers, in seconds (decision 2). 3 s is the default and
/// what the rail SWEEP button always uses.
inline constexpr std::array<double, 3> kRt60TailChoicesSeconds{3.0, 6.0, 10.0};

/// "0.80 s".
[[nodiscard]] std::string rt60Seconds(double seconds);
/// "125 Hz": whole hertz, rounded.
[[nodiscard]] std::string rt60Hertz(double hz);
/// "47.2 dB".
[[nodiscard]] std::string rt60Db(double db);

/// One band as the table shows it. A refused figure is "--" and the reason is
/// in `note`, with the numbers it was decided on.
struct Rt60Row {
    std::string hz;     ///< "63" ... "8000"
    std::string t30;    ///< "0.80 s" or "--"
    std::string t20;
    std::string edt;
    std::string range;  ///< "47.2 dB": the decay range reached, or "--"
    std::string note;   ///< empty when nothing was refused
};

/// The reason a band's figures are withheld, with its numbers: T30's reason if
/// T30 refused, else T20's, else EDT's. Empty when nothing refused.
[[nodiscard]] std::string rt60BandNote(const Rt60Band& band, const Rt60Result& result);
[[nodiscard]] Rt60Row rt60Row(const Rt60Band& band, const Rt60Result& result);

/// Why the whole analysis was withheld (no IR, no tail, no lead-in). Empty
/// when it was analysed. `irKept` is how many IRs the store keeps.
[[nodiscard]] std::string rt60WholeRefusalText(const Rt60Result& result, std::size_t irKept);
/// The reason for a SWEEP trace that has no IR: opened from a file, or dropped
/// from the store, which keeps `kept` of them. The picker shows it too.
[[nodiscard]] std::string rt60NoIrText(std::size_t kept);

/// Header, one line per entry: which IR, then tail / trusted band / method /
/// how long the analysis took. `irStillInMemory` false appends the eviction
/// notice (decision 9: the result stays shown).
[[nodiscard]] std::vector<std::string> rt60HeaderLines(const Rt60Result& result, const std::string& irName,
                                                       bool irStillInMemory);
/// Column heading, then one line per band, fixed widths for a monospaced font.
[[nodiscard]] std::vector<std::string> rt60TableLines(const Rt60Result& result);
/// The EDT footnote (decision 7).
[[nodiscard]] std::string rt60Footnote();

/// The advice line (plan F6), empty when none applies. Shown when any band
/// refused for want of a noise floor, or when 1.5 x the longest reported
/// T30/T20 exceeds the tail the IR held. It names the next TAIL choice; nothing
/// acts on it.
[[nodiscard]] std::string rt60Advice(const Rt60Result& result);

}  // namespace rta::measure
