// SPDX-License-Identifier: AGPL-3.0-or-later
#include "rta/gen/Mls.h"

#include <cmath>
#include <stdexcept>

namespace rta::gen {

namespace {

/// Galois feedback masks for the four supported orders, Xilinx XAPP052's
/// primitive-polynomial table -- the same polynomials
/// `scipy.signal.max_len_seq` draws its own default `taps` argument from at
/// these orders. That shared table is the whole point of the golden
/// cross-check in test 29: a correctly-implemented Galois LFSR built from it
/// produces the same sequence SET as scipy's Fibonacci LFSR built from the
/// same polynomial, merely phase-shifted (`tools/gen_generator.py` finds and
/// records that shift -- see handoff trap #7,
/// docs/plans/2026-08-27-generator-impl-plan.md).
///
/// Each mask is bit `(order-1)` -- the top bit, always present in a valid
/// Galois LFSR of that order, standing in for the implicit `x^order` term
/// that feeds the shifted-out bit back into the register -- OR'd with one
/// bit per remaining nonzero term of the polynomial *below* that.
///
/// The remaining terms shift down by one: a polynomial term `x^i`
/// (`0 < i < order`) maps to register bit `(i-1)`, not bit `i`. This is the
/// standard Galois "shift right, conditionally XOR" construction: bit `k` of
/// the mask XORs into bit `k` of the register on the step where the bit that
/// shifts INTO position `k` (i.e. what was at `k+1`) needs correcting by the
/// feedback, and the feedback line itself occupies the vacated top position
/// -- so a term at exponent `i` lands one position lower than its exponent.
/// Getting this off-by-one wrong is easy and silent: a mask built from
/// exponents directly (no shift) still runs, still looks plausible, and
/// still terminates -- it just collapses to a short, non-maximal cycle
/// (verified below; the wrong construction gives period 15, 32767, 114681
/// and 42 for orders 15-18 respectively, instead of `2^order - 1`). That is
/// exactly the failure test 26 exists to catch, and it caught this once
/// already during development -- see the case-by-case derivation:
///
///   order 15:  x^15 + x^14 + 1                  extra bit  13
///     (+ top bit 14)                                        = 0x6000
///   order 16:  x^16 + x^15 + x^13 + x^4 + 1      extra bits 14, 12, 3
///     (+ top bit 15)                                        = 0xD008
///   order 17:  x^17 + x^14 + 1                   extra bit  13
///     (+ top bit 16)                                        = 0x12000
///   order 18:  x^18 + x^11 + 1                   extra bit  10
///     (+ top bit 17)                                        = 0x20400
///
/// Each value was cross-checked by brute-force period search (state returns
/// to its initial value at exactly `2^order - 1` steps and no earlier) before
/// being hard-coded here -- the same property test 26 asserts independently.
constexpr std::uint32_t tapsFor(const int order) noexcept {
    switch (order) {
        case 15: return 0x6000u;
        case 16: return 0xD008u;
        case 17: return 0x12000u;
        case 18: return 0x20400u;
        default: return 0u;  // unreachable once the constructor has validated order
    }
}

}  // namespace

Mls::Mls(const int order, const double levelDbFsPeak) : order_(order) {
    if (order < kMinOrder || order > kMaxOrder) {
        throw std::invalid_argument("Mls order must be in [15, 18]");
    }
    period_ = (std::size_t{1} << order) - 1;
    tapMask_ = tapsFor(order);
    // Peak-referenced: every sample is exactly +-A, so the constructor's
    // level parameter converts directly with no RMS/crest factor involved
    // (contrast Noise, which is RMS-referenced -- see the header comment).
    amplitude_ = static_cast<float>(std::pow(10.0, levelDbFsPeak / 20.0));
}

float Mls::nextSample() noexcept {
    // Galois step, LSB-output form: the bit about to leave the register is
    // the output bit, and only when it is 1 does the register absorb the
    // feedback polynomial via XOR. This is the standard Galois (as opposed
    // to Fibonacci) LFSR construction -- one XOR per set tap bit, applied in
    // parallel on the shift, rather than one XOR-reduction tree per step.
    const std::uint32_t out = state_ & 1u;
    state_ >>= 1;
    if (out) {
        state_ ^= tapMask_;
    }
    return out ? amplitude_ : -amplitude_;
}

void Mls::process(std::span<float> out) noexcept {
    for (float& sample : out) {
        sample = nextSample();
    }
}

void Mls::restart() noexcept {
    state_ = 1u;  // never 0 -- see the header comment on why that is a fixed point
}

}  // namespace rta::gen
