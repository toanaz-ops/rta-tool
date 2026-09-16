// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md, "V2 -- Phase-
// alignment view (owner's #2)", and the G18 decision record
// docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.6.
#pragma once

#include "view/CrossoverSurface.h"

#include <juce_gui_extra/juce_gui_extra.h>

/// The V2 phase-alignment view, drawn from the REAL G18 model.
///
/// This was a canned-data mockup until L7-ALIGN task I. It now builds an
/// `rta::view::CrossoverSurface` over a synthetic 4th-order Butterworth pair
/// at 100 Hz -- analytic prototypes, from the Butterworth pole formula, not
/// hand-drawn curves -- and every line it draws is a series the model
/// computed: `H_A`, `H_B`, the PREDICTED sum through a pending G11 delay, the
/// pre-alignment GHOST sum, `arg(H_A conj H_B)` inside the fit window, and the
/// horizontal target line the ASKED topology puts there. The +6.02 dB and
/// designed-sum marks come from `CrossoverSurface::marks()`.
///
/// Synthetic, still: there is no microphone behind it. What changed is that
/// the picture is now what the shipping model produces, so a wrong sign or a
/// wrong sum shows up here as well as in ctest (ALIGN-R8, CLAUDE.md "Seeing
/// the GUI"). Paint-only, no timer -- same contract as its two siblings.
class PhaseAlignPreview final : public juce::Component {
public:
    PhaseAlignPreview();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void paintRelativePhase(juce::Graphics&, juce::Rectangle<int> area) const;
    void paintSummation(juce::Graphics&, juce::Rectangle<int> area) const;

    rta::view::CrossoverSurface surface_;

    juce::Rectangle<int> mastheadArea_;
    juce::Rectangle<int> phaseArea_;
    juce::Rectangle<int> summationArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseAlignPreview)
};
