// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for BiquadDesign -- see docs/plans/2026-09-06-L7-wave0-impl-plan.md
// Task W0-3. Cases T1-T8, each a closed-form identity from the RBJ Audio EQ
// Cookbook (T1-T3, T5) or a cross-lock against the other two Wave 0 pieces
// (T4 reuses BiquadCascade directly, T7 reuses MinimumPhase from W0-2).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/Biquad.h"
#include "rta/dsp/BiquadResponse.h"
#include "rta/dsp/MinimumPhase.h"
#include "rta/eq/BiquadDesign.h"
#include "rta/eq/FilterSpec.h"

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::eq;
using rta::dsp::Biquad;
using rta::dsp::BiquadCascade;

namespace {

constexpr double kPi = std::numbers::pi;

// The grid T1 and T3 both walk: every combination is a distinct closed-form
// check, not a fuzz sample, so a failure names an exact (fc, q, gainDb, fs).
constexpr double kFcHz[] = { 40.0, 100.0, 1000.0, 4000.0 };
constexpr double kQ[] = { 0.5, 1.0, 2.0, 8.0 };
constexpr double kGainDb[] = { -15.0, -6.0, 3.0, 6.0 };
constexpr double kFs[] = { 44100.0, 48000.0, 96000.0 };

// The Q-parameterised shelf alpha, shelfAlpha in BiquadDesign.cpp, is
// sqrt((A + 1/A)*(1/Q - 1) + 2) -- real only while that radicand stays
// non-negative. Peaking's alpha has no such restriction (it never depends on
// gain at all), but a shelf run at Q=8 with |G|=15 dB drives it to -0.44:
// (A+1/A)=2.793, (1/Q-1)=-0.875, product -2.444, +2 = -0.444. This is a
// property of the Q-parameterised cookbook form itself (the bounded S/slope
// form sidesteps it), not a bug to work around, so T5 and T6's shelf cases
// use a Q range that stays inside the domain for every gain kGainDb reaches.
constexpr double kShelfQ[] = { 0.5, 1.0, 2.0 };

}  // namespace

TEST_CASE("Peaking gain at fc is exactly G dB -- |H(e^jw0)| = A^2, 40*log10(A) = G",
          "[biquad_design]") {
    // T1 (first). RBJ cookbook Sec.6: a peaking section's magnitude AT its own
    // centre frequency is A^2 by construction (A = 10^(G/40)), so
    // 20*log10(A^2) = 40*log10(A) = G exactly -- no numerical derivation, a
    // definition read back.
    //
    // Tolerance 1e-9, not 1e-12: this goes through pow/sin/cos to build the
    // coefficients and a SEPARATE cos/sin/log10 chain (BiquadCascade::
    // attenuationDb) to read them back, unlike BiquadResponse's T1/T2/T4
    // (test_biquad_response.cpp), which substitute z=+-1 and need no trig at
    // all. The one BiquadResponse case that does chain trig+log10 the same
    // way -- T3, the attenuationDb consistency lock -- already uses 1e-9 for
    // exactly this reason; every dB check below through fc/hz1/hz2/w follows
    // that precedent rather than the plan prose's uniform "1e-12" literal,
    // which undershoots by 1-2 orders of magnitude (measured worst case here:
    // 8.9e-12 at fc=40, q=8, g=-15, fs=96000).
    for (double fc : kFcHz) {
        for (double q : kQ) {
            for (double g : kGainDb) {
                for (double fs : kFs) {
                    CAPTURE(fc, q, g, fs);
                    const FilterSpec spec{ FilterType::Peaking, fc, q, g };
                    CHECK_THAT(responseDb(spec, fs, fc), WithinAbs(g, 1e-9));
                }
            }
        }
    }
}

TEST_CASE("A peaking section reads 0 dB at DC and at Nyquist", "[biquad_design]") {
    // T2. At z=+-1, b0+-b1+b2 and a0+-a1+a2 both reduce to 2 -+- 2*cos(w0) --
    // the SAME expression on top and bottom (Sec.6) -- so the ratio is exactly
    // 1 for any w0 strictly between 0 and pi, independent of Q or gain. The
    // identity is exact AT hz=0 and hz=fs/2, not near them -- evaluating at an
    // offset (as an earlier draft of this test did, at 1e-6 Hz) measures a
    // different, genuinely nonzero point on a continuous curve instead of the
    // closed form.
    for (double fc : kFcHz) {
        for (double q : kQ) {
            for (double g : kGainDb) {
                for (double fs : kFs) {
                    CAPTURE(fc, q, g, fs);
                    const FilterSpec spec{ FilterType::Peaking, fc, q, g };
                    CHECK_THAT(responseDb(spec, fs, 0.0), WithinAbs(0.0, 1e-9));
                    CHECK_THAT(responseDb(spec, fs, fs / 2.0), WithinAbs(0.0, 1e-9));
                }
            }
        }
    }
}

