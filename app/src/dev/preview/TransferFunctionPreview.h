// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md, "Phase placement: P5
// transfer function" and CLAUDE.md "Before each phase" (this is the research-
// and-argue pass's SHAPE, applied to a mockup: the picture has to be
// defensible before the real dual-FFT feature (P2) exists to draw it).
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

/// Early-preview, dev-namespace mockup of the transfer-function view: a real
/// `juce::Component`, painted from constants declared in the .cpp, no timer
/// and no interaction. It exists for two reasons at once -- a marketing
/// screenshot today, and the seed `RtaView`-style component the lane-L5
/// implementation replaces piece by piece once P2 (dual-FFT) and G16
/// (environment input) exist to feed it real data instead of these curves.
///
/// Three stacked panes sharing one log-frequency x axis (`PlotGeometry`,
/// `PlotAxes` -- reused, not reinvented, per the house drawing style):
/// a coherence ribbon on top, the magnitude trace (dB, +-18 range) in the
/// middle, and the wrapped phase trace (+-180 deg) at the bottom. A delay
/// readout chip sits in the magnitude pane.
class TransferFunctionPreview final : public juce::Component {
public:
    TransferFunctionPreview();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void paintRibbon(juce::Graphics&, juce::Rectangle<int> area) const;
    void paintMagnitude(juce::Graphics&, juce::Rectangle<int> area) const;
    void paintPhase(juce::Graphics&, juce::Rectangle<int> area) const;

    juce::Rectangle<int> mastheadArea_;
    juce::Rectangle<int> ribbonArea_;
    juce::Rectangle<int> magnitudeArea_;
    juce::Rectangle<int> phaseArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransferFunctionPreview)
};
