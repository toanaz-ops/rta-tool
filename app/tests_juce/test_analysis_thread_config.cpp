// SPDX-License-Identifier: AGPL-3.0-or-later
//
// station-3 STORE plan task T1 (docs/plans/2026-09-27-store-lane-plan.md),
// corrected fix round 1 (MEDIUM F2): `AnalysisThread::captureConfig()` must
// return the four fields (`window`, `averaging`, `transferAveraging`,
// `transferFifoDepth`) this thread was constructed with, not
// `CaptureConfig{}`'s compile-time defaults -- the whole reason the getter
// exists (research A3.3: a v1 STORE reading `Analyser::Config{}` directly
// would silently go stale the day a runtime control changes one of these
// fields). Constructs a real CaptureBus + AnalysisThread, same shape as
// test_routing_live.cpp beside this file.
//
// Fix round 1's own structural check -- a reference return type would fail
// to COMPILE, before this test binary even runs -- lives as a static_assert
// right after the class in AnalysisThread.h; this file only proves the
// VALUES are the constructed ones, not Config{}'s/CaptureConfig{}'s.

#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::measure::CaptureConfig;
using rta::platform::CaptureBus;

namespace {

// Every field below is chosen to disagree with CaptureConfig{}'s own default
// (Analyser.h: window=Hann, averaging=Exponential, transferAveraging=Fifo,
// transferFifoDepth=16), so a getter that returns CaptureConfig{} instead of
// a copy of baseConfig_'s own fields cannot pass by accident.
Analyser::Config nonDefaultConfig() {
    Analyser::Config config;
    config.window = rta::dsp::WindowType::BlackmanHarris;
    config.averaging = rta::dsp::Averaging::Linear;
    config.transferAveraging = rta::dsp::TransferAveraging::Exponential;
    config.transferFifoDepth = 32;
    return config;
}

}  // namespace

TEST_CASE("AnalysisThread::captureConfig() returns the constructed values, not CaptureConfig{}'s defaults",
         "[analysis_thread_config]") {
    CaptureBus bus(4096);
    const auto config = nonDefaultConfig();
    AnalysisThread thread(bus, config);

    const CaptureConfig read = thread.captureConfig();
    CHECK(read.window == config.window);
    CHECK(read.averaging == config.averaging);
    CHECK(read.transferAveraging == config.transferAveraging);
    CHECK(read.transferFifoDepth == config.transferFifoDepth);

    // The mutant this row must catch (returning `CaptureConfig{}` instead of
    // a copy of baseConfig_'s own fields): every field above would then read
    // as the default instead, and every one disagrees with
    // nonDefaultConfig() by construction.
    const CaptureConfig defaults{};
    CHECK(read.window != defaults.window);
    CHECK(read.averaging != defaults.averaging);
    CHECK(read.transferAveraging != defaults.transferAveraging);
    CHECK(read.transferFifoDepth != defaults.transferFifoDepth);
}
