// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-B (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md; record
// docs/dsp/2026-09-16-spl-pro-l6a.md §2, §11): the per-channel SPL chain.
//
// JUCE-free, so it is proven with RTA_BUILD_APP=OFF on all three CI operating
// systems. Every acceptance is a closed-form identity or a shipped constant
// read back, never a value this code printed.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AllocationProbe.h"

#include "measure/Levels.h"
#include "measure/SplConfig.h"
#include "measure/SplMeter.h"

#include "rta/dsp/OverloadDetector.h"
#include "rta/dsp/Weighting.h"
#include "rta/meter/Detector.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::dsp::WeightingType;
using rta::measure::SplConfig;
using rta::measure::SplMeter;
using rta::meter::Block;
using rta::meter::BlockFlag;
using rta::meter::TimeWeighting;

namespace {

constexpr double kFs = 48000.0;

SplConfig oneSecondConfig(double offsetDb = 0.0) {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.referenceOffsetDb = offsetDb;
    return config;
}

/// Pushes `samples` through the meter in hops of `hop`, collecting every block
/// the meter completes.
std::vector<Block> feed(SplMeter& meter, std::span<const float> samples, std::size_t hop) {
    std::vector<Block> out;
    std::size_t off = 0;
    while (off < samples.size()) {
        const std::size_t n = std::min(hop, samples.size() - off);
        meter.push(samples.subspan(off, n));
        while (auto b = meter.poll()) out.push_back(*b);
        off += n;
    }
    return out;
}

std::vector<float> sine(std::size_t count, double amplitude, double frequencyHz) {
    std::vector<float> x(count);
    for (std::size_t n = 0; n < count; ++n) {
        const double t = static_cast<double>(n) / kFs;
        x[n] = static_cast<float>(amplitude
                                  * std::sin(2.0 * std::numbers::pi * frequencyHz * t));
    }
    return x;
}

}  // namespace

// --- B1: the chain is the shipped parts, not new ones -------------------

TEST_CASE("B1 the A weighting is rta::dsp::Weighting, not a reimplemented curve",
          "[splmeter]") {
    const double a = 0.5;
    const double frequency = 1000.0;
    SplMeter meter(oneSecondConfig(), WeightingType::A, kFs);

    // Two whole seconds of a 1 kHz sine: the first block lets the biquad
    // cascade's transient leave, the SECOND block is the steady state the
    // closed form describes.
    const auto x = sine(static_cast<std::size_t>(2 * kFs), a, frequency);
    const auto blocks = feed(meter, x, 1024);
    REQUIRE(blocks.size() == 2);

    const double measured = 10.0 * std::log10(blocks[1].sumSquares / blocks[1].blockSamples);
    // A unit-amplitude sine has mean square 0.5, hence the -3.0103; the filter
    // contributes 20*log10|H(1 kHz)|, which is what responseDb returns.
    const rta::dsp::Weighting reference(WeightingType::A, kFs);
    const double expectedDigital = 20.0 * std::log10(a)
                                   - rta::measure::kFullScaleSineOffsetDb
                                   + reference.responseDb(frequency);
    const double analytic = rta::dsp::Weighting::analyticDb(frequency, WeightingType::A);

    INFO("measured                   = " << measured);
    INFO("expected from responseDb   = " << expectedDigital);
    INFO("responseDb(1 kHz, A)       = " << reference.responseDb(frequency));
    INFO("analyticDb(1 kHz, A)       = " << analytic);
    INFO("digital residual           = " << (measured - expectedDigital));
    INFO("analytic residual          = "
         << (measured - (20.0 * std::log10(a) - rta::measure::kFullScaleSineOffsetDb + analytic)));

    // Against the DIGITAL cascade the meter actually runs: 1e-6, a float-path
    // bound (the samples cross a float buffer between the filter and the
    // accumulator).
    CHECK_THAT(measured, WithinAbs(expectedDigital, 1e-6));
    // And the digital cascade is within the record's published error of the
    // analytic design target at 1 kHz -- asserted loosely and printed, because
    // the size of that approximation is Weighting's property, not this one's.
    CHECK_THAT(reference.responseDb(frequency), WithinAbs(analytic, 0.05));
}

