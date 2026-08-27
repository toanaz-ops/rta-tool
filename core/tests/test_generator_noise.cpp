// SPDX-License-Identifier: AGPL-3.0-or-later
//
// White and pink noise -- RMS-referenced level, spectral shape, decorrelation.
// See docs/dsp/2026-08-27-generator.md ("Level conventions") and
// docs/plans/2026-08-27-generator-impl-plan.md section 4.3 for why these
// specific N, K, tolerances and not others: with K = 511 Welch frames a
// per-bin Welch estimate has relative sigma ~= 1/sqrt(511) ~= 4.4%, i.e.
// +-0.19 dB per bin; the slope fit (case 16) spans thousands of bins and each
// 1/3-octave band (cases 13, 17) sums dozens, so both land an order of
// magnitude inside their +-0.2 dB/oct and +-0.5 dB tolerances. The Welch
// log-domain estimator's own bias is E[log P] - log(true P) ~= -4.34/(2*511)
// ~= -0.004 dB -- negligible, but it is exactly the trap that makes a naive
// log-domain statistical test quietly biased at small K, so it is recorded
// here rather than silently relied on. A statistical test failing here is a
// real bug to find, not a tolerance to widen or an N to raise -- the FEAT
// acceptance numbers (-3.01 dB/oct +-0.2, 1/3-octave flatness +-0.5 dB) are
// binding, not tunable knobs (CLAUDE.md verification standard; plan section
// 4.3 / handoff trap 4).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/BandWeights.h"
#include "rta/dsp/OctaveBands.h"
#include "rta/dsp/RealFft.h"
#include "rta/dsp/SpectrumEngine.h"
#include "rta/gen/Noise.h"
#include "rta/gen/Prng.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::dsp;
using namespace rta::gen;

namespace {

// Shared fixture parameters for the statistical cases (12, 13, 16, 17, 18,
// 19). See the file banner for why these exact numbers are not knobs.
constexpr double kFs = 48000.0;
constexpr std::size_t kFftSize = 16384;
constexpr std::size_t kHop = 8192;
constexpr std::size_t kFixtureSamples = std::size_t(1) << 22;  // 4194304, ~87.4 s
// (kFixtureSamples - kFftSize) / kHop + 1 == 511 Welch frames, matching the
// K used to justify every tolerance in the file banner above.

std::vector<rta::test::GoldenCase> generatorGolden() {
    static const auto cases =
        rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/generator.txt");
    return cases;
}

const rta::test::GoldenCase& findCase(const std::vector<rta::test::GoldenCase>& cases,
                                       const std::string& name) {
    for (const auto& c : cases) {
        if (c.name == name) return c;
    }
    throw std::runtime_error("golden case not found: " + name);
}

/// Runs 2^22 samples of white or pink noise through the shared Welch fixture
/// and returns the resulting power spectral density, one value per bin.
/// Fed in 64k chunks -- not a single process() call -- for the same reason
/// test_spectrum.cpp feeds Welch golden input in awkward chunks: a real
/// caller never hands over the whole signal in one block.
std::vector<float> fixtureDensity(bool pink, std::uint64_t seed) {
    SpectrumEngine::Config config;
    config.fftSize = kFftSize;
    config.hopSize = kHop;
    config.sampleRate = kFs;
    config.window = WindowType::Hann;
    config.averaging = Averaging::Linear;
    SpectrumEngine engine(config);

    std::vector<float> chunk(65536);
    WhiteNoise white(Pcg32(seed, 1));
    PinkNoise pinkGen(Pcg32(seed, 1));

    std::size_t remaining = kFixtureSamples;
    while (remaining > 0) {
        const std::size_t n = std::min(chunk.size(), remaining);
        std::span<float> s(chunk.data(), n);
        if (pink) pinkGen.process(s); else white.process(s);
        engine.process(s);
        remaining -= n;
    }
    REQUIRE(engine.frameCount() == 511);
    return std::vector<float>(engine.density().begin(), engine.density().end());
}

/// Least-squares slope of y against x, dy/dx. Used to fit dB against log2(Hz)
/// -- an ordinary linear regression, no special DSP content, but written out
/// so the slope test (16) is not "whatever the code printed".
double leastSquaresSlope(const std::vector<double>& x, const std::vector<double>& y) {
    const auto n = static_cast<double>(x.size());
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        sx += x[i];
        sy += y[i];
        sxx += x[i] * x[i];
        sxy += x[i] * y[i];
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

double pearsonCorrelation(const std::vector<float>& a, const std::vector<float>& b) {
    double meanA = 0.0, meanB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) { meanA += a[i]; meanB += b[i]; }
    meanA /= (double) a.size();
    meanB /= (double) b.size();

    double cov = 0.0, varA = 0.0, varB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double da = a[i] - meanA, db = b[i] - meanB;
        cov += da * db;
        varA += da * da;
        varB += db * db;
    }
    return cov / std::sqrt(varA * varB);
}

