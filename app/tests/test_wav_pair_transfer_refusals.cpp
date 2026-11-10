// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 T3 (record E1-E7): every refusal, with the neighbour of each threshold on
// its accepted side. Most fixtures are integer-shifted copies (no interpolation
// anywhere, so they are cheap and exact); the two that need a drifting clock use
// WavPairFixture.h's independent resampler.
#include "WavPairFixture.h"

#include "measure/WavPairTransfer.h"
#include "rta/dsp/ClockDrift.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

using namespace rta::measure;
using namespace wavpair;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr double kRate = wavfix::kFs;

WavPairRequest request(double start, double length, double halfWidth = 0.1) {
    WavPairRequest r;
    r.startSeconds = start;
    r.lengthSeconds = length;
    r.referenceFileName = "console.wav";
    r.measurementFileName = "handheld.wav";
    r.clock.coarseHalfWidthSeconds = halfWidth;
    return r;
}

WavPairOutcome analyse(const Pair& p, const WavPairRequest& r, const std::atomic<bool>& cancel = std::atomic<bool>{false}) {
    return analyseWavPairTransfer(p.reference, 0, p.measurement, 0, r, cancel);
}

/// Raises `flag` on its n-th read: a cancel that arrives between windows, mid-run.
class CancellingSource final : public SampleBlockSource {
public:
    CancellingSource(const SampleBlockSource& inner, std::atomic<bool>& flag, int afterReads)
        : inner_(inner), flag_(flag), limit_(afterReads) {}
    int numChannels() const override { return inner_.numChannels(); }
    double sampleRate() const override { return inner_.sampleRate(); }
    std::int64_t lengthSamples() const override { return inner_.lengthSamples(); }
    bool read(int channel, std::int64_t start, std::span<float> out) const override {
        if (++reads_ == limit_) flag_.store(true);
        return inner_.read(channel, start, out);
    }

private:
    const SampleBlockSource& inner_;
    std::atomic<bool>& flag_;
    int limit_;
    mutable int reads_ = 0;
};

}  // namespace

TEST_CASE("H12 T3: different sample rates are refused, naming both", "[wavpair]") {
    auto p = shiftedPair(192000, 220000, 4800);
    p.measurement.fs = 44100.0;
    const auto out = analyse(p, request(0.5, 3.0));
    REQUIRE(out.refusal == WavPairRefusal::SampleRateMismatch);
    CHECK_FALSE(out.ok());
    const auto text = wavPairRefusalText(out);
    CHECK_THAT(text, ContainsSubstring("console.wav is 48000 Hz"));
    CHECK_THAT(text, ContainsSubstring("handheld.wav is 44100 Hz"));
}

TEST_CASE("H12 T3: a channel that does not exist is named per file; the same channel in two files is fine",
          "[wavpair]") {
    const auto p = shiftedPair(192000, 220000, 4800);
    const std::atomic<bool> cancel{false};
    const auto bad = analyseWavPairTransfer(p.reference, 1, p.measurement, 0, request(0.5, 3.0), cancel);
    REQUIRE(bad.refusal == WavPairRefusal::ChannelOutOfRange);
    CHECK_THAT(wavPairRefusalText(bad), ContainsSubstring("console.wav has 1 channels (ch1 to ch1)"));
    const auto badMeas = analyseWavPairTransfer(p.reference, 0, p.measurement, 2, request(0.5, 3.0), cancel);
    REQUIRE(badMeas.refusal == WavPairRefusal::ChannelOutOfRange);
    CHECK_THAT(wavPairRefusalText(badMeas), ContainsSubstring("handheld.wav has 1 channels"));
    // ch1 of one file against ch1 of the other: no SameChannel check across two sources.
    const auto ok = analyse(p, request(0.5, 3.0));
    CHECK(ok.ok());
    CHECK(ok.transfer.trace->meta().deviceName == "WAV handheld.wav ch1 vs console.wav ch1");
}

