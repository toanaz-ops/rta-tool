// SPDX-License-Identifier: AGPL-3.0-or-later
//
// TDD sequence for FilterBank -- see
// docs/plans/2026-08-27-filterbank-impl-plan.md section 5.3. Cases F1-F8.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/ButterworthDesign.h"
#include "rta/dsp/FilterBank.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace rta::dsp;

namespace {

/// Find the bank index whose Band::centre is closest to `hz`.
std::size_t nearestBandIndex(const FilterBank& bank, double hz) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < bank.size(); ++i) {
        if (std::abs(bank.band(i).centre - hz) < std::abs(bank.band(best).centre - hz)) best = i;
    }
    return best;
}

std::vector<float> unitSine(double freqHz, double sampleRate, std::size_t n) {
    std::vector<float> out(n);
    const double w = 2.0 * std::numbers::pi * freqHz / sampleRate;
    for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<float>(std::sin(w * static_cast<double>(i)));
    return out;
}

const rta::test::GoldenCase& findCase(const std::vector<rta::test::GoldenCase>& cases,
                                       const std::string& name) {
    return *std::find_if(cases.begin(), cases.end(), [&](const auto& c) { return c.name == name; });
}

/// Runs `tc`'s `input` through a default-Config bank and checks meanSquare()
/// against the golden `band_power` row. `checkGeometry` also checks
/// `index`/`centre` -- F4 and F5 share a bank layout, so only one needs to.
void checkBandPowers(const rta::test::GoldenCase& tc, bool checkGeometry) {
    FilterBank::Config config;
    config.sampleRate = tc.row("fs")[0];
    FilterBank bank(config);

    const auto& expectedPower = tc.row("band_power");
    REQUIRE(bank.size() == expectedPower.size());
    // No settling requirement: both sides process the identical samples
    // through the identical filters, transient included.
    bank.process(tc.floatRow("input"));

    for (std::size_t i = 0; i < bank.size(); ++i) {
        CAPTURE(i);
        if (checkGeometry) {
            CHECK(bank.band(i).index == static_cast<int>(tc.row("index")[i]));
            CHECK_THAT(bank.band(i).centre, WithinRel(tc.row("centre")[i], 1e-9));
        }
        // meanSquare() is float32 by design (house pattern, FilterBank.h).
        // Measured: narrowing a double accumulator to float32 alone
        // contributes up to ~3.5e-8 relative error, so 1e-9 (the plan's
        // figure, predating a float-narrowed accessor to measure against)
        // isn't achievable here; 1e-6 keeps two orders of margin above it.
        CHECK_THAT((double) bank.meanSquare()[i], WithinRel(expectedPower[i], 1e-6));
    }
}

}  // namespace

TEST_CASE("A tone at a band centre reads its mean square in that band", "[filterbank]") {
    constexpr double fs = 48000.0;

    FilterBank::Config config;
    config.sampleRate = fs;
    FilterBank bank(config);

    const std::size_t i = nearestBandIndex(bank, 3981.07);
    const double centre = bank.band(i).centre;

    // Run 0.1 s to let the filter's own transient settle, reset(), then run
    // 0.4 s so the averaged mean square reads a settled measurement -- not the
    // startup ring of a 12-pole resonator.
    bank.process(unitSine(centre, fs, static_cast<std::size_t>(0.1 * fs)));
    bank.reset();
    bank.process(unitSine(centre, fs, static_cast<std::size_t>(0.4 * fs)));

    // A unit-amplitude sine has mean square 1/2 by the closed form
    // <sin^2> = 1/2 (1 - cos(2wt)), averaged over many periods.
    CHECK_THAT((double) bank.meanSquare()[i], WithinAbs(0.5, 0.002));
}

