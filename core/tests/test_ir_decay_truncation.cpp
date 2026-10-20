// SPDX-License-Identifier: AGPL-3.0-or-later
// Lundeby crossing and truncation correction (Decay.cpp / DecayLundeby.cpp).
// Closed-form fixtures, no band filter and no random noise: every expected
// number below is derived in the case that uses it from the fixture's own
// construction, never from a value the implementation printed.
//
// Audit: D:\DEV CAVE EP3\shared\audits\prj010-fable-audit-20261021.md, findings
// "Lundeby truncation correction is seeded from ONE squared sample" and
// "Lundeby lastUsable takes the LAST block above noise+standoff".
#include "rta/ir/Decay.h"
#include "support/DecayFixtures.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::test::decay_fixtures;

namespace {

constexpr double kTwoPi = 6.28318530717958647692528676656;

/// A deterministic floor of constant mean power: three tones at frequencies
/// (cycles/sample) with no common period inside any block, each of amplitude
/// `amp`. Mean power 3*amp^2/2 -- the same in every block to within the ripple
/// of a tone averaged over a block, which is what a noise estimate is for.
double floorSample(std::size_t i, double amp) {
    const double t = static_cast<double>(i);
    return amp * (std::cos(kTwoPi * 0.0731 * t + 0.3) +
                  std::cos(kTwoPi * 0.1913 * t + 1.1) +
                  std::cos(kTwoPi * 0.3217 * t + 2.0));
}

constexpr double kT60 = 0.5;                       // power, seconds
constexpr double kTa = kT60 / (3.0 * 2.302585092994046) * kFs;   // amplitude tau, samples
constexpr double kFloorAmp = 5e-4;                 // floor power 3.75e-7 = -64 dB re 1

/// exp(-i/ta) [* cos(w i + phase)] + floor. Power of the signal part:
///   plain: exp(-2i/ta)            (mean-square of the sample itself)
///   osc:   exp(-2i/ta) * cos^2(.) with block mean exp(-2i/ta)/2.
std::vector<float> makeTone(std::size_t n, bool oscillating, double cyclesPerSample,
                            double phase) {
    std::vector<float> h(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i);
        double s = std::exp(-t / kTa);
        if (oscillating) s *= std::cos(kTwoPi * cyclesPerSample * t + phase);
        h[i] = static_cast<float>(s + floorSample(i, kFloorAmp));
    }
    return h;
}

}  // namespace

