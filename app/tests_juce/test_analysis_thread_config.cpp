// SPDX-License-Identifier: AGPL-3.0-or-later
//
// station-3 STORE plan task T1 (docs/plans/2026-09-27-store-lane-plan.md):
// `AnalysisThread::config()` must return the EXACT `Analyser::Config` this
// thread was constructed with, not `Config{}`'s compile-time defaults --
// the whole reason the getter exists (research A3.3: a v1 STORE reading
// `Analyser::Config{}` directly would silently go stale the day a runtime
// control changes one of these fields). Constructs a real CaptureBus +
// AnalysisThread, same shape as test_routing_live.cpp beside this file.

#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::platform::CaptureBus;

namespace {

// Every field below is chosen to disagree with Config{}'s own default
// (Analyser.h), so a getter that returns Config{} instead of baseConfig_
// cannot pass by accident.
Analyser::Config nonDefaultConfig() {
    Analyser::Config config;
    config.fftSize = 512;
    config.hopSize = 128;
    config.window = rta::dsp::WindowType::BlackmanHarris;
    config.averaging = rta::dsp::Averaging::Linear;
    config.timeConstantSeconds = 1.25;
    return config;
}

}  // namespace

TEST_CASE("AnalysisThread::config() returns the constructed Config, not Config{}'s defaults",
         "[analysis_thread_config]") {
    CaptureBus bus(4096);
    const auto config = nonDefaultConfig();
    AnalysisThread thread(bus, config);

    const auto& read = thread.config();
    CHECK(read.fftSize == config.fftSize);
    CHECK(read.hopSize == config.hopSize);
    CHECK(read.window == config.window);
    CHECK(read.averaging == config.averaging);
    CHECK(read.timeConstantSeconds == config.timeConstantSeconds);

    // The mutant this row must catch (`return Analyser::Config{};`): every
    // field above would then read as Config{}'s own default instead, and at
    // least fftSize/window/averaging disagree with nonDefaultConfig() by
    // construction.
    const Analyser::Config defaults{};
    CHECK(read.fftSize != defaults.fftSize);
    CHECK(read.window != defaults.window);
    CHECK(read.averaging != defaults.averaging);
}