TEST_CASE("A tone is rejected by neighbouring bands as the design goal says", "[filterbank]") {
    constexpr double fs = 48000.0;
    constexpr double kSections = 6.0;  // NOT BandWeights::kFilterOrder -- plan trap 11

    FilterBank::Config config;
    config.sampleRate = fs;
    FilterBank bank(config);

    const double g = std::pow(10.0, 3.0 / 10.0);
    const double ratio = std::pow(g, 1.0 / 3.0);  // one band away
    const double halfSpan = std::pow(g, 1.0 / 6.0) - std::pow(g, -1.0 / 6.0);
    const double designGoalDb =
        10.0 * std::log10(1.0 + std::pow((ratio - 1.0 / ratio) / halfSpan, 2.0 * kSections));
    // Cross-check against the plan's independently measured value before
    // trusting the rest of this test on it.
    REQUIRE_THAT(designGoalDb, WithinAbs(36.47, 0.01));

    // A band's own decay time scales with 1/fm at constant relative
    // bandwidth, so the lowest bands need close to a quarter second to
    // settle; reset() (FilterBank.cpp) discards the average without
    // discarding that settling, and a fixed 1 s settle covers every band trap
    // 8 leaves in.
    int checked = 0;
    for (std::size_t i = 0; i + 1 < bank.size(); ++i) {
        const double fm = bank.band(i).centre;
        if (fm * ratio >= fs / 8.0) continue;  // trap 8: stay clear of the fold

        bank.reset();
        // One continuous-phase tone sliced at the settle/measured boundary,
        // NOT two separate unitSine() calls both starting at phase 0 -- that
        // discontinuity is itself a step input, re-triggering exactly the
        // startup ring the settle phase exists to get away from.
        const std::size_t settleLen = static_cast<std::size_t>(1.0 * fs);
        const std::size_t measuredLen = static_cast<std::size_t>(0.3 * fs);
        const auto whole = unitSine(fm, fs, settleLen + measuredLen);
        bank.process(std::span(whole).first(settleLen));
        bank.reset();
        bank.process(std::span(whole).subspan(settleLen));

        const double onLevel = bank.meanSquare()[i];
        const double neighbourLevel = bank.meanSquare()[i + 1];
        CAPTURE(fm, onLevel, neighbourLevel);

        const double rejectionDb = 10.0 * std::log10(onLevel / std::max<double>(neighbourLevel, 1e-30));
        CHECK(rejectionDb >= 30.0);
        ++checked;
    }
    REQUIRE(checked > 0);
}

TEST_CASE("reset() discards the running mean square and the sample count", "[filterbank]") {
    FilterBank::Config config;
    FilterBank bank(config);

    bank.process(unitSine(1000.0, config.sampleRate, 4096));
    REQUIRE(bank.sampleCount() == 4096);

    bank.reset();
    CHECK(bank.sampleCount() == 0);
    for (std::size_t i = 0; i < bank.size(); ++i) {
        CHECK(bank.meanSquare()[i] == 0.0f);
        CHECK(bank.smoothed()[i] == 0.0f);
    }
}

TEST_CASE("Band powers on white noise match scipy", "[filterbank][golden]") {
    const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/filterbank.txt");
    checkBandPowers(findCase(cases, "noise_fs48000_frac3"), /*checkGeometry=*/true);
}

TEST_CASE("Band powers on an on-centre tone match scipy", "[filterbank][golden]") {
    const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/filterbank.txt");
    checkBandPowers(findCase(cases, "tone_fs48000_idx0"), /*checkGeometry=*/false);
}

TEST_CASE("The impulse response matches scipy sosfilt sample by sample", "[filterbank][golden]") {
    const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/filterbank.txt");

    // idx-17 (19.9526 Hz) is BELOW the 20 Hz floor of the standard 1/3-octave
    // table, deliberately, to stress the design chain at an extreme low
    // frequency. Design it directly via ButterworthDesign rather than looking
    // it up in a constructed FilterBank -- a range wide enough to contain
    // both indices supplies the band edges, matching gen_filterbank.py.
    const OctaveBands wideBands(3, 10.0, 20000.0);

    for (const std::string& name : { "impulse_fs48000_idx-17", "impulse_fs48000_idx0" }) {
        CAPTURE(name);
        const auto& tc = findCase(cases, name);
        const int index = name == "impulse_fs48000_idx-17" ? -17 : 0;
        const double fs = tc.row("fs")[0];

        const auto& band = *std::find_if(wideBands.bands().begin(), wideBands.bands().end(),
                                          [&](const auto& b) { return b.index == index; });
        const auto design = ButterworthDesign::bandPass(band.lower, band.upper, fs,
                                                          FilterBank::kDefaultSections);

        const auto& expected = tc.row("impulse");
        std::vector<float> impulse(expected.size(), 0.0f);
        impulse[0] = 1.0f;
        std::vector<float> out(expected.size());
        BiquadCascade(design.sections).process(impulse, out);

        for (std::size_t n = 0; n < expected.size(); ++n) {
            CAPTURE(n);
            // BiquadCascade::process narrows to float32 per sample -- same
            // reason as the 1e-6 note on the golden band-power cases above.
            CHECK_THAT((double) out[n], WithinRel(expected[n], 1e-6) || WithinAbs(expected[n], 1e-18));
        }
    }
}

