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
}

TEST_CASE("an unrecognised button falls back to Rta and reports it",
         "[pane_selector_decision]") {
    // No enumerator outside {Rta, Transfer, Spl} exists today, so this pins
    // the fallback branch itself against the same static_cast an
    // uninitialised or corrupted enum value could produce -- the same
    // "unknown -> Rta, reported" contract resolvePaneView("nonexistent")
    // already carries (test_spl_strip.cpp).
    const auto out = decidePaneSelection(static_cast<PaneSelectorButton>(99));
    CHECK(out.view == PaneView::Rta);
    CHECK(out.fellBack);
}