// --- B2 / B2b: short-term max-held, long-term integrated ----------------

TEST_CASE("B2 maxFastDb is the detector's step response, max-held over the block",
          "[splmeter]") {
    // Detector.h:26-31 states the closed form in its own words: the STATE is a
    // MEAN SQUARE whose step response is 1 - exp(-t/tau), so the level is
    // 10*log10 of that. tau is READ from the shipped constant, never typed:
    // IEC 61672-1:2013 clause 5.8 is the time-weighting clause, but the 125 ms
    // value itself is UNVERIFIED in record §14 (vendor pages only), so this
    // test depends on the published number for nothing.
    const double tau = rta::meter::Detector::riseTimeConstant(TimeWeighting::Fast);
    INFO("Detector::riseTimeConstant(Fast) = " << tau);

    const double burstAmplitude = 0.5;
    const double floorAmplitude = burstAmplitude / 10.0;  // exactly 20 dB down
    const std::uint32_t blockSamples = static_cast<std::uint32_t>(kFs);

    // Tolerance, DERIVED: Block::maxFastDb is a `float`. One float32 ULP at
    // the magnitudes here is about 7.63e-06 dB at 100 dB and smaller below it,
    // and the detector itself runs in double -- so 1e-4 dB is more than 13x
    // the storage granularity and is dominated by it, not by the algorithm.
    constexpr double kFloatStorageTolerance = 1e-4;

    for (double burstSeconds : {0.05, 5.0 * tau}) {
        SplMeter meter(oneSecondConfig(), WeightingType::Z, kFs);

        // 40 tau of pre-roll at the floor amplitude, so the detector's state
        // is the floor's mean square to within exp(-40) = 4.2e-18 -- far below
        // float. This is what makes the generalised step response below exact:
        // Detector.h says the plain 1 - exp(-t/tau) form holds only FROM
        // SILENCE, and a block whose floor is 20 dB down does not start there.
        const auto preRollSamples = static_cast<std::size_t>(40.0 * tau * kFs);
        const std::size_t preRollBlocks = (preRollSamples + blockSamples - 1) / blockSamples;
        std::vector<float> preRoll(preRollBlocks * blockSamples);
        for (std::size_t n = 0; n < preRoll.size(); ++n) {
            preRoll[n] = static_cast<float>((n % 2 == 0) ? floorAmplitude : -floorAmplitude);
        }
        const auto discarded = feed(meter, preRoll, 1024);
        REQUIRE(discarded.size() == preRollBlocks);

        // The measured block: the burst starts exactly on the block boundary.
        const auto burstSamples = static_cast<std::size_t>(burstSeconds * kFs);
        REQUIRE(burstSamples < blockSamples);
        std::vector<float> block(blockSamples);
        for (std::size_t n = 0; n < block.size(); ++n) {
            const double amp = (n < burstSamples) ? burstAmplitude : floorAmplitude;
            block[n] = static_cast<float>((n % 2 == 0) ? amp : -amp);
        }
        const auto blocks = feed(meter, block, 1024);
        REQUIRE(blocks.size() == 1);

        const double af = static_cast<double>(static_cast<float>(burstAmplitude));
        const double ff = static_cast<double>(static_cast<float>(floorAmplitude));
        const double y0 = ff * ff;
        const double rise = 1.0 - std::exp(-burstSeconds / tau);
        const double predicted = 10.0 * std::log10(y0 + (af * af - y0) * rise);
        const double deficit = predicted - 10.0 * std::log10(af * af);

        INFO("burst = " << burstSeconds << " s  (" << (burstSeconds / tau) << " tau)");
        INFO("1 - exp(-t/tau)     = " << rise);
        INFO("predicted maxFastDb = " << predicted);
        INFO("deficit from steady = " << deficit << " dB");
        INFO("measured maxFastDb  = " << blocks[0].maxFastDb);
        INFO("residual            = " << (static_cast<double>(blocks[0].maxFastDb) - predicted));
        CHECK_THAT(static_cast<double>(blocks[0].maxFastDb),
                   WithinAbs(predicted, kFloatStorageTolerance));
    }
}

