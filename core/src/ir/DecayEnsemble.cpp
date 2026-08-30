// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#include "rta/ir/DecayEnsemble.h"

#include <algorithm>
#include <vector>

namespace rta::ir {
namespace {

/// Median of an already-sorted, non-empty vector.
///
/// The even case averages the two middle values rather than taking the lower.
/// With four captures the lower-middle rule biases every reading downward by
/// half a gap, which for a figure whose spread is tens of percent is small but
/// is also free to avoid.
[[nodiscard]] double medianOf(const std::vector<double>& sorted) {
    const std::size_t n = sorted.size();
    if (n % 2 == 1) return sorted[n / 2];
    return 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
}

/// Linearly interpolated quantile, the definition NumPy uses by default.
///
/// Spelled out rather than taken as obvious because the alternative -- nearest
/// rank -- gives a visibly different answer at the capture counts an operator
/// will actually use. At four captures the two conventions disagree on the
/// inter-quartile spread by roughly a third, which would make this project's
/// figures incomparable with the Python probes that calibrated them.
[[nodiscard]] double quantileOf(const std::vector<double>& sorted, double q) {
    const auto n = static_cast<double>(sorted.size());
    const double pos = q * (n - 1.0);
    const auto lo = static_cast<std::size_t>(pos);
    const std::size_t hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = pos - static_cast<double>(lo);
    return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

/// Collect one figure across the curves that produced it, and describe both
/// what they agree on and how much they do not.
[[nodiscard]] DecaySpread gather(std::span<const EnergyDecayCurve> curves,
                                 DecayTime DecayTimes::* which) {
    DecaySpread out;

    std::vector<double> seconds;
    seconds.reserve(curves.size());
    DecayRefusal firstRefusal = DecayRefusal::NoDecayFound;
    bool sawRefusal = false;

    for (const auto& curve : curves) {
        const auto times = decayTimes(curve);
        const auto& figure = times.*which;
        if (figure.has()) {
            seconds.push_back(figure.seconds);
        } else if (!sawRefusal) {
            firstRefusal = figure.refusal;
            sawRefusal = true;
        }
    }

    out.captures = seconds.size();
    if (seconds.empty()) {
        // Every capture refused. Report the FIRST reason rather than a generic
        // one: the captures are of the same room, so the reasons agree in
        // practice, and a caller shown "no decay found" when the real answer
        // was "this band is too narrow" would go looking in the wrong place.
        out.value.refusal = firstRefusal;
        return out;
    }

    std::sort(seconds.begin(), seconds.end());
    out.value.seconds = medianOf(seconds);
    out.value.refusal = DecayRefusal::None;

    // Below three captures an inter-quartile spread is not a spread. With two
    // points it is the gap between them, which reads as a precision figure
    // while carrying none -- exactly the false reassurance this whole type was
    // built to avoid. Left at zero, and `spreadIsMeaningful()` says why.
    if (seconds.size() >= 3 && out.value.seconds > 0.0) {
        const double iqr = quantileOf(seconds, 0.75) - quantileOf(seconds, 0.25);
        out.spreadPercent = 100.0 * iqr / out.value.seconds;
    }
    return out;
}

}  // namespace

DecayTimesAcrossCaptures decayTimesAcross(std::span<const EnergyDecayCurve> curves) {
    DecayTimesAcrossCaptures out;
    out.edt = gather(curves, &DecayTimes::edt);
    out.t20 = gather(curves, &DecayTimes::t20);
    out.t30 = gather(curves, &DecayTimes::t30);
    return out;
}

}  // namespace rta::ir