TEST_CASE("Processing in chunks equals processing in one block", "[filterbank]") {
    FilterBank::Config config;
    FilterBank chunked(config);
    FilterBank whole(config);

    std::vector<float> samples(16384);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        samples[i] = static_cast<float>(0.3 * std::sin(0.013 * i) + 0.1 * std::sin(0.29 * i));
    }

    for (std::size_t offset = 0; offset < samples.size(); offset += 1024) {
        chunked.process(std::span(samples).subspan(offset, 1024));
    }
    whole.process(samples);

    REQUIRE(chunked.sampleCount() == whole.sampleCount());
    for (std::size_t i = 0; i < chunked.size(); ++i) {
        CAPTURE(i);
        CHECK(chunked.meanSquare()[i] == whole.meanSquare()[i]);
        CHECK(chunked.smoothed()[i] == whole.smoothed()[i]);
    }
}

TEST_CASE("reset() preserves cascade filter memory, not just accumulators", "[filterbank]") {
    // Regression lock for FilterBank.h's reset() contract: reset() clears the
    // accumulators but must NOT touch each cascade's own filter memory.
    //
    // A naive proof (settle, reset(), measure window A; reset() again,
    // measure the next window B; require A =~ B) does NOT catch a
    // state-clearing reset(): the broken reset() before BOTH windows
    // corrupts them identically, so A and B still agree -- just at the
    // wrong, ring-suppressed level (confirmed 2026-08-27, see task report).
    //
    // This form instead compares against a reference that never calls
    // reset(). meanSquare() is a running mean since construction, so a
    // trailing window's mean is reconstructable with no reset() involved:
    //   mean(window alone) = (mean(settle+window)*(settle+window)
    //                          - mean(settle)*settle) / window
    // A second, independent bank processes identical settle+window samples
    // with an actual reset() at the boundary. Matching cascade state makes
    // both sides average the same output sequence for the window; a
    // state-cleared reset() instead reads the filter's own startup ring,
    // diverging by orders of magnitude (confirmed by the same mutation).
    constexpr double fs = 48000.0;

    FilterBank::Config config;
    config.sampleRate = fs;

    FilterBank reference(config);
    // A low band: narrow relative bandwidth means the slowest ring, so a
    // state-cleared reset() reads furthest from the settled reference.
    const std::size_t i = nearestBandIndex(reference, 100.0);
    const double centre = reference.band(i).centre;

    const std::size_t settleLen = static_cast<std::size_t>(1.0 * fs);
    const std::size_t windowLen = static_cast<std::size_t>(0.05 * fs);
    const auto whole = unitSine(centre, fs, settleLen + windowLen);
    const auto settleSpan = std::span(whole).first(settleLen);
    const auto windowSpan = std::span(whole).subspan(settleLen);

    // Reset()-free reference: reconstructs the window's mean square without
    // ever calling reset(), so a broken reset() cannot corrupt it.
    reference.process(settleSpan);
    const double sumAtSettleEnd =
        static_cast<double>(reference.meanSquare()[i]) * static_cast<double>(settleLen);
    reference.process(windowSpan);
    const double sumAtWindowEnd = static_cast<double>(reference.meanSquare()[i]) *
                                   static_cast<double>(settleLen + windowLen);
    const double windowOnly = (sumAtWindowEnd - sumAtSettleEnd) / static_cast<double>(windowLen);

    // The actual reset() call under test, on an independently constructed
    // (but identically configured) bank fed the same two slices.
    FilterBank underTest(config);
    underTest.process(settleSpan);
    underTest.reset();
    underTest.process(windowSpan);
    const double windowAfterReset = underTest.meanSquare()[i];

    CAPTURE(centre, windowOnly, windowAfterReset);
    CHECK_THAT(windowAfterReset, WithinRel(windowOnly, 1e-4));
}

TEST_CASE("A sample rate low enough drops the top band entirely", "[filterbank]") {
    // 20 kHz's actual base-ten centre is 19952.6 Hz (index 13 -- see "A band
    // that reaches Nyquist is clamped and says so" in test_butterworth.cpp).
    // At fs=32000, edgeLimit = 0.995*16000 = 15920, so 19952.6 >= edgeLimit
    // and FilterBank.cpp's `continue` in the ctor loop must fire for it: the
    // drop branch, not merely the clamp branch exercised at fs=44100 below.
    constexpr double fs = 32000.0;

    FilterBank::Config config;
    config.sampleRate = fs;
    FilterBank bank(config);

    const OctaveBands fullTable(config.fraction, config.lowestHz, config.highestHz, config.base);
    const auto& topOfTable = fullTable[fullTable.size() - 1];
    REQUIRE_THAT(topOfTable.centre, WithinAbs(19952.6, 1.0));
    REQUIRE(topOfTable.centre >= ButterworthDesign::kNyquistEdgeFraction * (fs / 2.0));

    REQUIRE(bank.size() < fullTable.size());

    // The dropped band is specifically the top one: no bank band carries the
    // table's top index, and the bank's own top band is a lower index.
    bool topIndexSurvived = false;
    for (std::size_t b = 0; b < bank.size(); ++b) {
        if (bank.band(b).index == topOfTable.index) topIndexSurvived = true;
    }
    CHECK_FALSE(topIndexSurvived);
    CHECK(bank.band(bank.size() - 1).index < topOfTable.index);
}

