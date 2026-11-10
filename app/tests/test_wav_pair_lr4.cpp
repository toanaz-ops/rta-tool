// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 fix, review finding H2 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E3): a two-file pair
// whose system is NOT linear-phase -- an LR4 crossover's low-pass + high-pass sum at 100 Hz (a sub)
// and at 1500 Hz (a 2-way) -- through a drifting clock (+80 ppm, 3.2 s offset) and through a shared
// one (0 ppm, 1.7 s). The system's group delay (216 samples at 100 Hz, 15 at 1500 Hz) used to move
// every window's ABSOLUTE phase-slope delay past the fit's one-sample validity, so every window was
// dropped and the pair came back NoCorrelation. The delay is now measured relative to one window,
// where the system's phase cancels (rta::dsp::relativeFractionalDelay).
//
// EXPECTED VALUES. Truth is the fixture's own parameters.
//   ppm        |ppm_hat - 80| <= kDelta / rangeSamples * 1e6 = 0.32 ppm (kDelta = 0.1 sample of total
//              drift over the 6.5 s range, as test_wav_pair_transfer.cpp);
//   magnitude  the sum of an LR4 low-pass and high-pass is flat, so the stored magnitude must be 0 dB
//              within the bound of test_wav_pair_transfer.cpp::requireClosedForm: 4 sigma of the IR-tail
//              leakage over Neff averages (leak = sqrt(4/3) pi M1 / N, here M1 = sum |m - D| |h[m]|
//              taken about the delay D the analysis aligned on, from the system's own impulse response)
//              + 1e-3 for float32, plus the sinc a leftover misalignment kDelta would cost.
// NOT asserted: the OFFSET. The relative fit leaves the common fraction of the anchor window (at most
// half a sample of PHAT rounding, plus the all-pass's own group delay) in every delay, so the stored
// phase carries a tilt of that size; the ppm and the magnitude never see it.
#include "WavPairFixture.h"

#include "measure/WavPairTransfer.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>

using namespace rta::measure;
using namespace wavpair;

namespace {

constexpr double kDelta = 0.1;
constexpr double kPi = std::numbers::pi;

struct Case {
    const char* name;
    System system;
    double eps;
    double bSeconds;
    double measSeconds;
};

/// sum |m - D| |h[m]| of the system's impulse response.
double firstMoment(System system, double alignedDelay) {
    std::vector<float> impulse(16384, 0.0f);
    impulse[0] = 1.0f;
    const auto h = system(impulse);
    double m1 = 0.0;
    for (std::size_t m = 0; m < h.size(); ++m) m1 += std::fabs(static_cast<double>(m) - alignedDelay) * std::fabs(h[m]);
    return m1;
}

void runCase(const Case& c) {
    const auto begin = std::chrono::steady_clock::now();
    const double b = c.bSeconds * wavfix::kFs;
    const auto pair = makePairThrough(8 * 48000, static_cast<std::size_t>(c.measSeconds * wavfix::kFs),
                                      linearClock(b, c.eps), c.system, 11);
    const std::atomic<bool> cancel{false};
    WavPairRequest request;
    request.startSeconds = 0.5;
    request.lengthSeconds = 6.5;
    request.referenceFileName = "console.wav";
    request.measurementFileName = "handheld.wav";
    request.clock.priorOffsetSamples = static_cast<std::int64_t>(b);
    request.clock.coarseHalfWidthSeconds = 0.1;  // narrowed ONLY to keep the fixture small
    const auto out = analyseWavPairTransfer(pair.reference, 0, pair.measurement, 0, request, cancel);
    INFO(c.name << ": " << wavPairRefusalText(out));
    REQUIRE(out.ok());

    const double rangeSamples = request.lengthSeconds * wavfix::kFs;
    const double ppmBound = kDelta / rangeSamples * 1e6;
    if (c.eps == 0.0) {
        CHECK(out.sharedClock);
        CHECK(out.ppm == 0.0);
    } else {
        CHECK_FALSE(out.sharedClock);
        CHECK(std::abs(out.ppm - c.eps * 1e6) <= ppmBound);
    }

    const auto& trace = *out.transfer.trace;
    const double tolRel = 4.0 * std::sqrt(4.0 / 3.0) * kPi *
                              firstMoment(c.system, out.transfer.delaySamples) / 16384.0 /
                              std::sqrt(out.transfer.effectiveAverages) +
                          1e-3;
    const auto mag = trace.field(rta::trace::Field::Magnitude);
    const double binHz = trace.binHz();
    double worstRatio = 0.0, worstDb = 0.0;
    for (auto k = static_cast<std::size_t>(100.0 / binHz); k <= static_cast<std::size_t>(16000.0 / binHz); ++k) {
        const double x = kPi * (static_cast<double>(k) * binHz) * kDelta / wavfix::kFs;
        const double bound = 20.0 / std::log(10.0) * tolRel - 20.0 * std::log10(std::sin(x) / x);
        worstRatio = std::max(worstRatio, std::fabs(mag[k]) / bound);
        worstDb = std::max(worstDb, static_cast<double>(std::fabs(mag[k])));
    }
    std::printf("[%s] wall %.1f s | ppm %.3f (true %.3f, bound %.3f)  shared %d  windows %d  residual rms %.4f"
                " | worst |mag| %.4f dB (%.3f of bound), delay %.1f samples\n",
                c.name, std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count(), out.ppm,
                c.eps * 1e6, ppmBound, out.sharedClock ? 1 : 0, out.clock.estimate->windowsUsed, out.residualRms,
                worstDb, worstRatio, static_cast<double>(out.transfer.delaySamples));
    CHECK(worstRatio <= 1.0);
}

}  // namespace

TEST_CASE("H12 fix: +80 ppm through an LR4 crossover at 100 Hz is accepted, ppm and flat magnitude", "[wavpair]") {
    runCase({"lr4@100 +80ppm/3.2s", lr4At100, 80e-6, 3.2, 11.0});
}

TEST_CASE("H12 fix: +80 ppm through an LR4 crossover at 1500 Hz is accepted, ppm and flat magnitude", "[wavpair]") {
    runCase({"lr4@1500 +80ppm/3.2s", lr4At1500, 80e-6, 3.2, 11.0});
}

TEST_CASE("H12 fix: a shared clock (0 ppm, 1.7 s) through an LR4 crossover at 100 Hz is accepted", "[wavpair]") {
    runCase({"lr4@100 0ppm/1.7s", lr4At100, 0.0, 1.7, 9.5});
}

TEST_CASE("H12 fix: a shared clock (0 ppm, 1.7 s) through an LR4 crossover at 1500 Hz is accepted", "[wavpair]") {
    runCase({"lr4@1500 0ppm/1.7s", lr4At1500, 0.0, 1.7, 9.5});
}
