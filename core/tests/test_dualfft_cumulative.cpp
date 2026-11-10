// SPDX-License-Identifier: AGPL-3.0-or-later
//
// H11 W1 (docs/dsp/2026-11-10-g22-offline-wav-transfer.md D3): the identity that
// defines TransferAveraging::Cumulative. A FIFO deep enough never to drop a frame
// IS the cumulative average, so after F frames Cumulative must equal Fifo with
// depth >= F, and must NOT equal Fifo with depth F-1 (which has dropped frame 1).
//
// TOLERANCE, DERIVED (not read off a run). Both modes form each bin's mean as
// (running double sum of the per-frame values) / count. A recursive sum of F
// addends in a different order satisfies |S_fl - S| <= gamma_{F-1} * sum|x_f|,
// gamma_n ~= n*u, u = 2^-53. Here the order is in fact the same, so the
// expected difference is 0; the bound is what the test ASSERTS, so a future
// pairwise or compensated rewrite of either mode still passes while a dropped
// frame (relative ~1/F, twelve orders larger) cannot.
//   Sxx, Syy (addends >= 0):    sum|x_f| = F * mean   -> |d| <= 2 F u * mean
//   Sxy (complex addends):      sum|sxy_f| = sum sqrt(sxx_f syy_f)
//                                  <= sqrt(sum sxx_f * sum syy_f)   (Cauchy-Schwarz)
//                                  = F sqrt(Sxx Syy)    -> |d| <= 2 F u sqrt(Sxx Syy)
// (the factor 2: one bound per summation of the two engines being compared; the
// division by F adds one more u, folded into the same 2 F u.)
// Snapshot quantities are those means pushed through division and a float
// cast, so each gets its relative sensitivity added plus one float ULP
// (2^-23 relative) for a cast that rounds the other way.
#include "rta/dsp/AverageCount.h"
#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/TransferEstimator.h"
#include "rta/dsp/Window.h"
#include "rta/gen/Synthetic.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

using namespace rta::dsp;

namespace {

constexpr std::size_t kFft = 1024;
constexpr std::size_t kHop = 256;
constexpr std::size_t kFrames = 40;
constexpr double kU = 1.1102230246251565e-16;         // 2^-53
constexpr double kFloatUlp = 1.1920928955078125e-07;  // 2^-23
constexpr double kPi = 3.14159265358979323846;

using EnginePtr = std::unique_ptr<DualFftEngine>;

struct Input {
    std::vector<float> ref, meas;
};

/// meas = 0.7 * ref + independent pink (other seed): partially coherent, so
/// coherence is a non-trivial number in (0, 1) instead of a constant.
Input makeInput(std::size_t frames) {
    const std::size_t n = kFft + (frames - 1) * kHop;
    rta::gen::SyntheticPink a(4096, -12.0, 11);
    rta::gen::SyntheticPink b(4096, -12.0, 29);
    Input in{std::vector<float>(n), std::vector<float>(n)};
    std::vector<float> noise(n);
    a.render(in.ref);
    b.render(noise);
    for (std::size_t i = 0; i < n; ++i) in.meas[i] = 0.7f * in.ref[i] + noise[i];
    return in;
}

DualFftEngine::Config baseConfig() {
    DualFftEngine::Config c;
    c.fftSize = kFft;
    c.hopSize = kHop;
    c.window = WindowType::Hann;
    return c;
}

EnginePtr runFifo(const Input& in, std::size_t depth) {
    auto c = baseConfig();
    c.averaging = TransferAveraging::Fifo;
    c.fifoDepth = depth;
    auto e = std::make_unique<DualFftEngine>(c);
    e->process(in.ref, in.meas);
    return e;
}

EnginePtr runCumulative(const Input& in) {
    auto c = baseConfig();
    c.averaging = TransferAveraging::Cumulative;
    auto e = std::make_unique<DualFftEngine>(c);
    e->process(in.ref, in.meas);
    return e;
}

/// Bins where |a-b| exceeds the derived tolerance, for Sxx, Syy and Sxy.
std::size_t binsOutsideTolerance(const DualFftEngine& a, const DualFftEngine& b) {
    std::size_t outside = 0;
    const double tolDiag = 2.0 * kFrames * kU;
    for (std::size_t k = 0; k < a.numBins(); ++k) {
        const double sxx = a.referencePsd()[k], syy = a.measurementPsd()[k];
        const bool bad =
            std::abs(a.referencePsd()[k] - b.referencePsd()[k]) > tolDiag * sxx ||
            std::abs(a.measurementPsd()[k] - b.measurementPsd()[k]) > tolDiag * syy ||
            std::abs(a.crossPsd()[k] - b.crossPsd()[k]) > tolDiag * std::sqrt(sxx * syy);
        if (bad) ++outside;
    }
    return outside;
}

}  // namespace

