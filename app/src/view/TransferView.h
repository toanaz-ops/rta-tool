// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. Decisions 1-5 of
// docs/dsp/2026-08-29-display-layer-l5c.md.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "measure/SnapshotSource.h"
#include "view/BodeLayout.h"
#include "view/RepaintGate.h"
#include "view/StoredTraceLayer.h"

namespace rta::trace { class TraceLibrary; }

namespace rta::view {

/// The stacked Bode composite: coherence ribbon, magnitude pane, phase pane,
/// all three on the ONE shared log-frequency mapping (decision 1).
///
/// Stacking rather than REW's dual-axis single graph, because L5a already
/// committed to dozens of overlaid traces: dual-axis Bode is readable with one
/// or two, and with dozens every wrapped phase trace crosses every magnitude
/// trace several times per decade with nothing but memory to say which axis
/// each belongs to.
class TransferView final : public juce::Component, private juce::Timer {
public:
    explicit TransferView(const rta::measure::SnapshotSource& source);
    ~TransferView() override;

    void setSource(const rta::measure::SnapshotSource& source);

    /// Nullable, null by default -- same contract RtaView::setLibrary states.
    void setLibrary(const rta::trace::TraceLibrary* library);

    /// Wrapped +-180 is the default (decision 4): unwrap is ambiguous wherever
    /// coherence is low, and one bad bin steps every bin above it by 360 -- the
    /// wrapped view degrades locally, the unwrapped view degrades globally.
    /// When unwrapped, the axis extends in whole multiples of 360 to fit.
    ///
    /// Deliberately NOT REW's cursor re-referencing, however readable that is:
    /// it makes the drawn trace a function of the mouse position, which breaks
    /// the cached-layer architecture (stored traces rasterise once per
    /// revision, not per mouse move) and makes two screenshots of one
    /// measurement disagree.
    void setPhaseUnwrapped(bool unwrapped);
    [[nodiscard]] bool isPhaseUnwrapped() const noexcept { return unwrapped_; }

    void paint(juce::Graphics&) override;
    void resized() override;

    /// Renders into `area` with no message loop pumped and no timer fired --
    /// the same contract RtaView::renderTo provides, and for the same reason:
    /// tools/snapshot.cpp drives it offline.
    void renderTo(juce::Graphics& g, juce::Rectangle<int> area) const;

    /// Production API existing so the layout contract is TESTABLE rather than
    /// asserted -- the same trade StoredTraceLayer::rebuildCount documents.
    /// The 5:3 rule is entirely a claim about these rectangles, and reading it
    /// back from pixels would pass for a view that drew the right shape in the
    /// wrong place. `app/tests_juce/test_transfer_view.cpp` is its caller;
    /// deleting it deletes the test.
    [[nodiscard]] const BodePanes& panes() const noexcept { return panes_; }

    /// The two cached layers, exposed for the same reason and no other: the
    /// O(1)-in-trace-count property L5a bought has to survive multiplication by
    /// panes, and that property is entirely a claim about how often
    /// `rebuildCount()` moves. Every other observable -- the pixels -- is
    /// identical whether the images were reused or re-rasterised, so a
    /// composite that rebuilt both layers on every frame would look exactly
    /// like correct code and surface only as dropped frames at a live show.
    [[nodiscard]] const StoredTraceLayer& magnitudeLayer() const noexcept {
        return storedMagnitude_;
    }
    [[nodiscard]] const StoredTraceLayer& phaseLayer() const noexcept { return storedPhase_; }

private:
    void timerCallback() override;

    const rta::measure::SnapshotSource* source_;
    const rta::trace::TraceLibrary* library_ = nullptr;
    GateState gate_;
    bool unwrapped_ = false;
    BodePanes panes_;

    /// One cached layer per pane. The O(1)-in-trace-count property L5a bought
    /// has to survive multiplication by panes, which is what
    /// test_transfer_view asserts against `rebuildCount()`.
    mutable StoredTraceLayer storedMagnitude_{ rta::trace::Field::Magnitude };
    mutable StoredTraceLayer storedPhase_{ rta::trace::Field::Phase };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransferView)
};

}  // namespace rta::view
