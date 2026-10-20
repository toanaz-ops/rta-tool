// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. The impairments that make the hardware-free
// path produce a transfer function worth looking at.
#include "dev-support/SyntheticRoomComb.h"
#include "measure/SyntheticImpairment.h"
#include "rta/dsp/ButterworthDesign.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

TEST_CASE("the delay line shifts by exactly the requested samples",
          "[synthetic-impairment]") {
    rta::measure::DelayLine line(3);
    const std::vector<float> in{ 1, 2, 3, 4, 5, 6, 7, 8 };
    std::vector<float> out(in.size());
    line.process(in, out);

    const std::vector<float> expected{ 0, 0, 0, 1, 2, 3, 4, 5 };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(out[i] == Catch::Approx(expected[i]));
    }
}

TEST_CASE("the delay line carries state across block boundaries",
          "[synthetic-impairment]") {
    // The failure this catches is a delay implemented per-block: it looks
    // perfect in a one-block test and re-inserts three zeros every buffer in
    // the real app, which is a click train, not a delay.
    rta::measure::DelayLine line(3);
    const std::vector<float> first{ 1, 2, 3, 4 };
    const std::vector<float> second{ 5, 6, 7, 8 };
    std::vector<float> out(4);

    line.process(first, out);
    CHECK(out[3] == Catch::Approx(1.0f));

    line.process(second, out);
    const std::vector<float> expected{ 2, 3, 4, 5 };
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CHECK(out[i] == Catch::Approx(expected[i]));
    }
}

TEST_CASE("a zero delay is a copy", "[synthetic-impairment]") {
    rta::measure::DelayLine line(0);
    const std::vector<float> in{ 1, 2, 3 };
    std::vector<float> out(3);
    line.process(in, out);
    for (std::size_t i = 0; i < in.size(); ++i) CHECK(out[i] == Catch::Approx(in[i]));
}

TEST_CASE("added noise lands within a few percent of the requested RMS",
          "[synthetic-impairment]") {
    // Closed form: the generator is uniform on [-1, 1), whose RMS is
    // 1/sqrt(3), so the scale factor is rms*sqrt(3). Over 200k samples the
    // sample RMS of a uniform generator is within well under 1% of the
    // population value, so a 3% margin is loose enough never to flake and
    // tight enough to fail a missing sqrt(3).
    std::vector<float> block(200000, 0.0f);
    std::uint32_t state = 0x1234u;
    rta::measure::addNoise(block, 0.05f, state);

    double sumSq = 0.0;
    for (const float v : block) sumSq += static_cast<double>(v) * v;
    const double rms = std::sqrt(sumSq / static_cast<double>(block.size()));
    CHECK(rms == Catch::Approx(0.05).epsilon(0.03));
}

TEST_CASE("noise is added to, never substituted for, the signal",
          "[synthetic-impairment]") {
    std::vector<float> block(1000, 1.0f);
    std::uint32_t state = 0x1234u;
    rta::measure::addNoise(block, 0.01f, state);

    double mean = 0.0;
    for (const float v : block) mean += v;
    mean /= static_cast<double>(block.size());
    CHECK(mean == Catch::Approx(1.0).margin(0.01));
}

TEST_CASE("the same seed gives the same noise", "[synthetic-impairment]") {
    // Determinism is what makes transfer.png reviewable as a byte-for-byte
    // diff, the same property makeSyntheticSnapshot already buys for
    // rta-view.png.
    std::vector<float> a(64, 0.0f), b(64, 0.0f);
    std::uint32_t sa = 0x99u, sb = 0x99u;
    rta::measure::addNoise(a, 0.1f, sa);
    rta::measure::addNoise(b, 0.1f, sb);
    for (std::size_t i = 0; i < a.size(); ++i) CHECK(a[i] == Catch::Approx(b[i]));
}

TEST_CASE("the measurement path turns an impulse into the cascade's own impulse response "
          "shifted by D, bit-exact",
          "[synthetic-impairment]") {
    // Sweep-IR lane T1. Expectation: closed form by construction -- the SAME
    // BiquadCascade run on an undelayed impulse, then shifted. Delay-then-
    // filter feeds the biquads exact zeros before the impulse arrives, so
    // their state stays 0.0 and every later float operation is the cascade's
    // own; `==` (not Approx) is therefore the correct comparison.
    constexpr int kDelay = 5;
    constexpr std::size_t kLen = 1024;
    const auto sections = rta::dsp::ButterworthDesign::bandPass(40.0, 16000.0, 48000.0, 2).sections;

    std::vector<float> impulse(kLen, 0.0f);
    impulse[0] = 1.0f;

    std::vector<float> ir(kLen);
    rta::dsp::BiquadCascade cascade(sections);
    cascade.process(impulse, ir);

    // Two blocks, to prove delay and filter state both carry across the seam.
    rta::measure::MeasurementPath path(kDelay, sections);
    std::vector<float> out(kLen);
    const std::size_t half = kLen / 2;
    path.process(std::span<const float>(impulse).first(half), std::span<float>(out).first(half));
    path.process(std::span<const float>(impulse).subspan(half), std::span<float>(out).subspan(half));

    for (std::size_t n = 0; n < kLen; ++n) {
        const float expected = n < static_cast<std::size_t>(kDelay)
                                   ? 0.0f
                                   : ir[n - static_cast<std::size_t>(kDelay)];
        INFO("sample " << n);
        REQUIRE(out[n] == expected);
    }
    // Not vacuous: the IR is non-trivial (the filter does something).
    REQUIRE(ir[0] != 0.0f);
    REQUIRE(ir[1] != ir[0]);
}

