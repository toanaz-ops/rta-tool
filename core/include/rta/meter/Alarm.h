// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/meter/Block.h"

#include <optional>

namespace rta::meter {

/// Record 6's closed form: the highest CONSTANT level that can be run for the
/// rest of the window without the window exceeding its limit.
///
///     L_allow = 10*log10( ( T*10^(L_lim/10) - t*10^(L_t/10) ) / (T - t) )
///
/// The energy already spent is `t*10^(L_t/10)`; the whole window's budget is
/// `T*10^(L_lim/10)`; the difference, spread over the `T - t` that is left, is
/// a level. Checks, both exact: `L_t = L_lim` throughout gives `L_lim` back,
/// and at `t = T/2` with `L_t = L_lim - 10` the numerator is
/// `0.95*T*10^(L_lim/10)` and the answer is `L_lim + 10*log10(1.9)`.
///
/// ABSENT when the bracket is <= 0, or when there is no time left, or when the
/// inputs are not a window at all. "The window is already lost" is a FACT
/// ABOUT THE ARITHMETIC, not a threshold -- and it is the honest form of "you
/// cannot fix this by turning down now". Returning `kLevelFloorDb` or clamping
/// to `L_lim` instead would read as a measurement
/// (memory/a-placeholder-for-an-absent-result-erases-its-state.md).
///
/// PRECISION NOTE, because it is a property of the identity and not of this
/// code. `budget - spent` is a subtraction of two nearly equal numbers once
/// the window is nearly full, so the relative error on the bracket is
/// amplified by `T/(T - t)`: a factor of 1000 at `t = 0.999*T`. Measured on
/// MSVC 14.51 over 252 (T, t, L_lim) triples the worst deviation from the
/// L_t = L_lim fixed point is 6.25e-13 dB -- eleven orders under the 0.1 dB
/// the display shows, so nothing is done about it. It is written down because
/// a reader who assumes this is exact to the last bit near the end of a
/// window would be wrong, and W1-C1's bound is derived from exactly this
/// factor rather than rounded up to a comfortable constant.
///
/// WHY THERE IS NO TRAFFIC LIGHT HERE. Every surveyed competitor ships a
/// red/amber/green, and not one publishes its margin: Smaart SPL's alarm has
/// exactly two fields (`Level` and `Duration`, and `Duration` is post-fire
/// flash time, not a debounce) with user-defined colour thresholds and no
/// published default; 10EaZy's amber is a closed algorithm documented only
/// qualitatively. A margin shipped here would be ORIGINATED BY THIS PROJECT,
/// which is what memory/a-threshold-read-off-a-grid-is-that-grids-floor.md is
/// about. `L_allow` needs no margin to be meaningful, and "warn me below 3 dB
/// of headroom" is then the operator's own sentence about a number this
/// function already gives them.
///
/// @param windowSeconds  T, the window length. Must be > 0.
/// @param elapsedSeconds t, how much of it has been measured. [0, T).
/// @param elapsedLeqDb   L_t, the Leq over the elapsed part.
/// @param limitDb        L_lim.
[[nodiscard]] std::optional<double> headroomDb(double windowSeconds, double elapsedSeconds,
                                               double elapsedLeqDb, double limitDb) noexcept;

/// A two-state latch on a WINDOWED Leq: active while `Leq(W) > L_lim`, clear
/// otherwise. Equality is not an exceedance.
///
/// NO HYSTERESIS AND NO DEBOUNCE, and that is a decision with a reason rather
/// than an omission. Flicker is a property of the QUANTITY, not of the
/// comparator: what this compares is an integrated value over a window of
/// seconds to minutes, which by construction changes by a bounded amount per
/// block. Smaart ships a single threshold over a logged 3 s value and does not
/// flicker; 10EaZy compares an Leq over at least 3 minutes. Integration is
/// already doing the work a hysteresis would do, and adding one on top would
/// be a second undocumented constant defending against a problem the first
/// already solved. The state in this class is therefore exactly one bool, and
/// there is nowhere for a swallowed transition to hide.
///
/// It takes a `WindowResult` and NOT a bare dB. That is structural, not
/// advisory: a `WindowResult` is what `combineBlocks` produces, so the type
/// says the comparison is against a window. The bare-double overload is
/// DELETED so a caller reaching for an instantaneous level fails to compile.
class AlarmLatch {
public:
    enum class Transition {
        None,     ///< no state change (including: nothing to compare)
        Fired,    ///< clear -> active
        Cleared,  ///< active -> clear
    };

    /// Compares `windowed.leqDb` against `limitDb` and reports the change.
    ///
    /// An ABSENT `leqDb` -- an empty or not-yet-filled window -- is not
    /// compared at all: it returns `None` and leaves the state alone. Treating
    /// it as "below the limit" would CLEAR a live alarm because the window
    /// emptied, which is a state change the measurement never reported.
    Transition update(const WindowResult& windowed, double limitDb) noexcept;

    /// Deliberately deleted: record 6's quantity is an Leq over a window,
    /// never an instantaneous level, and this is where that is enforced.
    Transition update(double instantaneousDb, double limitDb) = delete;

    [[nodiscard]] bool active() const noexcept { return active_; }

    /// Back to clear, reporting nothing. For a session reset or a marker
    /// `reset` kind; not for a measurement.
    void reset() noexcept { active_ = false; }

private:
    bool active_ = false;
};

}  // namespace rta::meter
