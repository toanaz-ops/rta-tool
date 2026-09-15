// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The Butterworth HP/LP phase identity of the L7-ALIGN record Sec.3, locked in
// CI ahead of the wizard that depends on it.
//
// Why this file exists early. `docs/HUMAN-QA-QUEUE.md` carried an open item
// ("Order-4 mau thuan, ALIGN Sec.13.1"): the closed form predicts the CORRECT
// sign at a 4th-order crossover, while lane L4a MEASURED the wrong sign at
// orders 2 AND 4. The probe `tools/probe_align_order4.py` settled the
// attribution -- the identity is right and the L4a fixture was a pair of
// BAND-PASS boxes correlated under an UN-WHITENED peak-sign rule, not a
// matched-cutoff pair and not the shipped PHAT correlator (which fails at a
// different order on the same pair) -- and
// `docs/research/2026-09-15-l7-align-order4-probe.md` records it. This test is
// the C++ half of that settlement, so the next session cannot reopen the
// question from the code side. It is ALIGN Sec.10 item 2's first half, built
// now rather than in Wave 3; the Wave 3 test file folds it in rather than
// re-deriving it.
//
// Why the prototype is built here instead of called from `core/`. `rta::dsp::
// ButterworthDesign` offers only `bandPass` (ButterworthDesign.h:41-52) -- this
// repo has no low-pass/high-pass design to call, and adding one is Wave 3's
// job, not a probe's. The analog prototype is a closed form with no design
// choices in it, so writing it in the fixture costs nothing and asserts against
// a textbook rather than against an implementation:
//
//     poles  p_k = exp(j*pi*(2k + N - 1) / (2N)),  k = 1..N
//     LP(s)  = prod_k (-p_k) / prod_k (s - p_k)      -- LP(0) = 1
//     HP(s)  = s^N          / prod_k (s - p_k)       -- HP(inf) = 1
//     HP/LP  = s^N / prod_k (-p_k) = s^N             (prod_k (-p_k) = 1 for
//                                                     a normalised Butterworth)
//
// so on s = j*omega the ratio is (j)^N * omega^N: a POSITIVE real multiple of
// j^N, whose argument is N*90 degrees at every omega. No measurement, no
// tolerance argued from a grid.
//
// This is a closed-form assertion under CLAUDE.md's verification standard
// (preference 1), not a regression lock.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Complex = std::complex<double>;

namespace {

/// Normalised (wc = 1 rad/s) Butterworth pole set, order N.
std::vector<Complex> butterworthPoles(const int order) {
    std::vector<Complex> poles;
    poles.reserve(static_cast<std::size_t>(order));
    for (int k = 1; k <= order; ++k) {
        const double theta = std::numbers::pi
                           * static_cast<double>(2 * k + order - 1)
                           / static_cast<double>(2 * order);
        poles.emplace_back(std::cos(theta), std::sin(theta));
    }
    return poles;
}

Complex denominator(const std::vector<Complex>& poles, const Complex s) {
    Complex d{1.0, 0.0};
    for (const auto& p : poles) d *= (s - p);
    return d;
}

/// prod_k (-p_k): the numerator constant that makes LP(0) = 1. It is exactly
/// 1 for a normalised Butterworth, which the first test below asserts rather
/// than assumes.
Complex lowPassGain(const std::vector<Complex>& poles) {
    Complex g{1.0, 0.0};
    for (const auto& p : poles) g *= -p;
    return g;
}

Complex lowPass(const std::vector<Complex>& poles, const Complex s) {
    return lowPassGain(poles) / denominator(poles, s);
}

Complex highPass(const std::vector<Complex>& poles, const int order, const Complex s) {
    return std::pow(s, order) / denominator(poles, s);
}

/// Wrap to (-180, 180].
double wrapDegrees(double d) {
    d = std::fmod(d + 180.0, 360.0);
    if (d <= 0.0) d += 360.0;
    return d - 180.0;
}

constexpr int kMaxOrder = 8;

}  // namespace

TEST_CASE("a normalised Butterworth's pole product is exactly 1", "[align][butterworth]") {
    // The step that turns HP/LP into a bare s^N. If this drifts, the identity
    // below is still true but its derivation in the header comment is not.
    for (int n = 1; n <= kMaxOrder; ++n) {
        const auto g = lowPassGain(butterworthPoles(n));
        CHECK_THAT(g.real(), WithinAbs(1.0, 1e-12));
        CHECK_THAT(g.imag(), WithinAbs(0.0, 1e-12));
    }
}

TEST_CASE("Butterworth HP leads LP by N*90 degrees at EVERY frequency",
          "[align][butterworth]") {
    // ALIGN record Sec.3. The wizard's whole topology -> offset table is this
    // one line evaluated, so a fourth row for a new ORDER is arithmetic.
    for (int n = 1; n <= kMaxOrder; ++n) {
        const auto poles = butterworthPoles(n);
        const double expected = wrapDegrees(90.0 * n);

        double worst = 0.0;
        for (int i = 0; i < 64; ++i) {
            // 0.01 .. 100 rad/s, four decades about the cutoff.
            const double w = std::pow(10.0, -2.0 + 4.0 * i / 63.0);
            const Complex s{0.0, w};
            const double measured =
                std::arg(highPass(poles, n, s) / lowPass(poles, s)) * 180.0 / std::numbers::pi;
            worst = std::max(worst, std::abs(wrapDegrees(measured - expected)));
        }
        INFO("order " << n << ", expected " << expected << " deg");
        CHECK(worst < 1e-9);
    }
}