TEST_CASE("an empty filter leaves the measurement path a pure delay", "[synthetic-impairment]") {
    rta::measure::MeasurementPath path(3, {});
    const std::vector<float> in{1, 2, 3, 4, 5, 6};
    std::vector<float> out(in.size());
    path.process(in, out);
    const std::vector<float> expected{0, 0, 0, 1, 2, 3};
    REQUIRE(out == expected);
}

// --- RT60-UI lane T6: the feedback comb (decision 10) --------------------------

TEST_CASE("a comb with gain 0 is bit-identical to no comb, including a negative zero",
          "[synthetic-impairment]") {
    // `x + 0.0f` turns -0.0 into +0.0, so a comb that merely MULTIPLIES by zero is
    // not an identity. The input carries a -0.0 that reaches the comb with an
    // empty history; applying the comb at gain 0 would flip its sign bit.
    std::vector<float> in(3000);
    std::uint32_t state = 7u;
    for (auto& v : in) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        v = static_cast<float>(state) / 2147483648.0f - 1.0f;
    }
    in[0] = -0.0f;

    rta::measure::MeasurementPath plain(4, {});
    rta::measure::MeasurementPath inert(4, {}, 480, 0.0f);
    std::vector<float> a(in.size()), b(in.size());
    plain.process(in, a);
    inert.process(in, b);
    REQUIRE(std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
    CHECK(std::signbit(a[4]));  // the fixture really did carry the negative zero through
}

TEST_CASE("a comb turns an impulse into pulses at D0 + k*D of amplitude g^k and nothing else",
          "[synthetic-impairment]") {
    // Closed form: y[n] = x[n] + g*y[n-D] on an impulse is g^k at k*D and exactly
    // zero elsewhere. Behind the 4-sample path delay the first pulse is at 4.
    constexpr int kDelay = 480;
    constexpr int kPathDelay = 4;
    const float gain = static_cast<float>(rta::measure::roomCombGainFor(0.80, kDelay, 48000.0));
    CHECK(gain == Catch::Approx(0.91728f).margin(1e-5));  // 0.75 dB per trip

    std::vector<float> in(kPathDelay + kDelay * 20 + 100, 0.0f);
    in[0] = 1.0f;
    rta::measure::MeasurementPath whole(kPathDelay, {}, kDelay, gain);
    std::vector<float> out(in.size());
    whole.process(in, out);

    for (std::size_t n = 0; n < out.size(); ++n) {
        const long offset = static_cast<long>(n) - kPathDelay;
        if (offset >= 0 && offset % kDelay == 0) {
            const int k = static_cast<int>(offset / kDelay);
            const double expected = std::pow(static_cast<double>(gain), k);
            // k float products, each within half an ulp (6.0e-8 relative).
            INFO("pulse " << k << " at " << n << ": " << out[n] << " vs " << expected);
            CHECK(std::fabs(out[n] - expected) <= k * 6.0e-8 * expected + 1e-30);
        } else {
            INFO("sample " << n);
            CHECK(out[n] == 0.0f);
        }
    }

    // Blocks of 512 give the same bits: the state is the last D outputs, not a
    // per-block loop.
    rta::measure::MeasurementPath blocked(kPathDelay, {}, kDelay, gain);
    std::vector<float> chunked(in.size());
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        blocked.process(std::span<const float>(in).subspan(i, n), std::span<float>(chunked).subspan(i, n));
    }
    CHECK(std::memcmp(out.data(), chunked.data(), out.size() * sizeof(float)) == 0);
}

TEST_CASE("the comb gain for a chosen RT60 closes the loop: 60 dB in exactly that time",
          "[synthetic-impairment]") {
    // T = 3*D / (fs*log10(1/g)): the formula the T5 acceptance bounds are derived
    // from, run backwards from the gain it produced.
    for (const double t60 : {0.40, 0.80, 1.50}) {
        const double g = rta::measure::roomCombGainFor(t60, 480, 48000.0);
        CHECK(3.0 * 480.0 / (48000.0 * std::log10(1.0 / g)) == Catch::Approx(t60).epsilon(1e-12));
    }
    CHECK(20.0 * std::log10(1.0 / rta::measure::roomCombGainFor(0.80, 480, 48000.0)) ==
          Catch::Approx(0.75).epsilon(1e-12));
}
