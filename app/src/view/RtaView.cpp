// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5, §3.5.1.
#include "view/RtaView.h"

#include "measure/Snapshot.h"
#include "view/MeasureColours.h"
#include "view/PlotAxes.h"

#include <az_ui/az_ui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace rta::view {

namespace {

/// The analysis thread publishes at most every 50 ms (plan §1.4: "Analysis
/// publish rate at most 20 Hz"). Polling faster than the source can ever
/// change costs CPU during a show for nothing an eye can resolve; polling
/// at exactly that ceiling is what "sequence-gated" means in practice.
constexpr int kTimerHz = 20;

/// Room for the one-line readout above the plot.
constexpr int kReadoutHeight = 24;

/// Space kept clear at the right of the plot so the top decade's frequency
/// label ("20000") has the same room to breathe `PlotAxes::
/// kFrequencyLabelHalfWidth` gives every other label.
constexpr int kRightMargin = static_cast<int>(kFrequencyLabelHalfWidth);

/// A vertical engraved groove -- the same one-dark-line-plus-faint-sheen
/// bevel `az::ui::drawEngravedDivider` draws for a horizontal band (plan
/// §3.5.1 point 1), rotated: that helper only ever draws along the bottom
/// of a rectangle, so the resolution-limit boundary draws its own pair of
/// hairlines rather than press a horizontal-only primitive into a vertical
/// job.
void drawVerticalGroove(juce::Graphics& g, float x, float top, float bottom) {
    g.setColour(az::ui::shade);
    g.fillRect(x - 1.0f, top, 1.0f, bottom - top);
    g.setColour(az::ui::sheen);
    g.fillRect(x, top, 1.0f, bottom - top);
}

/// The rectangle a `BandReading` fills or outlines: full band width (minus
/// a hairline gap so adjacent bars stay visually separate), from the
/// component's floor up to its level.
juce::Rectangle<float> barBounds(const PlotGeometry& geometry, const rta::measure::BandReading& band) {
    float x1 = geometry.xForHz(band.lowerHz);
    float x2 = geometry.xForHz(band.upperHz);
    if (x2 < x1) std::swap(x1, x2);
    x1 = std::clamp(x1, geometry.left, geometry.right);
    x2 = std::clamp(x2, geometry.left, geometry.right);

    // Leave a 1 px gap between bars where there is room for one; a band
    // narrow enough that a gap would erase it entirely stays un-gapped
    // rather than vanish.
    if (x2 - x1 > 2.0f) {
        x1 += 0.5f;
        x2 -= 0.5f;
    }

    const float top = geometry.yForDb(static_cast<double>(band.levelDb));
    return { x1, top, x2 - x1, geometry.bottom - top };
}

/// The highest-frequency under-resolved band, if any -- `bands` runs low to
/// high frequency and under-resolution is a property of the low end of the
/// range (too few transform bins per band), so the flagged bands form a
/// prefix and its last element is the one boundary needs.
const rta::measure::BandReading* highestUnderResolvedBand(
    const std::vector<rta::measure::BandReading>& bands) {
    const rta::measure::BandReading* found = nullptr;
    for (const auto& band : bands) {
        if (band.underResolved) found = &band;
    }
    return found;
}

void drawEmptyState(juce::Graphics& g, juce::Rectangle<int> area) {
    g.setColour(emptyStateText);
    g.setFont(az::ui::legendFont(az::ui::captionFontSize, true, az::ui::trackingCaption));
    g.drawText("NO SIGNAL", area, juce::Justification::centred, false);
}

void drawReadout(juce::Graphics& g, juce::Rectangle<int> area, const rta::measure::Snapshot* snapshot) {
    juce::String line;
    if (snapshot != nullptr && !snapshot->bands.empty()) {
        // No band-selection gesture exists yet in this wave (that arrives
        // with the device panel / channel table), so the readout shows the
        // one band the snapshot already identifies as noteworthy: its
        // peak. `Analyser::publish` computes `peakBandCentreHz` /
        // `peakBandLevelDb` for exactly this.
        const juce::String hz(static_cast<juce::int64>(std::llround(snapshot->peakBandCentreHz)));
        const juce::String db(static_cast<double>(snapshot->peakBandLevelDb), 1);
        line = hz + " Hz    " + db + " dB";
    } else {
        line = "-- Hz    -- dB";
    }

    g.setColour(readoutText);
    g.setFont(az::ui::monoFont(az::ui::tableFontSize));
    g.drawText(line, area, juce::Justification::centredLeft, false);
}

}  // namespace

