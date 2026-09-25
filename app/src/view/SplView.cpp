// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
// Lane L6a task W2-D (record docs/dsp/2026-09-16-spl-pro-l6a.md §4, §11).
#include "view/SplView.h"

#include "view/MeasureColours.h"
#include "view/SplStrip.h"

#include <az_ui/az_ui.h>

namespace rta::view {

namespace {
constexpr int kTimerHz = 20;  // the same publish-rate ceiling RtaView polls at
}

SplView::SplView(const rta::measure::SnapshotSource& source) : source_(&source) {
    startTimerHz(kTimerHz);
}

SplView::~SplView() {
    stopTimer();
}

void SplView::timerCallback() {
    const auto snapshot = source_->latest();
    const std::uint64_t sequence = snapshot ? snapshot->sequence : 0;
    if (sequence != lastSequence_) {
        lastSequence_ = sequence;
        repaint();
    }
}

void SplView::resized() {}

void SplView::paint(juce::Graphics& g) {
    g.fillAll(az::ui::background);

    const auto snapshot = source_->latest();
    if (!snapshot || !snapshot->spl.has_value()) {
        g.setColour(rta::view::emptyStateText);
        g.setFont(az::ui::monoFont(az::ui::tableFontSize));
        g.drawText("NO SPL SESSION", getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    const auto& spl = *snapshot->spl;
    auto area = getLocalBounds().reduced(az::ui::gap * 2);
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));

    for (const auto& metric : spl.metrics) {
        auto row = area.removeFromTop(az::ui::fieldHeight);
        g.setColour(rta::view::readoutText);
        g.drawText(juce::String(metric.id), row.removeFromLeft(160),
                   juce::Justification::centredLeft, false);
        g.drawText(rta::view::splCurrentLevelLabel(static_cast<double>(metric.valueDb)),
                   row.removeFromLeft(100), juce::Justification::centredLeft, false);
        g.drawText(rta::view::splBufferFillLabel(static_cast<double>(metric.leqBufferFill)),
                   row, juce::Justification::centredLeft, false);
    }

    for (const auto& alarm : spl.alarms) {
        auto row = area.removeFromTop(az::ui::fieldHeight);
        // Filling is a third, honest state (record §15 A6, round 4): the
        // window has not yet held enough blocks for the latch to compare
        // anything, so it must not read as "clear" (compared, and under the
        // limit) -- `untrusted`'s own meaning is "judged by nobody", which is
        // exactly this. A switch (not a bool) so a future fourth state fails
        // to compile here instead of silently drawing nothing.
        juce::String suffix;
        juce::Colour colour;
        switch (alarm.state) {
            case rta::measure::SplAlarmState::Filling:
                suffix = " filling";
                colour = rta::view::untrusted;
                break;
            case rta::measure::SplAlarmState::Clear:
                suffix = " clear";
                colour = rta::view::match;
                break;
            case rta::measure::SplAlarmState::Fired:
                suffix = " FIRED";
                colour = rta::view::miss;
                break;
        }
        g.setColour(colour);
        g.drawText(juce::String(alarm.metricId) + suffix, row, juce::Justification::centredLeft,
                   false);
    }
}

}  // namespace rta::view
