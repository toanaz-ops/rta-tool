// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every closed-form property an m-sequence has is checkable with no external
// reference at all (cases 26-28, 30); only case 29 needs a golden fixture,
// and only to pin the exact phase this Galois implementation lands on
// against scipy's Fibonacci one (see Mls.cpp and handoff trap #7,
// docs/plans/2026-08-27-generator-impl-plan.md).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/RealFft.h"
#include "rta/gen/Mls.h"
#include "support/Golden.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::gen;

namespace {

std::vector<rta::test::GoldenCase> generatorGolden() {
    static const auto cases =
        rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/generator.txt");
    return cases;
}

/// Fetch by name, not position -- test_fft.cpp established this pattern
/// after a positional fetch silently picked up an unrelated case once the
/// golden file grew.
rta::test::GoldenCase namedCase(const std::string& name) {
    for (const auto& testCase : generatorGolden()) {
        if (testCase.name == name) return testCase;
    }
    throw std::runtime_error("no golden case named " + name);
}

/// FNV-1a 64, standard constants, one byte per output sample: 0x01 for +A,
/// 0x00 for -A. Matches tools/gen_generator.py's Python side exactly so the
/// two independent implementations can be compared bit-for-bit.
std::uint64_t fnv1a64(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (const std::uint8_t b : bytes) {
        hash ^= b;
        hash *= 0x100000001B3ull;  // uint64_t wraparound on overflow is correct and intentional
    }
    return hash;
}

}  // namespace

TEST_CASE("The MLS period is exactly 2^n - 1", "[mls]") {
    // The property that makes the tap table right or wrong: a wrong table
    // still cycles, just through a shorter orbit, so this alone -- with no
    // reference of any kind -- catches a transcription error in tapsFor().
    for (int order = Mls::kMinOrder; order <= Mls::kMaxOrder; ++order) {
        CAPTURE(order);
        Mls mls(order);
        const std::uint32_t initial = mls.debugState();
        const std::size_t expectedPeriod = (std::size_t{1} << order) - 1;
        REQUIRE(mls.period() == expectedPeriod);

        std::size_t steps = 0;
        do {
            mls.nextSample();
            ++steps;
            // No earlier step may return to the initial state -- that would
            // mean the true cycle is a proper divisor of 2^order - 1, which
            // happens for a non-primitive (wrong) polynomial.
            if (steps < expectedPeriod) {
                REQUIRE(mls.debugState() != initial);
            }
        } while (steps < expectedPeriod);

        REQUIRE(mls.debugState() == initial);
    }
}

TEST_CASE("The MLS is balanced", "[mls]") {
    // Every true m-sequence has exactly one more of one polarity than the
    // other over a full period, because the all-zero register state -- the
    // one state that would have produced the missing sample -- is excluded
    // from the cycle by construction (restart() never sets state to 0).
    for (int order = Mls::kMinOrder; order <= Mls::kMaxOrder; ++order) {
        CAPTURE(order);
        Mls mls(order);
        const std::size_t period = mls.period();

        std::size_t plusCount = 0;
        for (std::size_t i = 0; i < period; ++i) {
            if (mls.nextSample() > 0.0f) ++plusCount;
        }
        const std::size_t minusCount = period - plusCount;

        REQUIRE(plusCount == (std::size_t{1} << (order - 1)));
        REQUIRE(minusCount == (std::size_t{1} << (order - 1)) - 1);
    }
}

