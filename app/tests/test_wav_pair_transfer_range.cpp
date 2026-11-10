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
#include <algorithm>
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

namespace {

/// A long reference and a measurement much shorter than it: the measurement starts 0.1 s after the
/// reference's sample 0 and ends 12 s in, the reference runs 40 s. `silentSeconds` of digital silence
/// are laid over the start of BOTH files (the shift is an integer, so the same stretch).
Pair longReferencePair(double silentSeconds) {
    constexpr std::ptrdiff_t kShift = 4800;
    auto p = shiftedPair(40 * 48000, 12 * 48000, kShift);
    const auto silent = static_cast<std::size_t>(silentSeconds * kRate);
    std::fill_n(p.reference.channels[0].begin(), silent, 0.0f);
    std::fill_n(p.measurement.channels[0].begin(), silent + kShift, 0.0f);
    return p;
}

}  // namespace

TEST_CASE("H12 fix 2: LENGTH empty on a long reference retries the coarse look INSIDE the measurement", "[wavpair]") {
    // START sits over 3.5 s of silence, so the first coarse look (0.5 .. 3.23 s) is refused and the one
    // retry decides. Its window used to be the middle of the DEFAULT range (to the end of the 40 s
    // reference, ~19 s), 7 s past where the 12 s measurement ends: nothing was measured and the pair was
    // refused as NoCoarseAlignment, while typing the length succeeded.
    const auto p = longReferencePair(3.5);
    const auto toEnd = analyse(p, request(0.5, 0.0));
    INFO(wavPairRefusalText(toEnd));
    CHECK(toEnd.clock.refusal != ClockRefusal::NoCoarseAlignment);
    CHECK(toEnd.clock.ok());
    // Symmetry: with LENGTH typed (10 s) the retry already lay inside the measurement.
    const auto typed = analyse(p, request(0.5, 10.0));
    INFO(wavPairRefusalText(typed));
    CHECK(typed.clock.ok());
}

TEST_CASE("H12 fix 2: a to-end range ends within the ppm margin of the true overlap end", "[wavpair]") {
    // The margin was computed from the unresolved remainder of the 40 s reference (800 samples at
    // 400 ppm + 32 taps) and trimmed that much from a ~11 s range that needs 243.
    const auto p = longReferencePair(0.0);
    const auto out = analyse(p, request(0.5, 0.0));
    INFO(wavPairRefusalText(out));
    REQUIRE(out.clock.ok());
    const std::int64_t length = out.clock.estimate->rangeLength;
    const std::int64_t last = 24000 + length - 1;
    const std::int64_t trueLast = 12 * 48000 - 1 - 4800;  // the last reference sample with a partner
    const std::int64_t margin = static_cast<std::int64_t>(std::ceil(400e-6 * static_cast<double>(length))) + 32;
    CHECK(trueLast - last >= margin - 1);  // never past the margin ...
    CHECK(trueLast - last <= margin + 1);  // ... and not further in than it
}

TEST_CASE("H12 fix 2: a to-end range left shorter than the minimum by the overlap is RangeTooShort", "[wavpair]") {
    // The measurement ends 24000 + 2^17 + 4800 samples in: START 0.500 s leaves the overlap 2^17 - 85
    // samples (2.729 s) -- START is INSIDE the files, so this is not a "does not fit".
    const auto p = shiftedPair(288000, 24000 + (1 << 17) + 4800, 4800);
    const auto out = analyse(p, request(0.5, 0.0));
    INFO(wavPairRefusalText(out));
    REQUIRE(out.clock.refusal == ClockRefusal::RangeTooShort);
    CHECK(out.refusal != WavPairRefusal::RangeOutsideFile);
    const auto text = wavPairRefusalText(out);
    CHECK_THAT(text, ContainsSubstring("after the start"));
    CHECK_THAT(text, ContainsSubstring("2.729 s"));
    CHECK_THAT(text, ContainsSubstring("at least 2.731 s"));
    // Neighbour: START 0.4 s leaves 2^17 + 4800 - 85 samples, past the minimum: not RangeTooShort.
    const auto earlier = analyse(p, request(0.4, 0.0));
    INFO(wavPairRefusalText(earlier));
    CHECK(earlier.clock.refusal != ClockRefusal::RangeTooShort);
    CHECK(earlier.clock.ok());
}

TEST_CASE("H12 LOWs: a to-end START past the first clip but short of the overlap end is RangeTooShort", "[wavpair]") {
    // The first clip takes its margin from the WHOLE to-end length. With the ppm limit raised to 10 % that
    // margin (~148k samples) swallows the range, and the old code quoted that unclipped end as
    // RangeOutsideOverlap while ~119k partnered samples remain after START. (Raised so the margin can
    // exceed the 2^17 coarse window the pipeline needs inside the overlap; at 400 ppm the path cannot be
    // reached through the coarse look.) The remainder is re-margined from its own length.
    const auto p = longReferencePair(0.0);
    auto r = request(440000.0 / kRate, 0.0);
    r.clock.maxPpm = 100000.0;
    const auto out = analyse(p, r);
    INFO(wavPairRefusalText(out));
    REQUIRE(out.clock.refusal == ClockRefusal::RangeTooShort);
    CHECK(out.clock.overlapAfterStartSeconds > 118000.0 / kRate);
    CHECK(out.clock.overlapAfterStartSeconds < 121000.0 / kRate);
    CHECK(out.refusal != WavPairRefusal::RangeOutsideFile);
}

TEST_CASE("H12 LOWs: a remainder that rounds to 0.000 s is stated in samples", "[wavpair]") {
    // 12 samples are 0.00025 s: "Only 0.000 s" read as nothing at all. Built at the text level: the
    // pipeline's coarse look needs 2^17 partnered samples, so it never produces a remainder this small.
    WavPairOutcome o;
    o.referenceRate = kRate;
    o.clock.refusal = ClockRefusal::RangeTooShort;
    o.clock.overlapAfterStartSeconds = 12.0 / kRate;
    o.clock.minRangeSeconds = 131072.0 / kRate;
    const auto text = wavPairRefusalText(o);
    CHECK_THAT(text, ContainsSubstring("Only 12 samples of the reference after the start"));
    CHECK_THAT(text, !ContainsSubstring("0.000 s of the reference"));
    o.clock.overlapAfterStartSeconds = 1.0 / kRate;
    CHECK_THAT(wavPairRefusalText(o), ContainsSubstring("Only 1 sample of the reference after the start"));
    // At and above half a millisecond the seconds form is kept.
    o.clock.overlapAfterStartSeconds = 0.001;
    CHECK_THAT(wavPairRefusalText(o), ContainsSubstring("Only 0.001 s of the reference after the start"));
}
