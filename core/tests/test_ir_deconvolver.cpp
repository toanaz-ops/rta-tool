// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L4a: linear deconvolution of a swept-sine response.
// Decisions 1, 2, 3, 4 and 9 of docs/dsp/2026-08-30-sweep-ir-l4a.md.
//
// Every expectation here comes from a closed form, or from a measured table the
// record names and tools/verify_l4a_pulse.py re-derives. Where a figure is a
// regression lock against a measurement rather than a derivation, it says so.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/gen/Sweep.h"
#include "rta/ir/Deconvolver.h"

#include <cmath>
#include <limits>
#include <cstddef>
#include <stdexcept>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rta::gen::Sweep;

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kStartHz    = 100.0;
constexpr double kEndHz      = 10000.0;
constexpr double kDuration   = 2.0;

double lengthConstant() { return kDuration / std::log(kEndHz / kStartHz); }

/// Two octaves of fade-in, stated in SECONDS so this fixture does not depend on
/// `Sweep::Config`'s default fade width. Record decision 5 measures the
/// pre-arrival artefact floor as a function of fade width in octaves, and two
/// octaves is where it reaches -108.7 dB; `octaves * ln2 * L` is that width in
/// seconds.
double twoOctaveFadeSec() { return 2.0 * std::log(2.0) * lengthConstant(); }

Sweep::Config wideFadeConfig() {
    Sweep::Config cfg;
    cfg.sampleRate  = kSampleRate;
    cfg.startHz     = kStartHz;
    cfg.endHz       = kEndHz;
    cfg.durationSec = kDuration;
    cfg.fadeInSec   = twoOctaveFadeSec();
    return cfg;
}

/// f_lo = f1 * exp(fadeInSec / L): the frequency the sweep had reached when the
/// fade-in finished. With the fade expressed in octaves this collapses to
/// f1 * 2^octaves -- 400 Hz here -- so the lower band edge IS the fade width.
double validBandLowHz(const Sweep::Config& cfg) {
    const double fadeSec = static_cast<double>(
        Sweep(cfg).fadeInSamples()) / cfg.sampleRate;
    return cfg.startHz * std::exp(fadeSec / lengthConstant());
}

/// f_hi = f2 * exp(-fadeOutSec / L). The fade-out is NOT clamped in octaves and
/// must not be: measured, a wider fade-out makes the artefact floor worse by
/// about 3.5 dB per octave, the opposite of the fade-in.
double validBandHighHz(const Sweep::Config& cfg) {
    return cfg.endHz * std::exp(-cfg.fadeOutSec / lengthConstant());
}

std::vector<float> renderSweep(const Sweep& sweep) {
    std::vector<float> out(sweep.lengthSamples());
    Sweep(sweep).process(out);   // copy: leaves the caller's counter at zero
    return out;
}

rta::ir::DeconvolverConfig plainConfig() {
    return { kSampleRate, lengthConstant(), 1.0 };
}

std::size_t indexOfLargest(const std::vector<float>& v, std::size_t from, std::size_t to) {
    std::size_t best = from;
    for (std::size_t i = from; i < to && i < v.size(); ++i)
        if (std::abs(v[i]) > std::abs(v[best])) best = i;
    return best;
}

}  // namespace

TEST_CASE("The analysis pulse peaks at Ninv-1, positive", "[ir][deconv]") {
    Sweep sweep(wideFadeConfig());
    const auto excitation = renderSweep(sweep);
    const auto inverse = sweep.buildInverseFilter();

    const auto result = rta::ir::deconvolve(excitation, inverse, plainConfig());

    // Closed form: inv[m] = s[Ns-1-m]*e[m], so at lag k = Ns-1 every term of
    // the convolution sum becomes s[j]^2 * e[Ns-1-j] -- all non-negative, the
    // one lag at which the sum adds coherently. Hence the peak sits at Ninv-1,
    // and its sign is POSITIVE, which is what makes polarity readable at all.
    REQUIRE(result.originIndex == inverse.size() - 1);
    const auto peak = indexOfLargest(result.samples, 0, result.samples.size());
    CHECK(peak == result.originIndex);
    CHECK(result.samples[peak] > 0.0f);

    // Length is the full linear convolution: nothing is chopped, because the
    // negative-time region IS the distortion measurement a later lane reads.
    CHECK(result.samples.size() == excitation.size() + inverse.size() - 1);
    CHECK(result.harmonicSpacingL == lengthConstant());
}

TEST_CASE("A known response returns at the right index with the right gain",
          "[ir][deconv]") {
    Sweep sweep(wideFadeConfig());
    const auto excitation = renderSweep(sweep);
    const auto inverse = sweep.buildInverseFilter();

    constexpr std::size_t kDelay = 137;
    constexpr float kGain = 0.8f;
    std::vector<float> response(excitation.size() + kDelay, 0.0f);
    for (std::size_t i = 0; i < excitation.size(); ++i)
        response[i + kDelay] = kGain * excitation[i];

    const auto reference = rta::ir::deconvolve(excitation, inverse, plainConfig());
    const auto measured  = rta::ir::deconvolve(response, inverse, plainConfig());

    const auto peak = indexOfLargest(measured.samples, 0, measured.samples.size());
    CHECK(peak == measured.originIndex + kDelay);
    CHECK_THAT(static_cast<double>(measured.samples[peak])
                   / static_cast<double>(reference.samples[reference.originIndex]),
               WithinRel(static_cast<double>(kGain), 1.0e-3));
}

