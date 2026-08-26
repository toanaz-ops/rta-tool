// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainComponent.h"

#include <array>

namespace
{

struct Swatch
{
    const juce::Colour& colour;
    const char*         name;
};

// Every token in the palette, so a value that drifted is visible rather than
// inferred from a screen that happens not to use it.
const std::array<Swatch, 13> kSwatches {{
    { az::ui::background, "background" },
    { az::ui::panel,      "panel" },
    { az::ui::raise,      "raise" },
    { az::ui::well,       "well" },
    { az::ui::border,     "border" },
    { az::ui::shade,      "shade" },
    { az::ui::text,       "text" },
    { az::ui::dim,        "dim" },
    { az::ui::faded,      "faded" },
    { az::ui::accent,     "accent" },
    { az::ui::warn,       "warn" },
    { az::ui::ok,         "ok" },
    { az::ui::danger,     "danger" },
}};

/// Three faces, not two. A bool called `mono` cannot express the mono MEDIUM
/// weight, and a type that cannot express a value is a type that will silently
/// drop it -- which is exactly what happened: the countdown step is specified
/// as mono medium and was being drawn in regular, so the heaviest figure in the
/// scale rendered as the thinnest-looking one.
enum class Face { Legend, Mono, MonoMedium };

struct TypeStep
{
    float       size;
    const char* name;
    Face        face;
};

const std::array<TypeStep, 7> kTypeScale {{
    { az::ui::countdownFontSize, "countdown 26",  Face::MonoMedium },
    { az::ui::switchFontSize,    "switch 21",     Face::Legend     },
    { az::ui::brandFontSize,     "brand 16",      Face::Legend     },
    { az::ui::captionFontSize,   "caption 14.5",  Face::Legend     },
    { az::ui::columnFontSize,    "column 13",     Face::Legend     },
    { az::ui::tableFontSize,     "table 12.5",    Face::Mono       },
    { az::ui::hintFontSize,      "hint 11",       Face::Mono       },
}};

juce::Font faceFont (const TypeStep& step)
{
    switch (step.face)
    {
        case Face::Mono:       return az::ui::monoFont (step.size, false);
        case Face::MonoMedium: return az::ui::monoFont (step.size, true);
        case Face::Legend:     break;
    }
    return az::ui::legendFont (step.size, true, az::ui::trackingColumn);
}

} // namespace

MainComponent::MainComponent()
{
    latching.setClickingTogglesState (true);
    latching.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (latching);

    addAndMakeVisible (momentary);

    combo.addItemList ({ "Default device", "Focusrite Scarlett 18i20", "RME Fireface UFX" }, 1);
    combo.setSelectedId (2, juce::dontSendNotification);
    addAndMakeVisible (combo);

    toggle.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (toggle);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (az::ui::background);

    auto area = getLocalBounds().reduced (az::ui::gap * 2);

    auto masthead = area.removeFromTop (az::ui::mastheadHeight);
    g.setColour (az::ui::text);
    g.setFont (az::ui::legendFont (az::ui::brandFontSize, true, az::ui::trackingCaption));
    g.drawText ("RTA TOOL  \xe2\x80\x94  SODIUM RACK SPECIMEN",
                masthead, juce::Justification::centredLeft);
    az::ui::drawEngravedDivider (g, masthead.removeFromBottom (2));

    area.removeFromTop (az::ui::gap);
    paintSwatches (g, area.removeFromTop (140));

    area.removeFromTop (az::ui::gap * 2);
    paintTypeScale (g, area.removeFromTop (280));

    area.removeFromTop (az::ui::gap * 2);
    paintPrimitives (g, area);
}

void MainComponent::paintSwatches (juce::Graphics& g, juce::Rectangle<int> area)
{
    az::ui::drawCaption (g, "PALETTE", area.removeFromTop (az::ui::captionHeight), az::ui::dim);

    const int cellWidth = area.getWidth() / (int) kSwatches.size();

    for (const auto& swatch : kSwatches)
    {
        auto cell = area.removeFromLeft (cellWidth).reduced (az::ui::spacing / 2, 0);

        auto chip = cell.removeFromTop (cell.getHeight() - 20).toFloat();
        g.setColour (swatch.colour);
        g.fillRoundedRectangle (chip, az::ui::cornerRadius);
        g.setColour (az::ui::border);
        g.drawRoundedRectangle (chip.reduced (0.5f), az::ui::cornerRadius, 1.0f);

        g.setColour (az::ui::faded);
        const auto font = az::ui::monoFont (10.0f);
        g.setFont (font);
        g.drawText (az::ui::elideMiddle (font, swatch.name, (float) cell.getWidth()),
                    cell, juce::Justification::centredTop);
    }
}

void MainComponent::paintTypeScale (juce::Graphics& g, juce::Rectangle<int> area)
{
    az::ui::drawCaption (g, "TYPE SCALE", area.removeFromTop (az::ui::captionHeight), az::ui::dim);

    for (const auto& step : kTypeScale)
    {
        // 1.5x the type size, so a 26 px figure is not boxed in by a 34 px row.
        // Leading is part of legibility, not decoration.
        auto row = area.removeFromTop ((int) (step.size * 1.5f));

        auto gutter = row.removeFromLeft (az::ui::gutterWidth);
        g.setColour (az::ui::faded);
        g.setFont (az::ui::monoFont (10.0f));
        g.drawText (step.name, gutter, juce::Justification::centredLeft);

        g.setColour (az::ui::text);
        g.setFont (faceFont (step));
        g.drawText (step.face == Face::Legend ? "TRANSFER FUNCTION"
                                              : "1000.0 Hz  -20.0 dB  0.98",
                    row, juce::Justification::centredLeft);
    }
}

void MainComponent::paintPrimitives (juce::Graphics& g, juce::Rectangle<int> area)
{
    az::ui::drawCaption (g, "PRIMITIVES & CONTROLS",
                         area.removeFromTop (az::ui::captionHeight), az::ui::dim);

    // A panel face with a recessed well on it -- the two surfaces that
    // everything else in the system is built out of.
    auto panelArea = area.removeFromTop (az::ui::fieldHeight + az::ui::gap * 2);
    g.setColour (az::ui::panel);
    g.fillRoundedRectangle (panelArea.toFloat(), az::ui::cornerRadius);

    auto wellArea = panelArea.reduced (az::ui::gap)
                             .removeFromLeft (280)
                             .withHeight (az::ui::fieldHeight);
    az::ui::drawWell (g, wellArea.toFloat(), false);
    g.setColour (az::ui::dim);
    g.setFont (az::ui::monoFont (az::ui::readoutFontSize));
    g.drawText ("  48000 Hz  /  256 samples", wellArea, juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced (az::ui::gap * 2);
    auto row = area.removeFromBottom (az::ui::buttonCellHeight + az::ui::gap);
    row.removeFromTop (az::ui::gap);

    latching.setBounds (row.removeFromLeft (az::ui::buttonCellWidth));
    row.removeFromLeft (az::ui::gap);
    momentary.setBounds (row.removeFromLeft (az::ui::buttonCellWidth));
    row.removeFromLeft (az::ui::gap * 2);

    auto fieldColumn = row.removeFromLeft (300).withSizeKeepingCentre (300, az::ui::fieldHeight);
    combo.setBounds (fieldColumn);
    row.removeFromLeft (az::ui::gap * 2);
    toggle.setBounds (row.removeFromLeft (160).withSizeKeepingCentre (160, az::ui::touchTarget));
}