TEST_CASE("B2b the deficit is the RISE term, not the decay term -- 3.0819 dB apart",
          "[splmeter]") {
    // An earlier revision of B2 quoted 10*log10(1 - e^{-t/tau}) and printed
    // 10*log10(e^{-t/tau}) rounded. Both are asserted here BY NAME so the
    // complement can never be substituted for the step again, and the gap
    // between them is asserted too.
    const double x = 0.4;  // t/tau for the 50 ms burst at tau = 125 ms
    const double step = 10.0 * std::log10(1.0 - std::exp(-x));
    const double decay = 10.0 * std::log10(std::exp(-x));
    INFO("10*log10(1 - e^-0.4) = " << step);
    INFO("10*log10(e^-0.4)     = " << decay);
    INFO("gap                  = " << (step - decay));
    CHECK_THAT(step, WithinAbs(-4.8190745912, 1e-9));
    CHECK_THAT(decay, WithinAbs(-1.7371779276, 1e-9));
    CHECK_THAT(step - decay, WithinAbs(-3.0818966636, 1e-9));
    // The 0.5 dB tolerance an earlier revision derived from the decay term
    // would have failed a CORRECT implementation by about 4.3 dB.
    CHECK(std::fabs(step) - 0.5 > 4.0);
}

// --- B3: the overload run crosses the block boundary --------------------

TEST_CASE("B3 an overload run that straddles a block boundary is caught", "[splmeter]") {
    // OverloadDetector.h:22-27 warns in its own words that a run does not
    // carry across separate calls, so a hop boundary splits it in two. A BLOCK
    // boundary is a bigger version of the same boundary (SPL-R4).
    const std::uint32_t blockSamples = 480;
    SplConfig config = oneSecondConfig();
    config.blockSeconds = static_cast<double>(blockSamples) / kFs;
    SplMeter meter(config, WeightingType::Z, kFs);

    // Two full-scale samples at the END of block 0, one at the START of
    // block 1: the run of three COMPLETES in block 1, and neither block sees
    // three consecutive samples on its own.
    std::vector<float> x(2 * blockSamples, 0.01f);
    x[blockSamples - 2] = rta::dsp::kFullScaleThreshold;
    x[blockSamples - 1] = rta::dsp::kFullScaleThreshold;
    x[blockSamples] = rta::dsp::kFullScaleThreshold;

    // Hops of 160 so the run also straddles a HOP boundary, which is the
    // failure the header names.
    const auto blocks = feed(meter, x, 160);
    REQUIRE(blocks.size() == 2);
    CHECK_FALSE(rta::meter::hasFlag(blocks[0].flags, BlockFlag::Overload));
    CHECK(rta::meter::hasFlag(blocks[1].flags, BlockFlag::Overload));
}

// --- B4: no allocation after construction -------------------------------

TEST_CASE("B4 push allocates nothing after construction", "[splmeter]") {
    const std::uint32_t blockSamples = 480;
    SplConfig config = oneSecondConfig();
    config.blockSeconds = static_cast<double>(blockSamples) / kFs;
    SplMeter meter(config, WeightingType::A, kFs);

    std::vector<float> hop(160, 0.1f);
    // Ten blocks' worth of hops. Through W0-B0's SHARED probe -- a second
    // global operator new in this file would not link.
    std::size_t bytes = 0;
    {
        const rta::test::AllocationProbe probe;
        for (int i = 0; i < 10 * 3; ++i) {
            meter.push(hop);
            while (meter.poll()) {
            }
        }
        bytes = probe.bytes();
    }
    INFO("bytes allocated by 30 pushes = " << bytes);
    CHECK(bytes == 0);
}

// --- B5: the offset is DATA ---------------------------------------------

