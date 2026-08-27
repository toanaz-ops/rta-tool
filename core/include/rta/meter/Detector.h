// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <span>
#include <string_view>

namespace rta::meter {

/// IEC 61672-1 clause 5 time weightings, plus the legacy Impulse.
enum class TimeWeighting {
    Fast,     ///< tau = 125 ms
    Slow,     ///< tau = 1 s
    Impulse,  ///< APPROXIMATION: 35 ms rise / 1.5 s decay. See below.
};

[[nodiscard]] std::string_view toString(TimeWeighting weighting) noexcept;

/// The finite floor returned by levelDb() for a (near-)zero mean square,
/// instead of -inf: far below any real measurement, but arithmetic-safe --
/// an Lmin that has seen silence should not poison every later computation.
inline constexpr double kLevelFloorDb = -200.0;

/// Exponential mean-square detector.
///
/// y[n] = y[n-1] + alpha * (x[n]^2 - y[n-1]),  alpha = 1 - exp(-1/(fs*tau))
///
/// The state is a MEAN SQUARE, not an amplitude and not a dB value. That is
/// what makes the closed forms in test_detector.cpp exact: the step response
/// is 1 - exp(-t/tau) in the state itself, so 10*log10 of it is the level,
/// and a decay is a straight line in dB with slope 10*log10(e)/tau.
///
/// Impulse is NOT the IEC quasi-peak rectifier followed by a 1.5 s decay; it
/// is the two-time-constant asymmetric approximation of that historical
/// circuit, and is labelled an approximation because none of the surveyed
/// implementations reproduce the original either. It is outside the current
/// IEC normative scope -- no conformance claim is made for it.
class Detector {
public:
    Detector(TimeWeighting weighting, double sampleRate);

    /// Seeds the state. The DEFAULT IS ZERO and that is deliberate: the step
    /// response 1 - exp(-t/tau) is only the closed form if the detector
    /// starts from silence. A caller that wants to prime the detector --
    /// resuming a measurement, say -- passes the mean square explicitly.
    void reset(double initialMeanSquare = 0.0) noexcept;

    /// Advances the state by one sample and returns the new mean square.
    double processSample(float x) noexcept;

    /// Advances the state over `in` and returns the mean square after the
    /// last sample. Identical arithmetic to a per-sample loop -- no
    /// decimation, no shortcut (test D4 asserts bit equality against one).
    double process(std::span<const float> in) noexcept;

    /// As above, but writes the per-sample mean-square trace to
    /// `outMeanSquare` (sizes must match; extra output samples are
    /// untouched).
    void process(std::span<const float> in, std::span<double> outMeanSquare) noexcept;

    [[nodiscard]] double meanSquare() const noexcept;

    /// 10*log10(meanSquare) + referenceOffsetDb, floored at kLevelFloorDb
    /// (before the offset is added) so a zero state reads as a finite number.
    [[nodiscard]] double levelDb(double referenceOffsetDb = 0.0) const noexcept;

    /// Seconds. Equal to decayTimeConstant() for Fast and Slow; smaller for
    /// Impulse, which is where the asymmetry lives.
    [[nodiscard]] static double riseTimeConstant(TimeWeighting weighting) noexcept;
    [[nodiscard]] static double decayTimeConstant(TimeWeighting weighting) noexcept;

    [[nodiscard]] double sampleRate() const noexcept;
    [[nodiscard]] TimeWeighting weighting() const noexcept;

private:
    TimeWeighting weighting_;
    double sampleRate_;
    double alphaRise_;
    double alphaDecay_;
    double meanSquare_ = 0.0;
};

}  // namespace rta::meter
