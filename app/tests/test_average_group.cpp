// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Task B3 (record §6): the spatial average is the published trace, plus one
// soloed position; every other member publishes a summary and nothing else.
// T12's counting allocator (the last TEST_CASE here) overrides global
// operator new/delete for this whole test BINARY -- standard practice for
// this kind of measurement, and inert (a relaxed atomic load) everywhere
// outside the one window it is armed for.

#include <catch2/catch_test_macros.hpp>

#include "measure/AverageGroup.h"
#include "measure/Analyser.h"

#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <new>
#include <vector>

using rta::measure::Analyser;
using rta::measure::AverageGroup;
using rta::measure::AverageGroupMember;
using rta::measure::MemberRefusal;
using rta::measure::PositionSummary;

namespace {

/// A fast, real dual-FFT Analyser config -- small enough that a few dozen
/// pushPair calls clear the coherence gate in a fraction of a second.
Analyser::Config fastConfig() {
    Analyser::Config config;
    config.fftSize = 64;
    config.hopSize = 64;
    config.sampleRate = 48000.0;
    config.mtwEnabled = false;
    config.transferFifoDepth = 8;
    return config;
}

/// A hand-built TransferSnapshot -- valid for tests that check AverageGroup's
/// STRUCTURE (which positions publish what), not for tests that need a real
/// engine's own arithmetic (test 1 below drives real Analysers instead).
rta::dsp::TransferSnapshot makeSnapshot(float magnitudeDb, float coherence, double effectiveAverages,
                                        std::size_t bins) {
    rta::dsp::TransferSnapshot snap;
    snap.sampleRate = 48000.0;
    snap.binWidthHz = 48000.0 / 128.0;
    snap.effectiveAverages = effectiveAverages;
    const double magnitude = std::pow(10.0, static_cast<double>(magnitudeDb) / 20.0);
    snap.h.assign(bins, std::complex<double>(magnitude, 0.0));
    snap.magnitudeDb.assign(bins, magnitudeDb);
    snap.phaseRadians.assign(bins, 0.0f);
    snap.coherence = std::vector<float>(bins, coherence);
    return snap;
}

}  // namespace

TEST_CASE("A group over two real Analysers publishes an average equal to spatialAverage directly",
          "[averagegroup]") {
    Analyser a(fastConfig());
    Analyser b(fastConfig());

    std::vector<float> reference(64), measurement(64);
    for (std::size_t i = 0; i < 64; ++i) {
        const double t = static_cast<double>(i);
        reference[i] = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 6.0 * t / 64.0));
        measurement[i] = reference[i] * 0.7f;
    }
    // Enough frames to clear the default 8.0 minimumEffectiveAverages gate
    // at fifoDepth 8 -- both engines are fed the SAME pair every time, so
    // both clear together.
    for (int frame = 0; frame < 30; ++frame) {
        a.pushPair(reference, measurement);
        b.pushPair(reference, measurement);
    }

    const auto snapA = a.transferSnapshot();
    const auto snapB = b.transferSnapshot();
    REQUIRE(snapA.has_value());
    REQUIRE(snapB.has_value());
    REQUIRE(snapA->coherence.has_value());
    REQUIRE(snapB->coherence.has_value());

    AverageGroup group;
    REQUIRE(group.addMember(0, 0, "A") == MemberRefusal::None);
    REQUIRE(group.addMember(1, 0, "B") == MemberRefusal::None);

    const std::array<rta::dsp::TransferSnapshot, 2> positions{*snapA, *snapB};
    const auto grouped = group.publish(positions);

    const std::array<double, 2> u{1.0, 1.0};
    const auto direct = rta::dsp::spatialAverage(positions, u, rta::dsp::SpatialMode::Db);

    REQUIRE(direct.has_value());
    REQUIRE(grouped.average.has_value());
    CHECK(grouped.average->magnitudeDb == direct->magnitudeDb);
    CHECK(grouped.average->phaseAgreement == direct->phaseAgreement);
    CHECK(grouped.average->weightedCoherence == direct->weightedCoherence);
    REQUIRE(grouped.positions.size() == 2);
    CHECK(grouped.positions[0].tfIndex == 0);
    CHECK(grouped.positions[1].tfIndex == 1);
    CHECK(grouped.positions[0].gatePassed);
    CHECK(grouped.positions[1].gatePassed);
}

