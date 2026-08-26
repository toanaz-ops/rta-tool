// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace rta::dsp {

/// Single-producer / single-consumer lock-free ring buffer.
///
/// The producer is the audio callback; the consumer is the analysis thread.
/// After construction nothing here allocates, locks, or blocks, which is the
/// whole point: the audio callback must never do any of those three things or it
/// will glitch under load — at a live show, during the measurement you actually
/// needed.
///
/// ## Why `peek` + `discard` instead of just `read`
///
/// Spectral analysis uses **overlapping** blocks: an FFT of length 4096 with 75%
/// overlap consumes a new 1024-sample hop each time but must see the previous
/// 3072 samples again. A plain `read()` that consumes what it returns cannot
/// express that. So the consumer `peek()`s a whole block and then `discard()`s
/// only the hop. `read()` is kept for the non-overlapping case.
///
/// ## Indices are monotonic, not wrapped
///
/// `writeIndex_` and `readIndex_` only ever increase; the mask is applied at the
/// point of access. This avoids the classic "sacrifice one slot to tell full
/// from empty" trick, so the full capacity is usable and `availableToRead()` is
/// a plain subtraction. It relies on unsigned wraparound being well defined,
/// which it is — and at 192 kHz a 64-bit counter takes about 3 million years to
/// wrap.
template <typename T>
class RingBuffer {
public:
    /// @param minCapacity  minimum number of elements; rounded up to a power of
    ///                     two so that index masking is a single AND.
    explicit RingBuffer(std::size_t minCapacity) {
        if (minCapacity == 0) {
            throw std::invalid_argument("RingBuffer capacity must be greater than zero");
        }
        const std::size_t capacity = std::bit_ceil(minCapacity);
        storage_.assign(capacity, T{});
        mask_ = capacity - 1;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return storage_.size(); }

    [[nodiscard]] std::size_t availableToRead() const noexcept {
        return writeIndex_.load(std::memory_order_acquire) -
               readIndex_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t availableToWrite() const noexcept {
        return capacity() - availableToRead();
    }

    /// Producer side. Writes all of `src` or nothing.
    /// @return false if there was not enough free space (an overrun: the
    ///         consumer is not keeping up, and the caller should say so rather
    ///         than silently corrupting the stream).
    bool write(std::span<const T> src) noexcept {
        const std::size_t write = writeIndex_.load(std::memory_order_relaxed);
        const std::size_t read = readIndex_.load(std::memory_order_acquire);
        if (src.size() > capacity() - (write - read)) {
            return false;
        }
        for (std::size_t i = 0; i < src.size(); ++i) {
            storage_[(write + i) & mask_] = src[i];
        }
        writeIndex_.store(write + src.size(), std::memory_order_release);
        return true;
    }

    /// Consumer side. Copies without consuming, starting `offset` elements past
    /// the read position.
    /// @return number of elements actually copied (short if not enough data).
    [[nodiscard]] std::size_t peek(std::span<T> dst, std::size_t offset = 0) const noexcept {
        const std::size_t read = readIndex_.load(std::memory_order_relaxed);
        const std::size_t write = writeIndex_.load(std::memory_order_acquire);
        const std::size_t available = write - read;
        if (offset >= available) {
            return 0;
        }
        const std::size_t count = std::min(dst.size(), available - offset);
        for (std::size_t i = 0; i < count; ++i) {
            dst[i] = storage_[(read + offset + i) & mask_];
        }
        return count;
    }

    /// Consumer side. Advances the read position by up to `count` elements.
    /// @return number actually discarded.
    std::size_t discard(std::size_t count) noexcept {
        const std::size_t read = readIndex_.load(std::memory_order_relaxed);
        const std::size_t write = writeIndex_.load(std::memory_order_acquire);
        const std::size_t n = std::min(count, write - read);
        readIndex_.store(read + n, std::memory_order_release);
        return n;
    }

    /// Consumer side. `peek` followed by `discard` of the same amount.
    [[nodiscard]] std::size_t read(std::span<T> dst) noexcept {
        const std::size_t n = peek(dst);
        discard(n);
        return n;
    }

    /// Not thread-safe against concurrent access; call only when both sides are
    /// known to be stopped (device change, sample-rate change).
    void reset() noexcept {
        readIndex_.store(0, std::memory_order_relaxed);
        writeIndex_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<T> storage_;
    std::size_t mask_ = 0;

    // Producer and consumer indices sit on separate cache lines. Without this
    // they share one, and every write invalidates the line the reader is
    // polling -- false sharing, which costs more than the lock this class
    // exists to avoid. MSVC warns that the struct grew as a result (C4324);
    // that growth is the entire point.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
    alignas(64) std::atomic<std::size_t> writeIndex_{0};
    alignas(64) std::atomic<std::size_t> readIndex_{0};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
};

}  // namespace rta::dsp
