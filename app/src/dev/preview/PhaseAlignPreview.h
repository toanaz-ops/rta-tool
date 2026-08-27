// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md, "V2 -- Phase-
// alignment view (owner's #2)".
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

/// Early-preview, dev-namespace mockup of the V2 phase-alignment view: two
/// phase traces converging inside a highlighted crossover region, a Delta-
/// phi-under-threshold shading, a predicted-summation ghost curve, and the
/// AUTO DELAY solver chip. Paint-only, canned data, no timer -- same
/// contract as `TransferFunctionPreview` / `TargetMatchPreview`.
class PhaseAlignPreview final : public juce::Component {
public:
    PhaseAlignPreview();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void paintPhase(juce::Graphics&, juce::Rectangle<int> area) const;
    void paintSummationGhost(juce::Graphics&, juce::Rectangle<int> area) const;

    juce::Rectangle<int> mastheadArea_;
    juce::Rectangle<int> phaseArea_;
    juce::Rectangle<int> summationArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseAlignPreview)
};