TEST_CASE("Circular autocorrelation is 2^n - 1 at lag 0 and exactly -1 elsewhere", "[mls]") {
    // The strongest closed-form identity an m-sequence has, and it needs no
    // external reference. Order 15 keeps the FFT small (period 32767).
    //
    // rta::dsp::RealFft only transforms power-of-two sizes, and the period
    // itself (32767) is not one, so a direct length-P circular FFT of the
    // period alone is not available. Instead this gets the CIRCULAR
    // autocorrelation from a single zero-padded LINEAR one via the standard
    // wrap-around identity, so only one forward/inverse FFT pair is needed
    // (fewer floating-point stages than a two-signal cross-correlation would
    // take, which matters below):
    //
    //   A_lin[m] = sum_{n=0}^{P-1-m} x[n] x[n+m]              (no wrap)
    //   A_circ[m] = sum_{n=0}^{P-1} x[n] x[(n+m) mod P]        (the target)
    //
    // Splitting A_circ's sum at n = P-m and re-indexing the wrapped part
    // shows A_circ[m] = A_lin[m] + A_lin[P-m] for 1 <= m <= P-1, and
    // A_circ[0] = A_lin[0] = P exactly (lag 0 has no wrap term to add).
    //
    // A_lin itself comes from one zero-padded FFT: with `a` = one period of
    // x zero-padded to a power-of-two M, `IDFT(|DFT(a)|^2)[m]` equals
    // A_lin[m] exactly (no aliasing) for m in [0, P) whenever M >= 2P-1 --
    // the usual "linear correlation via a big enough circular one" bound.
    // With P = 32767, 2P-1 = 65533 < 65536 = 2^16, so M = 65536 is the
    // smallest power of two that clears it.
    constexpr int kOrder = 15;
    // 0 dBFS peak -> amplitude exactly 1.0, so the identity above (lag 0 ==
    // period, every other lag == -1) applies to the raw +-1 sequence with no
    // extra amplitude^2 scale factor to carry through the tolerance.
    Mls mls(kOrder, 0.0);
    const std::size_t period = mls.period();  // 32767
    constexpr std::size_t kFftSize = 65536;   // 2^16 > 2*period - 1

    std::vector<float> buf(kFftSize, 0.0f);
    for (std::size_t n = 0; n < period; ++n) {
        buf[n] = mls.nextSample();
    }

    rta::dsp::RealFft fft(kFftSize);
    const std::size_t numBins = fft.numBins();
    std::vector<std::complex<float>> spectrum(numBins);
    fft.forward(buf, spectrum);

    std::vector<std::complex<float>> powerSpectrum(numBins);
    for (std::size_t k = 0; k < numBins; ++k) {
        // |X[k]|^2, kept complex (zero imaginary part) because inverse()
        // takes a complex span; this is the Wiener-Khinchin product whose
        // inverse transform is the autocorrelation.
        powerSpectrum[k] = spectrum[k] * std::conj(spectrum[k]);
    }

    std::vector<float> linearCorr(kFftSize);
    fft.inverse(powerSpectrum, linearCorr);

    // Float round-off through a single 65536-point forward/inverse FFT pair
    // of an integer +-1 sequence: values here are O(1e4), and a fixed
    // absolute 1e-3 leaves ample margin over the observed few-ulp-scale
    // error from one FFT pass without being loose enough to hide a real
    // one-count tap error (which shows up as an offset of several units,
    // not a fraction of one).
    constexpr double kTolerance = 1e-3;

    const auto circularLagOf = [&](const std::size_t m) -> double {
        if (m == 0) return static_cast<double>(linearCorr[0]);
        return static_cast<double>(linearCorr[m]) + static_cast<double>(linearCorr[period - m]);
    };

    REQUIRE_THAT(circularLagOf(0), WithinAbs(static_cast<double>(period), kTolerance));

    for (std::size_t m = 1; m < period; ++m) {
        CAPTURE(m);
        REQUIRE_THAT(circularLagOf(m), WithinAbs(-1.0, kTolerance));
    }
}

TEST_CASE("The sequence matches the golden and its FNV-1a checksum", "[mls][golden]") {
    constexpr int kOrder = Mls::kDefaultOrder;  // 17
    // 0 dBFS (amplitude exactly 1.0), not the class's -6 dBFS default: the
    // golden's `head`/checksum rows pin the BIT SEQUENCE as raw +-1
    // (tools/gen_generator.py), a level-independent property -- amplitude
    // scaling itself is what "MLS output is peak-referenced" (below) checks.
    // Comparing at the default -6 dBFS against a golden written as +-1 fails
    // every sample by a constant scale factor, not a sequence error.
    Mls mls(kOrder, 0.0);
    const std::size_t period = mls.period();  // 131071

    const auto golden = namedCase("mls_17");
    const auto& expectedHead = golden.row("head");
    REQUIRE(expectedHead.size() == 8192);
    REQUIRE(golden.row("tap_mask").size() == 1);
    REQUIRE(static_cast<std::uint32_t>(golden.row("tap_mask").front()) == mls.taps());

    std::vector<std::uint8_t> bits;
    bits.reserve(period);
    for (std::size_t i = 0; i < period; ++i) {
        const float sample = mls.nextSample();
        if (i < 8192) {
            REQUIRE(static_cast<double>(sample) == expectedHead[i]);
        }
        bits.push_back(sample > 0.0f ? 1u : 0u);
    }

    const std::uint64_t hash = fnv1a64(bits);
    const auto hi = static_cast<std::uint32_t>(hash >> 32);
    const auto lo = static_cast<std::uint32_t>(hash & 0xFFFFFFFFu);

    // 2^64 cannot round-trip through the golden file's double storage
    // (Golden.h parses every row as double, and 2^64 > 2^53), so the
    // checksum is split into two 32-bit halves that each do round-trip
    // exactly -- see docs/plans/2026-08-27-generator-impl-plan.md handoff
    // trap #2.
    REQUIRE(static_cast<std::uint32_t>(golden.row("fnv1a_hi").front()) == hi);
    REQUIRE(static_cast<std::uint32_t>(golden.row("fnv1a_lo").front()) == lo);
}

TEST_CASE("MLS output is peak-referenced at the requested dBFS", "[mls]") {
    // MLS output has no intermediate values by construction -- every sample
    // is exactly +A or -A -- so this checks each sample lands on one of the
    // two expected values, never anything between them.
    for (const double db : {-6.0, -20.0, -1.0}) {
        CAPTURE(db);
        Mls mls(Mls::kDefaultOrder, db);
        const double expectedA = std::pow(10.0, db / 20.0);

        for (int i = 0; i < 256; ++i) {
            const double sample = static_cast<double>(mls.nextSample());
            const bool matchesPlus = std::abs(sample - expectedA) < 1e-7;
            const bool matchesMinus = std::abs(sample + expectedA) < 1e-7;
            CAPTURE(sample, expectedA);
            REQUIRE((matchesPlus || matchesMinus));
        }
    }
}
