// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H7 task T2 (docs/plans/2026-10-21-h7-generator-output-plan.md): the
// generator state machine against a REAL OutputEngine rendered by hand -- no
// device, no JUCE. Closed forms: the start ramp is 0.5(1 - cos(pi n / 480))
// (RampedGain, 10 ms at 48 kHz); a swap's hole is two ramps plus the block
// and poll granularity.

#include "measure/GeneratorRunner.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

using namespace rta::measure;
using rta::platform::OutputEngine;
using rta::platform::OutputRole;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 64;

struct Rig {
    OutputEngine engine;
    GeneratorEnvironment env;
    double nowMs = 0.0;
    std::vector<float> out0, out1;  ///< everything rendered so far, per output
    std::unique_ptr<GeneratorRunner> runner;

    Rig() {
        env.synthetic = true;
        env.numOutputs = 2;
        engine.prepare(kFs, 2);
        runner = std::make_unique<GeneratorRunner>(
            engine, GeneratorHost{[this] { return env; }, [this] { return nowMs; }});
    }

    /// One audio block, then one poll (the 100 Hz timer is slower than this;
    /// polling every block is the best case the hole bound allows).
    void block(bool poll = true) {
        std::vector<float> a(kBlock, 0.0f), b(kBlock, 0.0f);
        float* ptrs[2] = {a.data(), b.data()};
        engine.render(ptrs, 2, kBlock);
        out0.insert(out0.end(), a.begin(), a.end());
        out1.insert(out1.end(), b.begin(), b.end());
        nowMs += 1000.0 * kBlock / kFs;
        if (poll) runner->poll();
    }
    void blocks(int n, bool poll = true) {
        for (int i = 0; i < n; ++i) block(poll);
    }
    [[nodiscard]] std::size_t samples() const { return out0.size(); }

    [[nodiscard]] double peakOver(std::size_t from, std::size_t count) const {
        double p = 0.0;
        for (std::size_t i = from; i < from + count; ++i) p = std::max(p, std::abs(double(out0[i])));
        return p;
    }
};

GeneratorSettings sineSettings(double levelDb) {
    GeneratorSettings s;
    s.signal = GeneratorSignal::Sine;
    s.frequencyHz = 1000.0;
    s.levelDb = levelDb;
    return s;
}

double amp(double db) { return std::pow(10.0, db / 20.0); }

}  // namespace

TEST_CASE("generator runner: refusals leave the engine untouched") {
    Rig r;
    r.env.busyBy = "SWEEP";
    CHECK(r.runner->start() == GeneratorRefusal::Busy);
    CHECK(r.runner->readout() == "GEN: refused (SWEEP is running) -- pink -30.0 dBFS RMS, out 1");
    CHECK_FALSE(r.runner->active());
    CHECK(r.engine.sourceIsQuiescent());

    r.env.busyBy.clear();
    r.env.synthetic = false;
    r.env.deviceRunning = false;  // LIVE, device stopped
    CHECK(r.runner->start() == GeneratorRefusal::NoDevice);
    CHECK_FALSE(r.runner->active());

    r.env.deviceRunning = true;
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    CHECK(r.runner->start() == GeneratorRefusal::AlreadyRunning);
}

TEST_CASE("generator runner: start ramp is 0.5(1 - cos(pi n/480)) at the stated level") {
    Rig r;
    r.runner->apply(sineSettings(-30.0));
    // First ever start: master AND output gate both rise from Idle, so the
    // gain is g(n)^2 (platform test_output_engine.cpp pins that). Play, stop,
    // and start again: the output gate now stays Running and only the master
    // ramps, which is the closed form the operator's second press hears.
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(32);
    r.runner->stop();
    r.blocks(32);
    REQUIRE(r.runner->state() == GeneratorState::Idle);

    const std::size_t n0 = r.samples();
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(16);
    for (int n = 0; n < 480; ++n) {
        const double p = n / 480.0;
        const double gain = 0.5 * (1.0 - std::cos(std::numbers::pi * p));
        const double expected = amp(-30.0) * std::sin(2.0 * std::numbers::pi * 1000.0 * n / kFs) * gain;
        REQUIRE(std::abs(double(r.out0[n0 + static_cast<std::size_t>(n)]) - expected) < 1e-6);
    }
    // Steady state after the ramp: one cycle's peak is the closed-form level.
    CHECK(std::abs(r.peakOver(n0 + 480, 48) - amp(-30.0)) < 1e-6);
}

