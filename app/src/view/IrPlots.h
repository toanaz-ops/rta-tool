// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// H9 / L4c task A3 (docs/plans/2026-11-09-h9-l4c-ir-gate-plan.md): the two
// pictures of the IR pane. `IrEtcPlot` is where the operator places the gate:
// the energy-time curve with two draggable markers. `IrGatedPlot` shows what the
// gate leaves: the gated magnitude on the usual log-frequency axis, with the
// lowest valid frequency drawn so the left edge of the trusted part is visible.
//
// Both draw what the model hands them and compute nothing: the envelope, the
// gated spectrum and `fMin` come from measure/IrGateModel.h. What was drawn is
// kept so a test reads the data, not pixels.
#pragma once

#include "measure/IrGateModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace rta::view {

class IrEtcPlot final : public juce::Component {
public:
    /// The operator moved a marker; the argument is the new pair, already snapped
    /// to 0.1 ms. Not validated here: a start dragged past the end is the model's
    /// refusal to word, and the marker stays where the hand put it.
    std::function<void(const rta::measure::IrGateMarkers&)> onMarkersChanged;

    void setData(std::shared_ptr<const rta::measure::IrEtc> etc, const rta::measure::IrGateMarkers& markers,
                 bool gateOk);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    /// Where marker `start` (true) or `end` (false) is drawn, in this component's
    /// own coordinates, vertically mid-plot; a test delivers its drag there.
    [[nodiscard]] juce::Point<int> markerCentreForTest(bool start) const;
    [[nodiscard]] const rta::measure::IrGateMarkers& markersForTest() const noexcept { return markers_; }
    [[nodiscard]] std::size_t drawnPointCountForTest() const noexcept { return drawnPoints_; }

private:
    [[nodiscard]] juce::Rectangle<float> plotArea() const;
    [[nodiscard]] float xOfMs(double ms) const;
    [[nodiscard]] double msOfX(float x) const;
    void dragTo(float x);

    std::shared_ptr<const rta::measure::IrEtc> etc_;
    rta::measure::IrGateMarkers markers_;
    bool gateOk_ = false;
    bool draggingStart_ = true;
    std::size_t drawnPoints_ = 0;
};

class IrGatedPlot final : public juce::Component {
public:
    /// A null or refused analysis draws the empty state with `emptyText`.
    void setData(std::shared_ptr<const rta::measure::IrGateAnalysis> analysis, std::string emptyText);
    void paint(juce::Graphics&) override;

    /// The dB values of the polyline the last paint drew (one per drawn bin).
    [[nodiscard]] const std::vector<float>& drawnDbForTest() const noexcept { return drawn_; }

private:
    std::shared_ptr<const rta::measure::IrGateAnalysis> analysis_;
    std::string emptyText_;
    std::vector<float> drawn_;
};

}  // namespace rta::view
