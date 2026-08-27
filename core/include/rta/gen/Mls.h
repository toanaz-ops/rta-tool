// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rta::gen {

/// Maximum-length sequence (MLS): a Galois-form linear-feedback shift
/// register whose output cycles through every one of the `2^order - 1`
/// nonzero states of an `order`-bit register exactly once before repeating.
/// That exhaustive, non-repeating visitation is what gives an m-sequence its
/// defining property -- a circular autocorrelation that is a near-perfect
/// impulse (see test 28) -- which is why it is usable as an impulse-response
/// excitation signal (periodic-averaging workflows; the Farina sweep in
/// gen/Sweep.h is preferred where nonlinearity separation matters, per
/// docs/dsp/2026-08-27-generator.md).
///
/// **Peak-referenced**, like every other deterministic source in this
/// library (dual sine, sweep): an MLS sample is always exactly `+A` or `-A`,
/// so peak and RMS coincide up to that fixed `A` and there is no crest
/// factor to hide behind an RMS convention.
///
/// Tap polynomials are the Xilinx XAPP052 primitive-polynomial table -- see
/// Mls.cpp for the derivation of each `taps()` value and why it is the same
/// table `scipy.signal.max_len_seq` draws its own (Fibonacci-form) defaults
/// from at these orders.
class Mls {
public:
    static constexpr int kMinOrder = 15;
    static constexpr int kMaxOrder = 18;
    static constexpr int kDefaultOrder = 17;

    /// @param order          register length in bits, 15..18 inclusive --
    ///                       the range this project ships golden vectors
    ///                       for. Throws std::invalid_argument outside it.
    /// @param levelDbFsPeak  output amplitude in dBFS: A = 10^(levelDbFsPeak/20).
    explicit Mls(int order = kDefaultOrder, double levelDbFsPeak = -6.0);

    [[nodiscard]] int order() const noexcept { return order_; }

    /// `(1 << order) - 1`: the count of nonzero states of an `order`-bit
    /// register, and therefore the exact period of the sequence.
    [[nodiscard]] std::size_t period() const noexcept { return period_; }

    /// The Galois feedback mask actually in use for this instance's order:
    /// bit `(order - 1)` (the top bit, always present) OR'd with one bit per
    /// extra term of the primitive polynomial below that. See Mls.cpp.
    [[nodiscard]] std::uint32_t taps() const noexcept { return tapMask_; }

    /// One sample: `+A` or `-A`. Real-time safe: no allocation, and the only
    /// branch is on the register's own bit that just shifted out.
    float nextSample() noexcept;

    /// Fills `out` sample by sample via nextSample(). Real-time safe.
    void process(std::span<float> out) noexcept;

    /// Resets the register to 1. NEVER 0: an all-zero Galois LFSR state is a
    /// fixed point -- `0 >> 1 == 0`, the shifted-out bit is 0, so the XOR
    /// with `tapMask_` never fires -- so a register that ever reached 0 would
    /// output a silent, permanently-zero, non-maximal stream forever. This is
    /// the single most likely way this class gets misused by a caller who
    /// zero-initialises state by hand instead of calling restart() or the
    /// constructor.
    void restart() noexcept;

    /// Test-only: exposes the raw register so test_generator_mls.cpp can
    /// verify the period directly (case 26) instead of inferring it from the
    /// output stream. Not part of the class's intended runtime contract --
    /// callers outside the test suite should never need this.
    [[nodiscard]] std::uint32_t debugState() const noexcept { return state_; }

private:
    int order_;
    std::size_t period_;
    std::uint32_t tapMask_;
    float amplitude_;
    std::uint32_t state_ = 1u;
};

}  // namespace rta::gen
