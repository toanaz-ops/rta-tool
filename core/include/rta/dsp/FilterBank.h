// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include "rta/dsp/Biquad.h"
#include "rta/dsp/OctaveBands.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rta::dsp {

/// One IEC 61260 band-pass per OctaveBands band, single rate.
///
/// Single-rate, not a decimation cascade: measured at ~43 M multiply-adds/s for
/// the full 1/3-octave bank at 48 kHz, which is nothing on an analysis thread,
/// and it removes the per-band group-delay bookkeeping a decimating bank has to
/// manage. See docs/dsp/2026-08-27-filterbank.md.
///
/// Not thread-safe. One instance per analysis thread.
class FilterBank {
public:
    /// N = SOS sections = half the pole count. Six is the value the conformance
    /// analysis settled on; it passes every ANSI S1.11 Table B1 Class-1
    /// breakpoint with margin.
    static constexpr int kDefaultSections = 6;

    /// IEC 61672-1 clause 5 exponential detector time constants.
    static constexpr double kFastSeconds = 0.125;
    static constexpr double kSlowSeconds = 1.0;

    struct Config {
        int        fraction     = 3;
        double     lowestHz     = 20.0;
        double     highestHz    = 20000.0;
        double     sampleRate   = 48000.0;
        int        sections     = kDefaultSections;
        double     timeConstantSeconds = kFastSeconds;
        OctaveBase base         = OctaveBase::BaseTen;
    };

    struct Band {
        int    index = 0;            ///< the OctaveBands index, NOT the bank index
        double centre = 0.0;
        double lower = 0.0, upper = 0.0;   ///< as designed, after any clamp
        double maxPoleRadius = 0.0;
        bool   nyquistClamped = false;
    };

    explicit FilterBank(const Config& config);

    [[nodiscard]] const Config& config() const noexcept { return config_; }

    /// May be SMALLER than the OctaveBands table: bands whose centre reaches
    /// Nyquist are not constructed. Use Band::index to get back to the table.
    [[nodiscard]] std::size_t size() const noexcept { return bands_.size(); }
    [[nodiscard]] const Band& band(std::size_t i) const { return bands_.at(i); }
    [[nodiscard]] std::span<const Biquad::Coeffs> sections(std::size_t i) const {
        return cascades_.at(i).sections();
    }

    /// Allocation-free, lock-free, no I/O. Everything is sized in the ctor.
    void process(std::span<const float> samples) noexcept;

    /// Clears the accumulators (meanSquare(), smoothed(), sampleCount()) but
    /// deliberately leaves each cascade's own filter memory (s1, s2) alone.
    /// A caller settles a resonant band, then reset()s to discard the average
    /// that accumulated during that settling -- not the settling itself, which
    /// would reintroduce the very startup ring reset() was called to get away
    /// from. This is the mechanism the "settle, reset(), measure" pattern
    /// depends on for a settled reading rather than a diluted one; a live SPL
    /// meter never rewinds its own filter state on a display reset either.
    void reset() noexcept;

    [[nodiscard]] std::uint64_t sampleCount() const noexcept { return sampleCount_; }

    /// Mean square of each band's output over every sample since reset().
    /// Equal weight to every sample -- the settled measurement. reset()
    /// clears this accumulator but not the cascades' filter memory (see
    /// reset()), so "since reset()" bounds the averaging window only, not
    /// the filter state the samples in that window were produced from.
    [[nodiscard]] std::span<const float> meanSquare() const noexcept { return meanSquare_; }

    /// One-pole exponential mean square, time constant from the config.
    /// This is the IEC 61672-1 detector and the number a live display shows.
    [[nodiscard]] std::span<const float> smoothed() const noexcept { return smoothed_; }

    /// alpha = 1 - exp(-1/(fs*tau)). Public because the closed-form step and
    /// decay tests below are tests OF this expression.
    [[nodiscard]] static double detectorAlpha(double tau, double sampleRate);

private:
    Config                     config_;
    std::vector<Band>          bands_;
    std::vector<BiquadCascade> cascades_;

    double alpha_ = 0.0;
    std::uint64_t sampleCount_ = 0;

    std::vector<double> meanSquareAccum_;   ///< running mean, in double
    std::vector<double> smoothedAccum_;     ///< exponential mean, in double
    std::vector<float>  meanSquare_;
    std::vector<float>  smoothed_;
};

}  // namespace rta::dsp