TEST_CASE("Harmonic packets land at -L*ln(N)", "[ir][deconv]") {
    Sweep sweep(wideFadeConfig());
    const auto excitation = renderSweep(sweep);
    const auto inverse = sweep.buildInverseFilter();

    // A memoryless square law produces H2 and nothing else of interest; a cubic
    // produces H3. Two orders, so the LOGARITHMIC spacing is exercised rather
    // than a single offset that any monotone law would also have fitted.
    for (int order : { 2, 3 }) {
        std::vector<float> distorted(excitation.size());
        for (std::size_t i = 0; i < excitation.size(); ++i) {
            const float x = excitation[i];
            distorted[i] = x + 0.10f * (order == 2 ? x * x : x * x * x);
        }
        const auto result = rta::ir::deconvolve(distorted, inverse, plainConfig());

        const double expected = static_cast<double>(result.originIndex)
                              + result.harmonicOffsetSamples(order);
        // Search between this packet and the next lower one, so H3 cannot
        // re-find H2. The 5 ms guard keeps the window off both neighbours.
        const auto guard = static_cast<double>(0.005 * kSampleRate);
        const auto hi = static_cast<std::size_t>(expected + guard);
        const auto lo = static_cast<std::size_t>(static_cast<double>(result.originIndex)
                            + result.harmonicOffsetSamples(order + 1) + guard);
        REQUIRE(lo < hi);
        const auto found = indexOfLargest(result.samples, lo, hi);

        CAPTURE(order, expected, found);
        // +/-3 samples, and the reason it is not tighter is worth stating.
        // Measured for THIS fixture the packet peaks 2.44 samples late for H2
        // and 1.18 early for H3; for a 20 Hz-20 kHz sweep the same measurement
        // gives 0.0 and -0.1. The residual is real, config-dependent, and
        // exactly what Novak's synchronisation condition (f1*L an integer)
        // exists to remove -- see record decision 3's qualifier. The claim with
        // teeth here is not the last sample, it is the LOGARITHMIC spacing:
        // a wrong sign or a wrong base misses by thousands of samples, and the
        // two orders together rule out any single fixed offset.
        CHECK_THAT(static_cast<double>(found), WithinAbs(expected, 3.0));
    }
}

TEST_CASE("Deconvolve refuses input it cannot interpret", "[ir][deconv]") {
    const std::vector<float> ok(1024, 0.1f);
    const std::vector<float> empty;
    CHECK_THROWS_AS(rta::ir::deconvolve(ok, empty, plainConfig()), std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::deconvolve(empty, ok, plainConfig()), std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::deconvolve(ok, ok, { 0.0, 0.4, 1.0 }), std::invalid_argument);

    // A response SHORTER than the inverse filter cannot contain a whole sweep,
    // so originIndex would index past the end. Refuse rather than hand back a
    // structure whose stated origin is not in the data.
    const std::vector<float> tooShort(64, 0.1f);
    CHECK_THROWS_AS(rta::ir::deconvolve(tooShort, ok, plainConfig()), std::invalid_argument);
}

TEST_CASE("Normalisation centres the valid band on 0 dB", "[ir][deconv]") {
    const auto cfg = wideFadeConfig();
    Sweep sweep(cfg);
    const auto excitation = renderSweep(sweep);
    const auto inverse = sweep.buildInverseFilter();

    const double lowHz = validBandLowHz(cfg);      // 400 Hz for this fixture
    const double highHz = validBandHighHz(cfg);
    CHECK_THAT(lowHz, WithinRel(kStartHz * 4.0, 1.0e-3));   // f1 * 2^2

    const auto reference = rta::ir::deconvolve(excitation, inverse, plainConfig());
    const double gain = rta::ir::inBandNormalisation(reference, lowHz, highHz);
    REQUIRE(gain > 0.0);

    auto normalisedConfig = plainConfig();
    normalisedConfig.normalisationGain = gain;
    const auto normalised = rta::ir::deconvolve(excitation, inverse, normalisedConfig);
    const auto flat = rta::ir::bandFlatness(normalised, lowHz, highHz);

    // Normalising an ALREADY normalised deconvolution must ask for a gain of
    // exactly 1: the in-band mean magnitude is now unity, and count/sum is 1.
    // That is the assertion with teeth. A first draft checked the midpoint of
    // bandFlatness's dB range instead, which cannot test this at all --
    // bandFlatness divides by the band's own mean internally, so its output is
    // invariant under normalisationGain, and the midpoint of a range is not a
    // mean anyway. It read -20.1 dB and was measuring the pulse's shape.
    CHECK_THAT(rta::ir::inBandNormalisation(normalised, lowHz, highHz),
               WithinRel(1.0, 1.0e-4));

    // REGRESSION LOCK, labelled as one. Record decision 4's table has a row for
    // exactly this fixture -- 100 Hz-10 kHz, 2 s, two-octave fade-in, band
    // 400-9549.9 Hz -- measured at -0.03..+0.14 dB, a span of 0.17. The bound
    // is 0.30: clear of the float32 transform, and far below the 10.65 dB span
    // a narrow fade-in produces, which the next case pins from the other side.
    //
    // This could not be asserted until buildInverseFilter stopped applying a
    // second Tukey layer to the inverse kernel; before that, the number here
    // measured the defect rather than the analysis pulse.
    CHECK(flat.maxDb - flat.minDb < 0.30);
    CHECK(flat.maxDb > flat.minDb);
    CHECK(std::isfinite(flat.minDb));
    CHECK(std::isfinite(flat.maxDb));
}

