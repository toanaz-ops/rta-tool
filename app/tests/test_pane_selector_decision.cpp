// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. The pure half of the pane selector (owner
// decision 2026-09-26): PaneSelectorDecision.h maps a selector button to a
// PaneView through the same resolvePaneView a saved session uses, so this
// is provable OFF (no JUCE, no MainComponent) on all three CI operating
// systems.
#include "view/PaneSelectorDecision.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

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

    const auto align = decidePaneSelection(PaneSelectorButton::Align);
    CHECK(align.view == PaneView::Align);
    CHECK_FALSE(align.fellBack);
    CHECK(align.requested == "align");

    const auto rt60 = decidePaneSelection(PaneSelectorButton::Rt60);
    CHECK(rt60.view == PaneView::Rt60);
    CHECK_FALSE(rt60.fellBack);
    CHECK(rt60.requested == "rt60");
}

TEST_CASE("an unrecognised button falls back to Rta and reports it",
         "[pane_selector_decision]") {
    // No enumerator outside {Rta, Transfer, Spl, Xover, Eq, Align, Rt60} exists today, so
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
    // ALIGN-UI lane (plan T4): six enumerators now. Mutant: delete the
    // `if (view == PaneView::Align) return "align";` arm -- paneViewName(Align)
    // falls to "rta" and the round trip below goes RED (Align != Rta).
    // RT60-UI lane (plan T3): seven. Mutant: delete the `"rt60"` arm of
    // paneViewName (or of resolvePaneView) -- the round trip goes RED the same way.
    for (const auto view : { PaneView::Rta, PaneView::Transfer, PaneView::Spl, PaneView::Xover, PaneView::Eq,
                             PaneView::Align, PaneView::Rt60 }) {
        const auto name = rta::view::paneViewName(view);
        const auto resolution = rta::view::resolvePaneView(name);
        CHECK_FALSE(resolution.fellBack);
        CHECK(resolution.view == view);
    }
}

// ---- HW-176 multi-pane editing ----------------------------------------------

using rta::trace::PaneSpec;
using rta::view::canAddPane;
using rta::view::canRemovePane;
using rta::view::decideRemoveLast;
using rta::view::decideViewPress;
using rta::view::storableKinds;

namespace {
std::vector<std::string> names(const std::vector<PaneSpec>& panes) {
    std::vector<std::string> out;
    for (const auto& pane : panes) out.push_back(pane.view);
    return out;
}
}  // namespace

TEST_CASE("a plain view press replaces the workspace with exactly one pane",
         "[pane_selector_decision]") {
    const std::vector<PaneSpec> two{PaneSpec{"rta"}, PaneSpec{"transfer"}};
    const auto edit = decideViewPress(two, PaneSelectorButton::Spl, false);
    CHECK(edit.changed);
    CHECK(edit.refusal.empty());
    CHECK(names(edit.panes) == std::vector<std::string>{"spl"});
}

TEST_CASE("pressing the view already alone on screen changes nothing; in a multi-pane layout it collapses",
         "[pane_selector_decision]") {
    const auto same = decideViewPress({PaneSpec{"rta"}}, PaneSelectorButton::Rta, false);
    CHECK_FALSE(same.changed);

    const auto collapsed =
        decideViewPress({PaneSpec{"rta"}, PaneSpec{"transfer"}}, PaneSelectorButton::Rta, false);
    CHECK(collapsed.changed);
    CHECK(names(collapsed.panes) == std::vector<std::string>{"rta"});
}

TEST_CASE("an armed view press appends, duplicates allowed, with equal weights",
         "[pane_selector_decision]") {
    const auto edit = decideViewPress({PaneSpec{"transfer", 0.75f}}, PaneSelectorButton::Transfer, true);
    CHECK(edit.changed);
    REQUIRE(edit.panes.size() == 2u);
    CHECK(names(edit.panes) == std::vector<std::string>{"transfer", "transfer"});
    CHECK(edit.panes[0].weight == 1.0f);
    CHECK(edit.panes[1].weight == 1.0f);
}

TEST_CASE("the pane cap is kMaxPanes exactly: add allowed at kMaxPanes - 1, refused at kMaxPanes",
         "[pane_selector_decision]") {
    const auto cap = static_cast<std::size_t>(rta::trace::kMaxPanes);
    CHECK(canAddPane(cap - 1));
    CHECK_FALSE(canAddPane(cap));

    std::vector<PaneSpec> full(cap, PaneSpec{"rta"});
    const auto refused = decideViewPress(full, PaneSelectorButton::Spl, true);
    CHECK_FALSE(refused.changed);
    CHECK_FALSE(refused.refusal.empty());
    CHECK(refused.panes.size() == cap);

    std::vector<PaneSpec> room(cap - 1, PaneSpec{"rta"});
    const auto accepted = decideViewPress(room, PaneSelectorButton::Spl, true);
    CHECK(accepted.changed);
    CHECK(accepted.panes.size() == cap);
}

TEST_CASE("remove drops the LAST pane, and is refused at one pane", "[pane_selector_decision]") {
    CHECK_FALSE(canRemovePane(1));
    CHECK(canRemovePane(2));

    const auto edit = decideRemoveLast({PaneSpec{"rta"}, PaneSpec{"transfer"}, PaneSpec{"xover"}});
    CHECK(edit.changed);
    CHECK(names(edit.panes) == std::vector<std::string>{"rta", "transfer"});

    const auto refused = decideRemoveLast({PaneSpec{"rta"}});
    CHECK_FALSE(refused.changed);
    CHECK_FALSE(refused.refusal.empty());
    CHECK(refused.panes.size() == 1u);
}

TEST_CASE("STORE kinds: one per distinct storable view, first-seen order, others ignored",
         "[pane_selector_decision]") {
    const auto kinds = storableKinds(
        {PaneView::Xover, PaneView::Transfer, PaneView::Transfer, PaneView::Rta, PaneView::Spl});
    REQUIRE(kinds.size() == 2u);
    CHECK(kinds[0] == PaneView::Transfer);
    CHECK(kinds[1] == PaneView::Rta);

    CHECK(storableKinds({PaneView::Xover, PaneView::Spl, PaneView::Eq}).empty());
    CHECK(storableKinds({PaneView::Transfer, PaneView::Transfer}).size() == 1u);
}
