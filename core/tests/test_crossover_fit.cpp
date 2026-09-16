// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for spectralCrossover -- see
// docs/plans/2026-09-15-L7-align-impl-plan.md Task B, cases B1-B5, and the
// decision record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.2
// computed-(1) / Sec.9.
//
// Task C's fixtures live in test_crossover_band_fit.cpp, not here: the split
// seam is named in the plan so neither file grows past the 400-line cap.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/CrossoverFit.h"

#include <cmath>
#include <optional>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::dsp;

namespace {

std::optional<std::vector<float>> unitCoherence(std::size_t n) {
    return std::optional<std::vector<float>>{ std::vector<float>(n, 1.0f) };
}

}  // namespace

TEST_CASE("spectralCrossover interpolates exactly where the dB difference is linear",
          "[crossover_fit]") {
    // B1. The two-point crossing is exact when the difference is linear in k,
    // which pins the same arithmetic core/src/ir/Polarity.cpp:39-48 uses.
    //
    // DEVIATION from the plan, measured. The plan's constants are m = 0.7 with
    // the zero at k = 12.4, asserted to 1e-9. Neither 0.7 nor 12.4 is
    // representable in `float`, which is what magnitudeDb IS, and the rounding
    // alone puts the crossing 3.75e-6 Hz off 124.0 -- 3700x the plan's own
    // tolerance. m = 0.75 with the zero at k = 12.25 is binary-exact at every
    // step (0.75 = 3/4, 12.25 = 49/4), so the crossing comes out EXACT and the
    // case is asserted 1000x tighter than the plan asked instead of looser.
    constexpr double kBinHz = 10.0;
    constexpr float kSlope = 0.75f;
    constexpr float kZeroBin = 12.25f;

    std::vector<float> magA(40), magB(40);
    for (std::size_t k = 0; k < magA.size(); ++k) {
        const float v = kSlope * (static_cast<float>(k) - kZeroBin);
        magA[k] = +v;
        magB[k] = -v;
    }

    const auto result = spectralCrossover(magA, magB, unitCoherence(40), unitCoherence(40), kBinHz,
                                          0.0, std::nullopt);
    REQUIRE(result.refusal == CrossoverRefusal::None);
    INFO("crossing " << result.frequencyHz << " Hz, residual from 122.5: "
                     << std::abs(result.frequencyHz - 122.5));
    CHECK_THAT(result.frequencyHz, WithinAbs(122.5, 1e-12));
    REQUIRE(result.allCrossingsHz.size() == 1);
}

TEST_CASE("spectralCrossover on an analytic BW4 pair lands within half a bin of fc",
          "[crossover_fit]") {
    // B2. |L| = 1/sqrt(1+x^8), |H| = x^4/sqrt(1+x^8) with x = f/fc, so the two
    // magnitudes are equal exactly where x^4 = 1, i.e. at fc -- no numerical
    // search required to know the answer.
    //
    // The assertion is an INTERPOLATION BOUND, not "whatever it printed": the
    // dB difference 80*log10(f/fc) is linear in log f, the interpolation is
    // linear in f, and over one bin the two differ by less than half a bin.
    constexpr double kSampleRate = 48000.0;
    constexpr std::size_t kPoints = 4096;
    constexpr double kBinHz = kSampleRate / static_cast<double>(kPoints);
    constexpr double kFc = 100.0;

    std::vector<float> magA(kPoints / 2), magB(kPoints / 2);
    for (std::size_t k = 0; k < magA.size(); ++k) {
        const double f = std::max(static_cast<double>(k) * kBinHz, 1e-6);
        const double x = f / kFc;
        const double x8 = std::pow(x, 8.0);
        const double denom = std::sqrt(1.0 + x8);
        magA[k] = static_cast<float>(20.0 * std::log10(std::pow(x, 4.0) / denom));  // HP
        magB[k] = static_cast<float>(20.0 * std::log10(1.0 / denom));               // LP
    }

    const auto result = spectralCrossover(magA, magB, unitCoherence(magA.size()),
                                          unitCoherence(magB.size()), kBinHz, 0.0, std::nullopt);
    REQUIRE(result.refusal == CrossoverRefusal::None);
    const double residual = std::abs(result.frequencyHz - kFc);
    INFO("BW4 crossing " << result.frequencyHz << " Hz, residual " << residual
                         << " Hz, half-bin bound " << (kBinHz * 0.5));
    CHECK(residual <= kBinHz * 0.5);
}

