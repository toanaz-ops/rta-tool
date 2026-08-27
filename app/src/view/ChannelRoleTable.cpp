// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5 (Wave D / T9).
#include "view/ChannelRoleTable.h"

#include <az_ui/az_ui.h>

#include <array>
#include <string_view>

namespace rta::view {

namespace {

constexpr int kIndexColumnWidth = 30;
constexpr int kRoleColumnWidth = 150;

// Order matches the toggle's three stops (left/mid/right) below.
constexpr std::array<std::string_view, 3> kRoleLabels{"UNUSED", "MEAS", "REF"};

rta::platform::ChannelRole nextRole(rta::platform::ChannelRole role) {
    using rta::platform::ChannelRole;
    switch (role) {
        case ChannelRole::Unused: return ChannelRole::Measurement;
        case ChannelRole::Measurement: return ChannelRole::Reference;
        case ChannelRole::Reference: return ChannelRole::Unused;
    }
    return ChannelRole::Unused;
}

/// The same track-plus-travelling-knob shape `az::ui::AzLookAndFeel` draws
/// for a two-state toggle (theme/AzLookAndFeel_Widgets.cpp), extended to
/// three stops -- a channel ROLE needs a resting position for "not touching
/// this channel at all" that a binary toggle cannot express. Kept local
/// rather than added to az_ui itself: "role" is measurement vocabulary, and
/// az_ui stays free of it by design (project CLAUDE.md; plan trap T-10) --
/// this is the app naming a shape it draws with tokens az_ui already
/// exports (accent, warn, well, border, dim, text), not az_ui growing a new
/// one.
void drawRoleToggle(juce::Graphics& g, juce::Rectangle<int> bounds, rta::platform::ChannelRole role) {
    using rta::platform::ChannelRole;

    constexpr float trackH = 14.0f;
    constexpr float inset = 2.0f;
    const float trackW = juce::jmax(24.0f, static_cast<float>(bounds.getWidth()) - 58.0f);

    const auto track = juce::Rectangle<float>(trackW, trackH)
                            .withCentre({static_cast<float>(bounds.getX()) + trackW * 0.5f,
                                         static_cast<float>(bounds.getCentreY())});

    const bool isUnused = role == ChannelRole::Unused;
    const juce::Colour roleColour = role == ChannelRole::Measurement ? az::ui::accent : az::ui::warn;

    g.setColour(isUnused ? az::ui::well : roleColour.withAlpha(0.28f));
    g.fillRoundedRectangle(track, trackH * 0.5f);
    g.setColour(isUnused ? az::ui::border : roleColour.withAlpha(0.8f));
    g.drawRoundedRectangle(track.reduced(0.5f), trackH * 0.5f, 1.0f);

    const float knobD = trackH - 2.0f * inset;
    const int stop = isUnused ? 0 : (role == ChannelRole::Measurement ? 1 : 2);
    const float travel = track.getWidth() - knobD - 2.0f * inset;
    const float knobX = track.getX() + inset + travel * static_cast<float>(stop) / 2.0f;

    g.setColour(isUnused ? az::ui::dim : roleColour);
    g.fillEllipse(knobX, track.getY() + inset, knobD, knobD);

    g.setColour(isUnused ? az::ui::faded : az::ui::text);
    g.setFont(az::ui::monoFont(az::ui::hintFontSize));
    g.drawText(juce::String(kRoleLabels[static_cast<std::size_t>(stop)].data()),
               bounds.withTrimmedLeft(static_cast<int>(trackW) + az::ui::spacing),
               juce::Justification::centredLeft, false);
}

}  // namespace

ChannelRoleTable::ChannelRoleTable(rta::platform::ChannelConfig& channelConfig)
    : channelConfig_(channelConfig) {
    listBox_.setModel(this);
    listBox_.setRowHeight(az::ui::fieldHeight);
    listBox_.setColour(juce::ListBox::backgroundColourId, az::ui::panel);
    addAndMakeVisible(listBox_);
}

void ChannelRoleTable::setChannelNames(std::vector<std::string> names) {
    // A device reporting more inputs than the fixed-size role table can
    // hold (plan §1.4: kMaxChannels = 64, covering a MADI or Dante
    // interface) is clipped here rather than downstream -- the rows beyond
    // that point could never carry a role anyway.
    if (names.size() > static_cast<std::size_t>(rta::platform::kMaxChannels)) {
        names.resize(static_cast<std::size_t>(rta::platform::kMaxChannels));
    }
    channelNames_ = std::move(names);
    listBox_.updateContent();
    repaint();
}

int ChannelRoleTable::getNumRows() { return static_cast<int>(channelNames_.size()); }

void ChannelRoleTable::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool) {
    if (rowNumber < 0 || static_cast<std::size_t>(rowNumber) >= channelNames_.size()) return;

    juce::Rectangle<int> row(0, 0, width, height);

    // Alternating row shade -- the same raise/panel pairing SpecimenComponent
    // uses for a "cell that reads as its own row" -- so a 64-row list stays
    // scannable rather than one long grey field.
    g.setColour(rowNumber % 2 == 0 ? az::ui::panel : az::ui::raise);
    g.fillRect(row);

    auto indexArea = row.removeFromLeft(kIndexColumnWidth);
    g.setColour(az::ui::faded);
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    g.drawText(juce::String(rowNumber + 1).paddedLeft('0', 2), indexArea, juce::Justification::centred, false);

    auto roleArea = row.removeFromRight(kRoleColumnWidth);
    auto nameArea = row.reduced(az::ui::gap, 0);

    const auto nameFont = az::ui::monoFont(az::ui::tableFontSize);
    g.setColour(az::ui::text);
    g.setFont(nameFont);
    g.drawText(az::ui::elideMiddle(nameFont, channelNames_[static_cast<std::size_t>(rowNumber)],
                                    static_cast<float>(nameArea.getWidth())),
               nameArea, juce::Justification::centredLeft, false);

    drawRoleToggle(g, roleArea, channelConfig_.role(rowNumber));
}

void ChannelRoleTable::listBoxItemClicked(int row, const juce::MouseEvent&) {
    if (row < 0 || static_cast<std::size_t>(row) >= channelNames_.size()) return;
    channelConfig_.setRole(row, nextRole(channelConfig_.role(row)));
    listBox_.repaintRow(row);
}

void ChannelRoleTable::paint(juce::Graphics& g) {
    g.fillAll(az::ui::panel);

    auto header = getLocalBounds().removeFromTop(az::ui::captionHeight);
    header.removeFromLeft(kIndexColumnWidth);
    auto roleHeader = header.removeFromRight(kRoleColumnWidth);

    const auto font = az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn);
    g.setColour(az::ui::dim);
    g.setFont(font);
    g.drawText("CHANNEL", header.reduced(az::ui::gap, 0), juce::Justification::centredLeft, false);
    g.drawText("ROLE", roleHeader, juce::Justification::centredLeft, false);
}

void ChannelRoleTable::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(az::ui::captionHeight);
    listBox_.setBounds(area);
}

}  // namespace rta::view