TEST_CASE("A member naming a different reference channel is refused, group unchanged",
          "[averagegroup]") {
    AverageGroup group;
    REQUIRE(group.addMember(0, 5, "A") == MemberRefusal::None);
    CHECK(group.addMember(1, 6, "B") == MemberRefusal::DifferentReference);
    REQUIRE(group.memberCount() == 1);
    CHECK(group.members()[0].name == "A");
    CHECK(group.members()[0].referenceChannel == 5);
}

TEST_CASE("Publish carries the average, exactly one soloed position's full block, "
          "and a summary per member",
          "[averagegroup]") {
    constexpr std::size_t bins = 5;
    const auto s0 = makeSnapshot(-3.0f, 0.9f, 32.0, bins);
    const auto s1 = makeSnapshot(0.0f, 0.8f, 32.0, bins);
    const auto s2 = makeSnapshot(3.0f, 0.95f, 32.0, bins);

    AverageGroup group;
    REQUIRE(group.addMember(0, 0, "A") == MemberRefusal::None);
    REQUIRE(group.addMember(1, 0, "B") == MemberRefusal::None);
    REQUIRE(group.addMember(2, 0, "C") == MemberRefusal::None);
    group.setSolo(1);

    const std::array<rta::dsp::TransferSnapshot, 3> positions{s0, s1, s2};
    const auto soloed = group.publish(positions);
    REQUIRE(soloed.average.has_value());
    REQUIRE(soloed.positions.size() == 3);
    REQUIRE(soloed.soloTransfer.has_value());
    CHECK(soloed.soloTransfer->magnitudeDb == s1.magnitudeDb);

    group.clearSolo();
    const auto unsoloed = group.publish(positions);
    REQUIRE(unsoloed.average.has_value());
    REQUIRE(unsoloed.positions.size() == 3);
    CHECK_FALSE(unsoloed.soloTransfer.has_value());
}

// --- T12: publish churn is O(1) in N -----------------------------------

namespace {
std::atomic<bool> g_countingActive{false};
std::atomic<std::size_t> g_bytesAllocated{0};
}  // namespace

void* operator new(std::size_t size) {
    void* p = std::malloc(size);
    if (p == nullptr) throw std::bad_alloc();
    if (g_countingActive.load(std::memory_order_relaxed)) {
        g_bytesAllocated.fetch_add(size, std::memory_order_relaxed);
    }
    return p;
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

std::size_t measurePublishBytes(int memberCount) {
    constexpr std::size_t bins = 513;  // fftSize 1024's own real bin count
    AverageGroup group;
    for (int i = 0; i < memberCount; ++i) {
        REQUIRE(group.addMember(i, 0, "position") == MemberRefusal::None);
    }
    std::vector<rta::dsp::TransferSnapshot> snapshots;
    snapshots.reserve(static_cast<std::size_t>(memberCount));
    for (int i = 0; i < memberCount; ++i) {
        snapshots.push_back(makeSnapshot(-1.0f * static_cast<float>(i), 0.9f, 32.0, bins));
    }

    g_bytesAllocated.store(0, std::memory_order_relaxed);
    g_countingActive.store(true, std::memory_order_relaxed);
    const auto result = group.publish(snapshots);
    g_countingActive.store(false, std::memory_order_relaxed);

    REQUIRE(result.positions.size() == static_cast<std::size_t>(memberCount));
    return g_bytesAllocated.load(std::memory_order_relaxed);
}

}  // namespace

TEST_CASE("Publish churn is O(1) in N: bytes(8) - bytes(4) is bounded (record §6, T12)",
          "[averagegroup]") {
    const auto bytes1 = measurePublishBytes(1);
    const auto bytes4 = measurePublishBytes(4);
    const auto bytes8 = measurePublishBytes(8);

    // Report-only headline (plan's own instruction: "Report the 2x
    // headline beside it; do not assert it" -- N = 1's own cost is the
    // baseline every real per-position full publish used to pay ONCE, so
    // "within Nx of N = 1" is informative even though the real assertion is
    // the bounded DELTA below.
    INFO("bytes(1) = " << bytes1 << ", bytes(4) = " << bytes4 << ", bytes(8) = " << bytes8
                       << " (bytes(8)/bytes(1) = "
                       << (static_cast<double>(bytes8) / static_cast<double>(bytes1)) << "x)");
    REQUIRE(bytes8 >= bytes4);

    const auto delta = bytes8 - bytes4;
    const auto bound = 4 * sizeof(PositionSummary) + 4096;
    INFO("delta = " << delta << ", bound = " << bound);
    CHECK(delta <= bound);
}
