// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview.
// Lane L6a task W2-D (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §4, §11).
#pragma once

#include "rta/meter/Block.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <vector>

/// The SPL strip, over CANNED data -- the PhaseAlignPreview precedent
/// (paint-only, no timer, no live session). Renders a synthetic level-over-
/// time trace built from `rta::view::SplStrip.h`'s own geometry: this proves
/// the headless model draws something legible before any real `SplSession`
/// feeds it, exactly as `TransferFunctionPreview` did for the Bode composite
/// before task 8 landed.
class SplPreview final : public juce::Component {
public:
    SplPreview();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::vector<rta::meter::Block> blocks_;

    juce::Rectangle<int> mastheadArea_;
    juce::Rectangle<int> stripArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplPreview)
};
