// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "rta/dsp/RingBuffer.h"

#include <atomic>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

using rta::dsp::RingBuffer;

TEST_CASE("Capacity is rounded up to a power of two", "[ringbuffer]") {
    CHECK(RingBuffer<float>(1).capacity() == 1);
    CHECK(RingBuffer<float>(5).capacity() == 8);
    CHECK(RingBuffer<float>(1024).capacity() == 1024);
    CHECK(RingBuffer<float>(1025).capacity() == 2048);
    CHECK_THROWS_AS(RingBuffer<float>(0), std::invalid_argument);
}

TEST_CASE("Round-trip preserves data and accounting", "[ringbuffer]") {
    RingBuffer<float> rb(16);
    std::vector<float> in(10);
    std::iota(in.begin(), in.end(), 1.0f);

    REQUIRE(rb.availableToWrite() == 16);
    REQUIRE(rb.write(in));
    CHECK(rb.availableToRead() == 10);
    CHECK(rb.availableToWrite() == 6);

    std::vector<float> out(10, 0.0f);
    CHECK(rb.read(out) == 10);
    CHECK(out == in);
    CHECK(rb.availableToRead() == 0);
}

TEST_CASE("Writing more than the free space fails atomically", "[ringbuffer]") {
    RingBuffer<float> rb(8);
    const std::vector<float> six(6, 1.0f);
    REQUIRE(rb.write(six));

    const std::vector<float> four(4, 2.0f);
    CHECK_FALSE(rb.write(four));       // only 2 slots free
    CHECK(rb.availableToRead() == 6);  // and nothing was partially written

    const std::vector<float> two(2, 3.0f);
    CHECK(rb.write(two));
    CHECK(rb.availableToRead() == 8);
}

TEST_CASE("peek does not consume; discard advances", "[ringbuffer]") {
    RingBuffer<int> rb(16);
    std::vector<int> in(8);
    std::iota(in.begin(), in.end(), 0);
    REQUIRE(rb.write(in));

    std::vector<int> out(4, -1);
    CHECK(rb.peek(out) == 4);
    CHECK(out == std::vector<int>{0, 1, 2, 3});
    CHECK(rb.availableToRead() == 8);  // unchanged

    CHECK(rb.peek(out, 4) == 4);
    CHECK(out == std::vector<int>{4, 5, 6, 7});

    CHECK(rb.discard(3) == 3);
    CHECK(rb.availableToRead() == 5);
    CHECK(rb.peek(out) == 4);
    CHECK(out == std::vector<int>{3, 4, 5, 6});

    CHECK(rb.discard(999) == 5);  // clamps, does not run past the writer
    CHECK(rb.availableToRead() == 0);
}

TEST_CASE("Overlapping-block access pattern behaves like an FFT hop", "[ringbuffer]") {
    // The real use: block of 8, hop of 2 (75% overlap). Each analysis step must
    // see the previous 6 samples again.
    constexpr int kBlock = 8;
    constexpr int kHop = 2;
    RingBuffer<int> rb(64);

    int next = 0;
    for (int i = 0; i < kBlock; ++i) {
        const int v = next++;
        REQUIRE(rb.write(std::span<const int>(&v, 1)));
    }

    std::vector<int> block(kBlock);
    for (int step = 0; step < 5; ++step) {
        REQUIRE(rb.peek(block) == kBlock);
        for (int i = 0; i < kBlock; ++i) {
            REQUIRE(block[static_cast<std::size_t>(i)] == step * kHop + i);
        }
        rb.discard(kHop);
        for (int i = 0; i < kHop; ++i) {
            const int v = next++;
            REQUIRE(rb.write(std::span<const int>(&v, 1)));
        }
    }
}

TEST_CASE("Concurrent producer and consumer keep the stream intact", "[ringbuffer][threads]") {
    // A memory-ordering mistake shows up here as a gap or a repeat in the
    // sequence, which a single-threaded test can never catch.
    constexpr std::size_t kTotal = 1'000'000;
    constexpr std::size_t kChunk = 64;
    RingBuffer<std::size_t> rb(1024);

    std::atomic<bool> mismatch{false};
    std::atomic<std::size_t> produced{0};

    std::thread producer([&] {
        std::vector<std::size_t> chunk(kChunk);
        std::size_t next = 0;
        while (next < kTotal) {
            const std::size_t n = std::min(kChunk, kTotal - next);
            for (std::size_t i = 0; i < n; ++i) chunk[i] = next + i;
            std::span<const std::size_t> span(chunk.data(), n);
            while (!rb.write(span)) {
                std::this_thread::yield();  // buffer full: consumer is behind
            }
            next += n;
            produced.store(next, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&] {
        std::vector<std::size_t> chunk(kChunk);
        std::size_t expected = 0;
        while (expected < kTotal) {
            const std::size_t n = rb.read(std::span<std::size_t>(chunk.data(), kChunk));
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (std::size_t i = 0; i < n; ++i) {
                if (chunk[i] != expected + i) {
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
}
