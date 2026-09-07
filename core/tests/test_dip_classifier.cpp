// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for classifyDip -- lane L7, sub-lane L7-EQ, Task B
// (docs/plans/2026-09-07-L7-eq-impl-plan.md). Cases B1-B4. "The test this
// record exists for" (record docs/dsp/2026-09-06-l7-auto-eq.md Sec.9.4):
// the two-path pair with IDENTICAL magnitude and OPPOSITE verdicts is the
// case no magnitude-only allocator can separate (Sec.1.2).

#include <catch2/catch_test_macros.hpp>

#include "rta/eq/DipClassifier.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace rta::dsp;
using namespace rta::eq;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr std::size_t kNFft = 16384;
constexpr double kSampleRate = 48000.0;

struct CombFixture {
    std::vector<float> magnitude;
    std::vector<std::complex<double>> h;
    std::vector<std::uint8_t> trusted;
    std::vector<float> hz;
    std::vector<float> residualDb;  // this test's stand-in for the allocator's r_k: 20*log10|H|
};

CombFixture buildComb(double a, double d) {
    const std::size_t m = kNFft / 2 + 1;
    CombFixture f;
    f.magnitude.resize(m);
    f.h.resize(m);
    f.trusted.assign(m, 1);
    f.hz.resize(m);
    f.residualDb.resize(m);
    const double binWidthHz = kSampleRate / static_cast<double>(kNFft);
    for (std::size_t k = 0; k < m; ++k) {
        const double theta = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(kNFft);
        const std::complex<double> hk = 1.0 + a * std::polar(1.0, -theta * d);
        f.h[k] = hk;
        const double mag = std::abs(hk);
        f.magnitude[k] = static_cast<float>(mag);
        f.hz[k] = static_cast<float>(static_cast<double>(k) * binWidthHz);
        f.residualDb[k] = static_cast<float>(20.0 * std::log10(std::max(mag, 1e-12)));
    }
    return f;
}

/// Walks out from an analytic guess to the true local-minimum bin, then
/// finds the nearest local-maximum bin on each flank -- the same shape
/// EqAllocator's own placement (Task D) will use, simplified for this
/// test's own fixture-building (a comb has many periods; only the FIRST
/// notch away from DC is used, so the neighbourhood search stays local).
struct Region { std::size_t fL, fStar, fR; };

Region findNotchRegion(std::span<const float> magnitude, std::size_t guess) {
    std::size_t fStar = guess;
    bool improved = true;
    while (improved) {
        improved = false;
        if (fStar > 0 && magnitude[fStar - 1] < magnitude[fStar]) { --fStar; improved = true; }
        else if (fStar + 1 < magnitude.size() && magnitude[fStar + 1] < magnitude[fStar]) {
            ++fStar; improved = true;
        }
    }
    std::size_t fL = fStar;
    while (fL > 0 && magnitude[fL - 1] >= magnitude[fL]) --fL;
    std::size_t fR = fStar;
    while (fR + 1 < magnitude.size() && magnitude[fR + 1] >= magnitude[fR]) ++fR;
    return Region{ fL, fStar, fR };
}

/// The SECOND notch (theta = 3*pi/D), not the first (theta = pi/D): the
/// first notch's left flank sits AT DC (k=0, the comb's global peak) for
/// every D used in this file, and DC is where ExcessPhase.cpp's own
/// interpolation carve-out (ExcessPhase.h step 2's documented "no log10(0)"
/// rule) and every edge fallback in groupDelaySeconds concentrate their own
/// error -- picking the interior notch keeps this file's assertions about
/// the CLASSIFIER's own math from being entangled with those two, unrelated,
/// already-measured edge effects (test_excess_phase.cpp's A1 already covers
/// the edge-bin residual on its own terms).
std::size_t interiorNotchGuess(double d) {
    return static_cast<std::size_t>(3.0 * static_cast<double>(kNFft) / (2.0 * d));
}

}  // namespace

