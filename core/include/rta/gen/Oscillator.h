// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <atomic>
#include <cstddef>
#include <span>

namespace rta::gen {

/// One tone: a double-precision phase accumulator kept in TURNS ([0,1)), not
/// radians. `sin(2*pi*phase)` is evaluated only at the point of use, so the
/// argument handed to `std::sin` never grows past roughly `2*pi` -- the
/// accumulator's own magnitude stays near 1 for the whole run. A radian
/// accumulator instead grows without bound over an hours-long show, and the
/// `double` mantissa loses bits to that growing integer part -- audible pitch
/// and phase drift is the real failure this design avoids.
///
/// Level follows this codebase's deterministic-generator convention (see
/// `rta::gen::SyntheticSine` in Synthetic.h): PEAK-referenced dBFS, so
/// `amplitude = 10^(levelDbFsPeak/20)` and 0 dBFS is a unit-amplitude sine.
class Oscillator {
public:
    Oscillator(double sampleRate, double frequencyHz, double levelDbFsPeak) noexcept;

    /// Phase-continuous: does NOT reset `phase_`. A reset here would put an
    /// audible, spectrally visible discontinuity at every frequency change --
    /// exactly the artefact a phase accumulator exists to avoid.
    void setFrequency(double hz) noexcept;
    void setLevelDbFsPeak(double db) noexcept;

    float nextSample() noexcept;
    void  process(std::span<float> out) noexcept;

    /// Exposed so a test can assert the closed-form identity directly instead
    /// of inferring phase from sample values.
    [[nodiscard]] double phaseTurns() const noexcept { return phase_; }
    [[nodiscard]] double amplitude() const noexcept { return amplitude_; }

private:
    double sampleRateHz_;
    double frequencyHz_;
    double phase_ = 0.0;   ///< turns, in [0,1)
    double amplitude_;
};

/// Two independent oscillators summed, peak-referenced on the SUM.
///
/// Each `Oscillator` inside carries `amplitude/2` of the requested peak, so
/// that when the two sines line up in phase (the worst-case, and inevitable
/// at some point for two incommensurate frequencies) the resulting peak
/// equals the requested `levelDbFsPeak` exactly. "Each tone at -20 dBFS" and
/// "the pair at -20 dBFS" differ by 20*log10(2) ~= 6.02 dB and both are
/// defensible defaults -- this class chooses the latter (peak of the SUM),
/// explicitly, so a caller reading "-20 dBFS" on a dual-sine test tone knows
/// it will not clip a 0 dBFS-ceilinged system.
class DualSine {
public:
    DualSine(double sampleRate, double f1Hz, double f2Hz, double levelDbFsPeak) noexcept;

    float nextSample() noexcept;
    void  process(std::span<float> out) noexcept;

private:
    Oscillator osc1_;
    Oscillator osc2_;
};

/// The click-free start/stop state machine for the generator's OUTPUT stage.
///
/// This is deliberately ONE ramp for the whole generator, not one per source
/// type (sine/noise/sweep/MLS). A single output-stage ramp also makes
/// *switching source* click-free -- muting the old source and unmuting the
/// new one both pass through this same gain -- which per-source ramps would
/// not achieve on their own. A later addition of Noise/Sweep/Mls ramps
/// wired independently of this one would silently reintroduce the click this
/// class exists to remove.
///
/// Shape: `g(p) = 0.5*(1 - cos(pi*p))`, `p` the ramp position in [0,1] --
/// exactly half a raised-cosine period, so `g` and `dg/dp` are both zero at
/// `p=0` and `g=1`, `dg/dp=0` at `p=1`. That C1 continuity at both ends (value
/// AND slope hit their target smoothly) is why no click appears at the start
/// or end of the ramp.
///
/// A single scalar `pos_` (samples along the ramp) is the ONLY position
/// state, and `g` is evaluated purely as a function of it. On a direction
/// reversal (`requestOff()` mid-rise, or `requestOn()` mid-fall) only the
/// direction future samples move `pos_` in changes -- `pos_` itself never
/// jumps -- so gain stays continuous through the reversal instead of leaping
/// back to a stale endpoint.
class RampedGain {
public:
    enum class State { Idle, Rising, Running, Falling };

    /// @param rampSeconds  5-20 ms is the valid design range (decision record
    ///                     docs/dsp/2026-08-27-generator.md); default 10 ms.
    explicit RampedGain(double sampleRate, double rampSeconds = 0.010) noexcept;

    void  requestOn()  noexcept;   ///< UI/control thread
    void  requestOff() noexcept;   ///< UI/control thread
    float nextGain()   noexcept;   ///< audio thread, once per sample
    void  apply(std::span<float> block) noexcept;

    /// Device thread, callback quiesced (L7-OUT record §5: `RampedGain` is
    /// neither copyable nor movable, so a rate change cannot rebuild it by
    /// assignment or `emplace` -- that would race the control thread's own
    /// `requestOn()`/`requestOff()`, the exact use-after-free shape
    /// `CaptureBus.h` closed for the capture side). Retargets ONLY the
    /// audio-thread-owned fields this rewrites -- `rampLenSamples_` (from the
    /// stored `rampSeconds_`, so a later rate change still honours the ctor's
    /// original ramp duration), `pos_` (back to 0) and `state_` (back to
    /// `Idle`) -- and NEVER `target_`: a `requestOn()`/`requestOff()` the
    /// control thread published before this call is still the value the next
    /// `nextGain()` rises or falls towards. Safe to call from the device
    /// thread with no extra synchronisation for the same reason
    /// `CaptureBus::prepare` is: the callback is quiesced first.
    void prepare(double sampleRate) noexcept;

    [[nodiscard]] State state() const noexcept { return state_; }

private:
    std::atomic<bool> target_{false};   ///< the ONLY word touched by both threads

    // Audio-thread-only state below: never touched from the control thread.
    int   rampLenSamples_;
    int   pos_ = 0;        ///< samples from Idle-end (0) towards Running-end (rampLenSamples_)
    State state_ = State::Idle;

    /// Set once at construction, read again by every later `prepare()` --
    /// the ctor computes `rampLenSamples_` from `sampleRate*rampSeconds_` and
    /// would otherwise discard `rampSeconds_`, leaving `prepare(sampleRate)`
    /// no way to recompute the length at the ORIGINAL ramp duration (OUT-R2).
    double rampSeconds_;
};

}  // namespace rta::gen
