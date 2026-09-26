// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared fixture for the IR-decay tests, split out of test_ir_decay.cpp so
// that file (and its clarity/ensemble sibling, test_ir_decay_clarity.cpp)
// stay under CLAUDE.md's 400-line file cap. `inline` throughout: this header
// is included by two translation units in the same test binary.
#pragma once

#include <cmath>
#include <cstddef>
#include <random>
#include <vector>

namespace rta::test::decay_fixtures {

inline constexpr double kFs = 48000.0;

/// Lead-in placed before the arrival in every fixture here.
///
/// Not decoration. A zero-phase filter is non-causal; with the arrival at index
/// 0 its padding fabricates 38.7 dB of energy exactly where EDT reads. The
/// fixtures carry the lead-in for the same reason the shipped code refuses
/// without one.
inline constexpr std::size_t kLeadIn = 9600;   // 200 ms

/// Band-limited decay with a T60 that is exact by construction.
///
/// Energy of `n(t)*exp(-t/tau)` falls as `exp(-2t/tau)`, so a 60 dB drop takes
/// `T60 = 3*ln(10)*tau`. Inverting that is the only place the expected answer
/// comes from -- nothing here is a figure the implementation printed.
///
/// `snrDb` is measured against the fixture's OWN direct sound, never an
/// absolute amplitude: an absolute noise line makes the SNR depend on whatever
/// peak the signal happens to have, and 6 dB of SNR is enough to move the
/// truncation point and change T30 -- or change whether there is an answer.
[[nodiscard]] inline std::vector<float> makeDecay(double t60Sec, double seconds,
                                                  double snrDb, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);

    const auto tailLen = static_cast<std::size_t>(seconds * kFs);
    std::vector<float> h(kLeadIn + tailLen, 0.0f);

    const double tau = t60Sec / (3.0 * std::log(10.0));
    for (std::size_t i = 0; i < tailLen; ++i) {
        const double t = static_cast<double>(i) / kFs;
        h[kLeadIn + i] = static_cast<float>(gauss(rng) * std::exp(-t / tau));
    }
    for (std::size_t i = 0; i < static_cast<std::size_t>(0.006 * kFs); ++i) {
        h[kLeadIn + i] = 0.0f;
    }
    h[kLeadIn] = 1.0f;

    const double noiseRms = std::pow(10.0, -snrDb / 20.0);
    for (auto& v : h) v += static_cast<float>(gauss(rng) * noiseRms);
    return h;
}

}  // namespace rta::test::decay_fixtures
