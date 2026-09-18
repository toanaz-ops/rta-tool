// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/meter/Alarm.h"

#include <cmath>

namespace rta::meter {

std::optional<double> headroomDb(double windowSeconds, double elapsedSeconds,
                                 double elapsedLeqDb, double limitDb) noexcept {
    // Written with `!(a > b)` rather than `a <= b` so a NaN input takes the
    // absence branch instead of falling through into std::log10.
    if (!(windowSeconds > 0.0)) return std::nullopt;
    if (!(elapsedSeconds >= 0.0)) return std::nullopt;

    const double remaining = windowSeconds - elapsedSeconds;
    if (!(remaining > 0.0)) return std::nullopt;

    const double budget = windowSeconds * std::pow(10.0, limitDb / 10.0);
    const double spent = elapsedSeconds * std::pow(10.0, elapsedLeqDb / 10.0);
    const double bracket = (budget - spent) / remaining;

    // <= 0 is the window already lost. It is reported as an ABSENCE and never
    // as a floor: -inf dB of remaining allowance is a true statement, and
    // kLevelFloorDb or a clamp to limitDb would both read as a measurement.
    if (!(bracket > 0.0)) return std::nullopt;

    return 10.0 * std::log10(bracket);
}

AlarmLatch::Transition AlarmLatch::update(const WindowResult& windowed,
                                          double limitDb) noexcept {
    // Nothing to compare is NOT "below the limit". A window that emptied has
    // not reported a level, so the latch holds whatever it held.
    if (!windowed.leqDb.has_value()) return Transition::None;

    // The whole comparison, and the whole of the state transition. There is no
    // previous value here, no epsilon and no counter -- see the header for why
    // shipping one would be a constant nobody can check.
    const bool over = *windowed.leqDb > limitDb;
    if (over == active_) return Transition::None;
    active_ = over;
    return over ? Transition::Fired : Transition::Cleared;
}

}  // namespace rta::meter
