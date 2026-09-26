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

#include "view/PaneRegistry.h"

namespace rta::view {

/// The pane selector's three buttons. A plain enum, not three bools --
/// exactly one is ever "selected" at a time (a radio group, not three
/// independent switches), and this type cannot express "two selected" or
/// "none selected" the way three bools could.
enum class PaneSelectorButton { Rta, Transfer, Spl };

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
    if (button == PaneSelectorButton::Rta) return resolvePaneView("rta");

    // Only reachable once a future enumerator is added to PaneSelectorButton
    // with no matching branch above -- resolvePaneView("rta") above already
    // covers every enumerator that exists today.
    PaneResolution out;
    out.fellBack = true;
    out.requested = "<unrecognised pane selector button>";
    return out;
}

}  // namespace rta::view
