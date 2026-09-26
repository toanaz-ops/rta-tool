// SPDX-License-Identifier: AGPL-3.0-or-later
// Owner decision 2026-09-26 ("the gap"): before this task MainComponent
// built exactly one hardcoded `rta` workspace pane and nothing in the
// running app could ever reach the `spl` or `transfer` panes, even though
// both are built and tested (docs/HUMAN-QA-QUEUE.md "Muc moi mo khi dong
// lane L6a"; docs/reports/009-spl-pro.md). test_pane_selector_decision.cpp
// (app/tests, OFF) proves the pure button->PaneView mapping;
// test_main_component_pane_factory.cpp (this target) proves makePaneFactory
// itself. What only a REAL `MainComponent` can prove is the wiring between
// them: that `selectPaneView` actually rebuilds `workspace_` through that
// real factory, and that doing so never reaches into `analysisThread_`'s SPL
// logging -- a MainComponent-level regression no lower-level test can see,
// since neither PaneRegistry.h nor PaneFactory.h/.cpp know AnalysisThread
// exists.
//
// Constructing a real MainComponent needs no device or message loop, the
// same way tools/snapshot.cpp's main-live.png does: `setSyntheticMode(true)`
// drives the whole analysis chain from a deterministic in-process feed.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "measure/SplConfig.h"
#include "view/RtaView.h"
#include "view/SplView.h"
#include "view/TransferView.h"

#include <juce_core/juce_core.h>

#include <array>
#include <filesystem>
#include <functional>

using rta::view::PaneSelectorButton;
using rta::view::PaneView;
using rta::view::RtaView;
using rta::view::SplView;
using rta::view::TransferView;

namespace {

/// Polls `predicate` for up to `timeoutMs`, sleeping between checks -- the
/// same shape test_spl_log_wiring_disable.cpp's own `waitForSplLoggingOff`
/// gives, generalised: `enableSplLogging`/`disableSplLogging` only POST a
/// request (AnalysisThread::applyPendingSplRequest picks it up on the next
/// ~10 ms poll, AnalysisThread.cpp's own kPollMs), so a test that read the
/// flag immediately after calling either would be racing the analysis
/// thread rather than proving anything about it.
bool waitUntil(const std::function<bool()>& predicate, int timeoutMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (predicate()) return true;
        juce::Thread::sleep(5);
    }
    return predicate();
}

/// The opposite shape: true only if `predicate` holds for the WHOLE window,
/// false the instant it does not. `waitUntil` above answers "does this
/// become true eventually" -- checking it right after a call that must NOT
/// flip a flag would pass even against a mutant that flips it a few
/// milliseconds later, since `enableSplLogging`/`disableSplLogging` only
/// POST an async request. This is what actually catches a mutant that
/// starts an unwanted disable/enable mid-switch.
bool staysTrueFor(const std::function<bool()>& predicate, int durationMs) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(durationMs);
    while (juce::Time::getMillisecondCounter() < deadline) {
        if (!predicate()) return false;
        juce::Thread::sleep(5);
    }
    return predicate();
}

/// `AnalysisThread::isSplLoggingEnabled()` mirrors the LOG PIPELINE (the
/// disk writer), not the in-memory metering session -- `enableSplLogging`'s
/// own comment (AnalysisThread.h) is explicit that an empty `logDirectory`
/// starts metering only, with "nothing touches disk", so this test's one
/// observable (isSplLoggingEnabled) needs a real directory, the same
/// TempDir shape test_spl_log_wiring.cpp/test_spl_log_wiring_disable.cpp
/// both already use.
struct TempDir {
    std::filesystem::path path;
    TempDir()
        : path(std::filesystem::temp_directory_path() / "rta-test-mc-panes") {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

/// Finds a direct child of `root` that is a `juce::Button` with exactly this
/// text -- `wirePaneSelectorButtons()` adds all three selector buttons
/// directly to `MainComponent` (no intervening container), and every other
/// button MainComponent owns (SYNTHETIC, LOCATE, CAL START, ...) has a
/// distinct label, so a flat, one-level search is enough. Fix-round finding
/// 1 (verifier): the suite used to call `selectPaneView` directly everywhere,
/// so a swapped `onClick` lambda between two buttons went unnoticed -- this
/// is what lets a test find the REAL button an operator would click.
juce::Button* findButtonByText(juce::Component& root, const juce::String& text) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        if (auto* button = dynamic_cast<juce::Button*>(root.getChildComponent(i))) {
            if (button->getButtonText() == text) return button;
        }
    }
    return nullptr;
}

