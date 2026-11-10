// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 T3: (1) the fixture's own reference interpolator, checked rather than
// assumed; (2) the ppm ceiling, MEASURED on this fixture.
//
// ppm sweep, measured on this fixture (pink through the +6 dB / 1 kHz biquad,
// 6.5 s range at 48 kHz, 9 pass-1 windows of 2^15, prior known). Reproduce with
//   set RTA_PPM_SWEEP=1 & rtatool_analysis_tests "H12 T3: ppm sweep*"
// (the case skips itself otherwise: it prints, it does not gate).
//
// Rows: ppm | pass-1 windows accepted | lowest / best delay trust | verdict.
// BAND-LIMITED (coarse and pass 1: PHAT and phase-slope fit over 100 Hz .. 3 kHz):
//   -300 | 9/9 | 0.703 / 0.724 | ok, ppm error +0.001
//   +100 | 9/9 | 0.910 / 0.933 | ok, ppm error  0.000
//   +150 | 9/9 | 0.859 / 0.888 | ok, ppm error  0.000
//   +200 | 9/9 | 0.808 / 0.836 | ok, ppm error  0.000
//   +300 | 9/9 | 0.693 / 0.721 | ok, ppm error +0.013
//   +400 | 9/9 | 0.574 / 0.605 | ok, ppm error +0.062   <- the highest that is clean on every window
//   +600 | 4/9 | 0.357 / 0.396 | NoCoarseAlignment (2^17 coarse window smears 79 samples)
//  +1000 | 0/9 | 0.227 / 0.259 | NoCoarseAlignment
// BEFORE the band limit (full band) the same fixture gave 9/9 at 100, 7/9 at 125,
// 3/9 at 150 and 0/9 from 175 up (trust 0.285 at 100): drift smears each window,
// the smear decorrelates the HIGH bins first, and PHAT weights every bin equally.
// ("lowest" is over windows `suggestDelay` Accepted whether or not the phase-slope
// fraction then resolved.) The record's 1000 ppm (E4) is not reachable; the limit
// is the coarse window's smear. `ClockSearchOptions::maxPpm` = 400.
#include "WavPairFixture.h"

#include "measure/WavPairTransfer.h"
#include "measure/ResampledPairSource.h"
#include "measure/WavPairWindows.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace rta::measure;
using namespace wavpair;

TEST_CASE("H12 T3: the fixture's +-256-tap Blackman-Harris sinc reproduces sinusoids to 1e-4", "[wavpair]") {
    // Bound: the window's side lobes, -92 dB = 2.5e-5, with a 4x margin; see the
    // header of WavPairFixture.h. Checked at 0.1, 0.25 and 0.45 fs, fractional positions.
    constexpr double kPi = std::numbers::pi;
    for (const double f : {0.1, 0.25, 0.45}) {
        std::vector<double> y(4000);
        for (std::size_t i = 0; i < y.size(); ++i) y[i] = std::sin(2.0 * kPi * f * static_cast<double>(i) + 0.4);
        double worst = 0.0;
        for (int k = 0; k < 300; ++k) {
            const double t = 1000.0 + 0.37 + 1.618 * k;
            worst = std::max(worst, std::abs(sincInterpolate(y, t) - std::sin(2.0 * kPi * f * t + 0.4)));
        }
        std::printf("[reference interpolator] f = %.2f fs: worst error %.2e\n", f, worst);
        CHECK(worst < 1e-4);
    }
    // At integer positions it is the sample itself.
    std::vector<double> y{0.0, 1.0, -2.0, 3.5};
    CHECK(sincInterpolate(y, 2.0) == -2.0);
}

namespace {

bool sweepRequested() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    const bool set = _dupenv_s(&value, &size, "RTA_PPM_SWEEP") == 0 && value != nullptr;
    std::free(value);
    return set;
#else
    return std::getenv("RTA_PPM_SWEEP") != nullptr;
#endif
}

constexpr double kBSeconds = 2.0;

struct SweepRow {
    double ppm;
    int accepted = 0;
    int windows = 0;
    double lowestTrust = 0.0;
    double bestTrust = 0.0;
    std::string verdict;
};

