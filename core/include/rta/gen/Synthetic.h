// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rta::gen {

/// `SyntheticPink` and `SyntheticSine` are deterministic TEST feeds, not the
/// real-time product generator.
///
/// They exist so the whole analysis chain -- the FFT, the band summation, the
/// level meter, the RTA view -- can be exercised and screenshotted with no
/// sound card, no microphone, and a bit-identical result on every run. That is
/// what makes `rtatool_snapshot`'s `rta-view.png` reproducible and what lets
/// `rtatool_analysis_tests` run on CI.
///
/// A DIFFERENT module, arriving with the generator implementation plan
/// (`core/.../gen/{Prng,Oscillator,Noise,Sweep,Mls}`), is the real-time
/// product generator an operator plays through a loudspeaker during a show:
/// Kellet-style pink noise that never audibly repeats, phase-continuous
/// sweeps, MLS. That generator optimises for "sounds right, forever"; this one
/// optimises for "the exact same bytes every time, offline, on CI". Neither
/// replaces the other -- do not reach for the block-looped pink here when a
/// show needs output that never repeats, and do not reach for the product
/// generator when a test needs a result it can diff.

/// Frequency-domain pink noise: exactly -3.0103 dB per octave by construction,
/// not by a filter approximation.
///
/// One block of `blockSize` samples is built once, in the constructor: a
/// spectrum with magnitude `|X(k)| = k^-1/2` (DC forced to zero -- pink noise
/// has no defined level at zero hertz) and a phase drawn per bin from the
/// seeded PRNG, inverted with a single `RealFft::inverse`. Every `render()`
/// call after that just copies out of the block and loops, so the slope is
/// exact in every sample this class ever produces -- it is not a statistical
/// property that only shows up after enough averaging.
///
/// The PRNG is `std::mt19937`, seeded explicitly, mapped to `[0,1)` by hand --
/// never `std::uniform_real_distribution`, which the standard leaves
/// implementation-defined. That is what makes two instances built with the
/// same seed produce bit-identical output on MSVC and on libstdc++ alike.
class SyntheticPink {
public:
    /// @param blockSize  loop length in samples; power of two, >= 4 (it is the
    ///                   size of the one `RealFft` this class ever runs).
    /// @param levelDbFs  target level, dBFS per the project convention: a
    ///                   full-scale SINE reads 0.0 dB, i.e.
    ///                   `10*log10(power) + 3.0103`. The block is normalised
    ///                   to this RMS.
    /// @param seed       `std::mt19937` seed; the same seed always produces
    ///                   the same block.
    SyntheticPink(std::size_t blockSize, double levelDbFs, std::uint32_t seed);

    /// Fills `out` from the internal block, wrapping around it as many times
    /// as needed. Continues from wherever the last call (or the constructor)
    /// left off, so callers may pass any block size, in any sequence, without
    /// shifting the loop point.
    void render(std::span<float> out) noexcept;

    /// Rewinds to the start of the block. Does NOT re-draw the random phases:
    /// the block is fixed at construction, so a reset instance repeats
    /// exactly what it played from the beginning.
    void reset() noexcept;

    [[nodiscard]] std::size_t blockSize() const noexcept { return block_.size(); }

private:
    std::vector<float> block_;
    std::size_t position_ = 0;
};

/// An exact sine at a requested frequency and level, phase-continuous across
/// any sequence of `render()` calls.
///
/// The phase accumulates in `double` and is never reset per block. Resetting
/// it per block would put an audible -- and, worse, spectrally visible --
/// discontinuity at every buffer boundary; that is the exact bug this class's
/// contract exists to make impossible to reintroduce by accident.
class SyntheticSine {
public:
    /// @param sampleRateHz  samples per second; > 0.
    /// @param frequencyHz   tone frequency; > 0 and < sampleRateHz / 2.
    /// @param levelDbFs     target level, dBFS per the project convention
    ///                      (see `SyntheticPink`); 0.0 dB is a unit-amplitude
    ///                      sine, matching every analyser on a rack.
    SyntheticSine(double sampleRateHz, double frequencyHz, double levelDbFs);

    /// Continues the phase from wherever the last call left it.
    void render(std::span<float> out) noexcept;

    /// Rewinds the phase to zero. Frequency and level are unchanged.
    void reset() noexcept;

private:
    double phaseIncrement_;  ///< radians per sample
    double phase_ = 0.0;
    double amplitude_;
};

}  // namespace rta::gen
