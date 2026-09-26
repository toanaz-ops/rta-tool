// SPDX-License-Identifier: AGPL-3.0-or-later
// Shared fixtures for the sweep/inverse-filter tests, split out of
// test_generator_sweep.cpp so that file (and its deconvolution-heavy sibling,
// test_generator_sweep_deconv.cpp) stay under CLAUDE.md's 400-line file cap.
// `inline` throughout: this header is included by two translation units in
// the same test binary, and these are ordinary (non-template) functions, so
// without `inline` that would be an ODR violation at link time.
//
// [golden]-tagged cases read core/tests/golden/generator.txt via
// sweepGolden()/findCase(); if it hasn't landed yet, loadGolden() throws and
// those cases are legitimately RED, not a bug -- see the two .cpp files that
// include this header for the full context (station D, Farina sweep).
#pragma once

#include <catch2/catch_test_macros.hpp>

#include "rta/dsp/RealFft.h"
#include "rta/gen/Sweep.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rta::test::sweep_fixtures {

inline constexpr double kPi = 3.14159265358979323846;

inline const std::vector<rta::test::GoldenCase>& sweepGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/generator.txt");
    return cases;
}

// `name` is a by-value view, not `const std::string&`: gcc's -Wdangling-reference
// heuristic flags any reference-returning call that binds a temporary to a
// reference parameter, and the string built from a literal here is compared,
// never returned. A view has no reference for the heuristic to trip on and
// skips the allocation.
inline const rta::test::GoldenCase& findCase(const std::vector<rta::test::GoldenCase>& cases,
                                             std::string_view name) {
    for (const auto& c : cases) {
        if (c.name == name) return c;
    }
    throw std::runtime_error("golden case not found: " + std::string(name));
}

inline double amplitudeFromDb(double db) { return std::pow(10.0, db / 20.0); }
inline double raisedCosine(double p) { return 0.5 * (1.0 - std::cos(kPi * p)); }

inline std::size_t nextPow2(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

/// Linear convolution via zero-padded FFT of `size` (power of two, >=
/// a.size()+b.size()-1 for an alias-free result).
inline std::vector<float> convolveFft(std::span<const float> a, std::span<const float> b,
                                      std::size_t size) {
    rta::dsp::RealFft fft(size);
    std::vector<float> ap(size, 0.0f), bp(size, 0.0f);
    std::copy(a.begin(), a.end(), ap.begin());
    std::copy(b.begin(), b.end(), bp.begin());
    std::vector<std::complex<float>> A(fft.numBins()), B(fft.numBins()), C(fft.numBins());
    fft.forward(ap, A);
    fft.forward(bp, B);
    for (std::size_t k = 0; k < fft.numBins(); ++k) C[k] = A[k] * B[k];
    std::vector<float> out(size);
    fft.inverse(C, out);
    return out;
}

/// 3-point parabolic interpolation of a local extremum -- a discretely
/// sampled sine's true peak rarely lands on a sample, so this recovers it far
/// more accurately than the nearest raw sample would.
inline double refinePeak(double left, double centre, double right) {
    const double denom = left - 2.0 * centre + right;
    if (std::abs(denom) < 1.0e-15) return centre;
    const double p = 0.5 * (left - right) / denom;
    return centre - 0.25 * (left - right) * p;
}

struct DeconvResult {
    std::size_t mainIndex = 0;
    double mainValue = 0.0, snrDb = 0.0, amp1500Rel = 0.0, amp2300Rel = 0.0;
    std::ptrdiff_t offset1500 = 0, offset2300 = 0;
};

/// Render a sweep, convolve with a synthetic 3-tap room IR, deconvolve with
/// its own inverse filter, measure how cleanly the IR comes back. Shared by
/// the CI-fast and hidden full-range cases.
inline DeconvResult runSweepDeconvolution(double fs, double f1, double f2, double T,
                                          std::size_t fftSize) {
    rta::gen::Sweep::Config cfg;
    cfg.sampleRate = fs; cfg.startHz = f1; cfg.endHz = f2; cfg.durationSec = T;
    cfg.levelDbFsPeak = -6.0;
    rta::gen::Sweep sweep(cfg);
    const auto n = sweep.lengthSamples();

    std::vector<float> swept(n);
    sweep.process(swept);

    // Synthetic room IR: unit impulse at 1000, reflections 0.5@1500 and
    // -0.25@2300, zero elsewhere; sized to N per the plan's worked length
    // (191999 = 96000+96000-1).
    std::vector<float> ir(n, 0.0f);
    ir[1000] = 1.0f;
    ir[1500] = 0.5f;
    ir[2300] = -0.25f;

    REQUIRE(swept.size() + ir.size() - 1 <= fftSize);
    const auto measured = convolveFft(swept, ir, fftSize);

    const auto inv = sweep.buildInverseFilter();
    const auto recovered = convolveFft(measured, inv, fftSize);

    std::size_t mainIdx = 0;
    double mainVal = 0.0;
    for (std::size_t i = 0; i < recovered.size(); ++i) {
        const double v = std::abs((double) recovered[i]);
        if (v > mainVal) { mainVal = v; mainIdx = i; }
    }
    const double mainSigned = (double) recovered[mainIdx];

    auto findNear = [&](std::size_t predicted, std::size_t radius) {
        std::size_t bestI = predicted;
        double bestV = -1.0;
        const std::size_t lo = predicted > radius ? predicted - radius : 0;
        const std::size_t hi = std::min(predicted + radius, recovered.size() - 1);
        for (std::size_t i = lo; i <= hi; ++i) {
            const double v = std::abs((double) recovered[i]);
            if (v > bestV) { bestV = v; bestI = i; }
        }
        return bestI;
    };
    const std::size_t idx1500 = findNear(mainIdx + 500, 32);
    const std::size_t idx2300 = findNear(mainIdx + 1300, 32);

    // Flat only between f1 and f2, so each "impulse" rings physically for
    // ~2 cycles at f1 -- excluded below, not counted as noise.
    const std::size_t halfWidth = (std::size_t) std::llround(2.0 * fs / f1);
    auto inExclusion = [&](std::size_t i) {
        auto near = [&](std::size_t centre) { return (i > centre ? i - centre : centre - i) <= halfWidth; };
        return near(mainIdx) || near(idx1500) || near(idx2300);
    };
    double sumSq = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < recovered.size(); ++i) {
        if (inExclusion(i)) continue;
        const double v = (double) recovered[i];
        sumSq += v * v;
        ++count;
    }
    const double meanSq = count > 0 ? sumSq / (double) count : 1.0;

    DeconvResult result;
    result.mainIndex = mainIdx;
    result.mainValue = mainVal;
    result.snrDb = 10.0 * std::log10((mainVal * mainVal) / meanSq);
    result.amp1500Rel = (double) recovered[idx1500] / mainSigned;
    result.amp2300Rel = (double) recovered[idx2300] / mainSigned;
    result.offset1500 = (std::ptrdiff_t) idx1500 - (std::ptrdiff_t) mainIdx;
    result.offset2300 = (std::ptrdiff_t) idx2300 - (std::ptrdiff_t) mainIdx;
    return result;
}

}  // namespace rta::test::sweep_fixtures
