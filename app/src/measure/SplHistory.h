// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/src/measure. No JUCE, no Qt, no audio-device API:
// enforced by the measure_has_no_framework_deps ctest.
// Lane L6a task W2-A (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §4).
#pragma once

#include "rta/meter/Block.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rta::measure {

/// Smaart's own four marker kinds (record §6: "what the alarm records"),
/// reused here so the ring's marker list is the one place every annotated
/// event in a session lands, not just alarm transitions. `Gap` is a fifth,
/// added by the report payload builder (task W2-E2b fix round, MEDIUM
/// finding): the paired drain stalling is exactly as annotation-worthy as an
/// overload, and `rta::meter::BlockFlag::Gap` already carries the fact --
/// nothing upstream of the report ever turned it into a marker.
enum class SplMarkerKind { Alarm, Overload, Note, Reset, Gap };

/// A marker's quantity label, with NO heap allocation, ever -- fix round
/// 2026-09-25: `SplMarker::quantity` was a `std::string`, and an unreserved
/// `std::vector<SplMarker>` beside it together measured 8128 B across 40
/// alarm transitions (a Fired/Cleared marker on every one), which broke the
/// ring's own "allocated once, never grown" rule the moment anything actually
/// called `addMarker`. Truncates rather than allocates -- 31 characters is
/// long enough for every metric id this project actually configures
/// (`"LAeq,Fast"` is 9 of 31; `test_spl_history.cpp` A5 pins the shape).
class MarkerQuantity {
public:
    static constexpr std::size_t kCapacity = 32;  // 31 chars + NUL

    constexpr MarkerQuantity() noexcept : buf_{} {}
    MarkerQuantity(std::string_view s) noexcept { assign(s); }
    MarkerQuantity& operator=(std::string_view s) noexcept {
        assign(s);
        return *this;
    }

    [[nodiscard]] const char* c_str() const noexcept { return buf_.data(); }
    [[nodiscard]] std::string_view view() const noexcept { return std::string_view(buf_.data()); }
    [[nodiscard]] bool empty() const noexcept { return buf_[0] == '\0'; }

    [[nodiscard]] friend bool operator==(const MarkerQuantity& a, const MarkerQuantity& b) noexcept {
        return a.view() == b.view();
    }
    [[nodiscard]] friend bool operator==(const MarkerQuantity& a, std::string_view b) noexcept {
        return a.view() == b;
    }
    [[nodiscard]] friend bool operator==(std::string_view a, const MarkerQuantity& b) noexcept {
        return a == b.view();
    }

private:
    void assign(std::string_view s) noexcept {
        const std::size_t n = s.size() < (kCapacity - 1) ? s.size() : (kCapacity - 1);
        for (std::size_t i = 0; i < n; ++i) buf_[i] = s[i];
        buf_[n] = '\0';
    }
    std::array<char, kCapacity> buf_;
};

/// One annotated event on the ring's own block clock. `direction` is +1 for a
/// rising / fired transition and -1 for a falling / cleared one; 0 where a
/// kind has no direction (a plain `Note`). `quantity` names WHAT the marker is
/// about (a metric id, or empty); `window` and `value` are the windowed
/// reading that caused it, so a marker is self-contained without a second
/// lookup into the log.
///
/// `metricIndex` is the marker's REAL identity (fix round 2026-09-25, PR
/// #29 round-3 step 3): `quantity` truncates at `MarkerQuantity::kCapacity`
/// (31 chars), so two configured metric ids sharing a 31-char prefix
/// collide under it -- `quantity == quantity` cannot tell them apart. The
/// index into `SplConfig::metrics` cannot collide (it is the metric's own
/// position, resolved once at `SplChannelState`/`SplAlarms` construction,
/// the same place the weighting-routing lookup already happens in
/// `buildAlarmGroups`), so any IDENTITY comparison between markers must use
/// THIS field, never `quantity`. `nullopt` for a marker with no associated
/// metric (an alarm whose `metricId` named no configured metric; Overload/
/// Reset/Note kinds). `quantity` itself stays -- a bounded string display
/// buffer is still useful for rendering -- but it must never be trusted for
/// identity.
struct SplMarker {
    std::uint64_t blockIndex = 0;
    SplMarkerKind kind = SplMarkerKind::Note;
    int direction = 0;
    MarkerQuantity quantity;
    std::optional<std::size_t> metricIndex;
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

    /// Markers are ALLOCATED ONCE, at construction, and never grown --
    /// `markers_` is reserved to `kMaxMarkers` in the constructor and never
    /// past it. Fix round 2026-09-25 found the unreserved vector's own
    /// growth was the dominant allocator on the alarm-transition path
    /// (8128 B / 40 transitions); this ring is sized generously against real
    /// transition rates -- record §6's "no invented hysteresis, no invented
    /// debounce" means transitions track the signal, not manufactured
    /// flicker, so thousands of them in one session would itself be a
    /// misconfigured alarm, not normal operation.
    ///
    /// PAST CAPACITY, THE MARKER IS COUNTED AND DROPPED, never silently: the
    /// coordinator's own preference over a silently-rolling ring, because a
    /// marker (unlike a block) has no other record of its own -- the log
    /// writer (W2-C) reads blocks, not markers, so a rolled-off marker would
    /// be gone from every consumer, not just this ring's own accessor.
    /// `overflowedMarkers()` is the count.
    static constexpr std::size_t kMaxMarkers = 4096;

    void addMarker(SplMarker marker);

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
    /// How many `addMarker` calls were dropped because `kMaxMarkers` was
    /// already reached. Never silent -- see `kMaxMarkers`'s own comment.
    [[nodiscard]] std::uint64_t overflowedMarkers() const noexcept { return overflowedMarkers_; }

private:
    std::uint64_t capacity_;
    std::vector<rta::meter::Block> ring_;
    std::uint64_t pushedCount_ = 0;
    std::uint64_t firstIndex_ = 0;  ///< blockIndex of the very first block pushed

    std::vector<SplMarker> markers_;
    std::uint64_t overflowedMarkers_ = 0;
};

}  // namespace rta::measure
