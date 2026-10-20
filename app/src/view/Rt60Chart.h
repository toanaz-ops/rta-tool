// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// RT60-UI lane wave B, task T10 (docs/plans/2026-10-20-rt60-ui-lane-plan.md): the
// Schroeder curve of the band the operator clicked, with the three spans core
// fitted drawn over it. It answers the question a table cannot: "is this number a
// straight line, or an average of two slopes?" A T30 of 0.80 s read off a curve
// that bends at -15 dB is not the same measurement as one read off a straight
// fall, and the operator sees which at a glance.
//
// The component draws what `Rt60Band::edc` holds and nothing else: no analysis, no
// integration. What it drew is kept (`drawnPointsForTest`, `legendForTest`) so a
// test can read the picture's data without a screenshot.
#pragma once

#include "measure/Rt60Analysis.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>
#include <vector>

namespace rta::view {

class Rt60Chart final : public juce::Component {
public:
    /// What to draw: the curve (copied; at most ~1000 points), the band it belongs
    /// to and a caption naming the IR. An invalid curve draws the empty state.
    void setCurve(const rta::measure::Rt60Edc& edc, const std::string& caption);
    void clear();

    void paint(juce::Graphics&) override;

    /// The dB values of the polyline the last paint drew, in order.
    [[nodiscard]] const std::vector<float>& drawnPointsForTest() const noexcept { return drawn_; }
    /// One line per fit span the last paint drew, e.g. "T30 0.80 s (-5 to -35 dB)".
    [[nodiscard]] const std::vector<std::string>& legendForTest() const noexcept { return legend_; }
    [[nodiscard]] const std::string& captionForTest() const noexcept { return caption_; }
    [[nodiscard]] bool hasCurveForTest() const noexcept { return edc_.valid(); }

private:
    rta::measure::Rt60Edc edc_;
    std::string caption_;
    std::vector<float> drawn_;
    std::vector<std::string> legend_;
};

}  // namespace rta::view
