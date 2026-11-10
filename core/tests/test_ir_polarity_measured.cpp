// SPDX-License-Identifier: AGPL-3.0-or-later
// H11 L3: `PolarityResult::arrivalMeasured` says whether `confidenceDb` is a measured figure
// (the window peak over a non-zero pre-arrival floor). The app quotes the number only when
// it is true, so the flag must be false on every path that never computed one.
#include <catch2/catch_test_macros.hpp>

#include "PolarityFixture.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

using namespace polarity_fixture;
using rta::ir::Refusal;
using rta::ir::Sign;

namespace {

// The noise window is [originIndex - h2, originIndex): the span findPolarity divides the peak by.
std::size_t floorStart(const rta::ir::Deconvolution& d) {
    return d.originIndex - static_cast<std::size_t>(std::llround(-d.harmonicOffsetSamples(2)));
}

}  // namespace

TEST_CASE("arrivalMeasured is false on every path that returns before a confidence exists",
          "[ir][polarity]") {
    SECTION("an empty search window") {
        auto source = measureThrough(60.0, 15000.0, 4, +1.0f);
        source.samples.resize(source.originIndex);
        const auto got = rta::ir::findPolarity(source, {});
        CHECK(got.refusal == Refusal::NoSignal);
        CHECK_FALSE(got.arrivalMeasured);
    }
    SECTION("a silent search window") {
        auto source = measureThrough(60.0, 15000.0, 4, +1.0f);
        std::fill(source.samples.begin() + static_cast<std::ptrdiff_t>(source.originIndex),
                  source.samples.end(), 0.0f);
        const auto got = rta::ir::findPolarity(source, {});
        CHECK(got.refusal == Refusal::NoSignal);
        CHECK_FALSE(got.arrivalMeasured);
    }
    SECTION("a sweep whose trusted band cannot answer") {
        auto source = measureThrough(60.0, 15000.0, 4, +1.0f);
        source.trustedLowHz = 150.0;  // above the 100 Hz gate
        const auto got = rta::ir::findPolarity(source, {});
        CHECK(got.refusal == Refusal::SweepBandInsufficient);
        CHECK_FALSE(got.arrivalMeasured);
    }
    SECTION("a capture too short to size the band-edge window") {
        auto source = measureThrough(50.0, 71.0, 4, +1.0f);
        source.samples.resize(source.originIndex + static_cast<std::size_t>(0.15 * kSampleRate));
        const auto got = rta::ir::findPolarity(source, {});
        CHECK(got.refusal == Refusal::CaptureTooShort);
        CHECK_FALSE(got.arrivalMeasured);
    }
    SECTION("no harmonic spacing, so no noise window") {
        auto source = measureThrough(60.0, 15000.0, 4, +1.0f);
        source.harmonicSpacingL = 0.0;
        const auto got = rta::ir::findPolarity(source, {});
        CHECK(got.refusal == Refusal::NoNoiseEstimate);
        CHECK_FALSE(got.arrivalMeasured);
    }
}

TEST_CASE("arrivalMeasured is true once the confidence is computed, including on a refusal",
          "[ir][polarity]") {
    SECTION("an answered measurement") {
        const auto got = readPolarity(60.0, 15000.0, 4, +1.0f);
        REQUIRE(got.refusal == Refusal::None);
        CHECK(got.arrivalMeasured);
    }
    SECTION("a band refusal still carries a measured confidence") {
        const auto got = readPolarity(1000.0, 16000.0, 8, +1.0f);
        REQUIRE(got.refusal == Refusal::BandTooLow);
        CHECK(got.arrivalMeasured);
    }
    SECTION("a NoSignal from low confidence: the number is measured and quotable") {
        auto source = measureThrough(60.0, 15000.0, 4, +1.0f);
        for (std::size_t i = floorStart(source); i < source.originIndex; ++i) source.samples[i] *= 1.0e5f;
        const auto got = rta::ir::findPolarity(source, {});
        CHECK(got.refusal == Refusal::NoSignal);
        CHECK(got.sign == Sign::Unknown);
        CHECK(got.confidenceDb < rta::ir::PolarityConfig{}.minConfidenceDb);
        CHECK(got.arrivalMeasured);
    }
    SECTION("a zero pre-arrival floor has no ratio, so it is NOT measured (0.0 is a placeholder)") {
        auto source = measureThrough(60.0, 15000.0, 4, +1.0f);
        std::fill(source.samples.begin() + static_cast<std::ptrdiff_t>(floorStart(source)),
                  source.samples.begin() + static_cast<std::ptrdiff_t>(source.originIndex), 0.0f);
        const auto got = rta::ir::findPolarity(source, {});
        CHECK(got.confidenceDb == 0.0);
        CHECK_FALSE(got.arrivalMeasured);
    }
}
