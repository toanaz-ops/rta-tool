// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pinned against scipy.signal.csd/welch/coherence via tools/gen_transfer.py.
//
// scipy's `_spectral_helper` computes `conjugate(X) * Y` for the
// cross-spectrum -- the same convention `DualFftEngine::crossPsd()` uses --
// so `csd(reference, measurement)` lines up directly, with no conjugation
// anywhere. scipy's default `average="mean"` is a plain mean over segments,
// which our FIFO equals only once `fifoDepth` exceeds the frame count, so
// this test sets it far above anything these cases can produce.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "rta/dsp/DualFftEngine.h"
#include "rta/dsp/TransferEstimator.h"
#include "support/Golden.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::dsp;

namespace {

std::vector<rta::test::GoldenCase> transferGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/transfer.txt");
    return cases;
}

double peakAbs(const std::vector<double>& values) {
    double peak = 0.0;
    for (const double v : values) peak = std::max(peak, std::abs(v));
    return peak;
}

/// Two-term tolerance, not a bare `epsilon(1e-5)`: `RealFft` transforms in
/// `std::complex<float>` (memory/float32-fft-precision.md), so the absolute
/// error floor is set by the LARGEST bin in the array, not by the bin under
/// test. A relative-only tolerance is unattainable at a spectral null -- e.g.
/// `pxy_imag` is exactly (or near) zero at DC and Nyquist for a real signal,
/// and coherence can sit near zero at a filter notch. `1e-5` of the value
/// plus `1e-6` of the array's own peak is the same shape this codebase
/// already needed for test_dualfft.cpp and test_transfer_estimator.cpp.
double tolerance(double expected, double peak) {
    return std::abs(expected) * 1.0e-5 + peak * 1.0e-6;
}

}  // namespace

TEST_CASE("the golden vectors from scipy match Sxx, Syy, Sxy and coherence",
          "[transfer][golden]") {
    int checked = 0;

    for (const auto& testCase : transferGolden()) {
        ++checked;
        CAPTURE(testCase.name);

        const auto x = testCase.floatRow("input_x");
        const auto y = testCase.floatRow("input_y");
        const auto& expectedFreq = testCase.row("freq");
        const auto& expectedPxx = testCase.row("pxx");
        const auto& expectedPyy = testCase.row("pyy");
        const auto& expectedPxyReal = testCase.row("pxy_real");
        const auto& expectedPxyImag = testCase.row("pxy_imag");
        const auto& expectedCoherence = testCase.row("coherence");

        const auto nperseg = (std::size_t) testCase.row("nperseg").front();
        const auto noverlap = (std::size_t) testCase.row("noverlap").front();

        DualFftEngine::Config config;
        config.fftSize = nperseg;
        config.hopSize = nperseg - noverlap;
        config.sampleRate = 48000.0;
        config.window = WindowType::Hann;
        // Far above any frame count these 8192-sample cases can produce, so
        // the FIFO's mean is a PLAIN mean over every frame -- what scipy's
        // average="mean" computes. See the plan's task 7 note on this.
        config.fifoDepth = 1u << 16;
        DualFftEngine engine(config);
        engine.process(x, y);

        REQUIRE(engine.numBins() == expectedFreq.size());
        REQUIRE(engine.frameCount() > 0);
        REQUIRE(engine.frameCount() < config.fifoDepth);  // the plain-mean premise above

        const double pxxPeak = peakAbs(expectedPxx);
        const double pyyPeak = peakAbs(expectedPyy);
        // Real and imaginary parts of the same complex Sxy share one peak: a
        // bin whose energy is entirely in the imaginary part must not be held
        // to a tighter floor than a bin whose energy is entirely real.
        std::vector<double> pxyMagnitude(expectedPxyReal.size());
        for (std::size_t k = 0; k < pxyMagnitude.size(); ++k) {
            pxyMagnitude[k] =
                std::hypot(expectedPxyReal[k], expectedPxyImag[k]);
        }
        const double pxyPeak = peakAbs(pxyMagnitude);
        const double coherencePeak = peakAbs(expectedCoherence);  // <= 1.0 by definition

        for (std::size_t k = 0; k < engine.numBins(); ++k) {
            CAPTURE(k);
            CHECK_THAT(engine.binFrequency(k),
                       WithinAbs(expectedFreq[k], 1.0e-6));

            CHECK_THAT(engine.referencePsd()[k],
                       WithinAbs(expectedPxx[k], tolerance(expectedPxx[k], pxxPeak)));
            CHECK_THAT(engine.measurementPsd()[k],
                       WithinAbs(expectedPyy[k], tolerance(expectedPyy[k], pyyPeak)));
            CHECK_THAT(engine.crossPsd()[k].real(),
                       WithinAbs(expectedPxyReal[k], tolerance(expectedPxyReal[k], pxyPeak)));
            CHECK_THAT(engine.crossPsd()[k].imag(),
                       WithinAbs(expectedPxyImag[k], tolerance(expectedPxyImag[k], pxyPeak)));

            // Computed directly from the engine's own accumulators, the same
            // free function makeSnapshot() uses -- this pins the formula, not
            // just the accumulators it is fed from.
            const double coherence = magnitudeSquaredCoherence(
                engine.crossPsd()[k], engine.referencePsd()[k], engine.measurementPsd()[k]);
            CHECK_THAT(coherence,
                       WithinAbs(expectedCoherence[k],
                                 tolerance(expectedCoherence[k], coherencePeak)));
        }
    }

    REQUIRE(checked == 3);
}
