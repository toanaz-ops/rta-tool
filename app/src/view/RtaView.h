// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. See
// docs/plans/2026-08-27-audioio-rta-impl-plan.md §3.5.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/SnapshotSource.h"
#include "view/PaneRegistry.h"
#include "view/PlotGeometry.h"
#include "view/RepaintGate.h"
#include "view/StoredTraceLayer.h"

#include <cstdint>

namespace rta::view {

/// The band-bar plot: bars from the latest `Snapshot` in `source`, a
/// log-frequency / dB grid underneath them (`PlotAxes`), and a one-line
/// mono readout above.
///
/// Repaint is gated, not frame-driven: a `juce::Timer` polls at 20 Hz -- the
/// same ceiling the analysis thread publishes at (plan §1.4) -- and calls
/// `repaint()` only when something actually changed. Repainting on every
/// timer tick regardless would cost CPU during a show for a plot the eye
/// cannot resolve any faster than the publish rate anyway.
///
/// The gate watches TWO counters (`RepaintGate.h`), because a sequence-only
/// gate breaks in both directions once stored traces exist: they carry no
/// sequence, and editing one -- rename, hide, recolour, regroup -- produces
/// no sequence change, so the plot would never redraw and the edit would look
/// ignored.
class RtaView final : public juce::Component, public LibraryConsumer, private juce::Timer {
public:
    explicit RtaView(const rta::measure::SnapshotSource& source);
    ~RtaView() override;

    /// Repoints which source this view reads from -- e.g. switching between
    /// live and synthetic input -- without rebuilding the component.
    void setSource(const rta::measure::SnapshotSource& source);

    /// Points the view at the stored traces to draw underneath the live one.
    /// NULLABLE, and null is the default: `tools/snapshot.cpp` renders this
    /// view offline with no library at all, and MainComponent has none yet.
    /// A null library contributes NOTHING to the render -- not an empty
    /// cached image composited over the plot, nothing -- so that path stays
    /// byte-for-byte what it was before stored traces existed.
    ///
    /// `override`: implements `LibraryConsumer` (view/PaneRegistry.h), which
    /// is how `WorkspaceView` reaches this without including this header.
    void setLibrary(const rta::trace::TraceLibrary* library) override;

    /// What `setLibrary` last stored -- production API added purely so
    /// "the library reaches this pane" is a fact a test can read back
    /// (`library() != nullptr`) rather than infer from pixels. This is the
    /// exact seam L5a's stored-trace path sat unreached at for a whole
    /// session (docs/HANDOFF.md): a library was built, a view could draw
    /// one, and nothing called this setter. `app/tests_juce/
    /// test_workspace_view.cpp` is its caller; deleting it deletes that
    /// test's ability to say so.
    [[nodiscard]] const rta::trace::TraceLibrary* library() const noexcept { return library_; }

    /// The cached layer, exposed for the same reason `TransferView` exposes
    /// its two: the O(1)-in-trace-count property is entirely a claim about
    /// `rebuildCount()`, and multiplying this view by panes (`WorkspaceView`)
    /// has to leave that claim true per pane, not just per view.
    [[nodiscard]] const StoredTraceLayer& storedLayer() const noexcept { return storedLayer_; }

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
    const rta::trace::TraceLibrary* library_ = nullptr;
    GateState gate_;

    /// Mutable because `renderTo` is const for the snapshot tool's sake and
    /// this is a rendering cache, not observable state: rebuilding it changes
    /// what the pixels cost, never what they are.
    mutable StoredTraceLayer storedLayer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RtaView)
};

}  // namespace rta::view
