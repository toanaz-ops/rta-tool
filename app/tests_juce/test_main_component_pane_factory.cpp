// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rtatool_view_tests. Fix round, PR #26.
//
// makePaneFactory (PaneFactory.h) is the REAL closure MainComponent.cpp
// builds workspace_ with. This calls it directly -- not a hand-copied local
// fixture like test_workspace_view.cpp's own `makeFactory` (which exists to
// answer a different question: "does WorkspaceView call whatever factory it
// is handed", and would never notice a missing branch in the production
// factory itself). Before this fix, PaneView::Spl silently built an RtaView.
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PaneFactory.h"
#include "measure/SnapshotSource.h"
#include "view/RtaView.h"
#include "view/SplView.h"
#include "view/TransferView.h"

using rta::measure::StaticSnapshotSource;
using rta::view::PaneView;
using rta::view::RtaView;
using rta::view::SplView;
using rta::view::TransferView;

TEST_CASE("makePaneFactory builds an SplView, not an RtaView, for PaneView::Spl",
         "[main_component_pane_factory]") {
    StaticSnapshotSource source;
    auto factory = makePaneFactory(source);

    auto pane = factory(PaneView::Spl);
    REQUIRE(pane != nullptr);
    CHECK(dynamic_cast<SplView*>(pane.get()) != nullptr);
    // Never an RtaView masquerading as the SPL pane -- the exact bug this
    // test exists to catch (fellBack was false; the factory itself
    // substituted the wrong concrete type).
    CHECK(dynamic_cast<RtaView*>(pane.get()) == nullptr);
}

TEST_CASE("makePaneFactory still builds Transfer and Rta panes correctly",
         "[main_component_pane_factory]") {
    StaticSnapshotSource source;
    auto factory = makePaneFactory(source);

    auto transferPane = factory(PaneView::Transfer);
    REQUIRE(transferPane != nullptr);
    CHECK(dynamic_cast<TransferView*>(transferPane.get()) != nullptr);

    auto rtaPane = factory(PaneView::Rta);
    REQUIRE(rtaPane != nullptr);
    CHECK(dynamic_cast<RtaView*>(rtaPane.get()) != nullptr);
}
