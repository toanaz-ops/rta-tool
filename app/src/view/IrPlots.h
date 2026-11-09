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
#include "view/PlotGeometry.h"

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
    /// Fired on mouse-up after a press on the plot.
    std::function<void()> onMarkersSettled;

    void setData(std::shared_ptr<const rta::measure::IrEtc> etc, const rta::measure::IrGateMarkers& markers,
                 bool gateOk);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    /// The hand let go: the gate is settled (H10 starts its one expensive
    /// decomposition here, never per drag event).
    void mouseUp(const juce::MouseEvent&) override;

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
    /// A null or refused analysis draws the empty state with `emptyText`. In PHASE
    /// and EXCESS mode a null or invalid `excess` draws `emptyText` too: the
    /// kernel's own refusal sentence, never an empty curve.
    void setData(std::shared_ptr<const rta::measure::IrGateAnalysis> analysis,
                 std::shared_ptr<const rta::measure::IrExcessPhase> excess, rta::measure::IrLowerPlot mode,
                 std::string emptyText);
    void paint(juce::Graphics&) override;

    /// What the last paint drew, so a test reads data, not pixels. Each vector is
    /// the polyline's values, one per drawn bin; all are empty when the empty
    /// state was drawn.
    [[nodiscard]] const std::vector<float>& drawnDbForTest() const noexcept { return drawn_; }
    [[nodiscard]] const std::vector<float>& drawnMeasuredDegForTest() const noexcept { return drawnMeasured_; }
    [[nodiscard]] const std::vector<float>& drawnMinimumDegForTest() const noexcept { return drawnMinimum_; }
    [[nodiscard]] const std::vector<float>& drawnExcessDegForTest() const noexcept { return drawnExcess_; }
    [[nodiscard]] const std::vector<float>& drawnGroupDelayMsForTest() const noexcept { return drawnGroupDelay_; }
    [[nodiscard]] const std::string& emptyTextForTest() const noexcept { return emptyText_; }

private:
    void paintMagnitude(juce::Graphics&, PlotGeometry&);
    void paintPhase(juce::Graphics&, PlotGeometry&);
    void paintExcess(juce::Graphics&, PlotGeometry&);

    std::shared_ptr<const rta::measure::IrGateAnalysis> analysis_;
    std::shared_ptr<const rta::measure::IrExcessPhase> excess_;
    rta::measure::IrLowerPlot mode_ = rta::measure::IrLowerPlot::Magnitude;
    std::string emptyText_;
    std::vector<float> drawn_, drawnMeasured_, drawnMinimum_, drawnExcess_, drawnGroupDelay_;
};

}  // namespace rta::view
