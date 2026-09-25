// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API.
// Lane L6a task W2-A (record docs/dsp/2026-09-16-spl-pro-l6a.md §4).
#include "measure/SplHistory.h"

namespace rta::measure {

std::uint64_t SplHistory::capacityBlocks(double spanSeconds, double blockSeconds) noexcept {
    if (!(spanSeconds > 0.0) || !(blockSeconds > 0.0)) return 0;
    const double blocks = spanSeconds / blockSeconds;
    return static_cast<std::uint64_t>(blocks + 0.5);
}

SplHistory::SplHistory(std::uint64_t capacityBlocks)
    : capacity_(capacityBlocks), ring_(capacityBlocks) {
    // The ONE allocation: `ring_(capacityBlocks)` above sizes the vector in
    // the member-initializer list. Nothing past this constructor ever calls
    // resize/reserve/push_back on `ring_` -- `push` below always writes
    // through an existing index -- so W2-A1's counting-allocator fixture
    // reads 0 bytes for every push after construction.
}

void SplHistory::push(const rta::meter::Block& block) {
    if (capacity_ == 0) return;
    if (pushedCount_ == 0) firstIndex_ = block.blockIndex;
    ring_[pushedCount_ % capacity_] = block;
    ++pushedCount_;
}

std::optional<std::uint64_t> SplHistory::oldestBlockIndex() const noexcept {
    if (pushedCount_ == 0) return std::nullopt;
    if (pushedCount_ <= capacity_) return firstIndex_;
    // The newest index is `firstIndex_ + pushedCount_ - 1`; the ring holds
    // exactly `capacity_` of the most recent ones, so the oldest surviving
    // index is `newest - capacity_ + 1`. Advances by exactly one per push
    // once the ring is full -- W2-A3's "rolls, does not wrap silently".
    return firstIndex_ + pushedCount_ - capacity_;
}

std::optional<std::uint64_t> SplHistory::newestBlockIndex() const noexcept {
    if (pushedCount_ == 0) return std::nullopt;
    return firstIndex_ + pushedCount_ - 1;
}

std::optional<rta::meter::Block> SplHistory::at(std::uint64_t blockIndex) const noexcept {
    const auto oldest = oldestBlockIndex();
    const auto newest = newestBlockIndex();
    if (!oldest.has_value() || !newest.has_value()) return std::nullopt;
    if (blockIndex < *oldest || blockIndex > *newest) return std::nullopt;
    // Contiguous indices from `firstIndex_` is the contract `push` assumes
    // (BlockAccumulator's own guarantee), so the ring position is the offset
    // from that origin modulo the capacity -- valid whether or not the ring
    // has rolled yet.
    return ring_[(blockIndex - firstIndex_) % capacity_];
}

}  // namespace rta::measure
