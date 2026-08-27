// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/SnapshotSource.h"
#include "view/PlotGeometry.h"

#include <cstdint>

namespace rta::view {

/// The band-bar plot: bars from the latest `Snapshot` in `source`, a
/// log-frequency / dB grid underneath them (`PlotAxes`), and a one-line
/// mono readout above.
///
/// Repaint is sequence-gated, not frame-gated: a `juce::Timer` polls
/// `source.latest()->sequence` at 20 Hz -- the same ceiling the analysis
/// thread publishes at (plan §1.4) -- and calls `repaint()` only when that
/// number actually changed. Repainting on every timer tick regardless would
/// cost CPU during a show for a plot the eye cannot resolve any faster than
/// the publish rate anyway.
class RtaView final : public juce::Component, private juce::Timer {
public:
    explicit RtaView(const rta::measure::SnapshotSource& source);
    ~RtaView() override;

    /// Repoints which source this view reads from -- e.g. switching between
    /// live and synthetic input -- without rebuilding the component.
    void setSource(const rta::measure::SnapshotSource& source);

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Draws the full view -- grid, bars, resolution-limit boundary,
    /// readout line, empty state -- into `area` of `g`, reading whatever
    /// `source_->latest()` returns right now. `paint()` is a one-line call
    /// to this; it is factored out so `tools/snapshot.cpp` can render
    /// `rta-view.png` from a `StaticSnapshotSource` with no message loop
    /// ever pumped and no dependency on the timer above having fired even
    /// once (plan §3.5, trap T-6).
    void renderTo(juce::Graphics& g, juce::Rectangle<int> area) const;

private:
    void timerCallback() override;

    /// The plot rectangle inside `area`, after reserving the readout line
    /// on top and the axis-label margins `PlotAxes` defines (bottom and
    /// left). Pure layout, no drawing -- shared between `renderTo` and
    /// nothing else today, kept separate because the geometry math and the
    /// drawing both got long enough to want their own reading.
    [[nodiscard]] PlotGeometry layoutGeometry(juce::Rectangle<int> area) const;

    const rta::measure::SnapshotSource* source_;
    std::uint64_t lastPaintedSequence_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RtaView)
};

}  // namespace rta::view