TEST_CASE("H12 T3: the search width is a hard edge -- 1.2x the half-width is refused, 0.99x is found",
          "[wavpair]") {
    const double halfWidth = 0.1;  // 4800 samples
    {
        const auto p = shiftedPair(192000, 230000, static_cast<std::ptrdiff_t>(1.2 * halfWidth * kRate));
        const auto out = analyse(p, request(0.5, 3.0, halfWidth));
        REQUIRE(out.clock.refusal == ClockRefusal::NoCoarseAlignment);
        CHECK(out.clock.bestTrust < out.clock.trustFloor);
        const auto text = wavPairRefusalText(out);
        CHECK_THAT(text, ContainsSubstring("within 0.1 s"));
        CHECK_THAT(text, ContainsSubstring("is needed"));
    }
    {
        const auto shift = static_cast<std::ptrdiff_t>(0.99 * halfWidth * kRate);
        const auto p = shiftedPair(192000, 230000, shift);
        const auto out = analyse(p, request(0.5, 3.0, halfWidth));
        REQUIRE(out.ok());
        CHECK(out.sharedClock);
        CHECK(out.offsetSeconds * kRate == static_cast<double>(shift));
    }
}

TEST_CASE("H12 T3: a prior moves the search window (the offset found only through it)", "[wavpair]") {
    const auto shift = static_cast<std::ptrdiff_t>(2.0 * kRate);  // 20x the half-width
    const auto p = shiftedPair(192000, 330000, shift);
    auto r = request(0.5, 3.0, 0.1);
    CHECK(analyse(p, r).clock.refusal == ClockRefusal::NoCoarseAlignment);
    r.clock.priorOffsetSamples = shift + 1000;  // the prior may be off by up to the half-width
    const auto out = analyse(p, r);
    REQUIRE(out.ok());
    CHECK(out.offsetSeconds * kRate == static_cast<double>(shift));
}

TEST_CASE("H12 T3: two unrelated recordings are NoCoarseAlignment", "[wavpair]") {
    auto p = shiftedPair(192000, 230000, 4800);
    p.measurement = shiftedPair(192000, 230000, 4800, 99).measurement;  // a different programme
    const auto out = analyse(p, request(0.5, 3.0));
    CHECK(out.clock.refusal == ClockRefusal::NoCoarseAlignment);
}

TEST_CASE("H12 T3: a silent start is rescued by the one coarse retry at the range middle, then refused by coverage",
          "[wavpair]") {
    auto p = shiftedPair(288000, 297600, 4800);
    // Blank the measurement under the first coarse window (range start 0.5 s, 2^17
    // samples). The retry window at the range middle is 60 % intact, so the offset
    // IS found (without the retry this is NoCoarseAlignment) -- and then the pass-1
    // windows that survive span only the last third of the range, and a clock
    // fitted to a third of the range is refused rather than extrapolated.
    const std::size_t from = 4800 + 24000, to = from + 131072;
    for (std::size_t i = from; i < to; ++i) p.measurement.channels[0][i] = 0.0f;
    const auto out = analyse(p, request(0.5, 5.0));
    INFO(wavPairRefusalText(out));
    REQUIRE(out.clock.refusal == ClockRefusal::NoCorrelation);
    CHECK(out.clock.windowsAccepted >= out.clock.windowsNeeded);
    CHECK(out.clock.coverage < 0.6);
    CHECK_THAT(wavPairRefusalText(out), ContainsSubstring("cover only"));
}

TEST_CASE("H12 T3: correlated only at the start -> NoCorrelation with the windows counted", "[wavpair]") {
    auto p = shiftedPair(192000, 230000, 4800);
    const auto other = shiftedPair(192000, 230000, 4800, 77).measurement.channels[0];
    for (std::size_t i = 4800 + 24000 + 34000; i < other.size(); ++i) p.measurement.channels[0][i] = other[i];
    const auto out = analyse(p, request(0.5, 3.0));
    REQUIRE(out.clock.refusal == ClockRefusal::NoCorrelation);
    CHECK(out.clock.windowsAccepted < out.clock.windowsNeeded);
    CHECK_THAT(wavPairRefusalText(out), ContainsSubstring("of the delay windows"));
}

