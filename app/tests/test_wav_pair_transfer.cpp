// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H12 T3 (docs/dsp/2026-11-10-g22b-two-file-wav-drift.md E2-E9): two files, a
// clock that is off by eps and a start that is b late -> the stored transfer
// trace, against the closed form of the system the fixture built.
// Refusals: test_wav_pair_transfer_refusals.cpp. The ppm ceiling and the
// reference interpolator's own accuracy: test_wav_pair_transfer_limits.cpp.
//
// EXPECTED VALUES. The measurement is made by WavPairFixture.h's own
// +-256-tap Blackman-Harris sinc, not by the code under test. Truth for the
// clock is eps; for the offset it is b + eps n at the range start; for the
// transfer it is `rta::dsp::biquadResponse`.
//
// BOUNDS, DERIVED FROM A BUDGET, not from a printed value. E7 declares a total
// drift of 0.05 sample "no drift" because sinc(0.417 * 0.05) costs 0.01 dB at
// 20 kHz / 48 kHz; the budget used here is TWICE that, delta = 0.1 sample of
// total time misalignment over the range (0.04 dB at 20 kHz):
//   - ppm: |ppm_hat - ppm| <= delta / rangeLength * 1e6. For a 6.5 s range
//     (312000 samples) that is 0.32 ppm.
//   - offset at the range start: |offset_hat - offset| <= delta samples.
//   - transfer: the leftover misalignment delta turns into a phase slope
//     2 pi f delta / fs and a magnitude factor sinc(f delta / fs), on top of
//     test_wav_transfer.cpp's own tolerance (leakage of the IR tail across the
//     frame edge, 4 sigma over Neff averages, plus float32 rounding 1e-3). The
//     resampler itself (T1: A = 100 dB, 3e-5) and the fixture's reference
//     interpolator (3e-5) are both far below the 1e-3 floor.
// THE SYSTEM'S OWN PHASE (a measured finding, not a bound chosen to fit). The
// phase-slope fit measures the delay as the weighted least-squares slope of the
// H1 phase over 0..fs/4, and the +6 dB / 1 kHz peaking biquad's phase is not a
// straight line, so the fitted "delay" carries a share of the system's own group
// delay -- the same in every window, so it never reaches the ppm, but it does
// reach the offset and, through it, the stored phase as a pure TILT 2 pi f e / fs
// (the correction moves the measurement by e samples too far). Its size depends on
// the unknown bin weights gamma^2 / (1 - gamma^2); the biquad's least-squares
// delay over 1..12 kHz is -0.04 (equal weights), 0.00 (1/f^2), +0.18 (1/f),
// -0.12 (f), -0.11 (f^2) samples. So:
//   - the flat-system fixtures (H = 1, no phase to bias the fit) carry the TIGHT
//     claim: offset and tilt within delta = 0.1 sample;
//   - the biquad fixtures carry the SHAPE claim: after removing the best-fit
//     tilt, magnitude and phase match the closed form within the leakage bound,
//     and the tilt itself stays within 0.25 sample (the weight range above, 0.18,
//     plus half of delta). The shared-clock case moves nothing fractionally, and
//     its tilt is ~0.
// G22 has the same order of ambiguity: its integer alignment leaves up to 0.5
// sample of tilt.
#include "WavPairFixture.h"

#include "measure/WavPairTransfer.h"
#include "rta/dsp/BiquadResponse.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>

using namespace rta::measure;
using namespace wavpair;

