// SPDX-License-Identifier: AGPL-3.0-or-later
//
// PCG32 + SplitMix64 (rta/gen/Prng.h). Golden case 1 depends on
// core/tests/golden/generator.txt, produced independently by
// tools/gen_generator.py's own from-spec Python transcription of PCG32 --
// see that header's top comment. Independence rests entirely on the two
// transcriptions being written separately from the published algorithm; if
// the golden file or the `pcg32_seed42_seq54` case is not present yet, that
// is a legitimate "waiting on the sibling station" state, not a bug here, and
// this file will report it as such rather than silently skipping.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/gen/Prng.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::gen::ChannelSeeds;
using rta::gen::Pcg32;

namespace {

/// Streaming Pearson correlation over paired samples -- avoids holding two
/// multi-hundred-thousand-element vectors just to prove two streams don't
/// covary.
double pearsonCorrelation(std::size_t count,
                           const std::function<void(std::size_t, double&, double&)>& sample) {
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0, sumY2 = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        double x = 0.0, y = 0.0;
        sample(i, x, y);
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
        sumY2 += y * y;
    }
    const double n = static_cast<double>(count);
    const double covariance = n * sumXY - sumX * sumY;
    const double denominator =
        std::sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));
    return covariance / denominator;
}

rta::test::GoldenCase prngGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/generator.txt");
    for (const auto& c : cases) {
        if (c.name == "pcg32_seed42_seq54") return c;
    }
    throw std::runtime_error("golden file has no 'pcg32_seed42_seq54' case yet -- "
                              "waiting on tools/gen_generator.py (station F)");
}

}  // namespace

TEST_CASE("PCG32 reproduces the golden stream for seed 42, sequence 54", "[generator][prng][golden]") {
    const auto testCase = prngGolden();

    // Two fresh streams from the same (seed, sequence): one golden row is
    // "the first 32 raw outputs", the other is "the first 16 nextUniform()
    // outputs" -- each counted from the start of its own stream, not
    // continuing where the other left off.
    const auto& u32Row = testCase.row("u32");
    Pcg32 rawStream(42u, 54u);
    for (std::size_t i = 0; i < u32Row.size(); ++i) {
        // Golden values round-trip exactly through double: every uint32 is
        // < 2^53. Bit-exact `==`, not a tolerance -- this is a stream
        // reproduction check, not a statistical one.
        REQUIRE(rawStream.nextUInt32() == static_cast<std::uint32_t>(u32Row[i]));
    }

    const auto& uniformRow = testCase.row("uniform");
    Pcg32 uniformStream(42u, 54u);
    for (std::size_t i = 0; i < uniformRow.size(); ++i) {
        REQUIRE(static_cast<double>(uniformStream.nextUniform()) == static_cast<double>(
            static_cast<float>(uniformRow[i])));
    }
}

TEST_CASE("PCG32 uniform output has the analytic mean and variance", "[generator][prng]") {
    // Any fixed (seed, sequence) works for a statistical check; picked away
    // from the golden's (42, 54) so this case never depends on the fixture.
    Pcg32 rng(0x1234'5678'9abc'defull, 7u);

    constexpr std::size_t kDraws = 1u << 20;
    double sum = 0.0, sumSq = 0.0;
    float lo = 1.0f, hi = -1.0f;
    for (std::size_t i = 0; i < kDraws; ++i) {
        const float x = rng.nextUniform();
        sum += x;
        sumSq += static_cast<double>(x) * x;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    const double n = static_cast<double>(kDraws);
    const double mean = sum / n;
    const double variance = sumSq / n - mean * mean;

    CHECK_THAT(mean, WithinAbs(0.0, 0.002));
    CHECK_THAT(variance, WithinAbs(1.0 / 3.0, 0.002));
    CHECK(lo >= -1.0f);
    CHECK(hi < 1.0f);
}

TEST_CASE("PCG32 streams with different sequence selectors decorrelate", "[generator][prng]") {
    Pcg32 a(99u, 1u);
    Pcg32 b(99u, 2u);  // same seed, different sequence selector

    constexpr std::size_t kPairs = 1u << 18;
    const double r = pearsonCorrelation(kPairs, [&](std::size_t, double& x, double& y) {
        x = a.nextUniform();
        y = b.nextUniform();
    });

    // sigma ~= 1/sqrt(M) ~= 0.00191 for M = 2^18 samples under the null
    // hypothesis of zero correlation -- 0.02 is a ~10-sigma bound, not a
    // number tuned to whatever the implementation happened to produce.
    CHECK(std::abs(r) < 0.02);
}

TEST_CASE("Re-seeding a PCG32 replays the identical stream", "[generator][prng]") {
    constexpr std::uint64_t kSeed = 555u, kSequence = 11u;
    constexpr std::size_t kCount = 1024;

    Pcg32 rng(kSeed, kSequence);
    std::vector<std::uint32_t> first(kCount);
    for (auto& v : first) v = rng.nextUInt32();

    rng.reseed(kSeed, kSequence);
    std::vector<std::uint32_t> second(kCount);
    for (auto& v : second) v = rng.nextUInt32();

    REQUIRE(first == second);
}

TEST_CASE("SplitMix64 derives distinct, decorrelated channel seeds", "[generator][prng]") {
    constexpr std::uint64_t kMaster = 0xC0FFEE'D00D'F00D'BEull;
    constexpr std::size_t kChannels = 8;

    std::vector<std::uint32_t> firstOutputs;
    firstOutputs.reserve(kChannels);
    for (std::size_t ch = 0; ch < kChannels; ++ch) {
        Pcg32 rng = ChannelSeeds::forChannel(kMaster, ch);
        firstOutputs.push_back(rng.nextUInt32());
    }
    for (std::size_t i = 0; i < kChannels; ++i) {
        for (std::size_t j = i + 1; j < kChannels; ++j) {
            CHECK(firstOutputs[i] != firstOutputs[j]);
        }
    }

    constexpr std::size_t kSamples = 1u << 16;
    const std::vector<std::pair<std::size_t, std::size_t>> pairsToCheck = {
        {0, 1}, {2, 5}, {3, 7}, {0, 7}};
    for (const auto& [ci, cj] : pairsToCheck) {
        Pcg32 rngI = ChannelSeeds::forChannel(kMaster, ci);
        Pcg32 rngJ = ChannelSeeds::forChannel(kMaster, cj);
        const double r = pearsonCorrelation(kSamples, [&](std::size_t, double& x, double& y) {
            x = rngI.nextUniform();
            y = rngJ.nextUniform();
        });
        // sigma ~= 1/sqrt(2^16) ~= 0.0039 here -- 0.02 is still a >5-sigma
        // bound at this smaller sample count.
        CHECK(std::abs(r) < 0.02);
    }
}
