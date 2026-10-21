// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. measure/AnalyserRebuild.h: the sweep that
// replaces the Analyser set one slot per tick. The invariant everything else
// depends on is that a slot is FRESH only after its replacement exists, so a
// stale slot -- one still holding an Analyser of the previous configuration --
// is never reported fresh, and therefore never fed or published
// (test_analyser_rebuild_live.cpp proves the thread honours the answer).
#include <catch2/catch_test_macros.hpp>

#include "measure/AnalyserRebuild.h"

#include <memory>
#include <new>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AnalyserRebuild;

namespace {

constexpr std::size_t kSlots = 4;

Analyser::Config smallConfig(double sampleRate) {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 16;
    config.sampleRate = sampleRate;
    config.mtwEnabled = false;
    return config;
}

std::vector<std::unique_ptr<Analyser>> builtSet(double sampleRate) {
    std::vector<std::unique_ptr<Analyser>> set;
    for (std::size_t i = 0; i < kSlots; ++i) {
        set.push_back(std::make_unique<Analyser>(smallConfig(sampleRate)));
    }
    return set;
}

}  // namespace

TEST_CASE("a fresh sweep object starts with every slot fresh", "[analyser_rebuild]") {
    const AnalyserRebuild rebuild(kSlots);
    CHECK_FALSE(rebuild.pending());
    for (std::size_t slot = 0; slot < kSlots; ++slot) CHECK(rebuild.isFresh(slot));
}

TEST_CASE("begin() makes every slot stale; step() freshens exactly one per call, ascending",
          "[analyser_rebuild]") {
    auto set = builtSet(48000.0);
    AnalyserRebuild rebuild(kSlots);
    std::vector<double> ratesSeen;
    const auto make = [&](const Analyser::Config& config) {
        ratesSeen.push_back(config.sampleRate);
        return std::make_unique<Analyser>(config);
    };

    rebuild.begin(smallConfig(96000.0));
    CHECK(rebuild.pending());
    for (std::size_t slot = 0; slot < kSlots; ++slot) CHECK_FALSE(rebuild.isFresh(slot));

    for (std::size_t built = 1; built <= kSlots; ++built) {
        std::vector<const Analyser*> pointers;
        for (const auto& a : set) pointers.push_back(a.get());

        rebuild.step(set, make);

        // Exactly one more slot is fresh, and it is the next one in order.
        CHECK(rebuild.freshCount() == built);
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
            CHECK(rebuild.isFresh(slot) == (slot < built));
            // Only slot `built - 1` was replaced by this call.
            CHECK((set[slot].get() != pointers[slot]) == (slot == built - 1));
        }
    }
    CHECK_FALSE(rebuild.pending());
    REQUIRE(ratesSeen.size() == kSlots);
    for (const double rate : ratesSeen) CHECK(rate == 96000.0);

    // A finished sweep has nothing to build.
    rebuild.step(set, make);
    CHECK(ratesSeen.size() == kSlots);
}

TEST_CASE("a build that throws leaves the slot stale and the old Analyser untouched",
          "[analyser_rebuild]") {
    auto set = builtSet(48000.0);
    AnalyserRebuild rebuild(kSlots);
    rebuild.begin(smallConfig(96000.0));

    const Analyser* const original = set[0].get();
    const auto failing = [](const Analyser::Config&) -> std::unique_ptr<Analyser> {
        throw std::bad_alloc();
    };
    CHECK_THROWS_AS(rebuild.step(set, failing), std::bad_alloc);

    CHECK_FALSE(rebuild.isFresh(0));  // never reported fresh without a replacement
    CHECK(rebuild.freshCount() == 0);
    CHECK(set[0].get() == original);  // and never left null
    CHECK(rebuild.pending());

    // The retry the thread performs on its next tick completes the slot.
    rebuild.step(set, [](const Analyser::Config& c) { return std::make_unique<Analyser>(c); });
    CHECK(rebuild.isFresh(0));
    CHECK(set[0].get() != original);
}

TEST_CASE("begin() mid-sweep restarts at slot 0: slots built for the old config are stale again",
          "[analyser_rebuild]") {
    auto set = builtSet(48000.0);
    AnalyserRebuild rebuild(kSlots);
    const auto make = [](const Analyser::Config& c) { return std::make_unique<Analyser>(c); };

    rebuild.begin(smallConfig(96000.0));
    rebuild.step(set, make);
    rebuild.step(set, make);
    REQUIRE(rebuild.freshCount() == 2);

    rebuild.begin(smallConfig(192000.0));  // a second device change before the first finished
    CHECK(rebuild.freshCount() == 0);
    CHECK_FALSE(rebuild.isFresh(0));
    CHECK_FALSE(rebuild.isFresh(1));
    CHECK(rebuild.config().sampleRate == 192000.0);
}
