// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared fixture for test_ir_pane_view.cpp and test_ir_pane_view_h10.cpp (the 400-line-cap
// split of one file; no helper changed): the closed-form echo IRs, the Rig that builds the
// IR pane over a real MainComponent, and the mouse/decomposition helpers.
#pragma once

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "view/IrPaneView.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace irpane {

using Access = MainComponentTestAccess;
using Catch::Approx;
inline constexpr const char* kTag = "[ir_pane_view]";
inline constexpr double kFs = 48000.0;
inline constexpr std::size_t kOrigin = 1000;
inline constexpr long kArrival = 240;  // 5 ms after t = 0

inline rta::view::IrPaneView* irPaneOf(MainComponent& c) {
    return dynamic_cast<rta::view::IrPaneView*>(const_cast<juce::Component*>(&Access::pane(c)));
}

inline rta::trace::Trace tinyTrace(const std::string& id) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = kFs;
    meta.fftSize = 64;
    meta.deviceName = "Interface X";
    meta.channelRoles = "ch0=Measurement";
    auto trace = rta::trace::Trace::make(meta, std::vector<float>(rta::trace::pointCountFor(64), -6.0f));
    REQUIRE(trace.has_value());
    return std::move(*trace);
}

// A direct delta at the arrival and an echo of 0.5 at +12.5 ms (past the default gate).
inline std::shared_ptr<const rta::measure::SweepIr> echoIr() {
    auto ir = std::make_shared<rta::measure::SweepIr>();
    ir->samples.assign(6000, 0.0f);
    ir->originIndex = kOrigin;
    ir->arrivalSamples = kArrival;
    ir->sampleRate = kFs;
    ir->samples[kOrigin + static_cast<std::size_t>(kArrival)] = 1.0f;
    ir->samples[kOrigin + static_cast<std::size_t>(kArrival) + 600] = 0.5f;
    return ir;
}

// A direct delta and an echo of 2.0 at +3 ms (inside the default gate), from a sweep that
// states its trusted band: H = 1 + 2 e^{-jwd}, whose zero is outside the unit circle, so
// the gated response has a real excess phase and tau0 = d/fs = 3.00 ms (the closed form
// asserted in app/tests/test_ir_gate_model.cpp).
inline std::shared_ptr<const rta::measure::SweepIr> strongEchoIr() {
    auto ir = std::make_shared<rta::measure::SweepIr>(*echoIr());
    ir->samples.assign(6000, 0.0f);
    ir->samples[kOrigin + static_cast<std::size_t>(kArrival)] = 1.0f;
    ir->samples[kOrigin + static_cast<std::size_t>(kArrival) + 144] = 2.0f;
    ir->trustedLowHz = 50.0;
    ir->trustedHighHz = 20000.0;
    return ir;
}

inline std::size_t countGroup(MainComponent& c, const std::string& group) {
    std::size_t n = 0;
    for (const auto& e : Access::libraryForTest(c).entries()) n += e.group == group ? 1 : 0;
    return n;
}

inline void mouseAt(juce::Component& target, juce::Point<int> where, bool down) {
    const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), where.toFloat(),
                                 juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &target, &target,
                                 juce::Time::getCurrentTime(), where.toFloat(), juce::Time::getCurrentTime(), 1,
                                 false);
    if (down) target.mouseDown(event);
    else target.mouseDrag(event);
}

// Builds the pane over a library with one SWEEP trace and its IR, picked.
struct Rig {
    MainComponent c;
    rta::view::IrPaneView* pane = nullptr;
    std::string traceId;

    explicit Rig(std::shared_ptr<const rta::measure::SweepIr> ir = echoIr()) {
        auto& library = Access::libraryForTest(c);
        traceId = library.add(tinyTrace("swept"), "SWEEP out1 @ 10:00:00", "SWEEP");
        Access::sweepForTest(c).addIrForTest(traceId, std::move(ir));
        c.selectPaneView(rta::view::PaneSelectorButton::Ir);
        pane = irPaneOf(c);
        REQUIRE(pane != nullptr);
        pane->setBounds(0, 0, 900, 600);
        static_cast<juce::Component&>(*pane).resized();
        pane->tickForTest();
    }
};

// One decomposition is about half a second on the worker: pump the pane's own timer until it answers.
inline bool waitForExcess(Rig& rig) {
    for (int i = 0; i < 1000; ++i) {
        rig.pane->tickForTest();
        if (Access::irForTest(rig.c).excessForTest() != nullptr) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

// tau0 is the decomposition's own number, printed to 0.01 ms after the gate's readout, and it
// lands within one sample (0.021 ms) of the echo's 3.00 ms: ExcessPhase.h's step 6 measured
// 0.28 sample on a comparable two-path fixture, and the trusted band here (104..20000 Hz of
// 0..24000) leaves a part-period at each end of the mean, so one sample is the budget.
inline void expectTau0Shown(Rig& rig) {
    const auto excess = Access::irForTest(rig.c).excessForTest();
    REQUIRE(excess != nullptr);
    REQUIRE(excess->valid);
    CHECK(excess->broadbandDelayMs == Approx(3.0).margin(1.0 / kFs * 1000.0));
    CHECK(rig.pane->readoutForTest().endsWith(juce::String(juce::CharPointer_UTF8(excess->tau0Text().c_str()))));
}

inline void paintPane(Rig& rig) { rig.pane->createComponentSnapshot(rig.pane->getLocalBounds(), true); }

}  // namespace irpane
