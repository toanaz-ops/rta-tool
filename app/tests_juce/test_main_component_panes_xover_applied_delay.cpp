// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Z2 (docs/HUMAN-QA-QUEUE.md; decision record docs/dsp/2026-11-09-xover-applied-delay.md),
// pane level: two TraceLibrary entries captured under DIFFERENT applied delays
// go in through the real pickers, and the surface's predicted sum must come out
// coherent. The unit-level proof is app/tests/test_virtual_trace_applied_delay.cpp;
// what only this file can catch is CrossoverPaneView::refreshFromLibrary
// calling the plain factory instead of the restoring one.
#include <catch2/catch_test_macros.hpp>

#include "MainComponent.h"
#include "MainComponentTestAccess.h"
#include "trace/Trace.h"
#include "view/CrossoverPaneView.h"

#include <juce_core/juce_core.h>

#include <cmath>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using rta::view::CrossoverPaneView;
using rta::view::PaneSelectorButton;

namespace {

constexpr int kFftSize = 64;          // 33 bins, 750 Hz apart at 48 kHz
constexpr double kSampleRate = 48000.0;

/// Flat 0 dB, physically zero-phase, captured with `appliedSamples` applied:
/// stored phase = wrap(+2 pi f D/fs) (core/tests/test_dualfft_applied_delay_sign.cpp).
rta::trace::Trace flatTraceCapturedWith(const std::string& id, int appliedSamples) {
    rta::trace::CaptureMeta meta;
    meta.id = id;
    meta.sampleRate = kSampleRate;
    meta.fftSize = kFftSize;
    meta.appliedDelaySamples = appliedSamples;

    const std::size_t points = rta::trace::pointCountFor(kFftSize);
    std::vector<float> phase(points);
    for (std::size_t k = 0; k < points; ++k) {
        const double f = static_cast<double>(k) * kSampleRate / kFftSize;
        const double theta = 2.0 * std::numbers::pi * f * appliedSamples / kSampleRate;
        phase[k] = static_cast<float>(std::arg(std::polar(1.0, theta)));
    }
    auto trace = rta::trace::Trace::make(meta, std::vector<float>(points, 0.0f));
    REQUIRE(trace.has_value());
    REQUIRE(trace->setPhase(std::move(phase)));
    return std::move(*trace);
}

void choose(juce::ComboBox& combo, const std::vector<std::string>& ids, const std::string& id) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == id) {
            combo.setSelectedId(static_cast<int>(i) + 1, juce::sendNotificationSync);
            return;
        }
    }
    FAIL("traceId not found among eligible ids: " << id);
}

}  // namespace

TEST_CASE("XOVER restores each trace's own applied delay before summing",
          "[main_component_panes_xover][applied_delay]") {
    MainComponent component;
    component.setSyntheticMode(true);
    auto& library = MainComponentTestAccess::libraryForTest(component);
    // D = 16 samples: the first null of an UNRECONCILED sum is fs/(2*16) =
    // 1500 Hz, which is exactly bin 2 of this 750 Hz grid.
    const std::string idA = library.add(flatTraceCapturedWith("a", 0), "A", "default");
    const std::string idB = library.add(flatTraceCapturedWith("b", 16), "B", "default");

    component.selectPaneView(PaneSelectorButton::Xover);
    auto& pane = const_cast<CrossoverPaneView&>(
        dynamic_cast<const CrossoverPaneView&>(MainComponentTestAccess::pane(component)));
    choose(pane.highTraceComboForTest(), pane.eligibleTraceIdsForTest(), idA);
    choose(pane.lowTraceComboForTest(), pane.eligibleTraceIdsForTest(), idB);
    pane.topologyComboForTest().setSelectedId(1, juce::sendNotificationSync);
    pane.inversionComboForTest().setSelectedId(1, juce::sendNotificationSync);
    REQUIRE(pane.hasCompleteSelectionForTest());

    const auto& predicted = pane.surfaceForTest().predictedSumDb();
    REQUIRE(predicted.size() == rta::trace::pointCountFor(kFftSize));
    for (std::size_t k = 0; k < predicted.size(); ++k) {
        // |1 + 1| = 6.0206 dB; float phase storage moves it by < 1e-6 dB.
        CHECK(std::abs(predicted[k] - 6.0206f) < 1e-4f);
    }
    // The ghost is taken from the same sources at setSources time.
    CHECK(pane.surfaceForTest().ghostSumDb()[2] > 6.0f);
}