TEST_CASE("H12 T3: a range the overlap cannot hold is RangeOutsideFile, never ReadFailed", "[wavpair]") {
    // The measurement starts 0.1 s late and stops at 4.6 s: the reference timeline is covered to 4.5 s.
    const auto p = shiftedPair(288000, 220800, 4800);
    const auto out = analyse(p, request(0.5, 4.5));  // wants 0.5 .. 5.0 s
    REQUIRE(out.refusal == WavPairRefusal::RangeOutsideFile);
    CHECK_FALSE(out.transfer.refusal.has_value());
    CHECK(std::abs(out.overlapLastSeconds - (220800 - 4800) / kRate) < 1.0 / kRate);
    CHECK_THAT(wavPairRefusalText(out), ContainsSubstring("overlap"));
    // The neighbour: a range that ends inside the overlap is accepted.
    CHECK(analyse(p, request(0.5, 3.9)).ok());
    // And a range past the end of the reference itself is refused the same way.
    CHECK(analyse(p, request(7.0, 1.0)).refusal == WavPairRefusal::RangeOutsideFile);
}

TEST_CASE("H12 T3: a cancel that arrives mid-run stops it with no trace", "[wavpair]") {
    const auto p = shiftedPair(192000, 230000, 4800);
    std::atomic<bool> cancel{false};
    const CancellingSource slow(p.measurement, cancel, 4);
    const auto out = analyseWavPairTransfer(p.reference, 0, slow, 0, request(0.5, 3.0), cancel);
    CHECK(out.clock.refusal == ClockRefusal::Cancelled);
    CHECK_FALSE(out.ok());
    CHECK(wavPairRefusalText(out) == "Analysis cancelled.");
    const std::atomic<bool> already{true};
    CHECK(analyseWavPairTransfer(p.reference, 0, p.measurement, 0, request(0.5, 3.0), already).clock.refusal ==
          ClockRefusal::Cancelled);
}

namespace {

/// A delay that is flat for the first half and then drifts at `eps2`: its
/// straight line fits badly. `rIdeal` is the RMS of that shape about its best
/// line computed from the construction alone (the same Theil-Sen the code uses,
/// applied to exact delays at the windows' centres), so the gate neighbours are
/// set from geometry, not from what the code printed.
double idealResidualRms(double eps2, double rangeSamples) {
    // Pass 2 looks at clamp(floor(range / 2^16), 3, 32) windows of 2^16, evenly spread.
    const double width = 65536.0;
    const int count = static_cast<int>(std::clamp(std::floor(rangeSamples / width), 3.0, 32.0));
    std::vector<rta::dsp::DelayObservation> pts;
    for (int k = 0; k < count; ++k) {
        const double x = k * (rangeSamples - width) / (count - 1) + width / 2;
        pts.push_back({x, x > rangeSamples / 2 ? eps2 * (x - rangeSamples / 2) : 0.0});
    }
    return rta::dsp::fitDriftTheilSen(pts)->residualRms;
}

WavPairOutcome analyseBent(double eps2, bool system) {
    const double bendAt = (0.5 + 3.25) * kRate;  // middle of the 6.5 s range starting at 0.5 s
    const double b = 2.0 * kRate;
    const auto p = makePair(384000, 528000, bentClock(b, 0.0, eps2, bendAt), 11, system);
    auto r = request(0.5, 6.5);
    r.clock.priorOffsetSamples = static_cast<std::int64_t>(b);
    return analyse(p, r);
}

}  // namespace

TEST_CASE("H12 T3: a delay that bends mid-range is DriftNotLinear, with the residual measured", "[wavpair]") {
    const auto out = analyseBent(100e-6, true);
    INFO(wavPairRefusalText(out));
    REQUIRE(out.clock.refusal == ClockRefusal::DriftNotLinear);
    CHECK(out.clock.residualRms > 0.1);
    CHECK(out.clock.maxResidual >= out.clock.residualRms);
    CHECK_THAT(wavPairRefusalText(out), ContainsSubstring("straight line"));
}