TEST_CASE("At 44100 the top band is clamped, not dropped, and the clamp propagates",
          "[filterbank]") {
    // Mirrors "A band that reaches Nyquist is clamped and says so"
    // (test_butterworth.cpp) one layer up: FilterBank must carry
    // ButterworthDesign::Result::nyquistClamped and its clamped upper edge
    // through to Band, because that flag is exactly what the Class-1 mask
    // check ("Every band meets the ANSI S1.11 Class 1 mask", also in
    // test_butterworth.cpp) excludes a band on -- an un-propagated flag would
    // let a clamped, off-design edge silently fail that mask instead of being
    // skipped.
    constexpr double fs = 44100.0;

    FilterBank::Config config;
    config.sampleRate = fs;
    FilterBank bank(config);

    const auto& top = bank.band(bank.size() - 1);
    REQUIRE_THAT(top.centre, WithinAbs(19952.6, 1.0));  // present, not dropped
    CHECK(top.nyquistClamped);

    // The exclusion boundary itself: the propagated edge is pulled back to
    // exactly the same 0.995*Nyquist ButterworthDesign::bandPass clamps to,
    // not the unclamped design edge (which would sit above Nyquist here).
    CHECK_THAT(top.upper,
               WithinRel(ButterworthDesign::kNyquistEdgeFraction * (fs / 2.0), 1e-12));
}

TEST_CASE("The detector rises and decays at its time constant", "[filterbank]") {
    constexpr double fs = 48000.0;

    SECTION("alpha closed form") {
        CHECK_THAT(FilterBank::detectorAlpha(0.125, fs), WithinRel(1.6665277854932548e-4, 1e-12));
        CHECK_THAT(FilterBank::detectorAlpha(1.0, fs), WithinRel(2.083311632095075e-5, 1e-12));
    }

    SECTION("step response at t=tau reads -1.99200085 dB relative to settled") {
        FilterBank::Config config;
        config.sampleRate = fs;
        config.timeConstantSeconds = FilterBank::kFastSeconds;
        FilterBank bank(config);

        const std::size_t i = nearestBandIndex(bank, 3981.07);
        bank.process(unitSine(bank.band(i).centre, fs,
                               static_cast<std::size_t>(FilterBank::kFastSeconds * fs)));

        const double stepDb = 10.0 * std::log10((double) bank.smoothed()[i] / 0.5);
        // 10*log10(1 - exp(-1)) = -1.99200085. Tolerance covers the band's own
        // ~7 ms settling against the 125 ms time constant.
        CHECK_THAT(stepDb, WithinAbs(-1.99200085, 0.15));
    }

    SECTION("decay slope matches 10*log10(e)/tau") {
        for (const double tau : { FilterBank::kFastSeconds, FilterBank::kSlowSeconds }) {
            CAPTURE(tau);
            FilterBank::Config config;
            config.sampleRate = fs;
            config.timeConstantSeconds = tau;
            FilterBank bank(config);

            const std::size_t i = nearestBandIndex(bank, 3981.07);
            bank.process(unitSine(bank.band(i).centre, fs, static_cast<std::size_t>(1.0 * fs)));

            // Let the filter's own ring die out before reading the
            // DETECTOR's own decay -- not the same thing (plan section 4.5).
            bank.process(std::vector<float>(static_cast<std::size_t>(0.1 * fs), 0.0f));
            const double level1 = bank.smoothed()[i];

            bank.process(std::vector<float>(static_cast<std::size_t>(0.5 * fs), 0.0f));
            const double level2 = bank.smoothed()[i];

            const double slope = (10.0 * std::log10(level1) - 10.0 * std::log10(level2)) / 0.5;
            const double expected = 10.0 * std::log10(std::numbers::e) / tau;
            CHECK_THAT(slope, WithinRel(expected, 0.05));
        }
    }
}