TEST_CASE("generator runner: a 1 dB step is a swap whose hole is within 960 + one block") {
    for (const double target : {-31.0, -29.0}) {
        Rig r;
        r.runner->apply(sineSettings(-30.0));
        REQUIRE(r.runner->start() == GeneratorRefusal::None);
        r.blocks(32);
        CHECK(std::abs(r.peakOver(r.samples() - 48, 48) - amp(-30.0)) < 1e-6);

        const std::size_t n0 = r.samples();  // the instant the step is requested
        r.runner->apply(sineSettings(target));
        REQUIRE(r.runner->state() == GeneratorState::Swapping);
        REQUIRE(r.runner->needsFastPoll());
        std::size_t nArm = 0;
        for (int i = 0; i < 64 && nArm == 0; ++i) {
            r.block();
            if (r.runner->state() == GeneratorState::Playing) nArm = r.samples();
        }
        REQUIRE(nArm != 0);
        // Silent from disarm until the new source's ramp reaches full gain:
        // fall (480) + block granularity + rise (480).
        const std::size_t hole = (nArm - n0) + 480;
        CHECK(hole <= 960 + static_cast<std::size_t>(kBlock));

        r.blocks(32);
        // Steady at the NEW level, and the ceiling/floor were not involved.
        CHECK(std::abs(r.peakOver(r.samples() - 48, 48) - amp(target)) < 1e-6);
        // The dip was real: the output fell to exact silence in between.
        const auto first = r.out0.begin() + static_cast<std::ptrdiff_t>(n0);
        CHECK(std::count(first, first + static_cast<std::ptrdiff_t>(hole), 0.0f) >= 16);
    }
}

TEST_CASE("generator runner: an epoch change goes Idle with a reason and never re-arms") {
    Rig r;
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(16);
    r.engine.prepare(kFs, 2);  // device restart: epoch bumps, engine disarms
    r.runner->poll();
    CHECK(r.runner->state() == GeneratorState::Idle);
    CHECK(r.runner->readout().find("audio device restarted") != std::string::npos);

    const std::size_t n0 = r.samples();
    r.blocks(64);  // plenty of polls, nothing may start it again
    CHECK(r.runner->state() == GeneratorState::Idle);
    CHECK(r.engine.sourceIsQuiescent());
    CHECK(r.peakOver(n0, 64 * kBlock) == 0.0);
}

TEST_CASE("generator runner: a LIVE device stop goes Idle and does not resume") {
    Rig r;
    r.env.synthetic = false;
    r.env.deviceRunning = true;
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(16);
    r.env.deviceRunning = false;
    r.runner->poll();
    CHECK(r.runner->state() == GeneratorState::Idle);
    CHECK(r.runner->readout().find("audio device stopped") != std::string::npos);
    r.env.deviceRunning = true;  // the device comes back
    r.blocks(16);
    CHECK(r.runner->state() == GeneratorState::Idle);
}

TEST_CASE("generator runner: explicit cancel (input-mode change) is immediate") {
    Rig r;
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(16);
    r.runner->cancel("input mode changed");
    CHECK(r.runner->state() == GeneratorState::Idle);
    CHECK(r.runner->readout().find("input mode changed") != std::string::npos);
    r.blocks(16);
    CHECK(r.runner->state() == GeneratorState::Idle);
}

TEST_CASE("generator runner: Stopping gives up after 500 ms when nothing renders") {
    Rig r;
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(16);  // rendered while armed: the engine now reads "not idle"
    r.runner->stop();
    REQUIRE(r.runner->state() == GeneratorState::Stopping);
    CHECK(r.runner->active());

    r.nowMs += 499.0;  // no render: a stopped device
    r.runner->poll();
    CHECK(r.runner->state() == GeneratorState::Stopping);
    r.nowMs += 1.0;
    r.runner->poll();
    CHECK(r.runner->state() == GeneratorState::Idle);
    CHECK_FALSE(r.runner->active());
}

