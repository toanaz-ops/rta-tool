// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 fix (review): the gates added after the first review, each with a neighbour on the accepted side.
//   - CoarseSpanTooShort: its own sentence, not "best delay trust 0.00 and 0.00 is needed";
//   - RangeTooLong: the pass-1 lag search must stay under a quarter window;
//   - the 60 % coverage rule's ACCEPTED side;
//   - E7's 0.05-sample shared-clock threshold on both sides;
//   - ClockUncertain: the predicted drift error u = r2 sqrt(12 / K) range / spread (r2 = PASS 2's residual RMS,
//     the code's input; the limit u <= 0.1 is r2 <= 0.1 / (sqrt(12 / K) range / spread), so it moves with K), checked against
//     simulated Theil-Sen fits and, end to end, against a measurement with independent noise.
#include "WavPairFixture.h"

#include "measure/WavPairTransfer.h"
#include "measure/WavPairWindows.h"
#include "rta/dsp/ClockDrift.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <random>

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

WavPairOutcome analyse(const Pair& p, const WavPairRequest& r) {
    const std::atomic<bool> cancel{false};
    return analyseWavPairTransfer(p.reference, 0, p.measurement, 0, r, cancel);
}

/// Silence of any length: the range checks read nothing until the search starts.
class ZeroSource final : public SampleBlockSource {
public:
    explicit ZeroSource(std::int64_t length) : length_(length) {}
    int numChannels() const override { return 1; }
    double sampleRate() const override { return kRate; }
    std::int64_t lengthSamples() const override { return length_; }
    bool read(int, std::int64_t start, std::span<float> out) const override {
        if (start < 0 || start + static_cast<std::int64_t>(out.size()) > length_) return false;
        std::fill(out.begin(), out.end(), 0.0f);
        return true;
    }

private:
    std::int64_t length_;
};

}  // namespace

TEST_CASE("H12 fix: a measurement shorter than the coarse window is named for what it is", "[wavpair]") {
    const auto p = shiftedPair(288000, 100000, 4800);  // 2.083 s: less than 2^17 samples (2.731 s)
    const auto out = analyse(p, request(0.5, 5.0));
    REQUIRE(out.clock.refusal == ClockRefusal::CoarseSpanTooShort);
    const auto text = wavPairRefusalText(out);
    INFO(text);
    CHECK_THAT(text, ContainsSubstring("only 1.683 s"));   // 100000 - (24000 - 4800) = 80800 samples
    CHECK_THAT(text, ContainsSubstring("2.731 s are needed"));
    CHECK_THAT(text, ContainsSubstring("2.083 s long"));
    CHECK_FALSE(text.find("trust") != std::string::npos);
    // The neighbour: a measurement long enough to hold the window is lined up.
    CHECK(analyse(shiftedPair(288000, 230000, 4800), request(0.5, 3.0)).ok());
    // A prior that points past the end of the measurement is the same refusal, with its numbers.
    auto r = request(0.5, 3.0);
    r.clock.priorOffsetSamples = 400000;
    const auto outside = analyse(shiftedPair(288000, 330000, 4800), r);
    REQUIRE(outside.clock.refusal == ClockRefusal::CoarseSpanTooShort);
    CHECK_THAT(wavPairRefusalText(outside), ContainsSubstring("expected start offset is 8.333 s"));
}

TEST_CASE("H12 fix: a range whose ppm margin passes a quarter window is RangeTooLong, one sample under is not",
          "[wavpair]") {
    // maxPpm * 1e-6 * range <= 2^15 / 4 = 8192: at 20000 ppm the limit is 409600 samples.
    const auto p = shiftedPair(480000, 500000, 4800);
    auto r = request(0.5, 409601.0 / kRate);
    r.clock.maxPpm = 20000.0;
    const auto over = analyse(p, r);
    REQUIRE(over.clock.refusal == ClockRefusal::RangeTooLong);
    CHECK_THAT(wavPairRefusalText(over), ContainsSubstring("at most 0.1 minutes"));
    r.lengthSeconds = 409600.0 / kRate;
    INFO(wavPairRefusalText(analyse(p, r)));
    CHECK(analyse(p, r).clock.refusal != ClockRefusal::RangeTooLong);
    CHECK(analyse(p, r).ok());
    // At the shipped 400 ppm the limit is 20.48 M samples = 7.1 minutes at 48 kHz.
    const ZeroSource tape(21'000'000);
    const std::atomic<bool> cancel{false};
    WavPairRequest big = request(0.5, 20'480'001.0 / kRate);
    big.clock.maxPpm = 400.0;
    const auto tooLong = analyseWavPairTransfer(tape, 0, tape, 0, big, cancel);
    REQUIRE(tooLong.clock.refusal == ClockRefusal::RangeTooLong);
    CHECK_THAT(wavPairRefusalText(tooLong), ContainsSubstring("at most 7.1 minutes"));
    big.lengthSeconds = 20'480'000.0 / kRate;
    CHECK(analyseWavPairTransfer(tape, 0, tape, 0, big, cancel).clock.refusal != ClockRefusal::RangeTooLong);
}

