// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Split out of TransferView.cpp (task B0);
// see that header's own comment. No behaviour change from the code these
// functions had at TransferView.cpp:54-141 before the split.
#include "view/TransferViewDraw.h"

#include "measure/PhaseUnwrap.h"
#include "view/AxisMetrics.h"
#include "view/MeasureColours.h"

#include <az_ui/az_ui.h>

#include <cmath>
#include <numbers>

namespace rta::view {

void drawPhaseLabels(juce::Graphics& g, const PlotGeometry& geometry) {
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    g.setColour(rta::view::axisText);
    for (double deg = geometry.dbTop; deg >= geometry.dbBottom - 1e-6; deg -= 90.0) {
        const float y = geometry.yForDb(deg);
        juce::Rectangle<float> cell(geometry.left - static_cast<float>(kLevelLabelWidth), y - 7.0f,
                                    static_cast<float>(kLevelLabelWidth) - 6.0f, 14.0f);
        g.drawText(juce::String(static_cast<int>(std::llround(deg))), cell,
                  juce::Justification::centredRight, false);
    }
}

void drawNoReferenceState(juce::Graphics& g, PaneRect area) {
    g.setColour(rta::view::emptyStateText);
    g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
    g.drawText("NO REFERENCE CHANNEL",
              juce::Rectangle<int>(area.x, area.y, area.width, area.height),
              juce::Justification::centred, false);
}

void drawStoredPhaseHiddenState(juce::Graphics& g, const PlotGeometry& geometry) {
    g.setColour(rta::view::emptyStateText);
    g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
    juce::Rectangle<int> line(static_cast<int>(geometry.left) + az::ui::spacing,
                              static_cast<int>(geometry.top) + az::ui::spacing, 400,
                              az::ui::captionHeight);
    g.drawText("STORED PHASE HIDDEN  --  UNWRAPPED", line, juce::Justification::centredLeft, false);
}

UnwrappedPhase computeUnwrappedPhase(const rta::measure::TransferBlock& transfer) {
    UnwrappedPhase result;
    const std::size_t n = transfer.phaseDeg.size();
    if (n == 0) return result;

    constexpr double kDegToRad = std::numbers::pi / 180.0;
    constexpr double kRadToDeg = 180.0 / std::numbers::pi;

    std::vector<float> wrappedRad(n), unwrappedRad(n);
    for (std::size_t i = 0; i < n; ++i) {
        wrappedRad[i] = static_cast<float>(static_cast<double>(transfer.phaseDeg[i]) * kDegToRad);
    }

    // No coherence THRESHOLD is decided here -- every threshold (blanking,
    // gating, a minimum-average) is L5b's (dsp record, "what this record
    // does not decide"). The gated overload is still used when coherence
    // exists, with `minimumCoherence` left at its 0 default -- its own
    // header states 0 disables the gate -- so this call site is already
    // wired for L5b to set a real value later without this file changing.
    const rta::measure::UnwrapOptions options{};
    if (transfer.coherence.has_value() && transfer.coherence->size() == n) {
        rta::measure::unwrapPhase(wrappedRad, *transfer.coherence, unwrappedRad, options);
    } else {
        rta::measure::unwrapPhase(wrappedRad, unwrappedRad, options);
    }

    result.unwrappedDeg.resize(n);
    float lo = 0.0f, hi = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const float deg = static_cast<float>(static_cast<double>(unwrappedRad[i]) * kRadToDeg);
        result.unwrappedDeg[i] = deg;
        if (i == 0 || deg < lo) lo = deg;
        if (i == 0 || deg > hi) hi = deg;
    }

    result.dbTop = std::ceil(hi / 360.0) * 360.0;
    result.dbBottom = std::floor(lo / 360.0) * 360.0;
    // A degenerate (zero-height) enclosure -- e.g. a delay of exactly 0 --
    // still needs a real axis to plot against.
    if (result.dbTop - result.dbBottom < 360.0) result.dbTop += 360.0;
    return result;
}

}  // namespace rta::view