double rmsOf(const std::vector<float>& samples) {
    double sumSq = 0.0;
    for (const float s : samples) sumSq += (double) s * (double) s;
    return std::sqrt(sumSq / (double) samples.size());
}

/// Builds OctaveBands(3, 50, 10000) + BandWeights over the shared fixture's
/// FFT size, and asserts every band it returns is fully resolved -- handoff
/// trap 9: a band spanning fewer than 3 bins is a measurement of the window,
/// not of the source, and a test that skipped this check would be trusting
/// the range choice rather than verifying it.
BandWeights resolvedBandWeights() {
    OctaveBands bands(3, 50.0, 10000.0);
    BandWeights weights(bands, kFftSize, kFs);
    for (std::size_t i = 0; i < weights.size(); ++i) {
        CAPTURE(i);
        REQUIRE_FALSE(weights.band(i).underResolved);
    }
    return weights;
}

/// bandPower -> dB, then every band's deviation from the SET's own mean dB.
/// Shared by cases 13 and 17, with one deliberate difference between them --
/// see `normaliseByBandwidth` below, which is a considered DEVIATION from
/// the plan's literal wording of case 13, recorded here rather than silently
/// applied (CLAUDE.md verification standard: an assertion must be
/// independently justified, and "the plan said so" does not survive contact
/// with the arithmetic below).
///
/// `BandWeights::apply` returns *integrated* power over each band --
/// `density * weight` summed and multiplied by `binWidthHz_` -- not a power
/// *density*. A 1/3-octave band is constant-PERCENTAGE bandwidth: its width
/// in Hz grows in proportion to its centre frequency (`upper - lower ~=
/// 0.2316 * fc` for 1/3-octave). So for a source with genuinely FLAT power
/// spectral density (white noise), the integrated band power necessarily
/// RISES at +3.01 dB/octave from 50 Hz to 10 kHz -- not because anything is
/// biased, but because each higher band is integrating a proportionally
/// wider slice of an equally-tall density. This is the textbook reason a
/// pink -- not white -- reference is used to read flat on a constant-Q / RTA
/// display (also why SMPTE ST 2095-1 standardises pink, not white, for
/// calibration; docs/dsp/2026-08-27-generator.md cites the same standard for
/// pink's crest factor). It is also the OTHER HALF of why pink's own
/// unnormalised band power (case 17) comes out flat: pink's density falls at
/// -3.01 dB/octave (case 16), and that fall exactly cancels the bandwidth's
/// +3.01 dB/octave rise. The two cannot both be "flat in raw band power" --
/// they differ in density slope by construction, so their raw-band-power
/// slopes must differ by exactly the same 3.01 dB/octave.
///
/// Measured: driving this exact helper's un-normalised path with white noise
/// gives a ~20 dB rise from the lowest to the highest resolved band, in
/// close agreement with the ~23 dB (`10*log10(10000/50)`) the bandwidth
/// argument above predicts -- confirming the arithmetic, not an
/// implementation bug in WhiteNoise, SpectrumEngine or BandWeights.
///
/// So "is the analysis chain unbiased across frequency" (case 13's actual
/// intent, per the plan) is tested here by dividing each band's power by its
/// own bandwidth (`upper - lower`) before taking dB, recovering an
/// average-density-per-band quantity that IS flat for a flat-PSD source. The
/// band-response shape is identical in normalised (per-band) log-frequency
/// coordinates for every band (`BandWeights::responseAt`), so this division
/// is off from the band's true noise-equivalent bandwidth by at most a fixed
/// multiplicative constant, common to every band -- which cancels exactly in
/// a "deviation from the set's own mean" comparison. Case 17 (pink) does NOT
/// normalise: raw band power is the quantity the FEAT acceptance row is
/// about, and it is already flat for the right physical reason.
std::vector<double> bandDeviationsFromMeanDb(const BandWeights& weights,
                                              const std::vector<float>& density,
                                              bool normaliseByBandwidth) {
    std::vector<float> bandPower(weights.size());
    weights.apply(density, bandPower);

    std::vector<double> db(weights.size());
    double meanDb = 0.0;
    for (std::size_t i = 0; i < bandPower.size(); ++i) {
        double value = (double) bandPower[i];
        if (normaliseByBandwidth) {
            const auto& band = weights.bands()[i];
            value /= (band.upper - band.lower);
        }
        db[i] = 10.0 * std::log10(value);
        meanDb += db[i];
    }
    meanDb /= (double) db.size();

    std::vector<double> deviations(db.size());
    for (std::size_t i = 0; i < db.size(); ++i) deviations[i] = db[i] - meanDb;
    return deviations;
}

}  // namespace

