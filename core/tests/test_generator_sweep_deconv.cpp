// SPDX-License-Identifier: AGPL-3.0-or-later
// Station D: Farina exponential sweep + inverse filter (rta::gen::Sweep).
// Split out of test_generator_sweep.cpp (process-tooling PR, 400-line file
// cap): the hidden [.][slow] full-range deconvolution case (shares
// runSweepDeconvolution from support/SweepFixtures.h with the CI-fast case
// that stayed in test_generator_sweep.cpp) plus the inverse-filter-shape,
// fade-floor and valid-band cases, none of which touch the shared fixtures.
// See test_generator_sweep.cpp for the golden/L4b context these two files
// share.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/gen/Sweep.h"
#include "support/SweepFixtures.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rta::gen::Sweep;
using namespace rta::test::sweep_fixtures;

TEST_CASE("Full-range 20 Hz to 20 kHz sweep deconvolution", "[.][slow][sweep]") {
    const double fs = 48000.0, f1 = 20.0, f2 = 20000.0, t = 10.0;
    const auto result = runSweepDeconvolution(fs, f1, f2, t, 1u << 20);

    CHECK(result.offset1500 == 500);
    CHECK(result.offset2300 == 1300);
    CHECK_THAT(result.amp1500Rel, WithinRel(0.5, 0.005));
    CHECK_THAT(result.amp2300Rel, WithinRel(-0.25, 0.005));
    CHECK(result.snrDb > 60.0);
}

TEST_CASE("The inverse filter is the faded sweep reversed and shaped, nothing else",
          "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate  = 48000.0;
    cfg.startHz     = 100.0;
    cfg.endHz       = 10000.0;
    cfg.durationSec = 2.0;
    cfg.fadeInSec   = 0.30;    // ASYMMETRIC on purpose. Under equal fade widths
    cfg.fadeOutSec  = 0.02;    // every candidate construction agrees, which is
    Sweep sweep(cfg);          // exactly how a second, undocumented Tukey layer
                               // lived in buildInverseFilter for four days
                               // without a single test noticing.
    REQUIRE(sweep.fadeInSamples() != sweep.fadeOutSamples());

    const auto n = sweep.lengthSamples();
    std::vector<float> forward(n);
    sweep.process(forward);
    const auto inv = sweep.buildInverseFilter();
    REQUIRE(inv.size() == n);

    // Sweep.h specifies inv[m] = x[N-1-m] * (instantaneousFrequency(N-1-m)/endHz)
    // -- the already-rendered, already-faded sweep, reversed, shaped. No further
    // windowing of any kind. Pinning that as the SPEC, element by element, is
    // what makes a later "helpful" extra layer go red instead of going unnoticed.
    // The tolerance is a closed form, not a picked number. buildInverseFilter
    // computes the product in double and stores ONE float; process() renders
    // the same forward samples bit-for-bit through the same expression (see
    // Sweep::nextSample). So the only difference is a single double->float
    // rounding. Every |inv[m]| is below 1.0 -- asserted below rather than
    // assumed -- hence ulp <= 2^-24 and half an ulp is 2^-25 = 2.98e-8.
    //
    // NOT zero, deliberately. MSVC /fp:precise does not contract today so
    // bit-identical would pass, but a toolchain that enabled FMA in one of the
    // two translation units and not the other would make this red with nothing
    // wrong. One rounding is the right claim; zero roundings is a claim about
    // the compiler.
    // Measured for this fixture: worst 1.48995e-08 with largest |inv| = 0.4787.
    // Since that largest is below 0.5 its own ulp is 2^-25 and half of that is
    // 2^-26 = 1.49012e-08 -- so the measurement SATURATES the tighter sub-bound,
    // which is how we know 2^-25 is a real bound and not a comfortable margin.
    // The looser figure is asserted so a configuration whose samples reach into
    // [0.5, 1) does not turn a correct implementation red.
    constexpr double kHalfUlpBelowOne = 2.98023223876953125e-08;   // 2^-25
    double worst = 0.0;
    double largest = 0.0;
    for (std::size_t m = 0; m < n; ++m) {
        const std::size_t original = n - 1 - m;
        const double expected = static_cast<double>(forward[original])
                              * (sweep.instantaneousFrequency(original) / cfg.endHz);
        worst = std::max(worst, std::abs(static_cast<double>(inv[m]) - expected));
        largest = std::max(largest, std::abs(static_cast<double>(inv[m])));
    }
    CAPTURE(worst, largest);
    CHECK(largest < 1.0);              // the premise the bound rests on
    CHECK(worst <= kHalfUlpBelowOne);

    // Both ends already reach zero through the inherited fades, which is why no
    // second layer is needed to prevent a step in the kernel.
    CHECK_THAT(static_cast<double>(inv.front()), WithinAbs(0.0, 1.0e-9));
    CHECK_THAT(static_cast<double>(inv.back()), WithinAbs(0.0, 1.0e-9));
}