TEST_CASE("H12 fix: the 60 % coverage rule's accepted side -- windows spanning 70 % of the range pass",
          "[wavpair]") {
    // 10 s range: 14 pass-1 windows (2^15) and 7 pass-2 windows (2^16). Blanking the first 135000 samples of
    // the measurement kills pass-1 windows 0..3, leaving 4..13: they span 9/13 = 69 % of the first-to-last-
    // centre span, above 60 %, so the clock is fitted. Pass 2 loses its windows 0 and 1 and keeps 2..6
    // (4/6 = 67 %). The existing refusal case blanks four of seven windows and is left with 33 %.
    auto p = shiftedPair(528000, 537600, 4800);
    const std::size_t from = 4800 + 24000, to = from + 135000;
    for (std::size_t i = from; i < to; ++i) p.measurement.channels[0][i] = 0.0f;
    const auto out = analyse(p, request(0.5, 10.0));
    INFO(wavPairRefusalText(out));
    REQUIRE(out.clock.ok());
    CHECK(out.clock.estimate->windowsUsed >= 3);
    CHECK(out.clock.estimate->windowsUsed <= 10);
}

namespace {

WavPairOutcome analyseFlat(double eps) {
    const double b = 1.0 * kRate;
    const auto p = makePair(384000, 480000, linearClock(b, eps), 11, false);
    auto r = request(0.5, 6.5);
    r.clock.priorOffsetSamples = static_cast<std::int64_t>(b);
    return analyse(p, r);
}

}  // namespace

TEST_CASE("H12 fix: E7's shared-clock threshold has neighbours -- 0.025 sample total is shared, 0.094 is resampled",
          "[wavpair]") {
    // 6.5 s = 312000 samples. 0.08 ppm * 312000 = 0.025 sample <= 0.05; 0.30 ppm = 0.094 > 0.05.
    const double range = 6.5 * kRate;
    for (const double sign : {1.0, -1.0}) {
        const auto under = analyseFlat(sign * 0.08e-6);
        INFO(wavPairRefusalText(under));
        REQUIRE(under.ok());
        CHECK(under.sharedClock);
        const auto over = analyseFlat(sign * 0.30e-6);
        INFO(wavPairRefusalText(over));
        REQUIRE(over.ok());
        CHECK_FALSE(over.sharedClock);
        CHECK(std::abs(over.ppm - sign * 0.30) <= 0.1 / range * 1e6);
    }
}

