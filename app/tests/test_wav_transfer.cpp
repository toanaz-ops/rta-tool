// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H11 W2 (docs/dsp/2026-11-10-g22-offline-wav-transfer.md D3-D6): the numbers
// analyseWavTransfer stores, against the closed-form response of the system the
// fixture built. Refusals live in test_wav_transfer_refusals.cpp.
//
// EXPECTED VALUE. The fixture is meas[n] = (h * ref)[n - D], h the +6 dB / Q=1 /
// 1 kHz RBJ peaking biquad. The engine discards |D| samples before the FFT, so
// the stored H is exactly H_biquad(w) with NO residual delay slope (D is the
// true integer delay). The expectation is `rta::dsp::biquadResponse`, not a
// printed value.
//
// TOLERANCE, DERIVED. Two error sources.
//  1. Leakage of the IR tail across the window edge. A frame sees y = h * x but
//     only the x inside the frame, so Y_w = H X_w + E with
//        E = sum_m h[m] e^{-jwm} sum_n (w[n+m] - w[n]) x[n] e^{-jwn}.
//     For periodic Hann (w = sin^2(pi n/N)) |w[n+m] - w[n]| <= m pi / N, and
//     over a frame sum (dw)^2 ~= (m pi/N)^2 * N/2 against sum w^2 = 3N/8, so
//        |E| / |X_w| <= sqrt(4/3) * pi * M1 / N,   M1 = sum_m m |h[m]|
//     (M1 computed below from the biquad's own impulse response). That is the
//     rms of ONE frame's relative error. Its mean over frames is second order:
//     sum_n w[n] (w[n+m] - w[n]) = sum w w' + O(m^2 pi^2 / N) and sum w w' =
//     [w^2/2] = 0 over the frame. H1 therefore keeps only the zero-mean part,
//     averaged over Neff independent frames: rms / sqrt(Neff). A bin-wise
//     MAXIMUM over ~5000 bins of such a quantity is taken as 4x its rms --
//     a JUDGEMENT, not a derivation (an extreme-value bound would need the
//     bins' distribution). It is safe here because the rms itself is built
//     from the leakage UPPER bound |E|/|X_w| <= sqrt(4/3) pi M1 / N, which sits
//     above the typical error, so 4x that bound leaves slack no real maximum reaches.
//  2. float32 FFT (memory/float32-fft-precision.md): ~1e-6 relative on the
//     peak bin, which for pink at the top of the band is ~1e-6 * 500 = 5e-4
//     of that bin's own magnitude, far below (1). Added as a floor of 1e-3.
// Coherence of a deterministic linear system is 1 - O(eps^2): asserted as
// >= 1 - eps^2 - 1e-6, eps being the 4-sigma figure above.
#include "WavTransferFixture.h"

#include "rta/dsp/AverageCount.h"
#include "rta/dsp/BiquadResponse.h"
#include "rta/dsp/Window.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <complex>
#include <numbers>

using namespace rta::measure;
using namespace wavfix;

namespace {

constexpr std::size_t kN = 16384;

double leakageEps() {
    rta::dsp::BiquadCascade cascade({peaking()});
    double m1 = 0.0;
    for (int m = 0; m < 8192; ++m) m1 += m * std::abs(cascade.processSample(m == 0 ? 1.0 : 0.0));
    return std::sqrt(4.0 / 3.0) * std::numbers::pi * m1 / static_cast<double>(kN);
}

struct Checked {
    double worstRatio = 0.0;  ///< max relative error / tolerance over the band
};

/// Magnitude, phase and coherence over 100 Hz .. 16 kHz against the biquad.
void requireClosedForm(const WavTransferOutcome& out, int expectedDelay) {
    REQUIRE(out.ok());
    CHECK(out.delaySamples == expectedDelay);
    const auto& trace = *out.trace;
    CHECK(trace.meta().appliedDelaySamples == expectedDelay);
    CHECK(trace.meta().averagingType == "Cumulative");
    CHECK(trace.meta().fftSize == static_cast<int>(kN));
    CHECK(trace.meta().sampleRate == kFs);
    REQUIRE(trace.has(rta::trace::Field::Phase));
    REQUIRE(trace.has(rta::trace::Field::Coherence));

    const double eps = 4.0 * leakageEps() / std::sqrt(out.effectiveAverages);
    const double tolRel = eps + 1e-3;
    const auto coeffs = peaking();
    const auto mag = trace.field(rta::trace::Field::Magnitude);
    const auto phase = trace.field(rta::trace::Field::Phase);
    const auto coh = trace.field(rta::trace::Field::Coherence);
    const double binHz = trace.binHz();
    for (std::size_t k = static_cast<std::size_t>(100.0 / binHz);
         k <= static_cast<std::size_t>(16000.0 / binHz); ++k) {
        const std::complex<double> h =
            rta::dsp::biquadResponse(coeffs, 2.0 * std::numbers::pi * k * binHz / kFs);
        const double dbError = std::abs(mag[k] - 20.0 * std::log10(std::abs(h)));
        const double phaseError =
            std::abs(std::arg(std::polar(1.0, static_cast<double>(phase[k])) * std::conj(h) / std::abs(h)));
        REQUIRE(dbError <= 20.0 / std::log(10.0) * tolRel);
        REQUIRE(phaseError <= tolRel);
        REQUIRE(coh[k] >= 1.0 - eps * eps - 1e-6);
    }
}

}  // namespace

