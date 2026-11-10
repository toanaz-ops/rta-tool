// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 fix, review findings M1 and M2 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E3, E6):
//   M1  a range under 2^17 samples (4 pass-1 windows of 2^15 spread over >= 3/4 of it) has too short a
//       baseline for a ppm: refused, with the neighbours 2^17 - 1 (refused) and 2^17 (accepted);
//   M2  a measurement that ends before the range does is a RANGE problem, named as such with the
//       overlap in seconds (three decimals), not "the delay may bend"; and LENGTH 0 in pair mode is
//       "to the end of the overlap".
// Fixtures are integer-shifted copies: no interpolation anywhere, exact and cheap.
#include "WavPairFixture.h"

#include "measure/WavPairTransfer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <cmath>

using namespace rta::measure;
using namespace wavpair;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr double kRate = wavfix::kFs;

WavPairRequest request(double start, double length) {
    WavPairRequest r;
    r.startSeconds = start;
    r.lengthSeconds = length;
    r.referenceFileName = "console.wav";
    r.measurementFileName = "handheld.wav";
    r.clock.coarseHalfWidthSeconds = 0.1;
    return r;
}

WavPairOutcome analyse(const Pair& p, const WavPairRequest& r) {
    const std::atomic<bool> cancel{false};
    return analyseWavPairTransfer(p.reference, 0, p.measurement, 0, r, cancel);
}

}  // namespace

TEST_CASE("H12 fix M1: the minimum range is 2^17 samples -- 2^17 - 1 is refused, 2^17 goes through", "[wavpair]") {
    const auto p = shiftedPair(288000, 330000, 4800);
    const double under = static_cast<double>((1 << 17) - 1) / kRate;
    const double exact = static_cast<double>(1 << 17) / kRate;
    const auto refused = analyse(p, request(0.5, under));
    REQUIRE(refused.clock.refusal == ClockRefusal::RangeTooShort);
    // The sentence names the minimum to three decimals: 131072 / 48000 = 2.731 s.
    CHECK_THAT(wavPairRefusalText(refused), ContainsSubstring("at least 2.731 s"));
    const auto accepted = analyse(p, request(0.5, exact));
    INFO(wavPairRefusalText(accepted));
    CHECK(accepted.clock.ok());
    // The old limit, one pass-1 window (2^15 = 0.683 s), is far inside the new refusal.
    CHECK(analyse(p, request(0.5, 0.7)).clock.refusal == ClockRefusal::RangeTooShort);
}

TEST_CASE("H12 fix M2: a measurement that ends early is RangeOutsideFile with the overlap named, not a bent delay",
          "[wavpair]") {
    // The measurement starts 0.1 s after the reference's sample 0 and is 30 % shorter than the
    // reference: reference samples 0 .. 201599 - 4800 = 196799 (4.100 s) have a partner.
    const auto p = shiftedPair(288000, 201600, 4800);
    const auto out = analyse(p, request(0.5, 5.5));  // the whole rest of the reference: 0.5 .. 6.0 s
    INFO(wavPairRefusalText(out));
    REQUIRE(out.refusal == WavPairRefusal::RangeOutsideFile);
    CHECK_FALSE(out.clock.refusal.has_value());
    CHECK_FALSE(out.transfer.refusal.has_value());
    const auto text = wavPairRefusalText(out);
    CHECK_THAT(text, ContainsSubstring("start 0.500 s, length 5.500 s"));
    // The overlap, three decimals: it ends at 4.100 s less the ppm margin and the kernel's taps.
    CHECK_THAT(text, ContainsSubstring("runs from "));
    CHECK(std::abs(out.overlapLastSeconds - 4.1) < 200.0 / kRate);
    CHECK_THAT(text, ContainsSubstring(" s to 4.0"));
    CHECK_FALSE(out.clock.refusal == ClockRefusal::NoCorrelation);
}

TEST_CASE("H12 fix M2: the end of the overlap is a hard edge -- 200 samples inside it is accepted, 200 past it is not",
          "[wavpair]") {
    const auto p = shiftedPair(288000, 201600, 4800);
    const double start = 0.5;
    const auto startSample = static_cast<std::int64_t>(start * kRate);
    const auto lengthEnding = [&](std::int64_t lastSample) {
        return static_cast<double>(lastSample - startSample + 1) / kRate;
    };
    const std::int64_t overlapLast = 201599 - 4800;  // the last reference sample with a partner
    const auto inside = analyse(p, request(start, lengthEnding(overlapLast - 200)));
    INFO(wavPairRefusalText(inside));
    CHECK(inside.ok());
    const auto past = analyse(p, request(start, lengthEnding(overlapLast + 200)));
    CHECK(past.refusal == WavPairRefusal::RangeOutsideFile);
    // 50 samples inside the true overlap is already outside the margin (the ppm search + 32 taps).
    CHECK(analyse(p, request(start, lengthEnding(overlapLast - 50))).refusal == WavPairRefusal::RangeOutsideFile);
}

TEST_CASE("H12 fix M2: LENGTH 0 with two files analyses to the end of the overlap", "[wavpair]") {
    const auto p = shiftedPair(288000, 201600, 4800);
    const auto out = analyse(p, request(0.5, 0.0));
    INFO(wavPairRefusalText(out));
    REQUIRE(out.ok());
    CHECK(out.sharedClock);
    CHECK(out.offsetSeconds * kRate == 4800.0);
    CHECK(out.transfer.trace->meta().deviceName == "WAV handheld.wav ch1 vs console.wav ch1");
}