/// True if `ancestor` is somewhere in `node`'s parent chain. Fix-round
/// finding 3 (verifier): `addAndMakeVisible(*workspace_)` was deletable
/// green, because `paneComponentForTest()` reads through `workspace_`
/// regardless of whether `workspace_` itself was ever attached to
/// `MainComponent` -- walking the chain is what actually proves the
/// attachment.
bool isDescendantOf(const juce::Component& node, const juce::Component& ancestor) {
    for (const juce::Component* p = node.getParentComponent(); p != nullptr; p = p->getParentComponent()) {
        if (p == &ancestor) return true;
    }
    return false;
}

}  // namespace

TEST_CASE("selecting SPL builds an SplView through the real factory path",
         "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);

    component.selectPaneView(PaneSelectorButton::Spl);

    CHECK(component.currentPaneView() == PaneView::Spl);
    CHECK(dynamic_cast<const SplView*>(&component.paneComponentForTest()) != nullptr);
    // Never an RtaView masquerading as the SPL pane -- the exact PR #26 bug
    // makePaneFactory's own test already guards at the factory level; this
    // is the same property one layer up, through the real selector.
    CHECK(dynamic_cast<const RtaView*>(&component.paneComponentForTest()) == nullptr);
}

TEST_CASE("selecting back to RTA restores an RtaView", "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);

    component.selectPaneView(PaneSelectorButton::Spl);
    component.selectPaneView(PaneSelectorButton::Rta);

    CHECK(component.currentPaneView() == PaneView::Rta);
    const auto* rtaView = dynamic_cast<const RtaView*>(&component.paneComponentForTest());
    REQUIRE(rtaView != nullptr);
    // Fix-round finding 3 (verifier): setLibrary(&library_) was deletable
    // green. library() is RtaView's own accessor for what it was last told
    // (RtaView.h) -- pinning it to the SAME library MainComponent owns, not
    // merely non-null, after a switch away and back.
    CHECK(rtaView->library() == &component.libraryForTest());
}

TEST_CASE("selecting Transfer builds a TransferView", "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);

    component.selectPaneView(PaneSelectorButton::Transfer);

    CHECK(component.currentPaneView() == PaneView::Transfer);
    const auto* transferView = dynamic_cast<const TransferView*>(&component.paneComponentForTest());
    REQUIRE(transferView != nullptr);
    CHECK(transferView->library() == &component.libraryForTest());
}

TEST_CASE("switching panes does not stop or restart SPL logging",
         "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);
    const TempDir tempDir;

    const rta::measure::SplConfig config;
    const std::array<int, 1> channels{0};
    component.analysisThreadForTest().enableSplLogging(config, channels, tempDir.path.string());
    REQUIRE(waitUntil([&] { return component.analysisThreadForTest().isSplLoggingEnabled(); }, 2000));

    component.selectPaneView(PaneSelectorButton::Spl);
    // A mutant that adds a disable() call needs a moment for its OWN async
    // request to land -- staysTrueFor holds the assertion open across that
    // settle window instead of reading the flag once, immediately, which
    // would still read "true" a mutant would only flip a few ms later.
    CHECK(staysTrueFor([&] { return component.analysisThreadForTest().isSplLoggingEnabled(); }, 200));

    component.selectPaneView(PaneSelectorButton::Rta);
    CHECK(staysTrueFor([&] { return component.analysisThreadForTest().isSplLoggingEnabled(); }, 200));

    component.analysisThreadForTest().disableSplLogging();
    REQUIRE(waitUntil([&] { return !component.analysisThreadForTest().isSplLoggingEnabled(); }, 2000));
}

// --- Fix round 1 (verifier NOT SOUND, 4 MEDIUM): the tests above all called
// `selectPaneView` directly, so none of them could tell a correctly-wired
// button from a swapped one, an unpinned `resized()`/`setLibrary`/
// `addAndMakeVisible`/toggle-sync call, or a broken idempotent guard -- each
// was independently deletable (or swappable) with the suite staying green.
// The five cases below close those gaps, one property each.

