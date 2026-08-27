// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/dev/preview. See
// docs/specs/2026-08-28-interactive-tuning-visuals.md, "V1 -- Target-match
// view (owner's #1)".
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

/// Early-preview, dev-namespace mockup of the V1 target-match view: target
/// curve + tolerance corridor + a measured trace coloured per the shared
/// judgement grammar (match / miss / untrusted -- MeasureColours.h), two
/// suggestion chips at the worst-offending bands, and three match-score
/// chips (LF/MF/HF). Paint-only, canned data, no timer -- same contract as
/// `TransferFunctionPreview` and the reason is the same one: this is the
/// marketing screenshot AND the seed lane L5 grows the real view from once
/// P5 (target curves/traces) and G11 (virtual EQ) exist.
class TargetMatchPreview final : public juce::Component {
public:
    TargetMatchPreview();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void paintScoreChips(juce::Graphics&) const;
    void paintSuggestionChips(juce::Graphics&) const;

    juce::Rectangle<int> mastheadArea_;
    juce::Rectangle<int> plotArea_;
    juce::Rectangle<int> scoreChipRow_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TargetMatchPreview)
};