namespace {

constexpr double kDelta = 0.1;  // samples, the budget above
constexpr double kPi = std::numbers::pi;

double leakageEps(std::size_t n) {
    rta::dsp::BiquadCascade cascade({wavfix::peaking()});
    double m1 = 0.0;
    for (int m = 0; m < 8192; ++m) m1 += m * std::abs(cascade.processSample(m == 0 ? 1.0 : 0.0));
    return std::sqrt(4.0 / 3.0) * kPi * m1 / static_cast<double>(n);
}

/// Over 100 Hz .. 16 kHz: the phase error against the closed form is split into
/// a pure delay (`tiltSamples`, fitted through the origin) and what is left.
struct Worst {
    double magDb = 0.0, tiltSamples = 0.0, residualPhase = 0.0, magRatio = 0.0, residualRatio = 0.0;
};

Worst requireClosedForm(const WavPairOutcome& out, std::int64_t fftSize, bool system, double tiltBudget) {
    REQUIRE(out.ok());
    const auto& trace = *out.transfer.trace;
    const double tolRel =
        (system ? 4.0 * leakageEps(static_cast<std::size_t>(fftSize)) / std::sqrt(out.transfer.effectiveAverages) : 0.0) +
        1e-3;
    const auto mag = trace.field(rta::trace::Field::Magnitude);
    const auto phase = trace.field(rta::trace::Field::Phase);
    const double binHz = trace.binHz();
    const auto k0 = static_cast<std::size_t>(100.0 / binHz);
    const auto k1 = static_cast<std::size_t>(16000.0 / binHz);
    const auto response = [&](double f) {
        return system ? rta::dsp::biquadResponse(wavfix::peaking(), 2.0 * kPi * f / wavfix::kFs)
                      : std::complex<double>(1.0, 0.0);
    };
    std::vector<double> err(k1 + 1, 0.0);
    double num = 0.0, den = 0.0;
    for (std::size_t k = k0; k <= k1; ++k) {
        const double f = static_cast<double>(k) * binHz;
        const auto h = response(f);
        err[k] = std::arg(std::polar(1.0, static_cast<double>(phase[k])) * std::conj(h) / std::abs(h));
        num += f * err[k];
        den += f * f;
    }
    Worst worst;
    worst.tiltSamples = num / den * wavfix::kFs / (2.0 * kPi);
    for (std::size_t k = k0; k <= k1; ++k) {
        const double f = static_cast<double>(k) * binHz;
        const double x = kPi * f * tiltBudget / wavfix::kFs;
        const double magBound = 20.0 / std::log(10.0) * tolRel - 20.0 * std::log10(std::sin(x) / x);
        const double magErr = std::abs(mag[k] - 20.0 * std::log10(std::abs(response(f))));
        const double residual = std::abs(err[k] - 2.0 * kPi * f * worst.tiltSamples / wavfix::kFs);
        worst.magDb = std::max(worst.magDb, magErr);
        worst.residualPhase = std::max(worst.residualPhase, residual);
        worst.magRatio = std::max(worst.magRatio, magErr / magBound);
        worst.residualRatio = std::max(worst.residualRatio, residual / tolRel);
    }
    CHECK(std::abs(worst.tiltSamples) <= tiltBudget);
    CHECK(worst.magRatio <= 1.0);
    CHECK(worst.residualRatio <= 1.0);
    return worst;
}

struct Spec {
    const char* name;
    double eps;
    double bSeconds;
    double measSeconds;
    double startSeconds = 0.5;
    double lengthSeconds = 6.5;
    double refSeconds = 8.0;
    bool system = false;  ///< false: H = 1 (tight); true: the +6 dB biquad (shape)
    ClockSearchOptions clock{};
    [[nodiscard]] double budget() const { return system ? 0.25 : kDelta; }
};

struct Run {
    WavPairOutcome out;
    double seconds = 0.0;
};

Run run(const Spec& s) {
    const auto begin = std::chrono::steady_clock::now();
    const double b = s.bSeconds * wavfix::kFs;
    const auto pair = makePair(static_cast<std::size_t>(s.refSeconds * wavfix::kFs),
                               static_cast<std::size_t>(s.measSeconds * wavfix::kFs), linearClock(b, s.eps), 11,
                               s.system);
    const std::atomic<bool> cancel{false};
    WavPairRequest request;
    request.startSeconds = s.startSeconds;
    request.lengthSeconds = s.lengthSeconds;
    request.referenceFileName = "console.wav";
    request.measurementFileName = "handheld.wav";
    request.clock = s.clock;
    Run r;
    r.out = analyseWavPairTransfer(pair.reference, 0, pair.measurement, 0, request, cancel);
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    return r;
}

void requireClock(const Spec& s, const WavPairOutcome& out) {
    REQUIRE(out.ok());
    const double rangeSamples = s.lengthSeconds * wavfix::kFs;
    const double truePpm = s.eps * 1e6;
    const double trueOffset =
        trueDelayAt(s.bSeconds * wavfix::kFs, s.eps, s.startSeconds * wavfix::kFs) / wavfix::kFs;
    CHECK(std::abs(out.ppm - truePpm) <= kDelta / rangeSamples * 1e6);
    CHECK(std::abs(out.offsetSeconds - trueOffset) * wavfix::kFs <= s.budget());
    CHECK(out.residualRms <= 0.1);
    std::printf("[%s] ppm %.3f (true %.3f, bound %.3f)  offset error %.4f sample  residual rms %.4f\n",
                s.name, out.ppm, truePpm, kDelta / rangeSamples * 1e6,
                (out.offsetSeconds - trueOffset) * wavfix::kFs, out.residualRms);
}

void report(const Spec& s, const Run& r, const Worst& w) {
    std::printf("[%s] wall %.1f s | tilt %+.4f sample (budget %.2f), worst mag %.4f dB (%.2f of bound), "
                "worst residual phase %.5f rad (%.2f of bound)\n",
                s.name, r.seconds, w.tiltSamples, s.budget(), w.magDb, w.magRatio, w.residualPhase,
                w.residualRatio);
}

}  // namespace

