// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
//
// Internal to core/src/ir. Not installed and not part of the public surface:
// the crossing search is an implementation detail of `energyDecayCurve`, and
// exposing it would invite a caller to truncate by one rule and integrate by
// another.
#pragma once

#include "rta/ir/Decay.h"

#include <cstddef>
#include <span>
#include <vector>

namespace rta::ir::detail {

constexpr double kTiny = 2.2250738585072014e-308;  // std::numeric_limits<double>::min()

[[nodiscard]] double powerToDb(double p) noexcept;

/// Mean of `x` over blocks of `window` samples, with the block centres.
///
/// Returns false when fewer than two whole blocks fit -- a regression through
/// one point is not a slope, and returning a slope of zero would read as "no
/// decay" rather than "not enough data", which are different answers.
[[nodiscard]] bool blockAverage(std::span<const double> x, std::size_t window,
                                std::vector<double>& levels,
                                std::vector<double>& centres);

/// Least-squares line through (x, y). Returns false if x has no spread.
[[nodiscard]] bool leastSquares(std::span<const double> x, std::span<const double> y,
                                double& slope, double& intercept);

struct Crossing {
    std::size_t index = 0;
    double noisePower = 0.0;
    double slopeDbPerSample = 0.0;
};

/// Lundeby's iterative search for where the decay meets the noise floor.
///
/// Derived from the algorithm's description rather than transcribed from an
/// implementation, so that every constant in `DecayConfig` is a decision this
/// project can defend rather than an accident it inherited. The best-known open
/// implementation was consulted afterwards, as a check -- and a public issue
/// against it turned out to describe a state its `main` had already left, which
/// is its own lesson: a bug report has a date, and code must be read at a
/// commit.
///
/// Returns false rather than inventing a crossing. A truncation point made up
/// for a response that never decayed is the failure this whole lane exists to
/// avoid.
[[nodiscard]] bool findCrossing(std::span<const double> squared, double sampleRate,
                                const DecayConfig& cfg, Crossing& out);

}  // namespace rta::ir::detail
