// SPDX-License-Identifier: AGPL-3.0-or-later
// L7-EQ UI wave B, tasks T7/T8 (docs/plans/2026-09-29-eq-ui-lane-plan.md, D14):
// VERIFY and ADOPT through the REAL MainComponent and the pane's own buttons,
// and the LOCATE / CAL mutual-exclusion guards in both directions. The loop
// itself (dwell, timeouts, ADOPT semantics) is proven with no JUCE in
// app/tests/test_eq_verify_runner*.cpp; this file proves the WIRING no
// lower-level test can see: the device / SYNTHETIC mapping, the LOCATE and CAL
// flags, the 2 Hz poll, and the buttons.
//
// No hardware: the runner is re-attached to a standalone OutputEngine rendered
// by hand, with the real host's environment kept except that a device "is
// running", and a scripted snapshot. LOCATE and CAL are the REAL handlers.
#include <catch2/catch_test_macros.hpp>

#include "EqVerifyRunnerFixture.h"
#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "view/EqPaneView.h"

#include <cstdint>
#include <string>
#include <vector>

using rta::measure::VerifyBlock;
using rta::measure::VerifyState;
using rta::view::EqPaneView;
using rta::view::PaneSelectorButton;

namespace {

juce::Button* findButtonByText(juce::Component& root, const juce::String& text) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        if (auto* button = dynamic_cast<juce::Button*>(root.getChildComponent(i))) {
            if (button->getButtonText() == text) return button;
        }
    }
    return nullptr;
}

/// The readout label whose text starts with `prefix` ("delay:", "calibration:").
juce::String labelStartingWith(juce::Component& root, const juce::String& prefix) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        if (auto* label = dynamic_cast<juce::Label*>(root.getChildComponent(i))) {
            if (label->getText().startsWith(prefix)) return label->getText();
        }
    }
    return {};
}

/// A REAL MainComponent in LIVE mode (no device opens in a test), the EQ pane
/// with the bump measurement and AUTO EQ's filters, and the runner re-attached
/// to a standalone engine. Only the device flag and the snapshot are scripted;
/// the LOCATE / CAL flags in the environment are the component's own.
struct LiveEq {
    MainComponent component;
    rta::platform::OutputEngine engine;
    verifyfixture::SnapshotPtr pre, post;
    double nowMs = 1000.0;
    int freezes = 0;
    EqPaneView* pane = nullptr;
    rta::measure::EqVerifyRunner* runner = nullptr;

    explicit LiveEq(bool autoEq = true) {
        auto& library = MainComponentTestAccess::libraryForTest(component);
        library.add(eqfixture::makeBumpTrace("bump", verifyfixture::kFft, true), "TRANSFER @ 12:00:00", "TRANSFER");
        component.selectPaneView(PaneSelectorButton::Eq);
        pane = const_cast<EqPaneView*>(dynamic_cast<const EqPaneView*>(&MainComponentTestAccess::pane(component)));
        REQUIRE(pane != nullptr);
        pane->measurementComboForTest().setSelectedId(1, juce::sendNotificationSync);
        if (autoEq) pane->autoEqButtonForTest().onClick();
        REQUIRE(pane->modelForTest().hasMeasurement());

        const auto coherence = eqfixture::makeLinearFixture(verifyfixture::kBins).coherence;
        auto& model = pane->modelForTest();
        pre = verifyfixture::makeSnapshot(std::vector<float>(model.measuredDb().begin(), model.measuredDb().end()), coherence);
        std::vector<float> postDb;
        for (const double g : model.session().ghostDb()) postDb.push_back(static_cast<float>(g));
        post = verifyfixture::makeSnapshot(postDb, coherence);

        engine.prepare(eqfixture::kFs, verifyfixture::kChannels);
        runner = pane->verifyRunnerForTest();
        REQUIRE(runner != nullptr);
        auto host = runner->hostForTest();
        const auto realEnvironment = host.environment;
        host.environment = [realEnvironment] {
            auto env = realEnvironment();  // the component's own synthetic and LOCATE/CAL flags
            env.deviceRunning = true;      // ... with a device that "is running"
            return env;
        };
        host.latest = [this]() -> verifyfixture::SnapshotPtr {
            const std::uint64_t turnover = 16ull * static_cast<std::uint64_t>(verifyfixture::kFft);
            return engine.renderedSamples() >= turnover ? post : pre;
        };
        host.freeze = [this](const rta::measure::Snapshot& s) -> std::optional<rta::trace::Trace> {
            ++freezes;
            return eqfixture::makeEqTrace("verify-" + std::to_string(freezes), verifyfixture::kFft, eqfixture::kFs,
                                          s.transfer->magnitudeDb, *s.transfer->coherence,
                                          std::vector<float>(s.transfer->magnitudeDb.size(), 0.0f));
        };
        host.nowMs = [this] { return nowMs; };
        runner->attach(engine, library, std::move(host));
    }

