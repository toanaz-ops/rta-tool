// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests. The impairments that make the hardware-free
// path produce a transfer function worth looking at.
#include "measure/SyntheticImpairment.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