TEST_CASE("clicking each selector button in the component tree shows the matching pane",
         "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);

    // setToggleState(true, sendNotification) -- NOT triggerClick(), which
    // posts through postCommandMessage and is only delivered by a pumped
    // message loop this offscreen test never runs (Button.cpp's own
    // setToggleState calls sendClickMessage() synchronously; triggerClick()
    // does not).
    auto* splButton = findButtonByText(component, "SPL");
    REQUIRE(splButton != nullptr);
    splButton->setToggleState(true, juce::sendNotification);
    CHECK(dynamic_cast<const SplView*>(&component.paneComponentForTest()) != nullptr);

    auto* transferButton = findButtonByText(component, "TRANSFER");
    REQUIRE(transferButton != nullptr);
    transferButton->setToggleState(true, juce::sendNotification);
    CHECK(dynamic_cast<const TransferView*>(&component.paneComponentForTest()) != nullptr);

    auto* rtaButton = findButtonByText(component, "RTA");
    REQUIRE(rtaButton != nullptr);
    rtaButton->setToggleState(true, juce::sendNotification);
    CHECK(dynamic_cast<const RtaView*>(&component.paneComponentForTest()) != nullptr);
}

TEST_CASE("selectPaneView gives the new pane real bounds without waiting for a resize event",
         "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);
    // A real size, not the fixture's default 0x0 -- resized() lays out
    // `getLocalBounds()`, so a 0x0 component would pass even with the
    // trailing `resized()` call deleted from selectPaneView.
    component.setSize(1280, 800);

    component.selectPaneView(PaneSelectorButton::Spl);

    const auto& pane = component.paneComponentForTest();
    CHECK(pane.getWidth() > 0);
    CHECK(pane.getHeight() > 0);
}

TEST_CASE("selectPaneView actually attaches the new workspace into the component tree",
         "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);
    component.setSize(1280, 800);

    component.selectPaneView(PaneSelectorButton::Spl);

    const auto& pane = component.paneComponentForTest();
    CHECK(isDescendantOf(pane, component));

    // The pane's OWN visible flag is set unconditionally by WorkspaceView's
    // constructor (it addAndMakeVisible's every pane it builds), so checking
    // it would not catch a MainComponent that forgets to
    // addAndMakeVisible(*workspace_) itself. That flag lives on workspace_ --
    // the pane's immediate parent -- and a freshly constructed juce::Component
    // defaults to NOT visible (componentFlags(0)), which is what this pins.
    auto* workspace = pane.getParentComponent();
    REQUIRE(workspace != nullptr);
    CHECK(workspace->isVisible());
}

TEST_CASE("selectPaneView keeps the selector buttons' toggle state in sync",
         "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);

    // A PROGRAMMATIC call, not a click -- JUCE's own radio-group exclusion
    // (Button::turnOffOtherButtonsInGroup) only runs when a button's OWN
    // toggle state changes, which never happens on this path. The three
    // setToggleState lines in selectPaneView are the ONLY thing that can
    // keep the buttons honest here.
    component.selectPaneView(PaneSelectorButton::Spl);

    auto* rtaButton = findButtonByText(component, "RTA");
    auto* transferButton = findButtonByText(component, "TRANSFER");
    auto* splButton = findButtonByText(component, "SPL");
    REQUIRE(rtaButton != nullptr);
    REQUIRE(transferButton != nullptr);
    REQUIRE(splButton != nullptr);

    CHECK_FALSE(rtaButton->getToggleState());
    CHECK_FALSE(transferButton->getToggleState());
    CHECK(splButton->getToggleState());
}

TEST_CASE("selecting the pane already showing does not rebuild it", "[main_component_panes]") {
    MainComponent component;
    component.setSyntheticMode(true);

    component.selectPaneView(PaneSelectorButton::Spl);
    const auto* before = &component.paneComponentForTest();

    component.selectPaneView(PaneSelectorButton::Spl);  // already showing SPL
    const auto* after = &component.paneComponentForTest();

    // Not merely "still an SplView" -- the SAME object, proving the early
    // return actually skipped the rebuild rather than building an
    // indistinguishable new one.
    CHECK(before == after);
}