TEST_CASE("The two-path pair with identical magnitude classifies opposite verdicts",
          "[dip_classifier]") {
    // B1 (first). a=0.8 (minimum phase) and a=1.25 (not) at D=144 samples,
    // 48 kHz: |1+a e^{-jw}| = a|1+e^{-jw}/a| makes the two magnitudes
    // coincide bin for bin (Sec.1.2) -- the classifier is handed the SAME
    // depth either way and must still separate them on phase alone.
    const auto mpFixture = buildComb(0.8, 144.0);
    const auto nmpFixture = buildComb(1.25, 144.0);

    // Magnitudes coincide bin for bin AFTER the 'a' offset (record Sec.9.4:
    // "|1 + a e^{-jw}| = a|1 + e^{-jw}/a|" -- a constant 20*log10(1.25) dB
    // shift, not raw equality; that offset is exactly why the DEPTH read off
    // either curve comes out the same below -- it cancels in a peak-to-notch
    // difference).
    for (std::size_t k = 0; k < mpFixture.magnitude.size(); ++k) {
        const float expectedNmp = static_cast<float>(1.25 * mpFixture.magnitude[k]);
        REQUIRE(std::abs(nmpFixture.magnitude[k] - expectedNmp) <=
                1e-5f * std::max(expectedNmp, 1.0f));
    }

    const auto region = findNotchRegion(mpFixture.magnitude,
                                        interiorNotchGuess(144.0));
    CAPTURE(region.fL, region.fStar, region.fR);

    const auto mpXp = excessPhase(mpFixture.magnitude, mpFixture.h, mpFixture.trusted,
                                  kSampleRate, /*oversampleFactor=*/32);
    const auto nmpXp = excessPhase(nmpFixture.magnitude, nmpFixture.h, nmpFixture.trusted,
                                   kSampleRate, /*oversampleFactor=*/32);
    REQUIRE(mpXp.valid);
    REQUIRE(nmpXp.valid);

    const auto mp = classifyDip(mpXp, mpFixture.hz, region.fL, region.fStar, region.fR,
                                mpFixture.residualDb, /*isBoost=*/true);
    const auto nmp = classifyDip(nmpXp, nmpFixture.hz, region.fL, region.fStar, region.fR,
                                 nmpFixture.residualDb, /*isBoost=*/true);

    CAPTURE(mp.depthDb, mp.swingRad, mp.thresholdRad);
    CHECK(mp.verdict == DipVerdict::Boostable);
    CHECK(std::abs(mp.swingRad) <= 0.01);  // Shape-A-scale: reconstruction noise only

    CAPTURE(nmp.depthDb, nmp.swingRad, nmp.thresholdRad);
    CHECK(nmp.verdict == DipVerdict::NotMinimumPhase);
    CHECK(std::abs(nmp.depthDb - 19.08) <= 0.1);        // D ~= 19.1 dB (record Sec.9.4)
    CHECK(std::abs(nmp.swingRad - 4.0 * std::asin(0.8)) <= 0.02);  // S ~= 3.71 rad
    CHECK(std::abs(nmp.thresholdRad - 2.0 * std::asin(0.8)) <= 0.02);  // S* ~= 1.85 rad

    // Same depth for both (the identical-magnitude point of the whole test).
    CHECK(std::abs(mp.depthDb - nmp.depthDb) <= 0.05);
}

TEST_CASE("Shallow dips separate by their own depth-derived threshold, not a fixed angle",
          "[dip_classifier]") {
    // B2. a in {0.5, 2} (S = 120 deg) and {0.25, 4} (S = 58 deg, D ~= 4.4 dB,
    // S* = 29 deg) -- a fixed 90 deg threshold would misclassify the second
    // pair's NMP side as Boostable (record Sec.4.4); the depth-derived S*
    // must not.
    struct Case { double a; double expectedSwingDeg; double expectedThresholdDeg; };
    const Case cases[] = {
        { 2.0, 120.0, 60.0 },
        { 4.0, 4.0 * std::asin(0.25) * 180.0 / kPi, 2.0 * std::asin(0.25) * 180.0 / kPi },
    };

    for (const auto& c : cases) {
        CAPTURE(c.a);
        const auto fixture = buildComb(c.a, 144.0);
        const auto region = findNotchRegion(fixture.magnitude,
                                            interiorNotchGuess(144.0));
        const auto xp = excessPhase(fixture.magnitude, fixture.h, fixture.trusted, kSampleRate,
                                    /*oversampleFactor=*/32);
        REQUIRE(xp.valid);
        const auto result = classifyDip(xp, fixture.hz, region.fL, region.fStar, region.fR,
                                        fixture.residualDb, /*isBoost=*/true);

        const double swingDeg = result.swingRad * 180.0 / kPi;
        const double thresholdDeg = result.thresholdRad * 180.0 / kPi;
        CAPTURE(swingDeg, c.expectedSwingDeg, thresholdDeg, c.expectedThresholdDeg);
        CHECK(std::abs(swingDeg - c.expectedSwingDeg) <= 1.0);
        CHECK(std::abs(thresholdDeg - c.expectedThresholdDeg) <= 0.5);
        CHECK(result.verdict == DipVerdict::NotMinimumPhase);  // a > 1 in every case here
    }
}

