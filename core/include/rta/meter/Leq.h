// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/meter/Detector.h"

#include <cstddef>
#include <span>
#include <vector>

namespace rta::meter {

/// Ln percentile over a level history, PROJECT CONVENTION -- Ln does not
/// appear in IEC 61672-1 and implementations differ in sampling interval and
/// interpolation. This project's choice (recorded in
/// docs/dsp/2026-08-27-weighting-and-meters.md): the (1 - n/100) quantile of
/// the ascending-sorted levels, linear interpolation between order statistics
/// (numpy's default 'linear' method):
///
///     pos = (count - 1) * (1 - n/100)
///     Ln  = v[floor(pos)] + frac(pos) * (v[floor(pos)+1] - v[floor(pos)])
///
/// Ln is "the level exceeded n% of the time", so L10 is a HIGH number and L90
/// a LOW one -- pos grows as n shrinks. Sorts a local copy, so the result does
/// not depend on the caller's input order.
///
/// @throws std::invalid_argument if levelsDb is empty, or n is outside [0,100].
[[nodiscard]] double percentileLevelDb(std::span<const double> levelsDb, double n);

/// Accumulates Leq, SEL, sampled peak, and the Ln family (Lmax/Lmin/percentile)
/// over a stream of samples.
///
/// Leq = 10*log10(mean(p^2)) + referenceOffsetDb -- the closed-form energy
/// average, IEC 61672-1's own definition. SEL folds a variable-duration
/// measurement onto the fixed T0 = 1 s reference so it reads as a single
/// number rather than a rate. Peak is the SAMPLED peak (10*log10(max(p^2)));
/// it is NOT true peak -- the inter-sample maximum of a band-limited signal
/// can exceed it, and the 4x-interpolated true-peak meter is a later commit
/// (docs/dsp/2026-08-27-weighting-and-meters.md).
///
/// Lmax/Lmin/Ln all come from the SAME source: an internal time-weighted
/// Detector, sampled every 100 ms into a level history. They do not come from
/// raw samples -- a raw-sample percentile of a bursty signal reads many dB
/// low, because it is dominated by whatever is loudest OFTEN, not whatever a
/// human would call "the level" at any instant. Sampling the detector output
/// is what makes Lmax, Lmin and Ln consistent with each other.
class Leq {
public:
    /// @param sampleRate       hertz; must be > 0 (throws via the internal
    ///                         Detector otherwise)
    /// @param referenceOffsetDb  added to every dB reading; the calibration
    ///                         layer's job, not this class's -- Leq just adds
    ///                         the number it is given.
    /// @param timeWeighting    drives Lmax/Lmin and the Ln history. The
    ///                         project Ln convention is Fast; a different
    ///                         choice leaves Leq/SEL/peak valid but makes the
    ///                         Ln results non-conforming to that convention.
    explicit Leq(double sampleRate, double referenceOffsetDb = 0.0,
                 TimeWeighting timeWeighting = TimeWeighting::Fast);

    /// Returns to the state of a freshly-constructed Leq: no samples, no
    /// history, the internal Detector's mean square back to zero.
    void reset() noexcept;

    /// Feeds `in` through the energy accumulator, the sampled-peak tracker,
    /// and the internal Detector (which appends to the level history every
    /// historyIntervalSamples() samples).
    void process(std::span<const float> in) noexcept;

    /// 10*log10(mean(p^2)) + referenceOffsetDb. Exactly kLevelFloorDb (no
    /// offset) when sampleCount() == 0 -- "no data yet" reads differently
    /// from "measured silence", which floors to kLevelFloorDb + offset.
    [[nodiscard]] double leqDb() const noexcept;

    /// leqDb() + 10*log10(T / 1s), T = sampleCount()/sampleRate. The T0 = 1 s
    /// reference is what makes SEL a *number*, not a *rate*: for T < 1 s this
    /// SUBTRACTS from leqDb (a 0.1 s measurement reads leqDb - 10 dB).
    [[nodiscard]] double selDb() const noexcept;

    /// 10*log10(max(p^2)) + referenceOffsetDb -- the SQUARED form, tracked as
    /// max(x*x) rather than max(x). This is SAMPLED peak, not true peak (see
    /// the class comment).
    [[nodiscard]] double peakDb() const noexcept;

    /// max/min of the time-weighted level history (see the class comment) --
    /// NOT of raw samples.
    [[nodiscard]] double maxDb() const noexcept;
    [[nodiscard]] double minDb() const noexcept;

    /// Ln: percentileLevelDb() over a sorted copy of the level history.
    /// @throws std::invalid_argument if n is outside [0,100] or the history
    ///         is empty -- an empty history silently returning 0.0 dB would
    ///         read as a plausible measurement.
    [[nodiscard]] double percentileDb(double n) const;

    [[nodiscard]] double elapsedSeconds() const noexcept;
    [[nodiscard]] std::size_t sampleCount() const noexcept;

    /// The accumulated energy, sum(p^2), UNSCALED and with no offset applied:
    /// leqDb() is 10*log10(sumSquares()/sampleCount()) + referenceOffsetDb,
    /// bitwise (test_window_energy.cpp B1).
    ///
    /// It sits beside leqDb() for the reason record
    /// docs/dsp/2026-09-16-spl-pro-l6a.md section 10 puts sumSquares in the
    /// log file beside the rounded dB: a caller holding only a logarithm has
    /// to invert it before it can combine two measurements, and a reader
    /// holding only the rounded dB cannot reproduce the report's own numbers
    /// at all. Nothing is derived from this that leqDb() does not already
    /// carry -- it is the same accumulator, readable.
    [[nodiscard]] double sumSquares() const noexcept;

    /// The raw time-weighted level history, in chronological order (dB,
    /// already includes referenceOffsetDb). One entry every
    /// historyIntervalSamples() samples.
    [[nodiscard]] std::span<const double> levelHistory() const noexcept;

    /// std::lround(0.1 * sampleRate) -- 100 ms in samples, computed once at
    /// construction. 4800 at 48 kHz, 4410 at 44.1 kHz.
    [[nodiscard]] std::size_t historyIntervalSamples() const noexcept;

private:
    double sampleRate_;
    double referenceOffsetDb_;
    std::size_t historyIntervalSamples_;

    double sumSquares_ = 0.0;   ///< accumulated in double -- see plan 11.7
    double maxSquare_ = 0.0;    ///< max(x*x), never max(x) -- see plan 11.2
    std::size_t count_ = 0;

    Detector detector_;
    std::vector<double> history_;
};

}  // namespace rta::meter