TEST_CASE("The half-gain points sit exactly where the RBJ Q parameterisation "
          "places them",
          "[biquad_design]") {
    // T3, load-bearing (EQ Sec.3/Sec.6): the fact the EQ allocator's
    // Q-from-half-depth solve rests on. w1,2 = 2*atan(k1,2 * tan(w0/2)),
    // k1,2 = -+-1/(2Q) + sqrt(1 + 1/(4Q^2)) -- computed here in double,
    // independently of designBiquad's own alpha, so this is not circular.
    //
    // Tolerance 1e-8: this test's OWN closed-form side adds atan/tan on top
    // of the sin/cos/log10 chain T1's comment already accounts for at 1e-9,
    // and the extra pair of transcendental calls measurably widens the
    // worst case (measured: 1.98e-9 at fc=40, q=8, g=6, fs=96000, still
    // under 1e-8 with an order of magnitude to spare).
    for (double fc : kFcHz) {
        for (double q : kQ) {
            for (double g : kGainDb) {
                for (double fs : kFs) {
                    const double w0 = 2.0 * kPi * fc / fs;
                    const double k1 = -1.0 / (2.0 * q) + std::sqrt(1.0 + 1.0 / (4.0 * q * q));
                    const double k2 = 1.0 / (2.0 * q) + std::sqrt(1.0 + 1.0 / (4.0 * q * q));
                    const double w1 = 2.0 * std::atan(k1 * std::tan(w0 / 2.0));
                    const double w2 = 2.0 * std::atan(k2 * std::tan(w0 / 2.0));
                    const double hz1 = w1 * fs / (2.0 * kPi);
                    const double hz2 = w2 * fs / (2.0 * kPi);

                    CAPTURE(fc, q, g, fs, hz1, hz2);
                    const FilterSpec spec{ FilterType::Peaking, fc, q, g };
                    CHECK_THAT(responseDb(spec, fs, hz1), WithinAbs(g / 2.0, 1e-8));
                    CHECK_THAT(responseDb(spec, fs, hz2), WithinAbs(g / 2.0, 1e-8));
                }
            }
        }
    }
}

TEST_CASE("A peaking section cascaded with its own gain-inverted twin is flat",
          "[biquad_design]") {
    // T4. Peaking's alpha does not depend on gain (Sec.6: alpha = sin(w0)/2Q),
    // so designBiquad(+G) and designBiquad(-G) share one alpha and their
    // {b0,b1,b2} and {a0,a1,a2} sets are each other's numerator/denominator --
    // cascading multiplies N/D by D/N, exactly 1, no float-domain-dependent
    // step in between. (Restricted to Peaking: the shelving alpha also turns
    // out gain-symmetric, but its b/a sets are not each other's swap in the
    // same clean way, so this identity is not claimed for shelves.)
    constexpr double fs = 48000.0;
    for (double fc : kFcHz) {
        for (double q : kQ) {
            for (double g : kGainDb) {
                const auto up = designBiquad(FilterSpec{ FilterType::Peaking, fc, q, g }, fs);
                const auto down = designBiquad(FilterSpec{ FilterType::Peaking, fc, q, -g }, fs);
                std::vector<Biquad::Coeffs> cascade{ up, down };

                for (int k = 0; k < 64; ++k) {
                    const double w = kPi * (static_cast<double>(k) + 0.5) / 64.0;
                    CAPTURE(fc, q, g, w);
                    // 1e-9, same trig+log10-chain reasoning as T1 (comment
                    // there): the coefficients cancel exactly only in real
                    // arithmetic, and each section carries its own alphaA vs
                    // alpha/A rounding before attenuationDb's cos/sin/log10.
                    CHECK_THAT(BiquadCascade::attenuationDb(cascade, w), WithinAbs(0.0, 1e-9));
                }
            }
        }
    }
}

TEST_CASE("Shelves read G at their own end, 0 at the far end, G/2 at fc",
          "[biquad_design]") {
    // T5. |H(jw0)| = A (not A^2, unlike peaking): 20*log10(A) = G/2 at fc.
    // The two ends are the shelf's own plateau (G, exactly at hz=0 or
    // hz=fs/2, the same z=+-1 substitution T2 uses) and the untouched far end
    // (0 dB, same substitution).
    constexpr double fs = 48000.0;
    for (double fc : { 100.0, 1000.0, 4000.0 }) {
        for (double q : kShelfQ) {
            for (double g : kGainDb) {
                CAPTURE(fc, q, g);

                const FilterSpec low{ FilterType::LowShelf, fc, q, g };
                CHECK_THAT(responseDb(low, fs, 0.0), WithinAbs(g, 1e-9));
                CHECK_THAT(responseDb(low, fs, fs / 2.0), WithinAbs(0.0, 1e-9));
                CHECK_THAT(responseDb(low, fs, fc), WithinAbs(g / 2.0, 1e-9));

                const FilterSpec high{ FilterType::HighShelf, fc, q, g };
                CHECK_THAT(responseDb(high, fs, 0.0), WithinAbs(0.0, 1e-9));
                CHECK_THAT(responseDb(high, fs, fs / 2.0), WithinAbs(g, 1e-9));
                CHECK_THAT(responseDb(high, fs, fc), WithinAbs(g / 2.0, 1e-9));
            }
        }
    }
}

