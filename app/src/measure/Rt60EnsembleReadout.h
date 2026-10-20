// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
//
// RT60-UI lane wave B, task T9 (docs/plans/2026-10-20-rt60-ui-lane-plan.md,
// decisions 7 and 8): the words of the ensemble table, as plain strings so a test
// with no window can read them. Same units as Rt60Readout.h (seconds two
// decimals, whole hertz); the spread is a whole percent (decision 8).
//
// The rule this file carries: the spread is printed ONLY from three captures on
// (`Rt60Spread::spreadIsMeaningful()`). With two, the table shows the median alone
// and its header says why, because an IQR of two points is the gap between them
// and reads as a precision figure it is not (core's `DecaySpread`).
//
// Non-ASCII text is written as escaped UTF-8 bytes (the middle dot is "\xc2\xb7").
#pragma once

#include "measure/Rt60Analysis.h"
#include "measure/Rt60Readout.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rta::measure {

/// The most captures the ensemble takes (plan T9). Eight is core's measured
/// 1/sqrt(N) table's third row; more adds little and costs a minute of sweeps.
inline constexpr std::size_t kRt60MaxEnsembleCaptures = 8;

/// "0.80 s" for the median, then "  IQR 4%" once the spread is meaningful.
/// "--" for a figure no capture produced.
[[nodiscard]] std::string rt60SpreadCell(const Rt60Spread& spread);

/// One band: `range` carries how many captures contributed to T30 ("3 of 3"),
/// the number a reader needs beside a median. `note` is the band's reason when
/// T30 was withheld, as in the single table.
[[nodiscard]] Rt60Row rt60EnsembleRow(const Rt60EnsembleBand& band, const Rt60EnsembleResult& result);

[[nodiscard]] std::vector<std::string> rt60EnsembleTableLines(const Rt60EnsembleResult& result);

/// Header, one line per entry: how many captures and which, then tail / trusted
/// band / method / time, and the spread rule when fewer than three captures.
[[nodiscard]] std::vector<std::string> rt60EnsembleHeaderLines(const Rt60EnsembleResult& result,
                                                               const std::vector<std::string>& names);

/// Why the whole ensemble was withheld; empty when it was analysed.
[[nodiscard]] std::string rt60EnsembleRefusalText(const Rt60EnsembleResult& result);

}  // namespace rta::measure