TEST_CASE("The truncation correction is the regression line at the crossing, times tau",
          "[ir][decay][truncation]") {
    // If power decays as p(t) = p0 exp(-2t/ta) then the energy past the cut is
    // p(cut) * tau with tau = ta/2 samples. With signal amplitude exp(-i/ta):
    //   plain  fixture: block-mean power at i is exp(-2i/ta)         -> p0 = 1
    //   osc    fixture: block-mean power at i is exp(-2i/ta) / 2     -> p0 = 1/2
    // (cos^2 averages to 1/2; the 2w term is the ripple bounded below).
    //
    // Expected value, from the fixture alone:
    //   expected = p0 * exp(-2 cut / ta) * ta / 2,   cut = curve.crossingIndex.
    //
    // Tolerance, derived rather than chosen. The fit sees block means, not the
    // continuous curve:
    //  * A block of W samples averages exp(-2i/ta) to exp(-2c/ta) sinh(x)/x
    //    with x = W/ta and c the block centre. The window is sized for 5
    //    intervals per 10 dB, i.e. 2 dB of decay per block = 0.46 nepers of
    //    power over W, so x = 0.23 and sinh(x)/x = 1.009 -> +0.04 dB. Applied
    //    to the intercept, below.
    //  * The oscillating fixture adds a ripple of relative size
    //    1/(W |sin w|) in power to each block mean (geometric sum of cos(2wi)).
    //    W = 10/0.0025/5 = 800, w = 2 pi 0.0208 -> |sin w| = 0.13, ripple <= 1 %
    //    = 0.043 dB per block (eps). The ripple is DETERMINISTIC, so its effect
    //    is bounded by the absolute-value sum of the least-squares weights, not
    //    by the iid formula e sqrt(12/(n(n^2-1))) (that one is an RMS over
    //    random phases and understates the worst phase; lane-end LOW E2 -- it
    //    read 0.18 dB where the worst phase reaches 0.41 dB). Slope weight of
    //    block k on n = 10 fit blocks: (k - kbar)/Sxx, Sxx = n(n^2-1)/12 = 82.5,
    //    sum|k - kbar| = 25, so |slope error| <= 25/82.5 eps = 0.0130 dB/block,
    //    read 24 blocks past the fit's centre -> 0.31 dB. Intercept weights are
    //    convex (1/n each): <= eps = 0.043 dB.
    //  * The slope also sets tau = 10/(slope ln10): 0.0130/2 = 0.65 % = 0.028 dB.
    // Worst case <= 0.04 + 0.31 + 0.043 + 0.028 = 0.42 dB. The bound used is
    // 0.45 dB.
    const std::size_t n = static_cast<std::size_t>(1.2 * kFs);

    struct Case { bool osc; double w; double phase; double p0; };
    const Case cases[] = {
        {false, 0.0, 0.0, 1.0},
        {true, 0.0208, 0.0, 0.5},
        {true, 0.0208, 0.9, 0.5},
        {true, 0.0208, 1.8, 0.5},
        {true, 0.0208, 2.7, 0.5},
    };

    double worstOldSeedErrDb = 0.0;
    for (const auto& c : cases) {
        const auto h = makeTone(n, c.osc, c.w, c.phase);
        const auto curve = rta::ir::energyDecayCurve(h, 0, kFs, 0.0);
        REQUIRE(curve.has());

        const double cut = static_cast<double>(curve.crossingIndex);
        const double expected = c.p0 * std::exp(-2.0 * cut / kTa) * kTa / 2.0;
        const double errDb = 10.0 * std::log10(curve.truncationCorrection / expected);
        INFO("osc=" << c.osc << " phase=" << c.phase << " cut=" << cut
             << " got=" << curve.truncationCorrection << " expected=" << expected
             << " errDb=" << errDb);
        CHECK(std::abs(errDb) < 0.45);

        // What the old one-sample seed would have read, to show the fixture can
        // fail: squared sample just before the cut, times the same tau.
        const auto k = static_cast<std::size_t>(cut) - 1;
        const double old = static_cast<double>(h[k]) * static_cast<double>(h[k]) * kTa / 2.0;
        worstOldSeedErrDb = std::max(worstOldSeedErrDb,
                                     std::abs(10.0 * std::log10(old / expected)));
    }
    // The oscillating phases cover the cos^2 cycle, so at least one of them
    // puts the single-sample seed several dB off. Without this the cases above
    // could pass for the old implementation too.
    CHECK(worstOldSeedErrDb > 3.0);
}

TEST_CASE("A late burst after the decay met the noise does not drag the fit",
          "[ir][decay][truncation]") {
    // Lundeby ends the regression at the FIRST block that reaches
    // noise + standoff, walking forward from the peak. Taking the LAST block
    // above that line instead lets a late event (door, HVAC cycle) that rises
    // back above it drag the fit's end out to itself, across the whole knee.
    //
    // Fixture: the exact exponential of this file (T60 = 0.5 s power, so the
    // expected answer is 0.5 s by construction) plus a 30 ms burst of constant
    // amplitude at 1.1 s, 15 dB below the peak -- inside the 20 dB fit range, so
    // an end-of-fit that reaches it pulls the slope flat. The floor sits at
    // -64 dB, so the burst is 49 dB above it and is "usable" by level alone.
    //
    // The EDC of a single exponential is itself an exponential, so T is exact
    // up to the floor's 1e-4 power share and the truncation term; 1 % is far
    // wider than the measured 0.03 % and far narrower than the failure, which
    // is a REFUSAL (no crossing) -- measured with the old rule.
    const std::size_t n = static_cast<std::size_t>(1.5 * kFs);
    auto h = makeTone(n, false, 0.0, 0.0);
    const auto from = static_cast<std::size_t>(1.1 * kFs);
    const auto len = static_cast<std::size_t>(0.03 * kFs);
    const double amp = std::pow(10.0, -15.0 / 20.0);
    for (std::size_t i = from; i < from + len; ++i) {
        h[i] += static_cast<float>(amp * std::cos(kTwoPi * 0.05 * static_cast<double>(i)));
    }

    const auto curve = rta::ir::energyDecayCurve(h, 0, kFs, 0.0);
    REQUIRE(curve.has());
    CHECK_THAT(curve.lateDecaySec, WithinAbs(kT60, 0.01 * kT60));
    CHECK(curve.crossingIndex < from);   // the cut is on the decay, not the burst
}

