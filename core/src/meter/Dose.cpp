// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/meter/Dose.h"

#include <cmath>

namespace rta::meter {

void Dose::reset() noexcept {
    accumulated_ = 0.0;
    secondsBelow_ = 0.0;
    secondsTotal_ = 0.0;
}

void Dose::addBlock(double blockLeqDb, double blockSeconds) noexcept {
    if (!(blockSeconds > 0.0)) return;

    secondsTotal_ += blockSeconds;

    // Strictly below. A level AT the threshold contributes: NIOSH cl. 1.3.3
    // requires levels "from 80 to 140 dBA" to be integrated, so 80 is inside
    // the integral, which is also why its Table 1-1 starts at 80 rather than
    // at the 85 dBA criterion.
    if (blockLeqDb < settings_.thresholdDb) {
        secondsBelow_ += blockSeconds;
        return;  // contributes EXACTLY zero -- not a small number
    }

    // The one formula. `q` is a denominator carried as a number, so this line
    // is the same for an energy convention (q = 10) and for any declared
    // exchange rate (q = Q/log10 2) -- which is the whole point of not
    // shipping a "3 dB or 5 dB" boolean.
    accumulated_ +=
        blockSeconds * std::pow(10.0, (blockLeqDb - settings_.criterionLevelDb) / settings_.q);
}

double Dose::percent() const noexcept {
    if (!(settings_.criterionSeconds > 0.0)) return 0.0;
    return 100.0 * accumulated_ / settings_.criterionSeconds;
}

std::optional<double> Dose::projectedPercent() const noexcept {
    if (!(secondsTotal_ > 0.0)) return std::nullopt;
    return percent() * (settings_.criterionSeconds / secondsTotal_);
}

std::optional<double> Dose::twaDb() const noexcept {
    const double d = percent();
    if (!(d > 0.0)) return std::nullopt;
    return settings_.q * std::log10(d / 100.0) + settings_.criterionLevelDb;
}

std::optional<double> exposureLevelDb(double leqDb, double seconds) noexcept {
    // The 8 h reference is the definition of L_EX,8h, not a setting: 28800 s.
    // And the 10 is energy, not an exchange rate -- see the header.
    constexpr double kEightHoursSeconds = 8.0 * 3600.0;
    // No exposure time is an absence, never the bare Leq -- see the header.
    if (!(seconds > 0.0)) return std::nullopt;
    return leqDb + 10.0 * std::log10(seconds / kEightHoursSeconds);
}

}  // namespace rta::meter
