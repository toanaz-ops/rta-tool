// SPDX-License-Identifier: AGPL-3.0-or-later
#include "measure/PhaseUnwrap.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace rta::measure;

TEST_CASE("a pure delay unwraps to a straight line", "[unwrap]") {
    // phi[k] = -2*pi*k*D/N wrapped. Unwrapped it must be that line again --
    // an exact closed form, so no tolerance-shopping is possible.
    constexpr std::size_t kBins = 513, kN = 1024;
    constexpr double kD = 7.0;
    std::vector<float> wrapped(kBins), out(kBins);
    for (std::size_t k = 0; k < kBins; ++k) {
        const double exact = -2.0 * std::numbers::pi * static_cast<double>(k) * kD / kN;
        wrapped[k] = static_cast<float>(std::remainder(exact, 2.0 * std::numbers::pi));
    }
    unwrapPhase(wrapped, out);
    for (std::size_t k = 0; k < kBins; ++k) {
        const double exact = -2.0 * std::numbers::pi * static_cast<double>(k) * kD / kN;
        REQUIRE(out[k] == Catch::Approx(exact).margin(1e-4));
    }
}

TEST_CASE("an already-continuous trace is unchanged", "[unwrap]") {
    std::vector<float> in{0.0f, 0.1f, 0.2f, 0.15f, -0.3f}, out(5);
    unwrapPhase(in, out);
    for (std::size_t i = 0; i < in.size(); ++i) REQUIRE(out[i] == Catch::Approx(in[i]));
}

TEST_CASE("low coherence stops a step propagating upward", "[unwrap][gate]") {
    // Without the gate, an ambiguous run of bins shifts EVERY bin above it
    // by 2*pi.
    //
    // DEVIATION FROM THE PLAN: the plan's draft put a SINGLE bad bin between
    // two identical good runs (wrapped[8] = 3.0, everything else 0). That
    // construction cannot demonstrate persistent propagation from ANY
    // correct unwrap, gated or not: the step INTO the bad bin (0 -> 3.0,
    // delta +3.0) and the step OUT of it (3.0 -> 0, delta -3.0) are exact
    // opposites, so a standard consecutive-delta unwrap applies equal and
    // opposite corrections and self-heals by the very next bin -- this is
    // true for ANY single isolated value between two equal neighbours, not
    // a bug to fix, so no amount of coherence gating changes it (there is
    // nothing left to prevent). Two adjacent bad bins whose OWN internal
    // step exceeds pi (2.0 to -2.0, delta -4.0) have no such symmetry: the
    // correction they cause has no counterpart to cancel it. That is what
    // this data uses, and it is the smallest change that keeps the test's
    // point (an ambiguous run must not poison everything above it) provable.
    constexpr std::size_t kBins = 16;
    std::vector<float> wrapped(kBins, 0.0f), coherence(kBins, 1.0f), out(kBins);
    wrapped[8] = 2.0f;
    wrapped[9] = -2.0f;
    coherence[8] = 0.05f;
    coherence[9] = 0.05f;
    UnwrapOptions options; options.minimumCoherence = 0.2f;

    unwrapPhase(wrapped, coherence, out, options);
    REQUIRE(out[15] == Catch::Approx(0.0f).margin(1e-5));

    // Prove the gate is what did it: the same data ungated must NOT be clean.
    std::vector<float> ungated(kBins);
    unwrapPhase(wrapped, ungated);
    REQUIRE(std::abs(ungated[15]) > 1.0f);
}

TEST_CASE("unwrap refuses mismatched spans", "[unwrap][edge]") {
    const std::vector<float> a(8, 0.0f), c(4, 1.0f);
    std::vector<float> out(8);
    REQUIRE_THROWS_AS(unwrapPhase(a, c, out, UnwrapOptions{}), std::invalid_argument);
    std::vector<float> shortOut(4);
    REQUIRE_THROWS_AS(unwrapPhase(a, shortOut), std::invalid_argument);
}

TEST_CASE("an untrusted bin 0 does not anchor the trace", "[unwrap][gate][edge]") {
    // F2: DC is exactly where coherence is worst in practice, so bin 0 must
    // NOT be exempt from the gate. Before the fix, `anchor` was seeded from
    // wrapped[0] unconditionally, so a garbage DC reading became the
    // reference every later bin was measured against -- a permanent 2*pi
    // step injected by one untrustworthy bin. These are the exact numbers
    // that exposed it: wrapped[0]=3.0 (untrusted), wrapped[1]=-2.0
    // (trusted) used to give out[15] = 2*pi via the poisoned anchor.
    constexpr std::size_t kBins = 16;
    std::vector<float> wrapped(kBins, 0.0f), coherence(kBins, 1.0f), out(kBins);
    wrapped[0] = 3.0f;
    wrapped[1] = -2.0f;
    coherence[0] = 0.05f;
    UnwrapOptions options; options.minimumCoherence = 0.2f;

    unwrapPhase(wrapped, coherence, out, options);
    // Bin 0 is untrusted and contributes no anchor: bin 1 becomes the first
    // anchor instead, and every bin from there on is flat, so nothing above
    // it should carry a spurious 2*pi.
    REQUIRE(out[15] == Catch::Approx(0.0f).margin(1e-5));
}