TEST_CASE("A narrow fade-in is measurably less flat", "[ir][deconv]") {
    // The falsifier for the case above, from the other side. If bandFlatness
    // returned a constant, or measured the wrong band, these two could not
    // separate -- and they separate by a factor of sixty. Measured 10.65 dB of
    // span for this fixture against 0.17 dB with the two-octave fade.
    auto cfg = wideFadeConfig();
    cfg.fadeInSec = 0.001;      // below the 2/startHz floor, so that floor wins
    Sweep narrow(cfg);
    const auto excitation = renderSweep(narrow);
    const auto inverse = narrow.buildInverseFilter();

    const auto reference = rta::ir::deconvolve(excitation, inverse, plainConfig());
    const auto flat = rta::ir::bandFlatness(reference,
                                            validBandLowHz(cfg), validBandHighHz(cfg));
    CAPTURE(flat.minDb, flat.maxDb);
    CHECK(flat.maxDb - flat.minDb > 5.0);
}

TEST_CASE("The band queries refuse a band they cannot answer for", "[ir][deconv]") {
    Sweep sweep(wideFadeConfig());
    const auto excitation = renderSweep(sweep);
    const auto inverse = sweep.buildInverseFilter();
    const auto reference = rta::ir::deconvolve(excitation, inverse, plainConfig());

    CHECK_THROWS_AS(rta::ir::inBandNormalisation(reference, 0.0, 1000.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::inBandNormalisation(reference, 1000.0, 1000.0),
                    std::invalid_argument);
    CHECK_THROWS_AS(rta::ir::bandFlatness(reference, 2000.0, 1000.0),
                    std::invalid_argument);
    // A band above Nyquist contains no bins at all; returning 1.0 quietly would
    // hand the caller a normalisation derived from nothing.
    CHECK_THROWS_AS(rta::ir::inBandNormalisation(reference, 30000.0, 40000.0),
                    std::invalid_argument);
}

TEST_CASE("A capture truncated inside the decay is visibly wrong", "[ir][deconv]") {
    // Record decision 9: the capture must outlast the sweep by 1.5x RT60.
    // Measured, at 0.5x the worst bin is still out by 4.61 dB while the rms has
    // already fallen to 0.07 dB -- so this asserts a WORST-case departure, and
    // asserts that the bad case IS bad, not only that the good case is good.
    Sweep sweep(wideFadeConfig());
    const auto excitation = renderSweep(sweep);
    const auto inverse = sweep.buildInverseFilter();

    // A room with an exactly known decay: sparse taps every 97 samples on a
    // 10^(-3t/RT60) envelope, RT60 = 0.25 s. Sparse so the direct convolution
    // below stays cheap; the tail is what this case is about, not its density.
    constexpr double kRt60 = 0.25;
    const auto irLength = static_cast<std::size_t>(2.0 * kRt60 * kSampleRate);
    std::vector<float> room(irLength, 0.0f);
    room[0] = 1.0f;
    for (std::size_t i = 97; i < irLength; i += 97)
        room[i] = 0.25f * static_cast<float>(
            std::pow(10.0, -3.0 * static_cast<double>(i) / (kRt60 * kSampleRate)));

    std::vector<float> full(excitation.size() + irLength - 1, 0.0f);
    for (std::size_t j = 0; j < irLength; ++j) {
        if (room[j] == 0.0f) continue;
        const float tap = room[j];
        for (std::size_t i = 0; i < excitation.size(); ++i)
            full[i + j] += excitation[i] * tap;
    }

    const auto gap = static_cast<std::size_t>(0.5 * kRt60 * kSampleRate);
    const std::span<const float> truncated(full.data(), excitation.size() + gap);

    const auto whole = rta::ir::deconvolve(full, inverse, plainConfig());
    const auto cut   = rta::ir::deconvolve(truncated, inverse, plainConfig());

    double worstDb = 0.0;
    for (std::size_t i = 0; i < irLength; ++i) {
        const double a = std::abs(static_cast<double>(whole.samples[whole.originIndex + i]));
        const double b = std::abs(static_cast<double>(cut.samples[cut.originIndex + i]));
        if (a > 1.0e-4) worstDb = std::max(worstDb, std::abs(20.0 * std::log10(b / a)));
    }
    CAPTURE(worstDb);
    CHECK(worstDb > 1.0);   // truncation is NOT harmless, and the suite says so
}
