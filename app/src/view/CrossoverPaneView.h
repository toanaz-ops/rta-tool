// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// The ALIGN-R8 reversal (owner, 2026-09-26; amendment in docs/dsp/
// 2026-09-06-l7-alignment-wizard.md): G18 stops being ONLY the dev-preview
// specimen (app/src/dev/preview/PhaseAlignPreview.{h,cpp}) and becomes a
// live pane, reached through the 4th selector button (`XOVER`,
// PaneSelectorDecision.h) the same way `SplView` was mounted through
// `makePaneFactory` (PaneFactory.cpp) in PR #37.
//
// WHERE THE TWO SOURCES COME FROM, AND WHY THAT IS A PLACEHOLDER. There is
// no picker UI in this task: `AlignmentWizard` -- the flow that ASKS which
// stored capture is the high-pass side, which is the low-pass side, and
// what topology was actually built -- is out of scope here and gets its own
// lane (task brief). Until it is wired, this pane reads
// `TraceLibrary::entries()[0]` as the HIGH-PASS side and `entries()[1]` as
// the LOW-PASS side -- library INSERTION order, the only ordering a
// placeholder can pick with no operator input, stated here so nobody goes
// looking for a combo box this task does not add. `setAskedTopology`/
// `setWindow` are likewise FIXED DEFAULTS (CrossoverPaneView.cpp's own
// `kDefaultTopology`/`kDefaultWindow`) rather than asked answers -- a
// default is not a measurement result, and the corner chip says so.
//
// LIVE means the pane redraws when the LIBRARY changes -- a trace added,
// renamed, hidden or reordered -- polled the same way `RtaView`/
// `TransferView` gate their own repaint on `TraceLibrary::revision()`
// (`RepaintGate.h`). There is no separate live measurement bus behind this
// pane the way there is behind the other three: G18's whole subject is two
// ALREADY-CAPTURED traces, previewed through pending ops, never a running
// analyser (CrossoverSurface.h's own class comment, "THERE IS NO OBJECTIVE
// HERE").
#pragma once

#include "view/CrossoverSurface.h"
#include "view/PaneRegistry.h"
#include "view/RepaintGate.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <string>

namespace rta::view {

class CrossoverPaneView final : public juce::Component, public LibraryConsumer, private juce::Timer {
public:
    CrossoverPaneView();
    ~CrossoverPaneView() override;

    /// `override`: implements `LibraryConsumer` (`PaneRegistry.h`), the seam
    /// `WorkspaceView::setLibrary` reaches every pane through with no
    /// `CrossoverPaneView.h` include of its own. Nullable, same contract
    /// every other implementer states on its own `setLibrary`: null draws
    /// the empty state, same as "fewer than two traces".
    void setLibrary(const rta::trace::TraceLibrary* library) override;

    /// What `setLibrary` last stored -- production API, not a test-only
    /// friend seam, the same reason `RtaView::library()`/`TransferView::
    /// library()` are public: "the library reached this pane" is a fact a
    /// test reads back, not a private implementation detail.
    [[nodiscard]] const rta::trace::TraceLibrary* library() const noexcept { return library_; }

    /// False whenever the library has fewer than two entries, OR either of
    /// the first two has no phase to build a `VirtualTrace` from
    /// (`VirtualTrace::fromTrace` refuses a magnitude-only capture --
    /// VirtualTrace.h's own class comment). The empty state paints instead.
    [[nodiscard]] bool hasTwoTraces() const noexcept { return hasSources_; }

    /// The two `TraceLibrary` ids the model was last fed, empty when
    /// `hasTwoTraces()` is false -- what
    /// `test_main_component_panes_xover.cpp` pins against the ids a test
    /// added, so "receives exactly those two" is a property of THIS pane's
    /// own bookkeeping and not an inference from pixels.
    [[nodiscard]] const std::string& highTraceId() const noexcept { return highId_; }
    [[nodiscard]] const std::string& lowTraceId() const noexcept { return lowId_; }

    [[nodiscard]] const CrossoverSurface& surface() const noexcept { return surface_; }

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /// Rebuilds `surface_` from `library_`'s first two entries, or clears
    /// `hasSources_` when there are not two, or either lacks phase. Called
    /// from `setLibrary` (so the very first library handed to a freshly
    /// built pane is not stale for one whole timer tick) and from
    /// `timerCallback` on a revision change.
    void refreshFromLibrary();

    void paintEmptyState(juce::Graphics&, juce::Rectangle<int> area) const;

    const rta::trace::TraceLibrary* library_ = nullptr;
    CrossoverSurface surface_;
    GateState gate_;
    bool hasSources_ = false;
    std::string highId_;
    std::string lowId_;

    juce::Rectangle<int> phaseArea_;
    juce::Rectangle<int> summationArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrossoverPaneView)
};

}  // namespace rta::view
