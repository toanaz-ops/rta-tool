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
/// The specimen's own label column.
///
/// Deliberately NOT az::ui::gutterWidth. That token is 74 px because it sizes
/// the legend gutter of a real panel, where legends are single words. These
/// labels carry a name AND a size ("countdown 26"), which at columnFontSize
/// needs roughly 95 px -- reusing the panel token here would silently clip
/// them. A token means "this measurement", not "any measurement of about this
/// kind".
constexpr int kLabelColumn = 220;

/// Chip height in the palette band.
constexpr int kSwatchHeight = 84;

/// Leading factor. A figure needs room above and below it or the row boxes it
/// in; 1.5x is the smallest that still reads as a list rather than a stack.
constexpr float kLeading = 1.5f;

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

int typeScaleHeight()
{
    int total = 0;
    for (const auto& step : kTypeScale)
        total += (int) (step.size * kLeading);
    return total;
}

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

    // The title is drawn as two pieces rather than one string with a dash in
    // it. Two reasons: the wordmark and its qualifier want different weights,
    // and a one-piece title needs a non-ASCII dash, which is a source-encoding
    // question this project has no reason to open.
    auto masthead = area.removeFromTop (az::ui::transportHeight / 2);
    auto rule = masthead.removeFromBottom (2);

    const auto brandFont = az::ui::legendFont (az::ui::switchFontSize, true,
                                               az::ui::trackingCaption);
    g.setColour (az::ui::text);
    g.setFont (brandFont);
    g.drawText ("RTA TOOL", masthead, juce::Justification::centredLeft);

    masthead.removeFromLeft ((int) az::ui::stringWidth (brandFont, "RTA TOOL")
                             + az::ui::gap * 3);
    g.setColour (az::ui::dim);
    g.setFont (az::ui::legendFont (az::ui::columnFontSize, false, az::ui::trackingColumn));
    g.drawText ("SODIUM RACK SPECIMEN", masthead, juce::Justification::centredLeft);

    az::ui::drawEngravedDivider (g, rule);

    // Each band takes the height its content needs. Hard-coded band heights
    // are what left a hand's width of dead space under the type scale in the
    // first pass: the number was a guess, and a guess cannot follow the
    // content when the content changes.
    area.removeFromTop (az::ui::gap * 2);
    paintSwatches (g, area.removeFromTop (az::ui::captionHeight + kSwatchHeight
                                          + az::ui::captionHeight));

    area.removeFromTop (az::ui::gap * 2);
    paintTypeScale (g, area.removeFromTop (az::ui::captionHeight + typeScaleHeight()));

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

        auto chip = cell.removeFromTop (kSwatchHeight).toFloat();
        g.setColour (swatch.colour);
        g.fillRoundedRectangle (chip, az::ui::cornerRadius);

        // Outlined in `dim`, not in `border`. `border` is the engraved hairline
        // of the real UI and is nearly the value of the darkest chips, so the
        // `background` swatch came out invisible against the page it was drawn
        // on -- a palette whose first entry cannot be seen has failed at its
        // one job. This outline is specimen chrome, not part of the system.
        g.setColour (az::ui::dim);
        g.drawRoundedRectangle (chip.reduced (0.5f), az::ui::cornerRadius, 1.0f);

        g.setColour (az::ui::dim);
        const auto font = az::ui::monoFont (az::ui::hintFontSize);
        g.setFont (font);
        g.drawText (az::ui::elideMiddle (font, swatch.name, (float) cell.getWidth()),
                    cell.removeFromTop (az::ui::captionHeight),
                    juce::Justification::centredTop);
    }
}

void MainComponent::paintTypeScale (juce::Graphics& g, juce::Rectangle<int> area)
{
    az::ui::drawCaption (g, "TYPE SCALE", area.removeFromTop (az::ui::captionHeight), az::ui::dim);

    for (const auto& step : kTypeScale)
    {
        auto row = area.removeFromTop ((int) (step.size * kLeading));

        auto gutter = row.removeFromLeft (kLabelColumn);
        // Set at ITS OWN step's size, so the two columns descend together. Held
        // at one size the label column reads as a second, flat typographic
        // voice arguing with the descending one beside it. Hierarchy is carried
        // by colour instead -- dim against text -- and the smallest step is
        // hintFontSize, which the system already defines as readable, so no
        // floor is needed.
        g.setColour (az::ui::dim);
        const auto labelFont = az::ui::monoFont (step.size);
        g.setFont (labelFont);
        g.drawText (az::ui::elideMiddle (labelFont, step.name, (float) gutter.getWidth()),
                    gutter, juce::Justification::centredLeft);

        g.setColour (az::ui::text);
        g.setFont (faceFont (step));
        g.drawText (step.face == Face::Legend ? "TRANSFER FUNCTION"
                                              : "1000 Hz   -20.0 dB   0.98",
                    row, juce::Justification::centredLeft);
    }
}

void MainComponent::paintPrimitives (juce::Graphics& g, juce::Rectangle<int> area)
{
    az::ui::drawCaption (g, "PRIMITIVES",
                         area.removeFromTop (az::ui::captionHeight), az::ui::dim);

    // A panel face carrying the two field states, so a change to drawWell shows
    // up here rather than only inside whichever screen happens to use it.
    auto panelArea = area.removeFromTop (az::ui::fieldHeight * 2 + az::ui::gap * 5);
    g.setColour (az::ui::panel);
    g.fillRoundedRectangle (panelArea.toFloat(), az::ui::cornerRadius);
    panelArea.reduce (az::ui::gap * 2, az::ui::gap * 2);

    const auto readout = az::ui::monoFont (az::ui::readoutFontSize);

    struct Field { const char* legend; const char* value; bool focused; };
    const Field fields[] = {
        { "STREAM", "48000 Hz   256 samples",            false },
        { "DEVICE", "Focusrite Scarlett 18i20 USB Analogue 7", true  },
    };

    for (const auto& field : fields)
    {
        auto row = panelArea.removeFromTop (az::ui::fieldHeight);

        auto legendArea = row.removeFromLeft (az::ui::gutterWidth);
        g.setColour (az::ui::faded);
        g.setFont (az::ui::legendFont (az::ui::columnFontSize, true, az::ui::trackingColumn));
        g.drawText (field.legend, legendArea, juce::Justification::centredLeft);

        auto well = row.removeFromLeft (220);
        az::ui::drawWell (g, well.toFloat(), field.focused);

        // Deliberately narrower than the value it holds, so the middle-elision
        // is visible: a channel is identified by its LAST characters, and
        // trailing truncation would leave two ports looking identical.
        g.setColour (az::ui::text);
        g.setFont (readout);
        g.drawText (az::ui::elideMiddle (readout, field.value,
                                         (float) well.getWidth() - az::ui::gap * 2),
                    well.reduced (az::ui::gap, 0), juce::Justification::centredLeft);

        g.setColour (az::ui::faded);
        g.setFont (az::ui::monoFont (az::ui::hintFontSize));
        g.drawText (field.focused ? "drawWell (focused)" : "drawWell (idle)",
                    row.reduced (az::ui::gap * 2, 0), juce::Justification::centredLeft);

        panelArea.removeFromTop (az::ui::gap);
    }

    area.removeFromTop (az::ui::gap * 2);
    az::ui::drawCaption (g, "ENGRAVED DIVIDER",
                         area.removeFromTop (az::ui::captionHeight), az::ui::faded);
    az::ui::drawEngravedDivider (g, area.removeFromTop (2));
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