TEST_CASE("H12 T3: a bend too sharp to measure is refused, never read as a flat line", "[wavpair]") {
    // 0 then +250 ppm: the second half drifts 8 samples inside each pass-1 window, the
    // phase-slope fit gives up on them (|residual| > 1 sample), and the windows left
    // are all from the flat half. Without the coverage rule that fit reads "shared
    // clock" and the second half's 39 samples of drift go unseen.
    const auto out = analyseBent(250e-6, true);
    INFO(wavPairRefusalText(out));
    CHECK_FALSE(out.ok());
    CHECK_FALSE(out.sharedClock);
    CHECK(out.clock.refusal.has_value());
}

TEST_CASE("H12 T3: the linearity gate has neighbours on both sides (0.1 sample rms)", "[wavpair]") {
    const double range = 6.5 * kRate;
    // eps2 chosen so the shape's own rms is 0.7 and 1.5 times the gate. FLAT system
    // (H = 1): through the biquad the phase-slope delay moves by up to ~0.2 sample
    // between windows whose residual delays differ (the fit's weights change with
    // the residual), which is estimator jitter on top of the shape -- measured
    // 0.114 where the shape alone is 0.041. That jitter errs toward refusing, as E6
    // says, but it means the gate's neighbours can only be placed from geometry on
    // a system that has no phase to bias the fit.
    const double perEps = idealResidualRms(1.0, range);  // rms per unit eps2
    const double under = 0.7 * 0.1 / perEps;
    const double over = 1.5 * 0.1 / perEps;
    const auto accepted = analyseBent(under, false);
    INFO(wavPairRefusalText(accepted));
    CHECK(accepted.ok());
    CHECK(accepted.residualRms < 0.1);
    CHECK(analyseBent(over, false).clock.refusal == ClockRefusal::DriftNotLinear);
}

TEST_CASE("H12 T3: a symmetric V (-60 ppm then +60 ppm) has a median slope of 0 and is still not a shared clock",
          "[wavpair]") {
    // Theil-Sen's slope is a median: this delay goes down 9 samples and back up
    // again, its slope reads ~0, and without the linearity condition on the shared
    // branch it would be taken for "one clock" with only an integer offset applied.
    const double bendAt = (0.5 + 3.25) * kRate;
    const double b = 2.0 * kRate;
    const auto p = makePair(384000, 528000, bentClock(b, -60e-6, 60e-6, bendAt), 11, false);
    auto r = request(0.5, 6.5);
    r.clock.priorOffsetSamples = static_cast<std::int64_t>(b);
    const auto out = analyse(p, r);
    INFO(wavPairRefusalText(out));
    CHECK_FALSE(out.sharedClock);
    CHECK(out.clock.refusal == ClockRefusal::DriftNotLinear);
}

TEST_CASE("H12 T3: a drift that starts three quarters of the way through is not a shared clock either",
          "[wavpair]") {
    // 7 of 9 pass-1 windows sit on the flat part, so more than half of all pairwise
    // slopes are exactly 0 and the Theil-Sen slope is 0 -- the one case where the
    // slope alone says "one clock" while the last quarter drifts by 1 sample
    // (13 ppm over 78000 samples). Pass 1's own line leaves 0.33 samples rms; pass
    // 2 then sees the last window leave the line by 0.6 and refuses it (> 0.1).
    const double range = 6.5 * kRate;
    const double bendAt = 0.5 * kRate + 0.75 * range;
    const double b = 2.0 * kRate;
    const auto p = makePair(384000, 528000, bentClock(b, 0.0, 13e-6, bendAt), 11, false);
    auto r = request(0.5, 6.5);
    r.clock.priorOffsetSamples = static_cast<std::int64_t>(b);
    const auto out = analyse(p, r);
    INFO(wavPairRefusalText(out));
    CHECK_FALSE(out.sharedClock);
    CHECK(out.clock.refusal == ClockRefusal::DriftNotLinear);
}