    void render(int blocks) {
        for (int b = 0; b < blocks; ++b) {
            std::vector<std::vector<float>> buffers(verifyfixture::kChannels, std::vector<float>(verifyfixture::kBlock, 0.0f));
            std::vector<float*> ptrs;
            for (auto& channel : buffers) ptrs.push_back(channel.data());
            engine.render(ptrs.data(), verifyfixture::kChannels, verifyfixture::kBlock);
        }
    }
    /// One turn of each 2 Hz / 10 Hz timer the harness does not pump.
    void tick() {
        MainComponentTestAccess::timerTickForTest(component);
        pane->tickForTest();
    }
};

}  // namespace

TEST_CASE("VERIFY on the real pane in SYNTHETIC mode is refused by name, and both buttons are dark",
          "[main_component_eq_verify]") {
    MainComponent component;
    component.setSyntheticMode(true);
    MainComponentTestAccess::libraryForTest(component).add(eqfixture::makeBumpTrace("bump"), "TRANSFER @ 12:00:00", "TRANSFER");
    component.selectPaneView(PaneSelectorButton::Eq);
    auto* pane = const_cast<EqPaneView*>(dynamic_cast<const EqPaneView*>(&MainComponentTestAccess::pane(component)));
    REQUIRE(pane != nullptr);
    pane->measurementComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane->autoEqButtonForTest().onClick();
    REQUIRE(pane->modelForTest().unappliedCount() > 0);

    auto* runner = pane->verifyRunnerForTest();
    REQUIRE(runner != nullptr);
    CHECK(runner->blocker().kind == VerifyBlock::Synthetic);
    CHECK_FALSE(pane->verifyButtonForTest().isEnabled());
    CHECK_FALSE(pane->adoptButtonForTest().isEnabled());
    // The reason is on screen, not only in a tooltip.
    CHECK(pane->readoutForTest().contains("VERIFY off:"));
    CHECK(pane->readoutForTest().contains("SYNTHETIC"));
    CHECK_FALSE(runner->press());
    CHECK(runner->lastRefusalForTest() == VerifyBlock::Synthetic);
}

TEST_CASE("VERIFY in LIVE mode with no device open is refused as a stopped device, not as SYNTHETIC",
          "[main_component_eq_verify]") {
    MainComponent component;  // LIVE, and no test opens a device
    MainComponentTestAccess::libraryForTest(component).add(eqfixture::makeBumpTrace("bump"), "TRANSFER @ 12:00:00", "TRANSFER");
    component.selectPaneView(PaneSelectorButton::Eq);
    auto* pane = const_cast<EqPaneView*>(dynamic_cast<const EqPaneView*>(&MainComponentTestAccess::pane(component)));
    REQUIRE(pane != nullptr);
    pane->measurementComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane->autoEqButtonForTest().onClick();
    CHECK(pane->verifyRunnerForTest()->blocker().kind == VerifyBlock::DeviceNotRunning);
    CHECK_FALSE(pane->verifyButtonForTest().isEnabled());
}