TEST_CASE("W2: the leakage bound is a small fraction of a dB, not a loophole", "[wavtransfer]") {
    // Guards the derivation: if the tolerance ever grew past 0.5 dB the closed-form
    // checks below would stop meaning anything.
    CHECK(20.0 / std::log(10.0) * (4.0 * leakageEps() + 1e-3) < 0.5);  // one-frame worst case
}

TEST_CASE("W2: pink through a +6 dB peaking filter, measurement lagging by D = +37", "[wavtransfer]") {
    const auto source = twoChannel(480000, 37);
    const std::atomic<bool> cancel{false};
    const auto out = analyseWavTransfer(source, {0, 1, 0.0, 0.0, "pink.wav"}, cancel);
    requireClosedForm(out, 37);
    CHECK(out.trace->meta().deviceName == "WAV pink.wav ch2 vs ch1");
    CHECK(out.trace->meta().averagingDepth == static_cast<int>(out.frames));
    CHECK(out.trace->meta().effectiveAverages == out.effectiveAverages);
    CHECK(out.effectiveAverages > 8.0);
    // Whole-range cumulative average: every frame after the 37-sample discard,
    // and the overlap-corrected count of exactly that many (a 16-deep FIFO would
    // report the count of 16).
    CHECK(out.frames == (480000 - 37 - kN) / 4096 + 1);
    const rta::dsp::Window hann(rta::dsp::WindowType::Hann, kN);
    CHECK(out.effectiveAverages == rta::dsp::fifoEffectiveAverages(hann.coefficients(), 4096, out.frames));
}

TEST_CASE("W2: the same system with the measurement LEADING, D = -53", "[wavtransfer]") {
    const auto source = twoChannel(480000, -53);
    const std::atomic<bool> cancel{false};
    const auto out = analyseWavTransfer(source, {0, 1, 0.0, 0.0, "lead.wav"}, cancel);
    requireClosedForm(out, -53);
}

TEST_CASE("W2: a 4-channel file, channel 3 against channel 1, ignores the other two", "[wavtransfer]") {
    MemorySource source;
    source.channels.resize(4);
    makePair(480000, 5, [](std::size_t) { return 11; }, source.channels[0], source.channels[2]);
    source.channels[1] = pink(480000, 77);   // unrelated programme
    source.channels[3] = pink(480000, 78);
    const std::atomic<bool> cancel{false};
    const auto out = analyseWavTransfer(source, {0, 2, 0.0, 0.0, "four.wav"}, cancel);
    requireClosedForm(out, 11);
    CHECK(out.trace->meta().deviceName == "WAV four.wav ch3 vs ch1");
}

TEST_CASE("W2: a sub-range of the file analyses only that range", "[wavtransfer]") {
    // Second 3..9 of a 12 s file: a delay that changes OUTSIDE the range must not
    // matter, which is the whole reason the operator picks a range (record D2).
    MemorySource source;
    source.channels.resize(2);
    makePair(576000, 5, [](std::size_t n) { return n < 2 * 48000 ? 90 : 20; }, source.channels[0],
             source.channels[1]);
    const std::atomic<bool> cancel{false};
    const auto out = analyseWavTransfer(source, {0, 1, 3.0, 6.0, "range.wav"}, cancel);
    requireClosedForm(out, 20);
}

TEST_CASE("W2: a one-sample delay change inside the range is accepted (the neighbour of the refusal)",
          "[wavtransfer]") {
    MemorySource source;
    source.channels.resize(2);
    constexpr std::size_t n = 3 * (std::size_t{1} << 18);
    makePair(n, 5, [](std::size_t i) { return i < n / 2 ? 20 : 21; }, source.channels[0],
             source.channels[1]);
    const std::atomic<bool> cancel{false};
    const auto out = analyseWavTransfer(source, {0, 1, 0.0, 0.0, "neighbour.wav"}, cancel);
    REQUIRE(out.ok());
    CHECK((out.delaySamples == 20 || out.delaySamples == 21));
}
