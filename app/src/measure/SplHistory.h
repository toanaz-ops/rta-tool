// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
// Lane L6a task W2-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §4).
#pragma once

#include "rta/meter/Block.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rta::measure {

/// Smaart's own four marker kinds (record §6: "what the alarm records"),
/// reused here so the ring's marker list is the one place every annotated
/// event in a session lands, not just alarm transitions.
enum class SplMarkerKind { Alarm, Overload, Note, Reset };

/// One annotated event on the ring's own block clock. `direction` is +1 for a
/// rising / fired transition and -1 for a falling / cleared one; 0 where a
/// kind has no direction (a plain `Note`). `quantity` names WHAT the marker is
/// about (a metric id, or empty); `window` and `value` are the windowed
/// reading that caused it, so a marker is self-contained without a second
/// lookup into the log.
struct SplMarker {
    std::uint64_t blockIndex = 0;
    SplMarkerKind kind = SplMarkerKind::Note;
    int direction = 0;
    std::string quantity;
    std::uint64_t window = 0;
    double value = 0.0;
};

/// Record §4's declared-span ring: one `rta::meter::Block` per logged
/// channel, ALLOCATED ONCE at construction and never grown -- the vector is
/// sized to `capacityBlocks` in the constructor's member-initializer list and
/// nothing here ever calls `resize`, `reserve` or `push_back` on it after
/// that.
///
/// NUMBERS, NOT PIXELS (record §4, docs/dsp/2026-08-29-display-layer-l5c.md
/// §7 item 2): this class stores `rta::meter::Block` and `SplMarker` only --
/// no colour, no pixel position, no JUCE type -- so a resize of any view that
/// reads it leaves the stored history bit-identical. Decimation and colour
/// happen at draw time, one layer up.
class SplHistory {
public:
    /// Blocks in `spanSeconds` of `blockSeconds`-long blocks, rounded to the
    /// nearest whole block -- the one place a duration becomes a block count
    /// for this ring, so two callers never round differently.
    [[nodiscard]] static std::uint64_t capacityBlocks(double spanSeconds,
                                                       double blockSeconds) noexcept;

    /// @param capacityBlocks  how many blocks the ring holds; must be > 0.
    explicit SplHistory(std::uint64_t capacityBlocks);

    /// Appends one block, in increasing `block.blockIndex` order (the
    /// contract `rta::meter::BlockAccumulator` already guarantees). At
    /// capacity, the oldest block leaves memory and `oldestBlockIndex()`
    /// advances -- the ring ROLLS, it does not wrap silently and the session
    /// does not end.
    void push(const rta::meter::Block& block);

    void addMarker(SplMarker marker) { markers_.push_back(std::move(marker)); }

    [[nodiscard]] std::uint64_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::uint64_t size() const noexcept {
        return pushedCount_ < capacity_ ? pushedCount_ : capacity_;
    }
    [[nodiscard]] std::uint64_t pushedCount() const noexcept { return pushedCount_; }

    /// The oldest block index still held, or nullopt if nothing has been
    /// pushed yet.
    [[nodiscard]] std::optional<std::uint64_t> oldestBlockIndex() const noexcept;
    /// The newest block index held, or nullopt if nothing has been pushed.
    [[nodiscard]] std::optional<std::uint64_t> newestBlockIndex() const noexcept;

    /// The block at `blockIndex`, or an ABSENCE -- never a stale block --
    /// when `blockIndex` has rolled off the ring or has not arrived yet.
    [[nodiscard]] std::optional<rta::meter::Block> at(std::uint64_t blockIndex) const noexcept;

    [[nodiscard]] const std::vector<SplMarker>& markers() const noexcept { return markers_; }

private:
    std::uint64_t capacity_;
    std::vector<rta::meter::Block> ring_;
    std::uint64_t pushedCount_ = 0;
    std::uint64_t firstIndex_ = 0;  ///< blockIndex of the very first block pushed

    std::vector<SplMarker> markers_;
};

}  // namespace rta::measure
