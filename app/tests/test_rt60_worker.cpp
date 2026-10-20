// SPDX-License-Identifier: AGPL-3.0-or-later
//
// RT60-UI lane wave B, decision 11: the worker thread the third-octave and
// ensemble analyses run on. JUCE-free. No assertion here is a wall-clock budget:
// waits are bounded only so a hung worker fails the test instead of stalling the
// suite, and the bound is far past any load this machine produces.
#include <catch2/catch_test_macros.hpp>

#include "Rt60Fixture.h"
#include "measure/Rt60Worker.h"

#include <chrono>
#include <memory>
#include <thread>

using namespace rta::measure;
namespace fx = rt60_fixture;

namespace {

constexpr const char* kTag = "[rt60_worker]";

std::shared_ptr<const SweepIr> room(std::uint32_t seed) {
    fx::IrSpec spec;
    spec.components = fx::flatRoom(0.80, seed);
    spec.floorSeed = seed + 5;
    return std::make_shared<const SweepIr>(fx::buildIr(spec));
}

// Polls until an outcome arrives; empty if none does inside ten minutes.
std::optional<Rt60Worker::Outcome> waitFor(Rt60Worker& worker) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto out = worker.poll()) return out;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("worker: a single request returns the table, an ensemble request returns both", kTag) {
    Rt60Worker worker;
    Rt60Worker::Request single;
    single.ticket = 1;
    single.set = Rt60BandSet::Third;
    single.primary = room(1000);
    worker.submit(single);
    const auto first = waitFor(worker);
    REQUIRE(first.has_value());
    CHECK(first->ticket == 1);
    REQUIRE(first->single != nullptr);
    CHECK(first->single->bands.size() == 24);  // the set reached the analysis
    CHECK(first->ensemble == nullptr);
    CHECK_FALSE(worker.poll().has_value());  // taken once

    Rt60Worker::Request both;
    both.ticket = 2;
    both.primary = room(1000);
    both.ensemble = {room(1000), room(2000), room(3000)};
    worker.submit(both);
    const auto second = waitFor(worker);
    REQUIRE(second.has_value());
    REQUIRE(second->single != nullptr);
    REQUIRE(second->ensemble != nullptr);
    CHECK(second->ensemble->captureCount == 3);
    CHECK(second->single->bands.size() == 8);
}

TEST_CASE("worker: the newest request wins and a superseded job never publishes", kTag) {
    Rt60Worker worker;
    Rt60Worker::Request heavy;
    heavy.ticket = 1;
    heavy.set = Rt60BandSet::Third;
    heavy.ensemble = {room(1000), room(2000), room(3000), room(4000)};
    worker.submit(heavy);
    Rt60Worker::Request light;
    light.ticket = 2;
    light.primary = room(5000);
    worker.submit(light);
    const auto out = waitFor(worker);
    REQUIRE(out.has_value());
    CHECK(out->ticket == 2);
    REQUIRE(out->single != nullptr);
    CHECK(out->single->bands.size() == 8);
    // Nothing from ticket 1 arrives later: wait until the thread is idle, then look.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(worker.poll().has_value());
}

TEST_CASE("worker: destroying it mid-job joins (a cancelled job is dropped, the process does not hang)", kTag) {
    for (int i = 0; i < 3; ++i) {
        Rt60Worker worker;
        Rt60Worker::Request heavy;
        heavy.ticket = 1;
        heavy.set = Rt60BandSet::Third;
        heavy.ensemble = {room(1000), room(2000), room(3000), room(4000), room(5000), room(6000)};
        worker.submit(heavy);
        // The destructor must cancel at the next band boundary and join. Reaching
        // the end of this scope is the assertion.
    }
    SUCCEED("three workers destroyed with a job in flight");
}
