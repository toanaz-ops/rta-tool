// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include <az_ui/az_ui.h>

/// Phase 0 window contents: a specimen sheet for the design system.
///
/// It exists to make the theme falsifiable. A design system that is only ever
/// seen through the app that uses it hides its own drift -- a swatch that went
/// wrong, a type step that collapsed, a primitive that stopped being drawn the
/// same way in two places. Here every token is on screen at once, so a bad
/// value is visible rather than inferred.
///
/// It is replaced by the measurement UI in Phase 1, and the specimen moves to
/// a developer-only window.
class MainComponent final : public juce::Component
{
public:
    MainComponent();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void paintSwatches (juce::Graphics&, juce::Rectangle<int> area);
    void paintTypeScale (juce::Graphics&, juce::Rectangle<int> area);
    void paintPrimitives (juce::Graphics&, juce::Rectangle<int> area);

    juce::TextButton  latching  { "ARMED" };
    juce::TextButton  momentary { "CAPTURE" };
    juce::ComboBox    combo;
    juce::ToggleButton toggle   { "Enabled" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
