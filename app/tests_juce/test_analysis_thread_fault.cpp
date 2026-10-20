// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Audit prj010-fable-audit-20261021 (medium): an exception on the analysis
// thread ends measurement for good and `AnalysisThread::lastFault()` had no
// production reader. This proves the producing half against a real thread --
// run()'s catch records the fault and bumps its sequence -- through the
// throwOnNextTickForTest seam. The reading half (DevicePanel's fault slot on a
// real MainComponent) is test_main_component_fault_slot.cpp.
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"

#include <juce_core/juce_core.h>

#include <string>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::platform::CaptureBus;
using rta::platform::Fault;

TEST_CASE("an exception on the analysis thread is recorded as a fault", "[analysis_thread_fault]") {
    CaptureBus bus(4096);
    bus.prepare(48000.0, 1);
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 16;
    config.sampleRate = 48000.0;
    config.mtwEnabled = false;
    AnalysisThread thread(bus, config);

    CHECK(thread.lastFault().sequence == 0);
    CHECK(thread.lastFault().kind == Fault::Kind::None);

    thread.throwOnNextTickForTest();

    const auto deadline = juce::Time::getMillisecondCounter() + 2000u;
    while (thread.lastFault().sequence == 0 && juce::Time::getMillisecondCounter() < deadline) {
        juce::Thread::sleep(5);
    }

    const Fault fault = thread.lastFault();
    CHECK(fault.sequence == 1);
    CHECK(fault.kind == Fault::Kind::DeviceError);
    CHECK(fault.message == "injected analysis fault");
}