TEST_CASE("White noise hits its RMS-referenced level", "[generator][noise]") {
    // setLevelDbFsRms(d) must make the long-run RMS equal 10^(d/20) -- the
    // defining property of an RMS-referenced control (docs/dsp/
    // 2026-08-27-generator.md, "Level conventions").
    for (const double targetDb : { -12.0, -20.0, -6.0 }) {
        CAPTURE(targetDb);
        WhiteNoise gen(Pcg32(1, 1), targetDb);
        std::vector<float> samples(1u << 20);
        gen.process(samples);

        const double measuredDb = 20.0 * std::log10(rmsOf(samples));
        CHECK_THAT(measuredDb, WithinAbs(targetDb, 0.05));
    }
}

TEST_CASE("White noise is flat across 1/3-octave bands", "[generator][noise]") {
    // Bandwidth-normalised (see bandDeviationsFromMeanDb's comment for why):
    // raw, unnormalised 1/3-octave band power is NOT expected to be flat for
    // white noise -- it rises ~3 dB/octave because the bands themselves get
    // wider with frequency. Normalising by each band's own bandwidth
    // recovers an average-density-per-band quantity, which flat-PSD white
    // noise does make flat, and that is the actual property this test
    // exists to check: an unbiased analysis chain.
    const auto weights = resolvedBandWeights();
    const auto density = fixtureDensity(/*pink=*/false, /*seed=*/2);
    const auto deviations = bandDeviationsFromMeanDb(weights, density, /*normaliseByBandwidth=*/true);
    for (const double d : deviations) CHECK_THAT(d, WithinAbs(0.0, 0.5));
}

TEST_CASE("The Kellet pink filter reproduces the golden output", "[generator][noise][golden]") {
    // `cases` must be named: findCase() returns a reference into its
    // argument, and an unnamed generatorGolden() temporary dies at the end
    // of THIS statement -- binding straight to
    // findCase(generatorGolden(), ...) leaves goldenCase dangling (a
    // deterministic SIGSEGV on next use, once the freed memory is reused).
    const auto cases = generatorGolden();
    const auto& goldenCase = findCase(cases, "pink_kellet");
    const auto& expectedWhite = goldenCase.row("white");
    const auto& expectedPink = goldenCase.row("pink");
    REQUIRE(expectedWhite.size() == 4096);
    REQUIRE(expectedPink.size() == 4096);

    // The white row first, as a hard REQUIRE: if the two PRNG transcriptions
    // (this file's C++ one, tools/gen_generator.py's Python one) do not
    // already agree bit-exactly, the pink comparison below is comparing two
    // unrelated signals and any mismatch it reports would misdirect a reader
    // toward the filter coefficients (plan handoff trap 1).
    Pcg32 whiteCheck(7, 1);
    for (std::size_t i = 0; i < 4096; ++i) {
        REQUIRE(whiteCheck.nextUniform() == (float) expectedWhite[i]);
    }

    // Same seed/sequence, so the SAME white stream feeds this fresh filter.
    PinkNoise pink(Pcg32(7, 1));
    const double scale = std::pow(10.0, kDefaultNoiseLevelDbFsRms / 20.0) *
                          std::sqrt(3.0) / PinkNoise::kRmsGainVsWhite;
    double peak = 0.0;
    for (const double v : expectedPink) peak = std::max(peak, std::abs(v * scale));

    for (std::size_t i = 0; i < 4096; ++i) {
        CAPTURE(i);
        const double actual = (double) pink.nextSample();
        const double expected = expectedPink[i] * scale;
        CHECK_THAT(actual, WithinAbs(expected, 1.0e-6 * peak));
    }
}

