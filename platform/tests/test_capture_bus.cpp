// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "rta/platform/CaptureBus.h"

#include <atomic>
#include <cstdlib>
#include <numeric>
#include <thread>
#include <vector>

using rta::platform::CaptureBus;
using rta::platform::ChannelRole;
using rta::platform::kMaxChannels;

namespace {

/// Builds a `const float* const*` fixture by hand out of owned vectors -- no
/// JUCE, no device, exactly what the plan's fixture calls for.
struct CallbackBlock {
    std::vector<std::vector<float>> channels;
    std::vector<const float*> pointers;

    explicit CallbackBlock(int numChannels, int numSamples, float fillValue = 0.0f) {
        channels.assign(static_cast<std::size_t>(numChannels),
                         std::vector<float>(static_cast<std::size_t>(numSamples), fillValue));
        for (auto& ch : channels) pointers.push_back(ch.data());
    }

    const float* const* data() const noexcept { return pointers.data(); }
    int numChannels() const noexcept { return static_cast<int>(channels.size()); }
    int numSamples() const noexcept { return static_cast<int>(channels.empty() ? 0 : channels[0].size()); }
};

}  // namespace

TEST_CASE("An inactive bus writes nothing", "[capturebus]") {
    CaptureBus bus;
    bus.prepare(48000.0, 2, 4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.setActive(false);

    CallbackBlock block(2, 256, 1.0f);
    bus.pushFromCallback(block.data(), block.numChannels(), block.numSamples());

    CHECK(bus.ring(0)->availableToRead() == 0);
    // A block discarded on a stopped device is not a drop.
    CHECK(bus.totalDrops() == 0);
}

TEST_CASE("A measurement channel reaches its ring intact", "[capturebus]") {
    CaptureBus bus;
    bus.prepare(48000.0, 2, 4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.setActive(true);

    CallbackBlock block(2, 256);
    std::iota(block.channels[0].begin(), block.channels[0].end(), 1.0f);

    bus.pushFromCallback(block.data(), block.numChannels(), block.numSamples());

    auto* ring = bus.ring(0);
    REQUIRE(ring != nullptr);
    REQUIRE(ring->availableToRead() == 256);

    std::vector<float> out(256, -1.0f);
    CHECK(ring->read(out) == 256);
    CHECK(out == block.channels[0]);  // a copy, not arithmetic
}

TEST_CASE("An unassigned channel is not written", "[capturebus]") {
    CaptureBus bus;
    bus.prepare(48000.0, 2, 4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    // channel 1 stays Unused
    bus.setActive(true);

    CallbackBlock block(2, 256, 2.0f);
    bus.pushFromCallback(block.data(), block.numChannels(), block.numSamples());

    REQUIRE(bus.ring(1) != nullptr);
    CHECK(bus.ring(1)->availableToRead() == 0);
}

TEST_CASE("A full ring counts the whole block as dropped", "[capturebus]") {
    CaptureBus bus;
    // Small explicit capacity so filling it is cheap and exact.
    bus.prepare(48000.0, 1, 256);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.setActive(true);

    CallbackBlock fill(1, 256, 1.0f);
    bus.pushFromCallback(fill.data(), fill.numChannels(), fill.numSamples());
    REQUIRE(bus.ring(0)->availableToRead() == 256);
    REQUIRE(bus.dropCount(0) == 0);

    const auto beforeContents = [&] {
        std::vector<float> out(256, -1.0f);
        REQUIRE(bus.ring(0)->peek(out) == 256);
        return out;
    }();

    CallbackBlock more(1, 256, 9.0f);
    bus.pushFromCallback(more.data(), more.numChannels(), more.numSamples());

    CHECK(bus.dropCount(0) == 256);
    CHECK(bus.ring(0)->availableToRead() == 256);  // unchanged

    std::vector<float> after(256, -1.0f);
    REQUIRE(bus.ring(0)->peek(after) == 256);
    CHECK(after == beforeContents);  // existing contents untouched
}

TEST_CASE("prepare drains every ring and bumps the epoch", "[capturebus]") {
    CaptureBus bus;
    bus.prepare(48000.0, 2, 4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    REQUIRE(bus.config().setRole(1, ChannelRole::Reference));
    bus.setActive(true);

    CallbackBlock block(2, 512, 1.0f);
    bus.pushFromCallback(block.data(), block.numChannels(), block.numSamples());
    REQUIRE(bus.ring(0)->availableToRead() == 512);
    REQUIRE(bus.ring(1)->availableToRead() == 512);

    const auto epochBefore = bus.epoch();
    bus.prepare(96000.0, 2, 4096);

    CHECK(bus.ring(0)->availableToRead() == 0);
    CHECK(bus.ring(1)->availableToRead() == 0);
    CHECK(bus.sampleRate() == 96000.0);
    CHECK(bus.epoch() > epochBefore);
}

TEST_CASE("More channels than the bus was prepared for are clipped", "[capturebus]") {
    CaptureBus bus;
    bus.prepare(48000.0, 2, 4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    REQUIRE(bus.config().setRole(1, ChannelRole::Measurement));
    bus.setActive(true);

    CallbackBlock block(128, 64, 3.0f);
    // Must not crash, and must not touch anything beyond what was prepared.
    bus.pushFromCallback(block.data(), block.numChannels(), block.numSamples());

    CHECK(bus.ring(0)->availableToRead() == 64);
    CHECK(bus.ring(1)->availableToRead() == 64);
    CHECK(bus.ring(2) == nullptr);
    CHECK(bus.numChannels() == 2);
}

TEST_CASE("roles snapshot mid-block stays within bounds under concurrent role changes",
          "[capturebus][threads]") {
    // Not a data-race detector by itself, but a stress run that would crash
    // or read out of bounds if the bounds logic were wrong under contention.
    CaptureBus bus;
    bus.prepare(48000.0, 4, 4096);
    bus.setActive(true);

    std::atomic<bool> stop{false};
    std::thread mutator([&] {
        int ch = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            bus.config().setRole(ch % kMaxChannels, ChannelRole::Measurement);
            bus.config().setRole((ch + 1) % kMaxChannels, ChannelRole::Unused);
            ++ch;
        }
    });

    CallbackBlock block(4, 32, 0.5f);
    for (int i = 0; i < 20000; ++i) {
        bus.pushFromCallback(block.data(), block.numChannels(), block.numSamples());
        for (int ch = 0; ch < 4; ++ch) {
            // Draining keeps the rings from filling and keeps the test fast;
            // a bounds violation would show up as a crash long before this.
            std::vector<float> scratch(32);
            [[maybe_unused]] const auto discarded = bus.ring(ch)->read(scratch);
        }
    }

    stop.store(true, std::memory_order_relaxed);
    mutator.join();

    SUCCEED("no crash, no out-of-bounds access across 20000 contended blocks");
}

TEST_CASE("Concurrent producer and consumer keep the stream intact through pushFromCallback",
          "[capturebus][threads]") {
    // Mirrors core/tests/test_ringbuffer.cpp's gapless-stream pattern, but
    // through the actual pushFromCallback entry point rather than the ring
    // directly -- proving the role/bounds layer adds no gaps of its own.
    constexpr std::size_t kTotal = 200'000;
    constexpr int kChunk = 64;

    CaptureBus bus;
    bus.prepare(48000.0, 1, 4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.setActive(true);

    std::atomic<bool> mismatch{false};
    std::atomic<std::size_t> produced{0};

    std::thread producer([&] {
        auto* ring = bus.ring(0);
        std::vector<float> chunk(kChunk);
        std::size_t next = 0;
        while (next < kTotal) {
            const std::size_t n = std::min<std::size_t>(kChunk, kTotal - next);
            for (std::size_t i = 0; i < n; ++i) chunk[i] = static_cast<float>(next + i);
            // pushFromCallback never blocks by design -- a full ring is a
            // counted drop, not a stall (that is the whole point of §5.3's
            // "full ring" test). This producer is the only writer, so
            // waiting here for free space is safe and guarantees the
            // gapless stream this test is checking for, exactly like
            // core/tests/test_ringbuffer.cpp's producer spins on write().
            while (ring->availableToWrite() < n) {
                std::this_thread::yield();
            }
            const float* channels[1] = {chunk.data()};
            bus.pushFromCallback(channels, 1, static_cast<int>(n));
            next += n;
            produced.store(next, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&] {
        auto* ring = bus.ring(0);
        std::vector<float> chunk(kChunk);
        std::size_t expected = 0;
        while (expected < kTotal) {
            const std::size_t n = ring->read(std::span<float>(chunk.data(), kChunk));
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (chunk[i] != static_cast<float>(expected + i)) {
                    mismatch.store(true, std::memory_order_relaxed);
                    return;
                }
            }
            expected += n;
        }
    });

    producer.join();
    consumer.join();

    CHECK_FALSE(mismatch.load());
    CHECK(produced.load() == kTotal);
    CHECK(bus.dropCount(0) == 0);
}

// --- Allocation check ------------------------------------------------------
//
// The audio callback must allocate nothing, ever (CLAUDE.md real-time
// safety). A counting global operator new, armed only around the tight push
// loop below, proves pushFromCallback's steady-state path -- role snapshot on
// the stack, ring write into pre-sized storage -- touches the heap zero
// times. If this proves fragile on a given toolchain, the plan allows
// downgrading it to a review checklist item; it built and passed on this
// MSVC toolchain (see report), so it stays a real test.
namespace {
std::atomic<std::size_t> g_allocCount{0};
std::atomic<bool> g_counting{false};
}  // namespace

void* operator new(std::size_t size) {
    if (g_counting.load(std::memory_order_relaxed)) {
        g_allocCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

TEST_CASE("pushFromCallback allocates nothing", "[capturebus]") {
    CaptureBus bus;
    bus.prepare(48000.0, 2, 4096);
    REQUIRE(bus.config().setRole(0, ChannelRole::Measurement));
    bus.setActive(true);

    CallbackBlock block(2, 256, 0.1f);

    g_allocCount.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);
    for (int i = 0; i < 10000; ++i) {
        bus.pushFromCallback(block.data(), block.numChannels(), block.numSamples());
    }
    g_counting.store(false, std::memory_order_relaxed);

    CHECK(g_allocCount.load(std::memory_order_relaxed) == 0);
}
