// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. The EQ pane's chart, drawn as plain
// functions over spans so EqPaneView owns no drawing arithmetic (the same
// split CrossoverSurfaceRenderer makes for the G18 pane).
//
// L7-EQ UI task T5, decision D1 (docs/plans/2026-09-29-eq-ui-lane-plan.md):
// a dB chart of the measured trace, the predicted "ghost" and the target line
// (target + c, where the allocator actually aims), above a correction strip
// showing the sum of the un-applied filters. Untrusted bins are shaded and
// declined bands are hatched.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <span>

namespace rta::view {

/// Everything the chart reads, all on ONE grid (hz.size() entries each).
/// Non-owning: EqPaneView keeps the vectors and hands spans in per paint.
struct EqChartData {
    std::span<const float> hz;
    std::span<const float> measuredDb;
    std::span<const double> ghostDb;
    std::span<const double> targetLineDb;
    std::span<const double> correctionDb;
    std::span<const std::uint8_t> trusted;
    std::span<const std::uint8_t> excluded;
    double sampleRate = 48000.0;
};

/// The y range the upper chart will use for `data`: 10 dB steps, padded 3 dB
/// past the TRUSTED bins' measured, ghost and target-line values (every bin
/// when none is trusted) and never narrower than 30 dB, so PlotGeometry's
/// 10 dB gridlines land on tidy numbers. Exposed for the test.
struct EqDbRange {
    double top = 10.0;
    double bottom = -20.0;
};
[[nodiscard]] EqDbRange eqChartDbRange(const EqChartData& data);

/// The correction strip's half-range: the smallest multiple of 10 dB (at
/// least 10) that holds max|correction|.
[[nodiscard]] double eqStripHalfRangeDb(std::span<const double> correctionDb);

void paintEqChart(juce::Graphics& g, juce::Rectangle<int> area, const EqChartData& data);

}  // namespace rta::view