TEST_CASE("spectralCrossover lists every crossing and lets a seed select one",
          "[crossover_fit]") {
    // B3. A three-way system has two crossovers and the wizard must be able to
    // ASK which one is being aligned. So every crossing is reported, ascending,
    // and a seed picks the nearest one on a LOG axis -- frequency is a log
    // quantity, and a linear "nearest" would hand a seed sitting midway
    // between 100 Hz and 2 kHz to the 2 kHz side every time.
    constexpr double kBinHz = 48000.0 / 4096.0;
    constexpr std::size_t kPoints = 2048;

    std::vector<float> magA(kPoints), magB(kPoints, 0.0f);
    for (std::size_t k = 1; k < kPoints; ++k) {
        const double f = static_cast<double>(k) * kBinHz;
        // Zero exactly at 100 Hz and at 2000 Hz, one sign change at each.
        magA[k] = static_cast<float>(10.0 * std::log10(f / 100.0) * std::log10(2000.0 / f));
    }
    magA[0] = magA[1];  // bin 0 is DC; log10(0) has no value to report

    const auto all = spectralCrossover(magA, magB, unitCoherence(kPoints), unitCoherence(kPoints),
                                       kBinHz, 0.0, std::nullopt);
    REQUIRE(all.refusal == CrossoverRefusal::None);
    REQUIRE(all.allCrossingsHz.size() == 2);
    CHECK(all.allCrossingsHz[0] < all.allCrossingsHz[1]);
    CHECK_THAT(all.allCrossingsHz[0], WithinAbs(100.0, kBinHz));
    CHECK_THAT(all.allCrossingsHz[1], WithinAbs(2000.0, kBinHz));
    // No seed: the first crossing, and the full list for the wizard to ask about.
    CHECK(all.frequencyHz == all.allCrossingsHz.front());

    const auto high = spectralCrossover(magA, magB, unitCoherence(kPoints),
                                        unitCoherence(kPoints), kBinHz, 0.0, 1500.0);
    CHECK(high.frequencyHz == all.allCrossingsHz[1]);

    const auto low = spectralCrossover(magA, magB, unitCoherence(kPoints), unitCoherence(kPoints),
                                       kBinHz, 0.0, 300.0);
    CHECK(low.frequencyHz == all.allCrossingsHz[0]);
}

TEST_CASE("spectralCrossover's gate excludes bins, it does not fabricate a crossing",
          "[crossover_fit]") {
    // B4. A crossing that sits inside a stretch nobody measured is not a
    // crossing this function may report. Bins below the gate take no part, and
    // a crossing is only ever read between two ADJACENT gated bins -- straddling
    // an ungated gap would be exactly the fabrication this case forbids.
    constexpr double kBinHz = 10.0;
    std::vector<float> magA(40), magB(40);
    for (std::size_t k = 0; k < magA.size(); ++k) {
        const float v = 0.75f * (static_cast<float>(k) - 12.25f);
        magA[k] = +v;
        magB[k] = -v;
    }

    std::vector<float> coh(40, 1.0f);
    for (std::size_t k = 10; k <= 15; ++k) coh[k] = 0.1f;  // the only crossing lives here

    const auto result = spectralCrossover(magA, magB, std::optional<std::vector<float>>{ coh },
                                          std::optional<std::vector<float>>{ coh }, kBinHz, 0.5,
                                          std::nullopt);
    CHECK(result.refusal == CrossoverRefusal::NoCrossing);
    CHECK(result.frequencyHz == 0.0);
    CHECK(result.allCrossingsHz.empty());
}

TEST_CASE("spectralCrossover refuses when either coherence is absent -- absent is not unity",
          "[crossover_fit]") {
    // B5. std::nullopt coherence means the estimator was below
    // minimumEffectiveAverages, not that every bin is perfectly coherent.
    // Treating it as gamma^2 == 1 is the silent fallback that restores a fixed
    // defect (memory/a-fixed-defect-returns-through-the-silent-fallback.md);
    // this case is what goes red if anyone writes it.
    constexpr double kBinHz = 10.0;
    std::vector<float> magA(40), magB(40);
    for (std::size_t k = 0; k < magA.size(); ++k) {
        const float v = 0.75f * (static_cast<float>(k) - 12.25f);
        magA[k] = +v;
        magB[k] = -v;
    }

    for (const auto& pair : { std::pair{ unitCoherence(40), std::optional<std::vector<float>>{} },
                              std::pair{ std::optional<std::vector<float>>{}, unitCoherence(40) },
                              std::pair{ std::optional<std::vector<float>>{},
                                         std::optional<std::vector<float>>{} } }) {
        const auto result =
            spectralCrossover(magA, magB, pair.first, pair.second, kBinHz, 0.0, std::nullopt);
        CHECK(result.refusal == CrossoverRefusal::AllBinsAbsent);
        CHECK(result.frequencyHz == 0.0);
        CHECK(result.allCrossingsHz.empty());
    }
}