TEST_CASE("the VERIFY and ADOPT buttons drive a full run through the 2 Hz poll", "[main_component_eq_verify]") {
    LiveEq rig;
    auto& model = rig.pane->modelForTest();
    const std::size_t predicted = model.unappliedCount();
    REQUIRE(predicted >= 1);

    rig.tick();
    REQUIRE(rig.pane->verifyButtonForTest().isEnabled());
    CHECK_FALSE(rig.pane->adoptButtonForTest().isEnabled());
    rig.pane->verifyButtonForTest().onClick();
    CHECK(rig.runner->stateForTest() == VerifyState::Waiting);
    CHECK(rig.engine.role(0) == rta::platform::OutputRole::Routed);  // D14: output 0
    CHECK_FALSE(rig.pane->verifyButtonForTest().isEnabled());        // running: not pressable again
    CHECK(rig.pane->readoutForTest().contains("VERIFY: pink noise"));

    // timerCallback -> eq_.pollVerify(): the ONLY thing that advances a run.
    rig.render(1);
    rig.tick();
    CHECK(rig.runner->stateForTest() == VerifyState::Measuring);
    rig.render(130);  // past 512 + depth * fftSize rendered samples
    rig.tick();
    CHECK(rig.runner->stateForTest() == VerifyState::Settling);
    CHECK_FALSE(rig.pane->adoptButtonForTest().isEnabled());
    rig.render(8);
    rig.tick();
    REQUIRE(rig.runner->stateForTest() == VerifyState::Done);
    CHECK(rig.pane->readoutForTest().contains("VERIFY: "));
    CHECK(rig.pane->readoutForTest().contains("0 flagged"));
    REQUIRE(rig.pane->adoptButtonForTest().isEnabled());  // ADOPT lights at Done

    rig.pane->adoptButtonForTest().onClick();
    CHECK(model.unappliedCount() == 0);
    CHECK(model.measurement().id == rig.runner->afterTraceIdForTest());
    CHECK_FALSE(rig.pane->adoptButtonForTest().isEnabled());
    // The after-measurement is in the library, in group EQ.
    const auto* entry = MainComponentTestAccess::libraryForTest(rig.component).entry(model.measurement().id);
    REQUIRE(entry != nullptr);
    CHECK(entry->group == "EQ");
}

TEST_CASE("LOCATE refuses while a VERIFY runs, and leaves VERIFY's excitation alone", "[main_component_eq_verify]") {
    LiveEq rig;
    REQUIRE(rig.runner->press());
    REQUIRE(rig.runner->busy());

    auto* locate = findButtonByText(rig.component, "LOCATE");
    REQUIRE(locate != nullptr);
    locate->onClick();
    const auto readout = labelStartingWith(rig.component, "delay:");
    CHECK(readout.contains("VERIFY"));
    CHECK_FALSE(readout.contains("locating"));
    // VERIFY is undisturbed.
    CHECK(rig.runner->stateForTest() == VerifyState::Waiting);
    CHECK(rig.runner->busy());
}

TEST_CASE("CAL START and CAL END refuse while a VERIFY runs", "[main_component_eq_verify]") {
    LiveEq rig;
    REQUIRE(rig.runner->press());

    auto* start = findButtonByText(rig.component, "CAL START");
    auto* end = findButtonByText(rig.component, "CAL END");
    REQUIRE(start != nullptr);
    REQUIRE(end != nullptr);

    start->onClick();
    auto readout = labelStartingWith(rig.component, "calibration:");
    CHECK(readout.contains("refused"));
    CHECK(readout.contains("VERIFY"));
    CHECK_FALSE(readout.contains("measuring start check"));

    end->onClick();
    readout = labelStartingWith(rig.component, "calibration:");
    CHECK(readout.contains("refused"));
    CHECK(readout.contains("VERIFY"));
    CHECK_FALSE(readout.contains("run CAL START first"));  // the guard comes before that message
    CHECK(rig.runner->busy());
}

TEST_CASE("VERIFY refuses while LOCATE runs (the component's own flag reaches the runner)",
          "[main_component_eq_verify]") {
    LiveEq rig;
    findButtonByText(rig.component, "LOCATE")->onClick();
    REQUIRE(labelStartingWith(rig.component, "delay:").contains("locating"));

    CHECK(rig.runner->blocker().kind == VerifyBlock::CaptureBusy);
    CHECK_FALSE(rig.runner->press());
    CHECK(rig.runner->lastRefusalForTest() == VerifyBlock::CaptureBusy);
    CHECK_FALSE(rig.runner->busy());
    CHECK(rig.engine.role(0) == rta::platform::OutputRole::None);  // VERIFY's engine untouched
    rig.tick();
    CHECK_FALSE(rig.pane->verifyButtonForTest().isEnabled());
    CHECK(rig.pane->readoutForTest().contains("LOCATE or CAL"));
}

TEST_CASE("VERIFY refuses while CAL owns the shared capture", "[main_component_eq_verify]") {
    LiveEq rig;
    findButtonByText(rig.component, "CAL START")->onClick();
    REQUIRE(labelStartingWith(rig.component, "calibration:").contains("measuring start check"));

    CHECK(rig.runner->blocker().kind == VerifyBlock::CaptureBusy);
    CHECK_FALSE(rig.runner->press());
    CHECK(rig.runner->lastRefusalForTest() == VerifyBlock::CaptureBusy);
    CHECK_FALSE(rig.runner->busy());
}
