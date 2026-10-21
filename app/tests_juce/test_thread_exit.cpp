// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Exit path of the two juce::Threads the app owns. Both destructors used to call
// stopThread(2000), which TerminateThread()s a thread still running after 2 s --
// a thread killed that way can die holding the CRT heap or loader lock, and the
// process then hangs at exit. They now call stopThreadNoKill (ThreadStop.h): a
// stop request, then a join that never kills. These cases prove
//   1. the helper really does not kill a thread that overruns its warning
//      threshold (a kill would leave `finished` false), and
//   2. destroying a live AnalysisThread + SyntheticInput mid-rebuild, with every
//      core busy, finishes inside a bound derived from one slot build.
//
// A loop that ignored its stop flag would make (2) hang the executable, so the
// destruction runs on a helper thread under a watchdog that ends the PROCESS
// with a named failure line instead of waiting forever.
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalysisThread.h"
#include "measure/SyntheticInput.h"
#include "measure/ThreadStop.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalysisThread;
using rta::measure::SyntheticInput;
using rta::measure::stopThreadNoKill;
using rta::platform::CaptureBus;
using rta::platform::ChannelRole;

namespace {

// Ignores threadShouldExit() on purpose, then finishes its work and sets a flag.
class StubbornThread final : public juce::Thread {
public:
    explicit StubbornThread(int ignoreStopForMs) : juce::Thread("rta stubborn test thread"), ignoreMs_(ignoreStopForMs) {}
    void run() override {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(ignoreMs_);
        while (juce::Time::getMillisecondCounter() < end) juce::Thread::sleep(5);
        finished.store(true);
    }
    std::atomic<bool> finished{false};

private:
    int ignoreMs_;
};

// Ends the process, naming the line, if `done` is not set within `limitMs`.
// A hung destructor cannot be reported by Catch2 (the test thread is the one
// that would report it), so the failure has to be raised from here.
void watchdog(const std::atomic<bool>& done, int limitMs, int line) {
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<std::uint32_t>(limitMs);
    while (!done.load() && juce::Time::getMillisecondCounter() < deadline) juce::Thread::sleep(5);
    if (!done.load()) {
        std::fprintf(stderr, "THREAD EXIT HANG: destruction did not finish in %d ms (%s:%d)\n", limitMs,
                     __FILE__, line);
        std::fflush(stderr);
        std::_Exit(3);
    }
}

}  // namespace

TEST_CASE("stopThreadNoKill joins a thread that overruns the warning threshold instead of killing it",
          "[thread_exit]") {
    StubbornThread stubborn(300);
    stubborn.startThread();

    // Warns after 100 ms; the thread needs 300. stopThread(100) would terminate it.
    const auto elapsed = stopThreadNoKill(stubborn, 100);

    CHECK(stubborn.finished.load());  // it ran to completion: not killed
    CHECK_FALSE(stubborn.isThreadRunning());
    CHECK(elapsed >= 250u);  // and the join really waited for it
}

TEST_CASE("destroying a live analysis chain mid-rebuild, under full CPU load, finishes within the bound",
          "[thread_exit]") {
    CaptureBus bus(rta::platform::kFixedRingCapacitySamples);
    REQUIRE(bus.config().setRole(0, ChannelRole::Reference));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));

    Analyser::Config config;  // MTW defaults: the longest single iteration the thread has
    auto thread = std::make_unique<AnalysisThread>(bus, config);
    SyntheticInput::Config synthetic;
    synthetic.sampleRate = 192000.0;
    synthetic.bufferSize = 512;
    auto input = std::make_unique<SyntheticInput>(bus, synthetic);

    const auto start = juce::Time::getMillisecondCounter();
    while (thread->routeHopCount(0) < 40 && juce::Time::getMillisecondCounter() - start < 20000u) {
        juce::Thread::sleep(5);
    }
    REQUIRE(thread->routeHopCount(0) >= 40);

    // The bound. The longest a loop iteration can take is one slot build (the
    // drain checks threadShouldExit per hop, SyntheticInput works one block per
    // iteration, both waits are woken by the stop's notify), measured here on an
    // idle machine. Under load the join can also wait for the scheduler: ten
    // slot times plus 250 ms covers a 3-4x slowdown, and is still far below the
    // 2000 ms at which the old stopThread(2000) began killing.
    Analyser::Config at192 = config;
    at192.sampleRate = 192000.0;
    const auto t0 = std::chrono::steady_clock::now();
    { const auto probe = std::make_unique<Analyser>(at192); }
    const double slotMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double boundMs = 10.0 * slotMs + 250.0;

    std::atomic<bool> stopLoad{false};
    std::vector<std::thread> load;
    const unsigned workers = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < workers; ++i) {
        load.emplace_back([&stopLoad] {
            volatile double x = 0.5;
            while (!stopLoad.load(std::memory_order_relaxed)) x = std::sin(x + 1.0);
        });
    }

    thread->applyReferenceDelay(1);  // a sweep is about to be in flight...
    juce::Thread::sleep(12);         // ...or just starting a slot: the worst moment to stop

    std::atomic<bool> done{false};
    double elapsedMs = 0.0;
    std::thread destroyer([&] {
        const auto d0 = std::chrono::steady_clock::now();
        input.reset();   // reverse of MainComponent's member order: producer first
        thread.reset();
        elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - d0).count();
        done.store(true);
    });
    watchdog(done, 30000, __LINE__);
    destroyer.join();
    stopLoad.store(true);
    for (auto& t : load) t.join();

    INFO("slot build " << slotMs << " ms, destruction " << elapsedMs << " ms, bound " << boundMs << " ms");
    CHECK(elapsedMs < boundMs);
}
