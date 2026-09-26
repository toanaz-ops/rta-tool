// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view.
//
// The ALIGN-R8 reversal (owner, 2026-09-26; amendment in docs/dsp/
// 2026-09-06-l7-alignment-wizard.md): G18 becomes a live pane, reached
// through the 4th selector button (`XOVER`, PaneSelectorDecision.h) the same
// way `SplView` was mounted through `makePaneFactory` (PaneFactory.cpp) in
// PR #26.
//
// PR #45 fix round 1 (verifier, F3/F4): the first cut of this pane took
// `TraceLibrary::entries()[0]/[1]` and a hardcoded LR4 topology -- exactly
// the "maximise the sum" shape of inference the record's own ruling
// (docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.0 ruling 1, Sec.6) refuses
// for a MEASUREMENT, applied here to a PICK: which capture is which side,
// what topology was built, and whether the processor already inverted one
// output, are ASKED, never inferred from library order or hardcoded. This
// header is the asking: four pickers -- the high-pass trace, the low-pass
// trace, the topology, and the inversion -- each starting in an explicit
// "not asked" state, with no default.
//
// PR #45 fix round 2 (verifier, MEDIUM B): the round-1 cut asked topology
// but hardcoded `ProcessorInversion::No` at the call to `setAskedTopology`,
// which is exactly the wizard question (c) the record (Sec.2(c), Sec.11)
// says "must be ASKED, with three answers" -- silently answering it here
// is the same defect Sec.11 names as "one topology question, no inversion
// question". The fourth picker below answers it for real; `AlignmentWizard`
// (the flow that would additionally drive an actual solo sequence) is still
// a separate, unwired lane, but the ANSWER this pane reads is never assumed.
#pragma once

#include "measure/CrossoverTopology.h"
#include "view/CrossoverSurface.h"
#include "view/PaneRegistry.h"
#include "view/RepaintGate.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>
#include <string>
#include <vector>

namespace rta::view {

/// The live G18 pane. `LibraryConsumer` is how `WorkspaceView` reaches this
/// with no `CrossoverPaneView.h` include of its own (`PaneRegistry.h`'s
/// class comment) -- the same seam `RtaView`/`TransferView` already use.
class CrossoverPaneView final : public juce::Component, public LibraryConsumer, private juce::Timer {
public:
    CrossoverPaneView();
    ~CrossoverPaneView() override;

    /// `override`: implements `LibraryConsumer` (`PaneRegistry.h`). Nullable,
    /// same contract every other implementer states: null is the same
    /// refusal state as an empty library (fewer than 2 eligible traces).
    void setLibrary(const rta::trace::TraceLibrary* library) override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // --- test hooks -----------------------------------------------------
    // Every name below ends in `ForTest` and is referenced, whole-word, from
    // app/tests_juce/test_main_component_panes_xover.cpp -- the exact
    // contract tools/orphan_check.py's TEST HOOK category checks for (that
    // tool's own module comment): otherwise-unreachable-from-`rtatool`, but
    // proven live by a real test caller, so it is reported as a test hook
    // rather than failed as an orphan. There is no public, non-`ForTest`
    // accessor left on this class with zero production caller -- the PR #45
    // fix round's own finding was that the first cut had five such names.
    [[nodiscard]] bool hasCompleteSelectionForTest() const noexcept { return ready_; }
    [[nodiscard]] const std::string& chosenHighTraceIdForTest() const noexcept { return hpId_; }
    [[nodiscard]] const std::string& chosenLowTraceIdForTest() const noexcept { return lpId_; }
    [[nodiscard]] const CrossoverSurface& surfaceForTest() const noexcept { return surface_; }
    [[nodiscard]] const std::vector<std::string>& eligibleTraceIdsForTest() const noexcept {
        return eligibleIds_;
    }
    [[nodiscard]] juce::ComboBox& highTraceComboForTest() noexcept { return hpCombo_; }
    [[nodiscard]] juce::ComboBox& lowTraceComboForTest() noexcept { return lpCombo_; }
    [[nodiscard]] juce::ComboBox& topologyComboForTest() noexcept { return topologyCombo_; }
    [[nodiscard]] juce::ComboBox& inversionComboForTest() noexcept { return inversionCombo_; }

    /// Set by every `paint()` call: true iff the chart-drawing branch (not
    /// the refusal branch) ran. A numeric assertion on `surfaceForTest()`
    /// cannot tell a test whether `paint()` itself still gates on `ready_` --
    /// this can (test gap D, PR #45 fix round 2).
    [[nodiscard]] bool lastPaintDrewChartForTest() const noexcept { return lastPaintDrewChart_; }

private:
    void timerCallback() override;

    /// Rebuilds `eligibleIds_` (visible entries with phase -- see the .cpp's
    /// own comment on why hidden is excluded outright, not merely marked),
    /// then the two trace combos' contents, then `ready_`/`surface_`. Called
    /// from `setLibrary`, from a combo's `onChange`, and from `timerCallback`
    /// on a revision change -- one function, so "what the pane currently
    /// shows" can never be computed two different ways that drift.
    void refreshFromLibrary();

    /// Repopulates `combo` from `eligibleIds_` and restores `selectedId` if
    /// it is still eligible; otherwise clears `selectedId` to "" (not asked)
    /// -- the "a selected entry that is removed or hidden reverts to not
    /// asked" rule, applied at the one place both combos share.
    void rebuildTraceCombo(juce::ComboBox& combo, std::string& selectedId);

    void hpComboChanged();
    void lpComboChanged();
    void topologyComboChanged();
    void inversionComboChanged();

    void paintRefusal(juce::Graphics&, juce::Rectangle<int> area) const;
    [[nodiscard]] juce::String refusalMessage() const;

    const rta::trace::TraceLibrary* library_ = nullptr;
    CrossoverSurface surface_;
    GateState gate_;

    /// Trace ids with `visible == true` and a phase field, in library order.
    /// Combo item id `i+1` names `eligibleIds_[i]` -- REBUILT every refresh,
    /// because the eligible set itself can shrink or grow between refreshes
    /// (a rename, a hide, a new capture).
    std::vector<std::string> eligibleIds_;

    /// "" means not asked -- ids, not indices, so a picked entry that is
    /// later removed or hidden is detected by `rebuildTraceCombo` rather
    /// than silently continuing to point at whatever now sits at that index.
    std::string hpId_;
    std::string lpId_;
    std::optional<rta::measure::Topology> topology_;
    std::optional<rta::measure::ProcessorInversion> inversion_;

    /// True only once all four are chosen, the two trace ids are DISTINCT,
    /// and both still resolve to a real, phase-bearing trace. Gates
    /// `paint()` between the refusal message and the real chart -- there is
    /// no third, partial state. (Round 2, MEDIUM A: distinctness was
    /// missing, so the same trace id chosen on both sides -- or the only
    /// eligible trace chosen on both sides -- read as complete.)
    bool ready_ = false;

    /// Test-only, see `lastPaintDrewChartForTest()`.
    bool lastPaintDrewChart_ = false;

    juce::ComboBox hpCombo_;
    juce::ComboBox lpCombo_;
    juce::ComboBox topologyCombo_;
    juce::ComboBox inversionCombo_;

    juce::Rectangle<int> chartArea_;
    juce::Rectangle<int> phaseArea_;
    juce::Rectangle<int> summationArea_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CrossoverPaneView)
};

}  // namespace rta::view