TEST_CASE("The truncation correction is the fitted line at the crossing, not the noise estimate",
          "[ir][decay][truncation]") {
    // Lane-end LOW E3. After the final iteration the noise estimate and the line
    // at the crossing are the same number BY CONSTRUCTION (the crossing is where
    // the line meets the noise), so a fixture that converges cannot tell
    // `line * tau` from `noisePower * tau`. They part only when the loop ends
    // before it converges: with `maxIterations = 1` the crossing comes from the
    // first noise guess (last tenth of the record) and the noise is then
    // RE-ESTIMATED from crossing + margin to the end of the record. Put a burst in
    // that stretch (the fixture of the late-burst case below) and the re-estimate
    // is dragged up by the burst while the line is still the room.
    //
    // Expected, from the fixture alone: exp(-2 cut / ta) * ta / 2 (plain, p0 = 1).
    // The re-estimate a `noisePower` correction would use is the mean of h^2 from
    // start = cut + 10 dB / (dB per sample) to the end, computed here from h
    // itself; `mutantDb` is how far that sits above the expected value. The
    // bound is half of it: a correction that reads the noise estimate misses by
    // `mutantDb`, the right one by the first-window bias (sinh(x)/x with x = 1440
    // / ta = 0.41 -> 0.12 dB) plus the floor's share of the last fit block
    // (<= 0.4 dB), both far inside it.
    const std::size_t n = static_cast<std::size_t>(1.5 * kFs);
    auto h = makeTone(n, false, 0.0, 0.0);
    const auto from = static_cast<std::size_t>(1.1 * kFs);
    const auto len = static_cast<std::size_t>(0.03 * kFs);
    const double amp = std::pow(10.0, -15.0 / 20.0);
    for (std::size_t i = from; i < from + len; ++i) {
        h[i] += static_cast<float>(amp * std::cos(kTwoPi * 0.05 * static_cast<double>(i)));
    }

    rta::ir::DecayConfig config;
    config.maxIterations = 1;
    const auto curve = rta::ir::energyDecayCurve(h, 0, kFs, 0.0, config);
    REQUIRE(curve.has());
    const double cut = static_cast<double>(curve.crossingIndex);
    REQUIRE(curve.crossingIndex < from);

    const double expected = std::exp(-2.0 * cut / kTa) * kTa / 2.0;
    const double dbPerSample = 10.0 * std::log10(std::exp(2.0 / kTa));
    const auto start = static_cast<std::size_t>(cut + config.noiseMarginDb / dbPerSample);
    double sum = 0.0;
    for (std::size_t i = start; i < n; ++i) sum += static_cast<double>(h[i]) * static_cast<double>(h[i]);
    const double tailMean = sum / static_cast<double>(n - start);
    const double mutantDb = 10.0 * std::log10(tailMean * kTa / 2.0 / expected);
    INFO("cut=" << cut << " mutantDb=" << mutantDb << " got=" << curve.truncationCorrection
         << " expected=" << expected);
    REQUIRE(mutantDb > 20.0);   // the fixture can tell the two apart

    const double errDb = 10.0 * std::log10(curve.truncationCorrection / expected);
    CHECK(std::abs(errDb) < mutantDb / 2.0);
}