TEST_CASE("Linkwitz-Riley inherits the identity with the SAME N", "[align][butterworth]") {
    // LR-N is the Butterworth of order N/2 cascaded with itself (Linkwitz
    // 1976), so both sides square and the offset doubles: 2 * (N/2) * 90.
    //
    // The squared reading ALONE is a weak assertion and the half-order one
    // beside it is not decoration. Squaring maps an offset x to 2x, and 2x mod
    // 360 is unchanged when x flips sign at +-90 -- so a mutation that reverses
    // the half-order Butterworth's sign (rotating the prototype poles) leaves
    // every LR row below reading exactly the same number. Checking the
    // UN-squared half-order offset first is what makes such a mutation red
    // here rather than only in the test case above.
    for (const int n : {2, 4, 8}) {
        const auto poles = butterworthPoles(n / 2);
        const double expected = wrapDegrees(90.0 * n);
        const double halfExpected = wrapDegrees(90.0 * (n / 2));
        for (int i = 0; i < 32; ++i) {
            const double w = std::pow(10.0, -2.0 + 4.0 * i / 31.0);
            const Complex s{0.0, w};
            const Complex lp = lowPass(poles, s);
            const Complex hp = highPass(poles, n / 2, s);
            const double half =
                std::arg(hp / lp) * 180.0 / std::numbers::pi;
            const double measured =
                std::arg((hp * hp) / (lp * lp)) * 180.0 / std::numbers::pi;
            INFO("LR" << n << " at w = " << w);
            CHECK(std::abs(wrapDegrees(half - halfExpected)) < 1e-9);
            CHECK(std::abs(wrapDegrees(measured - expected)) < 1e-9);
        }
    }
}

TEST_CASE("the order-4 crossover sums IN PHASE and the order-2 one nulls",
          "[align][butterworth]") {
    // The row the open question turned on. At s = j (the cutoff) each output is
    // -3.0103 dB; the sum is +3.0103 dB when they are in phase and an exact
    // null when they are 180 degrees apart. This is the sign the ALIGN wizard
    // predicts at order 4, and it is POSITIVE.
    const Complex s{0.0, 1.0};
    constexpr double kThreeDb = 3.0102999566398120;

    struct Row { int order; bool inPhase; };
    for (const Row row : {Row{1, false}, Row{2, false}, Row{3, false},
                          Row{4, true},  Row{6, false}, Row{8, true}}) {
        const auto poles = butterworthPoles(row.order);
        const Complex lp = lowPass(poles, s);
        const Complex hp = highPass(poles, row.order, s);

        CHECK_THAT(20.0 * std::log10(std::abs(lp)), WithinAbs(-kThreeDb, 1e-12));
        CHECK_THAT(20.0 * std::log10(std::abs(hp)), WithinAbs(-kThreeDb, 1e-12));

        INFO("order " << row.order);
        if (row.inPhase) {
            // N = 4, 8: offset 0 degrees. Sum is +3 dB, difference is the null.
            CHECK_THAT(20.0 * std::log10(std::abs(lp + hp)), WithinAbs(kThreeDb, 1e-12));
            CHECK(std::abs(lp - hp) < 1e-15);
        } else if (row.order % 2 == 0) {
            // N = 2, 6: offset 180 degrees. The UN-inverted sum is the null and
            // the INVERTED sum is the +3 dB bump -- the way round that station-1
            // research D1/D6 had backwards (ALIGN Sec.1 correction 1).
            CHECK(std::abs(lp + hp) < 1e-15);
            CHECK_THAT(20.0 * std::log10(std::abs(lp - hp)), WithinAbs(kThreeDb, 1e-12));
        } else {
            // Odd N: quadrature, so the sum is all-pass -- 0 dB either polarity,
            // and no polarity choice can be read off the magnitude.
            CHECK_THAT(20.0 * std::log10(std::abs(lp + hp)), WithinAbs(0.0, 1e-12));
            CHECK_THAT(20.0 * std::log10(std::abs(lp - hp)), WithinAbs(0.0, 1e-12));
        }
    }
}

TEST_CASE("only an ODD-order crossover's reading can be flipped by a convention",
          "[align][butterworth]") {
    // Why the L4a order-4 finding cannot be blamed on a sign convention.
    // Conjugating the transfer function -- the one thing flipping
    // `Sxy = conj(X) * Y` (memory/dual-fft-conventions.md item 1) does -- negates
    // the offset. Modulo 360 that changes nothing at 0 and nothing at 180, so an
    // EVEN-order pair reads identically under either convention. Only +-90 moves.
    const Complex s{0.0, 1.0};
    for (int n = 1; n <= kMaxOrder; ++n) {
        const auto poles = butterworthPoles(n);
        const double offset =
            std::arg(highPass(poles, n, s) / lowPass(poles, s)) * 180.0 / std::numbers::pi;
        const double conjugated = -offset;
        const bool reachable = std::abs(wrapDegrees(offset - conjugated)) > 1e-9;
        INFO("order " << n << " offset " << offset);
        CHECK(reachable == (n % 2 == 1));
    }
}