TEST_CASE("the gate resumes the running offset instead of resetting it",
          "[unwrap][gate]") {
    // F3: the header used to say a gated bin "restarts" the unwrap, which
    // reads as "reset to zero and begin again". The code has never done
    // that -- it holds whatever offset a genuine EARLIER wrap already
    // established and simply skips the bad bins when deciding whether the
    // NEXT good bin needs a new correction. A mutant that zeroes `offset`
    // at every gated bin passes every other test in this file (none of them
    // has a real wrap BEFORE the gated run to lose), so this constructs one:
    // bins 0-2 take one genuine wrap (offset becomes +2*pi), bins 3-4 are
    // gated garbage, and bin 5 returns to the SAME value as bin 2. If the
    // offset survives the gate, bin 5 reads back at wrapped[5] + 2*pi; if a
    // mutant reset it to zero, bin 5 would read back as its raw -3.2 instead.
    constexpr std::size_t kBins = 6;
    std::vector<float> wrapped(kBins, 0.0f), coherence(kBins, 1.0f), out(kBins);
    wrapped[0] = 0.0f;
    wrapped[1] = -3.2f;  // delta -3.2 < -pi: one genuine wrap, offset = +2*pi
    wrapped[2] = -3.2f;  // delta 0: settled, offset stays +2*pi
    wrapped[3] = 5.0f;   // gated garbage
    wrapped[4] = -1.0f;  // gated garbage
    wrapped[5] = -3.2f;  // back to the pre-gate value
    coherence[3] = 0.05f;
    coherence[4] = 0.05f;
    UnwrapOptions options; options.minimumCoherence = 0.2f;

    unwrapPhase(wrapped, coherence, out, options);

    const double expected = -3.2 + 2.0 * std::numbers::pi;
    REQUIRE(out[5] == Catch::Approx(static_cast<float>(expected)).margin(1e-4f));
}

TEST_CASE("coherence exactly at the threshold is gated", "[unwrap][gate][edge]") {
    // F4: the gate compares with <=, not <. A bin whose coherence exactly
    // EQUALS minimumCoherence has not cleared the floor and must still be
    // treated as untrustworthy. Reuses the internal-jump construction from
    // "low coherence stops a step propagating upward" (the only shape that
    // can actually distinguish gated from ungated on this kind of data),
    // with coherence set to EXACTLY the threshold instead of comfortably
    // below it.
    constexpr std::size_t kBins = 16;
    std::vector<float> wrapped(kBins, 0.0f), coherence(kBins, 1.0f), out(kBins);
    wrapped[8] = 2.0f;
    wrapped[9] = -2.0f;
    UnwrapOptions options; options.minimumCoherence = 0.2f;
    coherence[8] = options.minimumCoherence;
    coherence[9] = options.minimumCoherence;

    unwrapPhase(wrapped, coherence, out, options);
    REQUIRE(out[15] == Catch::Approx(0.0f).margin(1e-5));
}

TEST_CASE("minimumCoherence of exactly 0 disables the gate even at coherence 0",
          "[unwrap][gate][edge]") {
    // F4: 0 means "gate off", full stop -- even a bin whose OWN coherence
    // reads exactly 0 is trusted when the option itself is 0, because the
    // option is what turns gating on at all. Reuses the F2 numbers, but
    // with the gate left at its default (disabled): the result must match
    // what plain, ungated unwrapPhase would produce on the same data, i.e.
    // the poisoned-anchor value -- there being no gate to prevent it.
    constexpr std::size_t kBins = 16;
    std::vector<float> wrapped(kBins, 0.0f), coherence(kBins, 1.0f), out(kBins);
    wrapped[0] = 3.0f;
    wrapped[1] = -2.0f;
    coherence[0] = 0.0f;
    UnwrapOptions options;  // minimumCoherence defaults to 0.0f: gate off

    unwrapPhase(wrapped, coherence, out, options);
    const double expected = 2.0 * std::numbers::pi;
    REQUIRE(out[15] == Catch::Approx(static_cast<float>(expected)).margin(1e-4f));
}

TEST_CASE("every bin gated is a raw passthrough", "[unwrap][gate][edge]") {
    // No bin is ever trusted, so no anchor is ever established and the
    // running offset never leaves zero: the output must be the input,
    // unchanged, bin for bin.
    constexpr std::size_t kBins = 10;
    const std::vector<float> wrapped{0.0f, 1.5f, -2.9f, 3.1f, -0.4f,
                                     2.2f, -1.1f, 0.7f, -3.0f, 1.9f};
    const std::vector<float> coherence(kBins, 0.05f);
    std::vector<float> out(kBins);
    UnwrapOptions options; options.minimumCoherence = 0.2f;

    unwrapPhase(wrapped, coherence, out, options);
    for (std::size_t i = 0; i < kBins; ++i) {
        REQUIRE(out[i] == Catch::Approx(wrapped[i]));
    }
}