TEST_CASE("H12 fix: the predicted drift error is the slope's standard error across the range", "[wavpair]") {
    // Closed form: K = 4, r2 = 0.05, spread = 3/4 * 2^17, range 2^17 -> 0.05 * sqrt(3) * 4/3.
    CHECK(std::abs(detail::predictedDriftError(0.05, 4, 98304.0, 131072) - 0.05 * std::sqrt(3.0) * 4.0 / 3.0) <
          1e-12);
    // Against simulation: K evenly spaced windows with independent Gaussian noise sigma, a Theil-Sen line,
    // the drift error over the range = slope error * range. The model's u uses the fit's own residual
    // RMS (what the code has), which underestimates sigma by sqrt((K - 2) / K), and 12 / K overstates
    // the spread of evenly spaced points by (K + 1) / (K - 1): the two nearly cancel.
    for (const int k : {4, 9}) {
        const double range = 6.5 * kRate, spread = 0.9 * range, sigma = 0.05;
        std::mt19937 gen(12345 + static_cast<unsigned>(k));
        std::normal_distribution<double> noise(0.0, sigma);
        double sumSq = 0.0, sumU = 0.0;
        constexpr int kTrials = 4000;
        for (int t = 0; t < kTrials; ++t) {
            std::vector<rta::dsp::DelayObservation> pts;
            for (int i = 0; i < k; ++i) {
                pts.push_back({0.05 * range + spread * i / (k - 1), noise(gen)});
            }
            const auto fit = rta::dsp::fitDriftTheilSen(pts);
            sumSq += fit->slope * range * fit->slope * range;
            sumU += detail::predictedDriftError(fit->residualRms, k, spread, static_cast<std::int64_t>(range));
        }
        const double actual = std::sqrt(sumSq / kTrials), predicted = sumU / kTrials;
        std::printf("[uncertainty] K=%d: simulated Theil-Sen error %.4f, predicted u %.4f (ratio %.3f)\n", k, actual,
                    predicted, predicted / actual);
        // An approximation (header of predictedDriftError), within 20 % of the simulation.
        CHECK(predicted / actual >= 0.8);
        CHECK(predicted / actual <= 1.2);
    }
}

namespace {

/// +50 ppm, flat system, `noise` x a fixed unit-variance sequence added to the measurement.
struct Noisy {
    Pair pair;
    std::vector<float> unit;
    explicit Noisy(unsigned seed) {
        const double b = 1.0 * kRate;
        pair = makePair(192000, 240000, linearClock(b, 50e-6), 11, false);
        std::mt19937 gen(seed);
        std::normal_distribution<float> n(0.0f, 1.0f);
        unit.resize(pair.measurement.channels[0].size());
        for (auto& v : unit) v = n(gen);
    }
    WavPairOutcome run(double sigma) {
        Pair p = pair;
        for (std::size_t i = 0; i < unit.size(); ++i) p.measurement.channels[0][i] += static_cast<float>(sigma) * unit[i];
        auto r = request(0.5, 3.0);
        r.clock.priorOffsetSamples = static_cast<std::int64_t>(1.0 * kRate);
        return analyse(p, r);
    }
};

}  // namespace

TEST_CASE("H12 fix: ClockUncertain has neighbours -- noise just under the limit is fitted, just over is refused",
          "[wavpair]") {
    Noisy fixture(777);
    // The unit pink has rms ~0.25 (SyntheticPink -12 dBFS): noise 0 .. 2 sweeps the measurement from clean
    // to buried. Bisection on the noise amplitude finds where the OUTCOME flips; the assertions are on
    // the two sides of the flip, not on where it lands.
    const auto classify = [&](double sigma) { return fixture.run(sigma); };
    double lo = 0.0, hi = 0.3;  // past ~0.5 the windows are lost (NoCorrelation) before the scatter shows
    REQUIRE(classify(hi).clock.refusal == ClockRefusal::ClockUncertain);
    REQUIRE(classify(lo).clock.ok());
    for (int i = 0; i < 14; ++i) {
        const double mid = 0.5 * (lo + hi);
        const auto out = classify(mid);
        (out.clock.ok() ? lo : hi) = mid;  // another refusal first would show in the assertion below
    }
    const auto under = classify(lo);
    const auto refused = classify(hi);
    INFO(wavPairRefusalText(refused));
    REQUIRE(under.clock.ok());
    REQUIRE(refused.clock.refusal == ClockRefusal::ClockUncertain);
    std::printf("[uncertainty] flips between noise %.4f (u = %.4f, accepted) and %.4f (u = %.4f, refused)\n", lo,
                under.clock.estimate->uncertaintySamples, hi, refused.clock.uncertaintySamples);
    CHECK(under.clock.estimate->uncertaintySamples <= 0.1);
    CHECK(refused.clock.uncertaintySamples > 0.1);
    CHECK(refused.clock.uncertaintyLimit == 0.1);
    // The two sides straddle the limit: the gate decided at 0.10, not far from it.
    CHECK(under.clock.estimate->uncertaintySamples > 0.05);
    CHECK(refused.clock.uncertaintySamples < 0.2);
    CHECK_THAT(wavPairRefusalText(refused), ContainsSubstring("0.10 is the limit"));
}