SweepRow sweepOne(double ppm) {
    const double eps = ppm * 1e-6;
    const double b = kBSeconds * wavfix::kFs;
    const auto p = makePair(384000, 528000, linearClock(b, eps));
    const std::atomic<bool> cancel{false};
    SweepRow row{};
    row.ppm = ppm;
    // Pass 1 alone, with the TRUE delay as the prior and a wide lag window, so the
    // table says where the windows stop being measurable, not where a coarse search fails.
    detail::WindowPass pass;
    pass.windowLength = 1 << 15;
    pass.windowCount = 9;
    pass.priorDelay = static_cast<std::int64_t>(trueDelayAt(b, eps, 0.5 * wavfix::kFs));
    pass.lagBound = 300;
    pass.sampleRate = wavfix::kFs;
    pass.minHz = detail::driftBand(wavfix::kFs).minHz;
    pass.maxHz = detail::driftBand(wavfix::kFs).maxHz;
    pass.measFirst = 0;
    pass.measLast = 528000 - 1;
    std::vector<rta::dsp::DelayObservation> obs;
    detail::WindowTally tally;
    (void)detail::measureWindows(p.reference, 0, p.measurement, 0, 24000, 312000, pass, cancel, obs, tally);
    row.accepted = tally.accepted;
    row.windows = pass.windowCount;
    row.lowestTrust = tally.lowestAcceptedTrust;
    row.bestTrust = tally.bestTrust;

    WavPairRequest request;
    request.startSeconds = 0.5;
    request.lengthSeconds = 6.5;
    request.referenceFileName = "ref.wav";
    request.measurementFileName = "meas.wav";
    request.clock.priorOffsetSamples = static_cast<std::int64_t>(b);
    request.clock.coarseHalfWidthSeconds = 0.1;
    request.clock.maxPpm = 5000.0;  // the sweep looks past the shipped ceiling
    const auto out = analyseWavPairTransfer(p.reference, 0, p.measurement, 0, request, cancel);
    if (out.ok()) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "ok, ppm error %+.3f, residual rms %.3f", out.ppm - ppm, out.residualRms);
        row.verdict = buf;
    } else {
        row.verdict = wavPairRefusalText(out).substr(0, 70);
    }
    return row;
}

}  // namespace

namespace {

WavPairOutcome analyseDrift(double ppm, double maxPpm, double lengthSeconds = 3.5) {
    const double b = 1.0 * wavfix::kFs;
    const auto p = makePair(384000, 480000, linearClock(b, ppm * 1e-6), 11, false);
    WavPairRequest request;
    request.startSeconds = 0.5;
    request.lengthSeconds = lengthSeconds;
    request.referenceFileName = "ref.wav";
    request.measurementFileName = "meas.wav";
    request.clock.priorOffsetSamples = static_cast<std::int64_t>(b);
    request.clock.coarseHalfWidthSeconds = 0.1;
    if (maxPpm > 0.0) request.clock.maxPpm = maxPpm;
    const std::atomic<bool> cancel{false};
    return analyseWavPairTransfer(p.reference, 0, p.measurement, 0, request, cancel);
}

}  // namespace

TEST_CASE("H12 T3: the shipped ceiling is the measured one: 380 ppm passes, 480 ppm is DriftTooLarge",
          "[wavpair]") {
    CHECK(ClockSearchOptions{}.maxPpm == 400.0);
    const auto under = analyseDrift(380.0, 0.0, 6.5);
    INFO(wavPairRefusalText(under));
    REQUIRE(under.ok());
    CHECK(std::abs(under.ppm - 380.0) < 0.3);
    for (const double ppm : {480.0, -480.0}) {
        const auto over = analyseDrift(ppm, 0.0, 6.5);
        INFO(wavPairRefusalText(over));
        REQUIRE(over.clock.refusal == ClockRefusal::DriftTooLarge);
        CHECK(std::abs(over.clock.ppm - ppm) < 2.0);  // pass 1 is open loop: 478.89 for 480
        CHECK(over.clock.maxPpm == 400.0);
    }
    // Far past the capture range the coarse search fails first: refused all the same.
    CHECK_FALSE(analyseDrift(1000.0, 0.0, 6.5).ok());
}

TEST_CASE("H12 T3: DriftTooLarge names the measured ppm and the limit; its neighbour just under is accepted",
          "[wavpair]") {
    const auto over = analyseDrift(100.0, 60.0);
    REQUIRE(over.clock.refusal == ClockRefusal::DriftTooLarge);
    CHECK(std::abs(over.clock.ppm - 100.0) < 0.3);
    const auto text = wavPairRefusalText(over);
    // Pass 1 is open loop (the record's own caveat): the number printed is pass 1's, 99.77 here.
    char measured[32];
    std::snprintf(measured, sizeof measured, "%.2f ppm", over.clock.ppm);
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring(measured));
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("60 ppm"));
    const auto under = analyseDrift(55.0, 60.0);
    INFO(wavPairRefusalText(under));
    CHECK(under.ok());
    // The sign does not matter to the limit.
    CHECK(analyseDrift(-100.0, 60.0).clock.refusal == ClockRefusal::DriftTooLarge);
}

