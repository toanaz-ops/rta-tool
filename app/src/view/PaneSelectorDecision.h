// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/view. No JUCE: enforced by the
// measure_has_no_framework_deps ctest.
//
// Owner decision 2026-09-26 ("the gap"): MainComponent built exactly one
// hardcoded `rta` workspace pane and nothing in the running app could ever
// reach the `spl` or `transfer` panes, even though both are built and
// tested (docs/HUMAN-QA-QUEUE.md "Muc moi mo khi dong lane L6a";
// docs/reports/009-spl-pro.md). This header is the pure half of the fix: a
// selector button maps to a `PaneView` the same way a saved session's
// `[pane] view=` string does.
#pragma once

#include "trace/Workspace.h"
#include "view/PaneRegistry.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rta::view {

/// The pane selector's buttons. A plain enum, not N bools -- exactly one is
/// ever "selected" at a time (a radio group, not independent switches), and
/// this type cannot express "two selected" or "none selected" the way bools
/// could.
///
/// `Xover` added by the ALIGN-R8 reversal (owner, 2026-09-26): the 4th
/// selector button, `XOVER`, routed through the same seam as the other
/// three.
///
/// `Eq` added by the L7-EQ UI lane (plan T4): the 5th button, same seam.
///
/// `Align` added by the ALIGN-UI lane (plan T4): the 6th button, same seam.
///
/// `Rt60` added by the RT60-UI lane (plan T3): the 7th button, same seam.
///
/// `Ir` added by the H9 / L4c IR-gate lane (plan A3): the 8th button, same seam.
enum class PaneSelectorButton { Rta, Transfer, Spl, Xover, Eq, Align, Rt60, Ir };

/// Maps a selector button to the `PaneView` the workspace should rebuild
/// itself to show. Routed through `resolvePaneView` (`PaneRegistry.h`)
/// rather than a second, hand-written mapping: that function is already the
/// one place a pane name resolves to a `PaneView`, and reusing it means the
/// selector and a saved session's `[pane] view=` string can never disagree
/// about what "spl" means -- the exact kind of drift PR #26's own bug
/// (`makePaneFactory`'s `Spl` branch silently building an `RtaView`) came
/// from two switches that were supposed to agree and did not.
///
/// `fellBack` is always false for the three defined buttons today; the
/// field exists so a FUTURE enumerator added here with no branch below
/// reports itself through the same "fell back to Rta, and says so" contract
/// every other reader of a pane name already carries, rather than silently
/// drawing RTA with nothing to grep for. Not a `switch` with no `default`:
/// an exhaustive switch over all three current enumerators leaves the
/// trailing fallback unreachable for THIS enum, which is a dead branch a
/// warnings-as-errors build flags -- the if-chain shape `resolvePaneView`
/// itself already uses has no such branch.
[[nodiscard]] inline PaneResolution decidePaneSelection(PaneSelectorButton button) {
    if (button == PaneSelectorButton::Transfer) return resolvePaneView("transfer");
    if (button == PaneSelectorButton::Spl) return resolvePaneView("spl");
    if (button == PaneSelectorButton::Xover) return resolvePaneView("xover");
    if (button == PaneSelectorButton::Eq) return resolvePaneView("eq");
    if (button == PaneSelectorButton::Align) return resolvePaneView("align");
    if (button == PaneSelectorButton::Rt60) return resolvePaneView("rt60");
    if (button == PaneSelectorButton::Ir) return resolvePaneView("ir");
    if (button == PaneSelectorButton::Rta) return resolvePaneView("rta");

    // Only reachable once a future enumerator is added to PaneSelectorButton
    // with no matching branch above -- resolvePaneView("rta") above already
    // covers every enumerator that exists today.
    PaneResolution out;
    out.fellBack = true;
    out.requested = "<unrecognised pane selector button>";
    return out;
}

// ---- Multi-pane editing (owner card HW-176 = A, 2026-11-09) ----------------
//
// A plain view press still REPLACES the workspace with one pane. `+ PANE` arms
// a one-shot: the NEXT view press appends that view instead (duplicates
// allowed -- two TRANSFER panes at different sources is a real layout), then
// the arm clears. `- PANE` drops the last pane. Everything below is pure so the
// rules are provable on all three CI operating systems with no JUCE.
//
// Why not shift-click on the view buttons: invisible, the radio group the
// selector had then lit only one button (it is gone now, HW-176), "remove" collides with duplicates (which TRANSFER?), and a
// programmatic click carries no modifier keys. See
// docs/dsp/2026-11-09-multi-pane-selector.md.

