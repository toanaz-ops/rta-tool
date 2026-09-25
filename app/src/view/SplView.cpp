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
        const bool fired = alarm.state == rta::measure::SplAlarmState::Fired;
        g.setColour(fired ? rta::view::miss : rta::view::match);
        g.drawText(juce::String(alarm.metricId) + (fired ? " FIRED" : " clear"), row,
                   juce::Justification::centredLeft, false);
    }
}

}  // namespace rta::view