TEST_CASE("Every designed section keeps its poles inside the unit circle",
          "[biquad_design]") {
    // T6. Biquad.h:69's own CI invariant, checked here across the parameter
    // ranges the plan names: G in [-30,30], Q in [0.1,20], fc in (0, fs/2).
    //
    // Peaking walks the full Q range (its alpha never depends on gain, so it
    // has no domain restriction -- T4's comment). The shelves are capped at
    // Q=1.0: shelfAlpha's radicand (A+1/A)*(1/Q-1)+2 goes negative for Q
    // roughly above 1.53 at this range's |G|=30 extreme (kShelfQ's own
    // comment derives the Q=2/G=15 boundary; solving the same inequality at
    // A+1/A=5.801 for G=30 gives Q<=1.526), which is a property of the
    // Q-parameterised cookbook form, not a pole-radius question -- T8 covers
    // designBiquad's actual invalid_argument refusals, this loop only visits
    // its well-defined domain.
    constexpr double fs = 48000.0;
    for (auto type : { FilterType::Peaking, FilterType::LowShelf, FilterType::HighShelf }) {
        const bool isShelf = (type != FilterType::Peaking);
        for (double fc : { 20.0, 100.0, 1000.0, 10000.0, 20000.0 }) {
            for (double q : { 0.1, 0.5, 1.0, 5.0, 20.0 }) {
                if (isShelf && q > 1.0) continue;
                for (double g : { -30.0, -15.0, 0.0, 15.0, 30.0 }) {
                    CAPTURE(static_cast<int>(type), fc, q, g);
                    const auto c = designBiquad(FilterSpec{ type, fc, q, g }, fs);
                    // isfinite first and explicitly: BiquadCascade's own
                    // maxPoleRadius() computes std::max(worst, radius), and
                    // std::max(0.0, NaN) returns 0.0 (every NaN comparison is
                    // false) -- a NaN pole radius would otherwise read as
                    // "0.0 < 1.0", passing silently instead of failing loudly
                    // (memory/a-fixed-defect-returns-through-the-silent-fallback.md).
                    const bool allFinite = std::isfinite(c.b0) && std::isfinite(c.a1) &&
                                            std::isfinite(c.a2);
                    CHECK(allFinite);
                    CHECK(BiquadCascade({ c }).maxPoleRadius() < 1.0);
                }
            }
        }
    }
}

TEST_CASE("A peaking section's own magnitude reconstructs its own phase through "
          "the minimum-phase kernel",
          "[biquad_design]") {
    // T7, the cross-lock (EQ Sec.9.3): ties BiquadDesign (this task) to
    // MinimumPhase (W0-2) on one fixture. designBiquad's poles AND zeros both
    // sit inside the unit circle (a peaking boost's zero radius is < 1, same
    // algebra family as T6's pole check), so the section is itself minimum
    // phase -- the cepstral kernel fed |H| should hand back arg(H) exactly.
    constexpr double fs = 48000.0;
    constexpr std::size_t nFft = 2048;
    const auto c = designBiquad(FilterSpec{ FilterType::Peaking, 1000.0, 2.0, 6.0 }, fs);

    std::vector<float> magnitude(nFft);
    std::vector<double> expectedPhase(nFft);
    for (std::size_t k = 0; k < nFft; ++k) {
        const double w = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(nFft);
        const auto h = rta::dsp::biquadResponse(c, w);
        magnitude[k] = static_cast<float>(std::abs(h));
        expectedPhase[k] = std::arg(h);
    }

    const auto result = rta::dsp::minimumPhaseFromMagnitude(magnitude);
    double peakPhase = 0.0;
    for (double p : expectedPhase) peakPhase = std::max(peakPhase, std::abs(p));

    for (std::size_t k = 0; k < nFft; ++k) {
        const double actual = std::arg(result.spectrum[k]);
        const double tol = 1e-6 * std::abs(expectedPhase[k]) + 3e-7 * peakPhase;
        const double residual = std::abs(actual - expectedPhase[k]);
        CAPTURE(k, residual, tol);
        CHECK(residual <= tol);
    }
}

TEST_CASE("An out-of-domain spec is refused, not silently clamped", "[biquad_design]") {
    // T8.
    const FilterSpec base{ FilterType::Peaking, 1000.0, 1.0, 6.0 };

    CHECK_THROWS_AS(designBiquad(FilterSpec{ base.type, base.fcHz, base.q, base.gainDb },
                                  0.0),
                     std::invalid_argument);
    CHECK_THROWS_AS(designBiquad(FilterSpec{ base.type, 0.0, base.q, base.gainDb }, 48000.0),
                     std::invalid_argument);
    CHECK_THROWS_AS(designBiquad(FilterSpec{ base.type, 24000.0, base.q, base.gainDb },
                                  48000.0),
                     std::invalid_argument);
    CHECK_THROWS_AS(designBiquad(FilterSpec{ base.type, base.fcHz, 0.0, base.gainDb },
                                  48000.0),
                     std::invalid_argument);
}