TEST_CASE("The fade-out has a floor of two cycles at endHz", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate  = 48000.0;
    cfg.startHz     = 20.0;
    cfg.endHz       = 20000.0;
    cfg.durationSec = 2.0;
    cfg.fadeOutSec  = 0.0;     // legitimate request; must not leave a step
    Sweep sweep(cfg);

    // 2 / 20000 Hz = 0.1 ms = 5 samples at 48 kHz.
    const auto expected = (std::size_t) std::llround((2.0 / cfg.endHz) * cfg.sampleRate);
    CHECK(sweep.fadeOutSamples() == expected);
    CHECK(expected == 5u);

    // Dormant at the shipped default: 0.02 s is 960 samples, far above the floor.
    auto defaultCfg = cfg;
    defaultCfg.fadeOutSec = 0.02;
    CHECK(Sweep(defaultCfg).fadeOutSamples() == 960u);
}

TEST_CASE("The fade-in clamp is expressed in octaves of sweep travel", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 50.0;
    cfg.endHz = 5000.0;
    cfg.durationSec = 3.0;
    cfg.fadeInSec = 0.001;      // below every floor, so a floor must win

    // Three floors compete. L = T/ln(f2/f1) = 3/ln(100) = 0.651442 s, so two
    // octaves is 2*ln2*L = 0.903090 s -- an order of magnitude above the
    // 0.04 s cycles floor, which is the whole point: the cycles rule is
    // expressed in seconds, and the artefact floor is governed by octaves.
    Sweep sweep(cfg);
    // Read L from the sweep: synchronisation rounds it, so 3/ln(100) is the
    // requested value, not the one the fade floor is computed against.
    const double octaveFloorSec = 2.0 * std::log(2.0) * sweep.lengthConstantL();
    CHECK(sweep.fadeInSamples()
          == (std::size_t) std::llround(octaveFloorSec * cfg.sampleRate));
    CHECK_THAT(sweep.fadeInOctavesAchieved(), WithinRel(2.0, 1.0e-5));

    // The cycles floor still governs where it is the larger of the two: a very
    // short sweep travels half an octave faster than two cycles at f1 take.
    auto shortCfg = cfg;
    shortCfg.durationSec = 0.05;
    shortCfg.fadeInOctaves = 0.5;
    Sweep shortSweep(shortCfg);
    CHECK(shortSweep.fadeInSamples()
          == (std::size_t) std::llround((2.0 / shortCfg.startHz) * shortCfg.sampleRate));

    // Zero restores the pre-2026-08-30 behaviour exactly, which is what makes
    // the trade-off in record decision 5 selectable rather than imposed.
    auto legacyCfg = cfg;
    legacyCfg.fadeInOctaves = 0.0;
    CHECK(Sweep(legacyCfg).fadeInSamples()
          == (std::size_t) std::llround((2.0 / cfg.startHz) * cfg.sampleRate));

    // And a LONGER sweep keeps the same width in octaves. Under the cycles rule
    // alone it would have shrunk -- 0.04 s is 0.5 octave at T=3 s but 0.15
    // octave at T=10 s -- so lengthening the sweep made the band edges worse,
    // which is the defect this floor exists to remove.
    auto longCfg = cfg;
    longCfg.durationSec = 10.0;
    CHECK_THAT(Sweep(longCfg).fadeInOctavesAchieved(), WithinRel(2.0, 1.0e-5));
}

TEST_CASE("The valid band's edges follow from the fades", "[sweep]") {
    Sweep::Config cfg;
    cfg.sampleRate = 48000.0;
    cfg.startHz = 100.0;
    cfg.endHz = 10000.0;
    cfg.durationSec = 2.0;
    Sweep sweep(cfg);

    // f_lo = f1*exp(fadeInSec/L). When the octave floor binds, fadeInSec is
    // exactly octaves*ln2*L, so the exponential collapses to f1 * 2^octaves --
    // the lower band edge IS the knob. 100 Hz * 2^2 = 400 Hz.
    CHECK_THAT(sweep.validBandLowHz(), WithinRel(400.0, 1.0e-4));

    // f_hi = f2*exp(-fadeOutSec/L), with L read from the sweep rather than
    // recomputed -- synchronisation rounds it.
    CHECK_THAT(sweep.validBandHighHz(),
               WithinRel(10000.0 * std::exp(-0.02 / sweep.lengthConstantL()), 1.0e-9));
    CHECK(sweep.validBandHighHz() < cfg.endHz);
    CHECK(sweep.validBandLowHz() > cfg.startHz);
}
