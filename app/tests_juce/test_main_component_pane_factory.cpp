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
#include "view/AlignPaneView.h"
#include "view/CrossoverPaneView.h"
#include "view/EqPaneView.h"
#include "view/Rt60PaneView.h"
#include "view/RtaView.h"
#include "view/SplView.h"
#include "view/TransferView.h"

using rta::measure::StaticSnapshotSource;
using rta::view::AlignPaneBinding;
using rta::view::AlignPaneView;
using rta::view::CrossoverPaneView;
using rta::view::EqPaneBinding;
using rta::view::EqPaneView;
using rta::view::PaneView;
using rta::view::Rt60PaneBinding;
using rta::view::Rt60PaneView;
using rta::view::Rt60Settings;
using rta::view::RtaView;
using rta::view::SplView;
using rta::view::TransferView;

TEST_CASE("makePaneFactory builds an SplView, not an RtaView, for PaneView::Spl",
         "[main_component_pane_factory]") {
    StaticSnapshotSource source;
    rta::measure::EqPaneModel eqModel;
    rta::measure::AlignRunner alignRunner;
    auto factory = makePaneFactory(source, EqPaneBinding{ &eqModel, {} }, AlignPaneBinding{ &alignRunner }, Rt60PaneBinding{});

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
    rta::measure::EqPaneModel eqModel;
    rta::measure::AlignRunner alignRunner;
    auto factory = makePaneFactory(source, EqPaneBinding{ &eqModel, {} }, AlignPaneBinding{ &alignRunner }, Rt60PaneBinding{});

    auto transferPane = factory(PaneView::Transfer);
    REQUIRE(transferPane != nullptr);
    CHECK(dynamic_cast<TransferView*>(transferPane.get()) != nullptr);

    auto rtaPane = factory(PaneView::Rta);
    REQUIRE(rtaPane != nullptr);
    CHECK(dynamic_cast<RtaView*>(rtaPane.get()) != nullptr);
}

TEST_CASE("makePaneFactory builds a CrossoverPaneView for PaneView::Xover",
         "[main_component_pane_factory]") {
    // ALIGN-R8 reversal (owner, 2026-09-26): the same "own branch, own
    // concrete type" property the Spl case above pins, one enumerator later.
    StaticSnapshotSource source;
    rta::measure::EqPaneModel eqModel;
    rta::measure::AlignRunner alignRunner;
    auto factory = makePaneFactory(source, EqPaneBinding{ &eqModel, {} }, AlignPaneBinding{ &alignRunner }, Rt60PaneBinding{});

    auto xoverPane = factory(PaneView::Xover);
    REQUIRE(xoverPane != nullptr);
    CHECK(dynamic_cast<CrossoverPaneView*>(xoverPane.get()) != nullptr);
    CHECK(dynamic_cast<RtaView*>(xoverPane.get()) == nullptr);
}

TEST_CASE("makePaneFactory builds an EqPaneView, on the model it was handed, for PaneView::Eq",
         "[main_component_pane_factory]") {
    // L7-EQ UI lane, plan T4: the same "own branch, own concrete type" property
    // the Spl and Xover cases pin, one enumerator later. The model is the
    // caller's, so a pane rebuilt by a selector click reads the SAME committed
    // list the previous pane wrote (plan T5's survive-a-rebuild acceptance).
    StaticSnapshotSource source;
    rta::measure::EqPaneModel eqModel;
    rta::measure::AlignRunner alignRunner;
    auto factory = makePaneFactory(source, EqPaneBinding{ &eqModel, {} }, AlignPaneBinding{ &alignRunner }, Rt60PaneBinding{});

    auto first = factory(PaneView::Eq);
    REQUIRE(first != nullptr);
    auto* eqPane = dynamic_cast<EqPaneView*>(first.get());
    REQUIRE(eqPane != nullptr);
    CHECK(dynamic_cast<RtaView*>(first.get()) == nullptr);
    CHECK(&eqPane->modelForTest() == &eqModel);

    auto second = factory(PaneView::Eq);
    CHECK(&dynamic_cast<EqPaneView&>(*second).modelForTest() == &eqModel);
}

TEST_CASE("makePaneFactory builds an AlignPaneView, on the runner it was handed, for PaneView::Align",
          "[main_component_pane_factory]") {
    // ALIGN-UI lane, plan T4: the same "own branch, own concrete type"
    // property the Spl, Xover and Eq cases pin, one enumerator later. The
    // runner is the caller's, so a pane rebuilt by a selector click keeps the
    // operator's answers. Mutant: delete the Align branch in PaneFactory.cpp --
    // the fall-through builds an RtaView and the first CHECK goes RED.
    StaticSnapshotSource source;
    rta::measure::EqPaneModel eqModel;
    rta::measure::AlignRunner alignRunner;
    auto factory = makePaneFactory(source, EqPaneBinding{ &eqModel, {} }, AlignPaneBinding{ &alignRunner }, Rt60PaneBinding{});

    auto first = factory(PaneView::Align);
    REQUIRE(first != nullptr);
    CHECK(dynamic_cast<AlignPaneView*>(first.get()) != nullptr);
    CHECK(dynamic_cast<RtaView*>(first.get()) == nullptr);

    // An answer written through one pane is read back by the next one.
    auto* pane = dynamic_cast<AlignPaneView*>(first.get());
    REQUIRE(pane != nullptr);
    pane->topologyComboForTest().setSelectedId(2, juce::sendNotificationSync);
    REQUIRE(alignRunner.answers().topology.has_value());
    first.reset();
    auto second = factory(PaneView::Align);
    auto* next = dynamic_cast<AlignPaneView*>(second.get());
    REQUIRE(next != nullptr);
    CHECK(alignRunner.answers().topology.has_value());
}

TEST_CASE("makePaneFactory builds an Rt60PaneView, on the settings it was handed, for PaneView::Rt60",
          "[main_component_pane_factory]") {
    // RT60-UI lane, plan T3: the seventh enumerator's own branch. The settings
    // are the caller's (MainComponentRt60 owns them), so a pane rebuilt by a
    // selector click starts on the TAIL the operator last chose. Mutant: delete
    // the Rt60 branch in PaneFactory.cpp -- the fall-through builds an RtaView and
    // the first CHECK goes RED.
    StaticSnapshotSource source;
    rta::measure::EqPaneModel eqModel;
    rta::measure::AlignRunner alignRunner;
    Rt60Settings settings;
    Rt60PaneBinding binding;
    binding.settings = &settings;
    auto factory = makePaneFactory(source, EqPaneBinding{ &eqModel, {} }, AlignPaneBinding{ &alignRunner }, binding);

    auto first = factory(PaneView::Rt60);
    REQUIRE(first != nullptr);
    auto* pane = dynamic_cast<Rt60PaneView*>(first.get());
    CHECK(pane != nullptr);
    CHECK(dynamic_cast<RtaView*>(first.get()) == nullptr);
    REQUIRE(pane != nullptr);
    CHECK(pane->tailComboForTest().getSelectedId() == 1);  // 3 s, the default

    pane->tailComboForTest().setSelectedId(2, juce::sendNotificationSync);  // 6 s
    CHECK(settings.tailSeconds == 6.0);
    first.reset();
    auto second = factory(PaneView::Rt60);
    auto* next = dynamic_cast<Rt60PaneView*>(second.get());
    REQUIRE(next != nullptr);
    CHECK(next->tailComboForTest().getSelectedId() == 2);  // restored, not reset to 3 s
}