TEST_CASE("H12 T3: ppm sweep (RTA_PPM_SWEEP=1 to print it)", "[wavpair][sweep]") {
    if (!sweepRequested()) SKIP("set RTA_PPM_SWEEP=1 to print the sweep");
    for (const double ppm : {-300.0, 100.0, 150.0, 200.0, 300.0, 400.0, 600.0, 1000.0}) {
        const SweepRow r = sweepOne(ppm);
        std::printf("[sweep] %6.0f ppm: pass-1 windows %d/%d accepted, trust lowest %.3f best %.3f | %s\n", r.ppm,
                    r.accepted, r.windows, r.lowestTrust, r.bestTrust, r.verdict.c_str());
    }
}

TEST_CASE("H12 T3: the pair source copies bitwise on an integer offset and fails outside the file, not inside it",
          "[wavpair]") {
    const auto p = shiftedPair(20000, 22000, 700);
    ResampledPairSource copy(p.reference, 0, p.measurement, 0, ClockMapping{700.0, 1.0, true});
    CHECK(copy.numChannels() == 2);
    CHECK(copy.lengthSamples() == 20000);
    std::vector<float> out(500);
    REQUIRE(copy.read(1, 1000, out));
    CHECK(std::memcmp(out.data(), p.measurement.channels[0].data() + 1700, out.size() * sizeof(float)) == 0);
    REQUIRE(copy.read(0, 1000, out));
    CHECK(std::memcmp(out.data(), p.reference.channels[0].data() + 1000, out.size() * sizeof(float)) == 0);
    // t(n) = n + 700 must stay in [0, 22000): n <= 21299, and the reference ends at 19999.
    const auto valid = copy.validRange();
    CHECK(valid.first == 0);
    CHECK(valid.last == 19999);
    ResampledPairSource late(p.reference, 0, p.measurement, 0, ClockMapping{2500.0, 1.0, true});
    CHECK(late.validRange().last == 22000 - 1 - 2500);
    std::vector<float> two(2);
    CHECK(late.read(1, late.validRange().last - 1, two));
    CHECK_FALSE(late.read(1, late.validRange().last, two));
    ResampledPairSource early(p.reference, 0, p.measurement, 0, ClockMapping{-300.0, 1.0, true});
    CHECK(early.validRange().first == 300);
    CHECK_FALSE(early.read(1, 299, two));
}

TEST_CASE("H12 T3: the pair source's resampled channel follows t(n) = offset + n * ratio", "[wavpair]") {
    // A 0.2 fs tone on the measurement: the pair's channel 1 must be that tone seen at
    // t(n), so a wrong ratio, offset or sign shows as an O(1) error; the bound is the
    // Kaiser kernel's passband error (T1, A = 100 dB: 1e-5) with a 10x margin.
    constexpr double kPi = std::numbers::pi;
    constexpr double f = 0.2;
    wavfix::MemorySource meas, ref;
    meas.channels.assign(1, std::vector<float>(30000));
    ref.channels.assign(1, std::vector<float>(20000, 0.0f));
    for (std::size_t i = 0; i < 30000; ++i) meas.channels[0][i] = static_cast<float>(std::sin(2.0 * kPi * f * static_cast<double>(i) + 0.3));
    const ClockMapping mapping{10.3, 1.0 + 120e-6, false};
    const ResampledPairSource pair(ref, 0, meas, 0, mapping);
    const auto valid = pair.validRange();
    // t(valid.first) - 31 >= 0 and t(valid.last) + 32 <= 29999, one sample tighter either side.
    CHECK(mapping.position(valid.first) - 31.0 >= 0.0);
    CHECK(mapping.position(valid.first - 1) - 31.0 < 0.0);
    CHECK(valid.first == 21);  // t(21) = 31.30, t(20) = 30.30
    CHECK(valid.last == 19999);  // t(19999) = 10.3 + 19999 * 1.00012 = 20012.7, taps well inside the file
    std::vector<float> out(4000);
    REQUIRE(pair.read(1, 5000, out));
    double worst = 0.0;
    for (std::size_t k = 0; k < out.size(); ++k) {
        const double t = mapping.position(5000 + static_cast<std::int64_t>(k));
        worst = std::max(worst, std::abs(static_cast<double>(out[k]) - std::sin(2.0 * kPi * f * t + 0.3)));
    }
    std::printf("[pair source] worst resampling error %.2e\n", worst);
    CHECK(worst < 1e-4);
    CHECK_FALSE(pair.read(1, valid.first - 1, std::span<float>(out).first(2)));
}
