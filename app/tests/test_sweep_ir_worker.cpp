// SPDX-License-Identifier: AGPL-3.0-or-later
//
// SWEEP lane task T4 (docs/plans/2026-09-29-sweep-ir-lane-plan.md). JUCE-free.
// The worker is tested with an injected `analyse` that blocks on a latch, so
// the claim "start() returns before the job finishes" is observed directly
// rather than inferred from timing.
#include <catch2/catch_test_macros.hpp>

#include "measure/SweepIrWorker.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using rta::measure::SweepCapture;
using rta::measure::SweepIrOutcome;
using rta::measure::SweepIrRequest;
using rta::measure::SweepIrStatus;
using rta::measure::SweepIrWorker;

namespace {

// A one-shot gate. waitFor() has a deadline, so a worker that wrongly runs the
// job on the caller's thread makes the TEST fail at the deadline instead of
// hanging the suite: the mutant "start() is synchronous" must be RED, not a
// deadlock.
class Latch {
public:
    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            open_ = true;
        }
        cv_.notify_all();
    }
    [[nodiscard]] bool waitFor(std::chrono::milliseconds limit) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, limit, [this] { return open_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool open_ = false;
};

// Polls for the published result for at most 2 s (the plan's deadline).
std::shared_ptr<const SweepIrOutcome> awaitResult(const SweepIrWorker& worker) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (worker.latest() == nullptr && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return worker.latest();
}

}  // namespace

TEST_CASE("start() returns while the job is blocked; the result arrives after release",
          "[sweep_ir_worker]") {
    Latch gate;
    std::atomic<int> entered{0};
    std::atomic<bool> timedOut{false};
    SweepIrWorker worker([&](const SweepCapture& capture, const SweepIrRequest&) {
        entered.fetch_add(1);
        // 10 s is far longer than the 2 s result deadline, and short enough that
        // a broken worker still ends the suite.
        if (!gate.waitFor(10s)) timedOut = true;
        SweepIrOutcome outcome;
        outcome.status = SweepIrStatus::Ok;
        outcome.delaySamples = static_cast<long>(capture.reference.size());
        return outcome;
    });

    SweepCapture capture;
    capture.reference.assign(7, 0.0f);
    capture.measurement.assign(7, 0.0f);
    worker.start(capture, SweepIrRequest{});

    // Had start() run the job inline, the call above would already have waited
    // out the latch and set timedOut.
    CHECK_FALSE(timedOut.load());
    CHECK(worker.latest() == nullptr);

    gate.release();
    const auto result = awaitResult(worker);
    REQUIRE(result != nullptr);
    CHECK(result->status == SweepIrStatus::Ok);
    CHECK(result->delaySamples == 7);  // the job saw the capture it was given
    CHECK(entered.load() == 1);
    CHECK_FALSE(timedOut.load());
}

TEST_CASE("the destructor joins a job that is still running", "[sweep_ir_worker]") {
    Latch gate;
    std::atomic<bool> finished{false};
    std::thread releaser;
    {
        SweepIrWorker worker([&](const SweepCapture&, const SweepIrRequest&) {
            (void)gate.waitFor(10s);
            finished = true;
            return SweepIrOutcome{};
        });
        worker.start(SweepCapture{}, SweepIrRequest{});
        CHECK_FALSE(finished.load());
        releaser = std::thread([&gate] {
            std::this_thread::sleep_for(50ms);
            gate.release();
        });
    }  // ~SweepIrWorker blocks here until the releaser opens the gate
    CHECK(finished.load());
    releaser.join();
}

TEST_CASE("an analyse that throws becomes a Failed outcome, not terminate", "[sweep_ir_worker]") {
    SweepIrWorker worker([](const SweepCapture&, const SweepIrRequest&) -> SweepIrOutcome {
        throw std::runtime_error("boom");
    });
    worker.start(SweepCapture{}, SweepIrRequest{});
    const auto result = awaitResult(worker);
    REQUIRE(result != nullptr);
    CHECK(result->status == SweepIrStatus::Failed);
    CHECK(result->message == "boom");
}

TEST_CASE("a second start() clears the previous result until its own job finishes",
          "[sweep_ir_worker]") {
    Latch gate;
    std::atomic<int> calls{0};
    SweepIrWorker worker([&](const SweepCapture&, const SweepIrRequest&) {
        if (calls.fetch_add(1) == 1) (void)gate.waitFor(10s);  // only the 2nd job blocks
        SweepIrOutcome outcome;
        outcome.status = SweepIrStatus::Ok;
        return outcome;
    });
    worker.start(SweepCapture{}, SweepIrRequest{});
    REQUIRE(awaitResult(worker) != nullptr);

    worker.start(SweepCapture{}, SweepIrRequest{});
    // The first job's result must not be mistaken for the second's.
    CHECK(worker.latest() == nullptr);
    gate.release();
    CHECK(awaitResult(worker) != nullptr);
}