TEST_CASE("B5 referenceOffsetDb is data -- no calibration flow in this path", "[splmeter]") {
    // This is what makes Wave 3 REMOVABLE (record §13 Q2's flip): waves 0-2
    // take the calibration offset as a number on SplConfig and nothing here
    // knows a calibrator exists.
    const double a = 1.0;
    const double offset = 94.0 - 20.0 * std::log10(a) + rta::measure::kFullScaleSineOffsetDb;
    SplMeter meter(oneSecondConfig(offset), WeightingType::Z, kFs);

    // 1000 whole periods of a 1 kHz sine in a 48 000-sample block.
    const auto x = sine(static_cast<std::size_t>(kFs), a, 1000.0);
    const auto blocks = feed(meter, x, 1024);
    REQUIRE(blocks.size() == 1);

    const double published = SplMeter::blockLevelDb(blocks[0], offset);
    INFO("offset    = " << offset);
    INFO("published = " << published);
    INFO("residual  = " << (published - 94.0));
    // TOLERANCE DEVIATION FROM THE PLAN, MEASURED NOT ARGUED. W0-B B5 asks for
    // 1e-9. A SINE cannot reach it, and the reason is not summation: each
    // sample is rounded to float32, so a sample near amplitude 1.0 carries up
    // to half a float ULP (2^-25 = 2.98e-08) of relative amplitude error,
    // hence up to ~6e-08 of relative mean-square error, hence ~2.6e-07 dB. The
    // measured residual here is -8.27e-08 dB -- inside that bound and 80x
    // OUTSIDE 1e-9. 1e-6 dB is the float-derived bound; the exact form of the
    // identity is asserted in the next section, where it belongs.
    CHECK_THAT(published, WithinAbs(94.0, 1e-6));

    SECTION("the same identity with an EXACTLY representable mean square") {
        // Alternating +-a: every sample squares to a*a exactly and 48 000
        // copies of it sum exactly when a = 1, so the mean square is exactly
        // 1.0 and the identity is bitwise. `test_leq.cpp:33-39` uses the same
        // fixture for the same reason. There is no 3.0103 here, because there
        // is no sine -- which is itself the point of W0-E.
        const double exactOffset = 94.0 - 20.0 * std::log10(a);
        SplMeter exact(oneSecondConfig(exactOffset), WeightingType::Z, kFs);
        std::vector<float> square(static_cast<std::size_t>(kFs));
        for (std::size_t n = 0; n < square.size(); ++n) {
            square[n] = static_cast<float>((n % 2 == 0) ? a : -a);
        }
        const auto exactBlocks = feed(exact, square, 1024);
        REQUIRE(exactBlocks.size() == 1);
        CHECK(exactBlocks[0].sumSquares == kFs);
        CHECK(SplMeter::blockLevelDb(exactBlocks[0], exactOffset) == 94.0);
    }
}

// --- the derived histogram base, run through the gate it feeds ----------

TEST_CASE("SplConfig::histogramBaseDb is derived from the offset, never typed", "[splmeter]") {
    // Defect 4 / memory/a-default-must-be-run-through-the-gate-it-feeds.md: a
    // hard -20.0 made every uncalibrated session's Ln permanently BelowSpan.
    // W1-A7 and W1-A8 are the fixtures that would have caught it; this is the
    // arithmetic, asserted where the default lives.
    SplConfig config;
    CHECK(config.referenceOffsetDb == 0.0);
    CHECK_THAT(config.histogramBaseDb(), WithinAbs(rta::measure::kLevelFloorDb, 1e-15));
    // Uncalibrated the span is [-120, +80): a full-scale sine at 0.0 dBFS
    // lands at bin 1200 of 2000 -- 60 % up, 80 dB of headroom above it.
    CHECK_THAT((0.0 - config.histogramBaseDb()) / 0.1, WithinAbs(1200.0, 1e-9));

    config.referenceOffsetDb = 100.0;
    // Calibrated at a typical +100 dB the span becomes [-20, +180), which is
    // EXACTLY record §5's own default recovered rather than contradicted.
    CHECK_THAT(config.histogramBaseDb(), WithinAbs(-20.0, 1e-15));
    CHECK_THAT((140.0 - config.histogramBaseDb()) / 0.1, WithinAbs(1600.0, 1e-9));
}
