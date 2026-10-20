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
    //    = 0.043 dB per block. LS slope error from ripple e on n = 10 fit blocks
    //    is e sqrt(12/(n(n^2-1))) = 0.0047 dB/block, read 24 blocks past the
    //    fit's centre -> 0.11 dB; intercept error e/sqrt(n) = 0.014 dB.
    //  * The slope also sets tau = 10/(slope ln10): 0.0047/2 = 0.24 %.
    // Total <= 0.04 + 0.11 + 0.014 + 0.01 = 0.18 dB. The bound used is 0.25 dB.
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
        CHECK(std::abs(errDb) < 0.25);

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
