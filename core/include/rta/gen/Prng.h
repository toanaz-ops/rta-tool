// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rta::gen {

/// PCG32 (`pcg_setseq_64_xsh_rr_32`), the 64-bit-state / 32-bit-output member
/// of the PCG family published by M.E. O'Neill, "PCG: A Family of Simple Fast
/// Space-Efficient Statistically Good Algorithms for Random Number
/// Generation" (2014), reference implementation `imneme/pcg-c`
/// (pcg-random.org). Transcribed directly from the published algorithm below
/// -- NOT copied from any other PCG32 implementation in this repo. The golden
/// fixture in core/tests/golden/generator.txt is only a meaningful check if
/// this transcription and the Python one in tools/gen_generator.py were
/// written independently from the same spec; see
/// docs/dsp/2026-08-27-generator.md.
///
/// Output permutation: XSH-RR (xorshift the high bits, then rotate by an
/// amount taken from the state itself). Two 64-bit words of state: `state_`
/// advances every step under a 64-bit LCG; `inc_` (forced odd) selects one of
/// 2^63 independent output *sequences* from that one LCG -- the "setseq" part
/// of the name, and the mechanism `ChannelSeeds` below relies on to give each
/// audio channel its own decorrelated stream.
///
/// Every member is noexcept and allocates nothing: this type is called once
/// per sample from a real-time audio callback.
class Pcg32 {
public:
    Pcg32(std::uint64_t seed, std::uint64_t sequence) noexcept { reseed(seed, sequence); }

    /// `pcg32_srandom_r`, verbatim. The two `step()` calls are not symmetric
    /// padding: the first mixes `sequence` into the LCG before `seed` is ever
    /// added, and the second folds `seed` through one more LCG step so the
    /// very first `nextUInt32()` is already well-mixed -- a seed of 0 does
    /// not produce a degenerate first output. Reordering this breaks every
    /// golden row: it is a different seeding function, not the same one
    /// evaluated differently.
    void reseed(std::uint64_t seed, std::uint64_t sequence) noexcept {
        state_ = 0u;
        inc_ = (sequence << 1u) | 1u;  // inc must be odd -- an LCG's increment
                                       // has to be coprime with its modulus
                                       // (2^64, from the wraparound of the
                                       // multiply-add below), or the stream
                                       // does not visit the full period.
        step();
        state_ += seed;
        step();
    }

    /// `pcg32_random_r`. The permutation is computed from `old`, the state
    /// value BEFORE this call's advance -- PCG's defining trick is that a
    /// step's output is a function of the state the LCG *had*, not the one it
    /// is left in. Getting this ordering backwards (permuting the
    /// post-advance state) is the single easiest bug to introduce here and
    /// produces a stream that looks statistically fine but matches no golden
    /// row.
    [[nodiscard]] std::uint32_t nextUInt32() noexcept {
        const std::uint64_t old = state_;
        step();
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        // `(0u - rot) & 31u` is `(-rot) & 31` from the published spec, spelled
        // to avoid MSVC C4146 (unary minus on an unsigned operand) -- the
        // wraparound is the same modular arithmetic either way.
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    /// Uniform sample in [-1, 1). Mapping is pinned bit-for-bit against the
    /// independent Python transcription in tools/gen_generator.py, which does
    /// `np.float32(x / 2**32 * 2 - 1)`: widen to double, scale, shift, then
    /// narrow to float exactly ONCE at the end. Any other order (int32
    /// reinterpretation, division by UINT32_MAX, or doing the arithmetic in
    /// float instead of double) rounds differently in the last bit or two and
    /// silently breaks the golden comparison, even though the statistics
    /// would still look correct.
    ///
    /// Analytic moments of this stream -- relied on by Noise.h's level
    /// scaling, so pinned here where the mapping is defined: Uniform[-1, 1)
    /// has mean 0 and variance (b-a)^2/12 = (2)^2/12 = 1/3, so
    /// RMS = sqrt(1/3) ~= 0.5773503, i.e. a raw (unscaled) uniform stream
    /// sits at 20*log10(sqrt(1/3)) ~= -4.771 dBFS RMS.
    [[nodiscard]] float nextUniform() noexcept {
        const std::uint32_t x = nextUInt32();
        return static_cast<float>(static_cast<double>(x) * 0x1p-32 * 2.0 - 1.0);
    }

private:
    void step() noexcept { state_ = state_ * 6364136223846793005ull + inc_; }

    std::uint64_t state_ = 0u;
    std::uint64_t inc_ = 1u;
};

/// SplitMix64 (Vigna 2015 / Steele, Lea & Flood 2014's `SplittableRandom`;
/// the constants below are the ones both the xoshiro/xoroshiro authors' own
/// reference code and the original Java `SplittableRandom` use for seeding a
/// *different* generator from one integer). Used ONLY to derive independent
/// per-channel PCG32 seeds from one master seed -- it never generates audio
/// itself. `state` is the caller-owned splitmix state, advanced in place so
/// repeated calls walk the same stream forward.
[[nodiscard]] constexpr std::uint64_t splitMix64(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30u)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27u)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31u);
}

/// One master seed -> N statistically independent channel generators,
/// reproducible from that single number (the point of picking SplitMix64 for
/// this: it is specifically designed to give well-mixed, decorrelated output
/// for consecutive inputs, which is exactly the "channel 0, 1, 2, ..." access
/// pattern here -- unlike, say, XORing the channel index into the master
/// seed, which can correlate low-order bits across nearby channels).
struct ChannelSeeds {
    [[nodiscard]] static Pcg32 forChannel(std::uint64_t master, std::size_t channel) noexcept {
        std::uint64_t mixer = master;
        std::uint64_t seed = 0u;
        std::uint64_t sequence = 0u;
        // Walk the SplitMix64 stream forward two draws per channel (one for
        // the PCG32 seed, one for its sequence selector) so channel N's
        // generator does not reuse any word channel N-1 already produced.
        for (std::size_t i = 0; i <= channel; ++i) {
            seed = splitMix64(mixer);
            sequence = splitMix64(mixer);
        }
        return Pcg32(seed, sequence);
    }
};

}  // namespace rta::gen