TEST_CASE("The pink RMS gain constant matches its analytic value", "[generator][noise][golden]") {
    // Same dangling-reference trap as "pink_kellet" above.
    const auto cases = generatorGolden();
    const auto& goldenCase = findCase(cases, "pink_rms_gain");
    const auto& value = goldenCase.row("value");
    REQUIRE(value.size() == 1);
    CHECK_THAT(PinkNoise::kRmsGainVsWhite, WithinRel(value.front(), 1.0e-6));
}

TEST_CASE("Pink noise falls at -3.01 dB per octave", "[generator][noise]") {
    const auto density = fixtureDensity(/*pink=*/true, /*seed=*/3);

    SpectrumEngine::Config config;
    config.fftSize = kFftSize;
    config.sampleRate = kFs;
    SpectrumEngine probe(config);  // only used for binFrequency(); not processed

    std::vector<double> logFreq, levelDb;
    for (std::size_t k = 0; k < density.size(); ++k) {
        const double f = probe.binFrequency(k);
        if (f < 50.0 || f > 10000.0) continue;
        logFreq.push_back(std::log2(f));
        levelDb.push_back(10.0 * std::log10((double) density[k]));
    }
    REQUIRE(logFreq.size() > 1000);  // thousands of bins, per the file banner

    const double slope = leastSquaresSlope(logFreq, levelDb);
    CHECK_THAT(slope, WithinAbs(-10.0 * std::log10(2.0), 0.2));
}

TEST_CASE("Pink noise is flat in 1/3-octave bands within 0.5 dB", "[generator][noise]") {
    // FEAT acceptance row. Raw, un-normalised band power -- unlike the white
    // test above, pink's -3.01 dB/octave density fall exactly cancels the
    // 1/3-octave bands' own +3.01 dB/octave bandwidth growth, so this is the
    // one case where raw integrated band power comes out flat by itself.
    const auto weights = resolvedBandWeights();
    const auto density = fixtureDensity(/*pink=*/true, /*seed=*/3);
    const auto deviations = bandDeviationsFromMeanDb(weights, density, /*normaliseByBandwidth=*/false);
    for (const double d : deviations) CHECK_THAT(d, WithinAbs(0.0, 0.5));
}

TEST_CASE("Two channels seeded from one master are uncorrelated", "[generator][noise]") {
    constexpr std::uint64_t kMaster = 424242;
    PinkNoise ch0(ChannelSeeds::forChannel(kMaster, 0));
    PinkNoise ch1(ChannelSeeds::forChannel(kMaster, 1));

    std::vector<float> a(1u << 18), b(1u << 18);
    ch0.process(a);
    ch1.process(b);

    CHECK(std::abs(pearsonCorrelation(a, b)) < 0.02);
}

TEST_CASE("Pink noise crest factor sits in the SMPTE ST 2095-1 range", "[generator][noise]") {
    // RANGE CHECK, not a conformance claim: SMPTE ST 2095-1 itself fixes
    // calibration pink noise at 11.5-12 dB crest. The wider [10, 14] dB
    // window here is the sampling spread of one finite 2^20-sample run, not
    // a re-statement of the standard's own tolerance.
    PinkNoise gen(Pcg32(5, 1));
    std::vector<float> samples(1u << 20);
    gen.process(samples);

    double peak = 0.0;
    for (const float s : samples) peak = std::max(peak, (double) std::abs(s));
    const double crestDb = 20.0 * std::log10(peak / rmsOf(samples));

    CHECK(crestDb >= 10.0);
    CHECK(crestDb <= 14.0);
}