TEST_CASE("generator runner: Stopping completes on quiescence well before the timeout") {
    Rig r;
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(16);
    r.runner->stop();
    r.blocks(16);  // 16 x 64 = 1024 samples >= the 480-sample fall
    CHECK(r.runner->state() == GeneratorState::Idle);
    CHECK(r.runner->readout() == "GEN: stopped -- pink -30.0 dBFS RMS, out 1");
}

TEST_CASE("generator runner: outputs change live with no swap, additive, last tick protected") {
    Rig r;
    r.runner->apply(sineSettings(-30.0));
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(32);
    CHECK(r.engine.role(0) == OutputRole::Routed);
    CHECK(r.engine.role(1) != OutputRole::Routed);

    GeneratorSettings both = r.runner->settings();
    both.outputs = {0, 1};
    CHECK(r.runner->apply(both) == GeneratorRefusal::None);
    CHECK(r.runner->state() == GeneratorState::Playing);  // no swap
    CHECK(r.engine.role(0) == OutputRole::Routed);        // additive: out 1 stays
    CHECK(r.engine.role(1) == OutputRole::Routed);
    r.blocks(32);
    CHECK(std::abs(r.peakOver(r.samples() - 48, 48) - amp(-30.0)) < 1e-6);
    double p1 = 0.0;
    for (std::size_t i = r.samples() - 48; i < r.samples(); ++i) p1 = std::max(p1, std::abs(double(r.out1[i])));
    CHECK(std::abs(p1 - amp(-30.0)) < 1e-6);
    CHECK(r.runner->readout() == "GEN: playing sine 1000 Hz -30.0 dBFS peak on out 1+2");

    GeneratorSettings none = r.runner->settings();
    none.outputs = {};
    CHECK(r.runner->apply(none) == GeneratorRefusal::LastOutput);
    CHECK(r.runner->settings().outputs == std::vector<int>{0, 1});

    GeneratorSettings only1 = r.runner->settings();
    only1.outputs = {1};
    CHECK(r.runner->apply(only1) == GeneratorRefusal::None);
    CHECK(r.engine.role(0) != OutputRole::Routed);
    CHECK(r.engine.role(1) == OutputRole::Routed);
}

TEST_CASE("generator runner: an above-ceiling request plays at -12 dBFS") {
    Rig r;
    GeneratorSettings s = sineSettings(0.0);
    r.runner->apply(s);
    CHECK(r.runner->settings().levelDb == kGeneratorCeilingDb);
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(32);
    CHECK(std::abs(r.peakOver(r.samples() - 48, 48) - amp(kGeneratorCeilingDb)) < 1e-6);
    CHECK(r.runner->readout() == "GEN: playing sine 1000 Hz -12.0 dBFS peak on out 1 (ceiling)");
}

TEST_CASE("generator runner: outputs are pruned when the device shrinks") {
    Rig r;
    GeneratorSettings both = r.runner->settings();
    both.outputs = {0, 1};
    r.runner->apply(both);
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.env.numOutputs = 1;
    r.runner->poll();
    CHECK(r.runner->settings().outputs == std::vector<int>{0});
    CHECK(r.engine.role(1) != OutputRole::Routed);
}

TEST_CASE("generator runner: a signal change while stopped does not touch the engine") {
    Rig r;
    GeneratorSettings s = r.runner->settings();
    s.signal = GeneratorSignal::White;
    r.runner->apply(s);
    CHECK(r.runner->state() == GeneratorState::Idle);
    CHECK(r.engine.sourceIsQuiescent());
    CHECK(r.runner->readout() == "GEN: stopped -- white -30.0 dBFS RMS, out 1");
}

TEST_CASE("generator runner: destroying a playing runner leaves the engine disarmed") {
    Rig r;
    REQUIRE(r.runner->start() == GeneratorRefusal::None);
    r.blocks(16);
    r.runner.reset();
    r.blocks(16, false);
    CHECK(r.engine.sourceIsQuiescent());
}