RtaView::RtaView(const rta::measure::SnapshotSource& source) : source_(&source) {
    startTimerHz(kTimerHz);
}

RtaView::~RtaView() {
    stopTimer();
}

void RtaView::setSource(const rta::measure::SnapshotSource& source) {
    source_ = &source;
    repaint();
}

void RtaView::timerCallback() {
    const auto snapshot = source_->latest();
    const auto sequence = snapshot ? snapshot->sequence : 0;
    if (sequence != lastPaintedSequence_) {
        lastPaintedSequence_ = sequence;
        repaint();
    }
}

void RtaView::paint(juce::Graphics& g) {
    renderTo(g, getLocalBounds());
}

void RtaView::resized() {
    // All layout is recomputed per paint from `area` in `renderTo` -- there
    // is no child component to place and no cached geometry that a resize
    // could leave stale.
}

PlotGeometry RtaView::layoutGeometry(juce::Rectangle<int> area) const {
    auto working = area;
    working.removeFromTop(kReadoutHeight);
    working.removeFromBottom(kFrequencyLabelHeight);
    working.removeFromLeft(kLevelLabelWidth);
    working.removeFromRight(kRightMargin);

    PlotGeometry geometry;
    geometry.left = static_cast<float>(working.getX());
    geometry.right = static_cast<float>(working.getRight());
    geometry.top = static_cast<float>(working.getY());
    geometry.bottom = static_cast<float>(working.getBottom());
    return geometry;
}

void RtaView::renderTo(juce::Graphics& g, juce::Rectangle<int> area) const {
    g.setColour(az::ui::background);
    g.fillRect(area);

    const auto snapshot = source_->latest();

    // `layoutGeometry` reserves its own `kReadoutHeight` strip from the top
    // of `area`, so the readout is drawn from a COPY of `area` here rather
    // than by trimming `area` itself -- otherwise the height would be
    // removed twice and the plot would start one readout-line too low.
    auto readoutArea = area;
    drawReadout(g, readoutArea.removeFromTop(kReadoutHeight).reduced(az::ui::gap, 0), snapshot.get());

    const PlotGeometry geometry = layoutGeometry(area);

    drawGrid(g, geometry);
    drawFrequencyLabels(g, geometry);
    drawLevelLabels(g, geometry);

    if (!snapshot || snapshot->bands.empty()) {
        drawEmptyState(g, juce::Rectangle<int>(static_cast<int>(geometry.left), static_cast<int>(geometry.top),
                                                static_cast<int>(geometry.right - geometry.left),
                                                static_cast<int>(geometry.bottom - geometry.top)));
        return;
    }

    for (const auto& band : snapshot->bands) {
        const auto bounds = barBounds(geometry, band);
        if (band.underResolved) {
            g.setColour(underResolved);
            g.drawRect(bounds, 1.0f);
        } else {
            g.setColour(trace);
            g.fillRect(bounds);
        }
    }

    // §3.5.1: the boundary is a visible line plus a legend, never a tint
    // alone -- a dimmed bar in a dark room is a dimmed bar nobody notices.
    // Drawn only when at least one band is flagged; at the default 4096 @
    // 48 kHz analysis down to 20 Hz several bottom bands will be, which is
    // the expected picture, not a bug.
    if (const auto* limit = highestUnderResolvedBand(snapshot->bands)) {
        const float x = geometry.xForHz(static_cast<double>(limit->upperHz));
        drawVerticalGroove(g, x, geometry.top, geometry.bottom);

        const juce::String freqLabel(static_cast<juce::int64>(std::llround(limit->upperHz)));
        juce::Rectangle<int> legendArea(static_cast<int>(x) + az::ui::spacing, static_cast<int>(geometry.top),
                                         200, az::ui::captionHeight);
        g.setColour(underResolved);
        g.setFont(az::ui::legendFont(az::ui::columnFontSize, true, az::ui::trackingColumn));
        g.drawText("RESOLUTION LIMIT " + freqLabel + " HZ", legendArea, juce::Justification::centredLeft, false);
    }
}

}  // namespace rta::view