TEST_CASE("H12 T3: +80 ppm and a 3.2 s start offset, found through a +-0.1 s search around the prior",
          "[wavpair]") {
    Spec s{"+80ppm/3.2s", 80e-6, 3.2, 11.0};
    s.clock.priorOffsetSamples = static_cast<std::int64_t>(3.2 * wavfix::kFs);
    s.clock.coarseHalfWidthSeconds = 0.1;  // narrowed ONLY to keep the fixture small; see the default-width case
    const Run r = run(s);
    requireClock(s, r.out);
    CHECK_FALSE(r.out.sharedClock);
    const auto w = requireClosedForm(r.out, 16384, s.system, s.budget());
    CHECK(r.out.transfer.trace->meta().deviceName == "WAV handheld.wav ch1 vs console.wav ch1");
    report(s, r, w);
}

TEST_CASE("H12 T3: +80 ppm through the +6 dB biquad: the stored transfer is the system's", "[wavpair]") {
    Spec s{"+80ppm/3.2s biquad", 80e-6, 3.2, 11.0};
    s.system = true;
    s.clock.priorOffsetSamples = static_cast<std::int64_t>(3.2 * wavfix::kFs);
    s.clock.coarseHalfWidthSeconds = 0.1;
    const Run r = run(s);
    requireClock(s, r.out);
    const auto w = requireClosedForm(r.out, 16384, s.system, s.budget());
    CHECK(r.out.transfer.trace->meta().deviceName == "WAV handheld.wav ch1 vs console.wav ch1");
    report(s, r, w);
}

TEST_CASE("H12 T3: -150 ppm, the other sign", "[wavpair]") {
    Spec s{"-150ppm/2.5s biquad", -150e-6, 2.5, 10.5};
    s.system = true;
    // The brief names -150 ppm; the shipped ceiling (100, measured: see
    // test_wav_pair_transfer_limits.cpp) is below it, so this case raises the limit.
    // It resolves 3 windows of 9 (a thin fit) and the sign path is what it proves.
    s.clock.maxPpm = 200.0;
    s.clock.priorOffsetSamples = static_cast<std::int64_t>(2.5 * wavfix::kFs);
    s.clock.coarseHalfWidthSeconds = 0.1;
    const Run r = run(s);
    requireClock(s, r.out);
    const auto w = requireClosedForm(r.out, 16384, s.system, s.budget());
    report(s, r, w);
}

TEST_CASE("H12 T3: 0 ppm with a 1.7 s start offset is the shared-clock branch, and nothing is resampled",
          "[wavpair]") {
    Spec s{"0ppm/1.7s biquad", 0.0, 1.7, 9.5};
    s.system = true;
    s.clock.priorOffsetSamples = static_cast<std::int64_t>(1.7 * wavfix::kFs);
    s.clock.coarseHalfWidthSeconds = 0.1;
    const Run r = run(s);
    REQUIRE(r.out.ok());
    CHECK(r.out.sharedClock);       // E7: no pass 2, no resampling
    CHECK(r.out.ppm == 0.0);
    CHECK(r.out.residualRms == 0.0);  // pass 2 never ran
    CHECK(r.out.offsetSeconds == 1.7);  // 81600 samples, an exact integer offset
    // The pair source copies: the offset is the whole story, and G22 then sees delay 0.
    CHECK(r.out.transfer.delaySamples == 0);
    const auto w = requireClosedForm(r.out, 16384, s.system, s.budget());
    report(s, r, w);
}

TEST_CASE("H12 T3: the production 10 s search width finds a small offset on a short pair", "[wavpair]") {
    // Default options: prior 0, +-10 s. The fixture is short (4.5 s), so the span
    // is clipped to the measurement file -- the clip is what this case exercises.
    Spec s{"default-width +60ppm/0.3s", 60e-6, 0.3, 5.0, 0.5, 3.5, 4.5};
    const Run r = run(s);
    requireClock(s, r.out);
    CHECK_FALSE(r.out.sharedClock);
    const auto w = requireClosedForm(r.out, 16384, s.system, s.budget());
    report(s, r, w);
}