/// What a press leaves behind. `panes` is the layout to build (== the input
/// when `changed` is false). `refusal` is the readout text when the press was
/// declined; it is empty otherwise.
struct PaneEdit {
    std::vector<rta::trace::PaneSpec> panes;
    bool changed = false;
    std::string refusal;
};

/// Equal weights: appending or removing re-splits the workspace evenly, the
/// same split a fresh session of N panes gets. Carrying the old weights over
/// would give a new pane a share nobody chose.
[[nodiscard]] inline std::vector<rta::trace::PaneSpec> withEqualWeights(
    std::vector<rta::trace::PaneSpec> panes) {
    for (auto& pane : panes) pane.weight = 1.0f;
    return panes;
}

[[nodiscard]] inline bool canAddPane(std::size_t paneCount) {
    return paneCount < static_cast<std::size_t>(rta::trace::kMaxPanes);
}

[[nodiscard]] inline bool canRemovePane(std::size_t paneCount) { return paneCount > 1; }

/// A view button pressed with the add-arm in state `addArmed`.
[[nodiscard]] inline PaneEdit decideViewPress(const std::vector<rta::trace::PaneSpec>& current,
                                              PaneSelectorButton button, bool addArmed) {
    const auto resolution = decidePaneSelection(button);
    PaneEdit edit;
    edit.panes = current;
    if (addArmed) {
        if (!canAddPane(current.size())) {
            edit.refusal = "panes: at most " + std::to_string(rta::trace::kMaxPanes) +
                           " panes -- remove one first";
            return edit;
        }
        edit.panes.push_back(rta::trace::PaneSpec{resolution.requested});
        edit.panes = withEqualWeights(std::move(edit.panes));
        edit.changed = true;
        return edit;
    }
    // Idempotent: the one pane already showing this view. A multi-pane layout
    // is never "already showing" a single view -- pressing RTA there collapses
    // it to one RTA pane, which is what the operator asked for.
    if (current.size() == 1 && resolvePaneView(current.front().view).view == resolution.view) {
        return edit;
    }
    edit.panes = {rta::trace::PaneSpec{resolution.requested}};
    edit.changed = true;
    return edit;
}

/// `- PANE`: removes the LAST pane (the one most recently appended).
[[nodiscard]] inline PaneEdit decideRemoveLast(const std::vector<rta::trace::PaneSpec>& current) {
    PaneEdit edit;
    edit.panes = current;
    if (!canRemovePane(current.size())) {
        edit.refusal = "panes: the last pane cannot be removed";
        return edit;
    }
    edit.panes.pop_back();
    edit.panes = withEqualWeights(std::move(edit.panes));
    edit.changed = true;
    return edit;
}

/// The views STORE can freeze, one entry per distinct kind, in first-seen
/// order. SPL has no per-bin curve; XOVER/EQ/ALIGN/RT60/IR consume stored traces
/// rather than produce them (STORE research C2). Two TRANSFER panes are one
/// kind: they read the same fixed-FFT block, so a second trace would be a
/// byte-identical duplicate.
[[nodiscard]] inline std::vector<PaneView> storableKinds(const std::vector<PaneView>& present) {
    std::vector<PaneView> kinds;
    for (const auto view : present) {
        if (view != PaneView::Rta && view != PaneView::Transfer) continue;
        bool seen = false;
        for (const auto kind : kinds) seen = seen || kind == view;
        if (!seen) kinds.push_back(view);
    }
    return kinds;
}

/// The views the specs resolve to, in pane order.
[[nodiscard]] inline std::vector<PaneView> viewsOf(const std::vector<rta::trace::PaneSpec>& panes) {
    std::vector<PaneView> out;
    out.reserve(panes.size());
    for (const auto& pane : panes) out.push_back(resolvePaneView(pane.view).view);
    return out;
}


}  // namespace rta::view
