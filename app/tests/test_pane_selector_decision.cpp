// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. The pure half of the pane selector (owner
// decision 2026-09-26): PaneSelectorDecision.h maps a selector button to a
// PaneView through the same resolvePaneView a saved session uses, so this
// is provable OFF (no JUCE, no MainComponent) on all three CI operating
// systems.
#include "view/PaneSelectorDecision.h"

#include <catch2/catch_test_macros.hpp>

using rta::view::decidePaneSelection;
using rta::view::PaneSelectorButton;
using rta::view::PaneView;

TEST_CASE("each defined button resolves to its own PaneView, with no fallback",
         "[pane_selector_decision]") {
    const auto rta = decidePaneSelection(PaneSelectorButton::Rta);
    CHECK(rta.view == PaneView::Rta);
    CHECK_FALSE(rta.fellBack);
    CHECK(rta.requested == "rta");

    const auto transfer = decidePaneSelection(PaneSelectorButton::Transfer);
    CHECK(transfer.view == PaneView::Transfer);
    CHECK_FALSE(transfer.fellBack);
    CHECK(transfer.requested == "transfer");

    const auto spl = decidePaneSelection(PaneSelectorButton::Spl);
    CHECK(spl.view == PaneView::Spl);
    CHECK_FALSE(spl.fellBack);
    CHECK(spl.requested == "spl");

    const auto xover = decidePaneSelection(PaneSelectorButton::Xover);
    CHECK(xover.view == PaneView::Xover);
    CHECK_FALSE(xover.fellBack);
    CHECK(xover.requested == "xover");

    const auto eq = decidePaneSelection(PaneSelectorButton::Eq);
    CHECK(eq.view == PaneView::Eq);
    CHECK_FALSE(eq.fellBack);
    CHECK(eq.requested == "eq");
}

TEST_CASE("an unrecognised button falls back to Rta and reports it",
         "[pane_selector_decision]") {
    // No enumerator outside {Rta, Transfer, Spl, Xover, Eq} exists today, so
    // this pins the fallback branch itself against the same static_cast an
    // uninitialised or corrupted enum value could produce -- the same
    // "unknown -> Rta, reported" contract resolvePaneView("nonexistent")
    // already carries (test_spl_strip.cpp).
    const auto out = decidePaneSelection(static_cast<PaneSelectorButton>(99));
    CHECK(out.view == PaneView::Rta);
    CHECK(out.fellBack);
}

TEST_CASE("paneViewName round-trips through resolvePaneView for every PaneView",
         "[pane_selector_decision]") {
    // PR #45 fix round 3 (PR #43 reconciliation checklist item 2):
    // paneViewName() (PaneRegistry.h) is the inverse resolvePaneView() a
    // session Save writes with -- PR #43 added it missing the Xover arm, so
    // saving with XOVER showing silently wrote "rta" and Session Open lost
    // the pane choice on the very next round trip. Every CURRENT
    // PaneView enumerator is named explicitly here (not looped over a cast
    // range) so the list this test checks is exactly the list a reviewer
    // reads, the same reason resolvePaneView's own test above names each
    // button rather than iterating.
    //
    // Mutant: delete the `if (view == PaneView::Xover) return "xover";` arm
    // -- paneViewName(Xover) falls through to "rta", resolvePaneView("rta")
    // resolves to PaneView::Rta, and the round trip below goes RED (Xover !=
    // Rta) rather than reporting a fallback that would ALSO have failed a
    // `fellBack` check, so this one check alone catches it.
    // L7-EQ UI (plan T4): five enumerators now. Mutant: delete the
    // `if (view == PaneView::Eq) return "eq";` arm -- paneViewName(Eq) falls to
    // "rta" and the round trip below goes RED (Eq != Rta), same shape as above.
    for (const auto view : { PaneView::Rta, PaneView::Transfer, PaneView::Spl, PaneView::Xover, PaneView::Eq }) {
        const auto name = rta::view::paneViewName(view);
        const auto resolution = rta::view::resolvePaneView(name);
        CHECK_FALSE(resolution.fellBack);
        CHECK(resolution.view == view);
    }
}