TEST_CASE("W1: Cumulative after F frames equals Fifo(depth F) and Fifo(depth F+7) bin for bin",
          "[dualfft][cumulative]") {
    const Input in = makeInput(kFrames);
    const auto cumPtr = runCumulative(in);
    const DualFftEngine& cum = *cumPtr;
    REQUIRE(cum.frameCount() == kFrames);

    for (const std::size_t depth : {kFrames, kFrames + 7}) {
        const auto fifoPtr = runFifo(in, depth);
        const DualFftEngine& fifo = *fifoPtr;
        REQUIRE(fifo.frameCount() == kFrames);
        CHECK(binsOutsideTolerance(cum, fifo) == 0);

        const auto a = makeSnapshot(cum, Estimator::H1);
        const auto b = makeSnapshot(fifo, Estimator::H1);
        REQUIRE(a.coherence.has_value());
        REQUIRE(b.coherence.has_value());
        for (std::size_t k = 0; k < cum.numBins(); ++k) {
            const double sxx = cum.referencePsd()[k], syy = cum.measurementPsd()[k];
            const double sxy = std::abs(cum.crossPsd()[k]);
            if (sxy <= 0.0 || sxx <= 0.0) continue;
            // relative error of Sxy/Sxx, of Sxy, and of coherence |Sxy|^2/(Sxx Syy)
            const double relSxx = 2.0 * kFrames * kU;
            const double relSxy = 2.0 * kFrames * kU * std::sqrt(sxx * syy) / sxy;
            const double relH = relSxx + relSxy;
            const double relCoh = 2.0 * relSxy + 2.0 * relSxx;
            CHECK(std::abs(a.magnitudeDb[k] - b.magnitudeDb[k]) <=
                  20.0 / std::log(10.0) * relH + kFloatUlp * (std::abs(a.magnitudeDb[k]) + 1.0));
            // phase wraps at +-pi: compare on the circle
            double dp = std::abs(static_cast<double>(a.phaseRadians[k]) - b.phaseRadians[k]);
            dp = std::min(dp, 2.0 * kPi - dp);
            CHECK(dp <= relSxy + kFloatUlp * 4.0);
            CHECK(std::abs((*a.coherence)[k] - (*b.coherence)[k]) <=
                  relCoh * (*a.coherence)[k] + kFloatUlp);
        }
    }
}

TEST_CASE("W1: Cumulative differs from Fifo(depth F-1), which has dropped frame 1",
          "[dualfft][cumulative]") {
    const Input in = makeInput(kFrames);
    const auto cumPtr = runCumulative(in);
    const DualFftEngine& cum = *cumPtr;
    const auto shallowPtr = runFifo(in, kFrames - 1);
    const DualFftEngine& shallow = *shallowPtr;
    // Dropping one of 40 frames moves a bin's mean by ~1/40 of its relative
    // scatter, about 13 orders above the derived tolerance: demand it on 90 %
    // of bins (DC of a pink Hann frame can be near-degenerate).
    const std::size_t outside = binsOutsideTolerance(cum, shallow);
    CHECK(outside > cum.numBins() * 9 / 10);
}

TEST_CASE("W1: Cumulative effective averages are fifoEffectiveAverages at the frame count",
          "[dualfft][cumulative]") {
    const Input in = makeInput(kFrames);
    const auto cumPtr = runCumulative(in);
    const DualFftEngine& cum = *cumPtr;
    const Window window(WindowType::Hann, kFft);
    CHECK(cum.effectiveAverages() == fifoEffectiveAverages(window.coefficients(), kHop, kFrames));
    // not the raw count, and not the shallower FIFO's
    CHECK(cum.effectiveAverages() < static_cast<double>(kFrames));
    CHECK(cum.effectiveAverages() > fifoEffectiveAverages(window.coefficients(), kHop, kFrames - 1));
}

TEST_CASE("W1: reset clears Cumulative to frame count 0 and no coherence", "[dualfft][cumulative]") {
    const Input in = makeInput(kFrames);
    auto cumPtr = runCumulative(in);
    DualFftEngine& cum = *cumPtr;
    REQUIRE(makeSnapshot(cum, Estimator::H1).coherence.has_value());
    cum.reset();
    CHECK(cum.frameCount() == 0);
    CHECK_FALSE(makeSnapshot(cum, Estimator::H1).coherence.has_value());
    for (std::size_t k = 0; k < cum.numBins(); ++k) {
        REQUIRE(cum.referencePsd()[k] == 0.0);
        REQUIRE(std::abs(cum.crossPsd()[k]) == 0.0);
    }
    // and the sums restart from zero: a second run reproduces the first
    cum.process(in.ref, in.meas);
    CHECK(cum.frameCount() == kFrames);
    CHECK(binsOutsideTolerance(cum, *runFifo(in, kFrames)) == 0);
}
