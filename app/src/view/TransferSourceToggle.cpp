// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See TransferSourceToggle.h.
#include "view/TransferSourceToggle.h"

namespace rta::view {

namespace {
constexpr int kRadioGroupId = 0xADAF7;  // arbitrary, non-zero, unique to this pair.
}  // namespace

TransferSourceToggle::TransferSourceToggle(TransferView& view, TransferPane pane)
    : view_(view), pane_(pane) {
    mtwButton_.setRadioGroupId(kRadioGroupId, juce::dontSendNotification);
    fixedButton_.setRadioGroupId(kRadioGroupId, juce::dontSendNotification);
    mtwButton_.setClickingTogglesState(true);
    fixedButton_.setClickingTogglesState(true);

    mtwButton_.onClick = [this] { select(TransferSource::Mtw); };
    fixedButton_.onClick = [this] { select(TransferSource::Fixed); };

    addAndMakeVisible(mtwButton_);
    addAndMakeVisible(fixedButton_);
    refresh();
}

void TransferSourceToggle::resized() {
    auto area = getLocalBounds();
    const int half = area.getWidth() / 2;
    mtwButton_.setBounds(area.removeFromLeft(half));
    fixedButton_.setBounds(area);
}

void TransferSourceToggle::placeInPane(PaneRect pane, int height) {
    constexpr int kWidth = 108;
    setBounds(pane.right() - kWidth, pane.y, kWidth, height);
}

void TransferSourceToggle::refresh() {
    const bool mtw = view_.source(pane_) == TransferSource::Mtw;
    // dontSendNotification: this is the read-back half of the click handler
    // below, not a user gesture -- re-entering onClick from here would fire
    // `select` a second time for the same click.
    mtwButton_.setToggleState(mtw, juce::dontSendNotification);
    fixedButton_.setToggleState(!mtw, juce::dontSendNotification);
}

void TransferSourceToggle::select(TransferSource source) {
    view_.setSource(pane_, source);
    refresh();
}

}  // namespace rta::view