TEST_CASE("The verdict's fields are carried for display, not re-gated", "[dip_classifier]") {
    // B3. D, S, S* and their margin S/S* are all reportable directly off the
    // returned struct -- classifyDip does not drop or hide the candidate,
    // it only reports (Polarity.h:103-110's lesson, restated here since this
    // file has no analogous memory entry of its own yet).
    const auto fixture = buildComb(1.25, 144.0);
    const auto region = findNotchRegion(fixture.magnitude,
                                        interiorNotchGuess(144.0));
    const auto xp = excessPhase(fixture.magnitude, fixture.h, fixture.trusted, kSampleRate,
                                /*oversampleFactor=*/32);
    REQUIRE(xp.valid);
    const auto result = classifyDip(xp, fixture.hz, region.fL, region.fStar, region.fR,
                                    fixture.residualDb, /*isBoost=*/true);

    CHECK(result.depthDb > 0.0);
    CHECK(result.swingRad > 0.0);
    CHECK(result.thresholdRad > 0.0);
    const double margin = result.swingRad / result.thresholdRad;
    CAPTURE(margin);
    CHECK(margin > 1.0);  // NMP: swing exceeds the threshold
}

TEST_CASE("A cut candidate at the same NMP dip still reads NotMinimumPhase honestly",
          "[dip_classifier]") {
    // B4. classifyDip's own verdict does not change with isBoost -- it is
    // EqAllocator's job (Task D) to act only on isBoost==true. This proves
    // the classifier itself never lies about the physics to make a cut look
    // safer, which is what "cuts not gated" actually rests on.
    const auto fixture = buildComb(1.25, 144.0);
    const auto region = findNotchRegion(fixture.magnitude,
                                        interiorNotchGuess(144.0));
    const auto xp = excessPhase(fixture.magnitude, fixture.h, fixture.trusted, kSampleRate,
                                /*oversampleFactor=*/32);
    REQUIRE(xp.valid);
    const auto asBoost = classifyDip(xp, fixture.hz, region.fL, region.fStar, region.fR,
                                     fixture.residualDb, /*isBoost=*/true);
    const auto asCut = classifyDip(xp, fixture.hz, region.fL, region.fStar, region.fR,
                                   fixture.residualDb, /*isBoost=*/false);

    CHECK(asBoost.verdict == DipVerdict::NotMinimumPhase);
    CHECK(asCut.verdict == DipVerdict::NotMinimumPhase);
    CHECK(asBoost.swingRad == asCut.swingRad);
    CHECK(asBoost.depthDb == asCut.depthDb);
}

TEST_CASE("An untrusted excessPhase result classifies Untrusted", "[dip_classifier]") {
    const auto fixture = buildComb(1.25, 144.0);
    std::vector<std::uint8_t> almostNoneTrusted(fixture.trusted.size(), 0);
    almostNoneTrusted[0] = 1;  // below excessPhase's own kMinTrustedBins
    const auto xp = excessPhase(fixture.magnitude, fixture.h, almostNoneTrusted, kSampleRate,
                                /*oversampleFactor=*/8);
    REQUIRE_FALSE(xp.valid);

    const auto region = findNotchRegion(fixture.magnitude,
                                        interiorNotchGuess(144.0));
    const auto result = classifyDip(xp, fixture.hz, region.fL, region.fStar, region.fR,
                                    fixture.residualDb, /*isBoost=*/true);
    CHECK(result.verdict == DipVerdict::Untrusted);
}

TEST_CASE("classifyDip refuses malformed regions", "[dip_classifier]") {
    const auto fixture = buildComb(1.25, 144.0);
    const auto xp = excessPhase(fixture.magnitude, fixture.h, fixture.trusted, kSampleRate,
                                /*oversampleFactor=*/8);
    REQUIRE(xp.valid);

    SECTION("fStar out of [fL, fR] order") {
        CHECK_THROWS_AS(classifyDip(xp, fixture.hz, 100, 50, 200, fixture.residualDb, true),
                        std::invalid_argument);
    }
    SECTION("fR past the array") {
        CHECK_THROWS_AS(
            classifyDip(xp, fixture.hz, 10, 20, fixture.hz.size(), fixture.residualDb, true),
            std::invalid_argument);
    }
    SECTION("mismatched array lengths") {
        std::vector<float> shortHz(10, 0.0f);
        CHECK_THROWS_AS(classifyDip(xp, shortHz, 1, 2, 3, fixture.residualDb, true),
                        std::invalid_argument);
    }
}
