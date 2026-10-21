// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- rta_core. No JUCE, no Qt, no audio-device API.
//
// Wall-clock benchmark of Fft and RealFft. Not a test and not registered with
// ctest: timings are machine-dependent, so nothing here may gate a build. It is
// built only with -DRTA_BUILD_BENCH=ON and exists so a performance claim about
// the transform can be re-measured by anyone rather than taken on trust.
//
// Output is the median over several runs; each run times enough back-to-back
// transforms to last ~20 ms, so timer resolution and one-off cache misses fall
// out of the figure. The data is transformed in place and never reset: a
// forward then inverse pair returns the input to within float rounding, so the
// buffer stays bounded and no run feeds denormals or infinities to the FFT.

#include "rta/dsp/Fft.h"
#include "rta/dsp/RealFft.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kRuns = 15;

template <typename Fn>
double medianMicroseconds(Fn&& fn) {
    // Calibrate the repeat count so one run lasts about 20 ms.
    std::size_t reps = 1;
    for (;;) {
        const auto t0 = Clock::now();
        for (std::size_t i = 0; i < reps; ++i) fn();
        const double ms =
            std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (ms >= 20.0) break;
        reps *= 2;
    }
    std::vector<double> samples;
    for (int run = 0; run < kRuns; ++run) {
        const auto t0 = Clock::now();
        for (std::size_t i = 0; i < reps; ++i) fn();
        const double us =
            std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        samples.push_back(us / static_cast<double>(reps));
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

}  // namespace

int main() {
    std::printf("%-22s %8s %14s\n", "case", "N", "median us");
    double sink = 0.0;
    for (const std::size_t n : { std::size_t{256}, std::size_t{4096}, std::size_t{65536} }) {
        std::vector<std::complex<float>> data(n);
        for (std::size_t i = 0; i < n; ++i) {
            data[i] = { std::sin(0.01f * static_cast<float>(i)),
                        std::cos(0.013f * static_cast<float>(i)) };
        }
        rta::dsp::Fft fft(n);
        // The complex transform works in place, so repeating it on one buffer
        // would overflow (forward grows by N) or decay to denormals (inverse
        // shrinks by N), and either distorts the timing. Each repeat therefore
        // restores a pristine copy first; the cost of that copy alone is
        // measured the same way and subtracted.
        const std::vector<std::complex<float>> pristine = data;
        const double copyUs = medianMicroseconds([&] {
            std::copy(pristine.begin(), pristine.end(), data.begin());
        });
        const double fwdUs = medianMicroseconds([&] {
            std::copy(pristine.begin(), pristine.end(), data.begin());
            fft.forward(data);
        });
        sink += static_cast<double>(data[1].real());
        const double invUs = medianMicroseconds([&] {
            std::copy(pristine.begin(), pristine.end(), data.begin());
            fft.inverse(data);
        });
        sink += static_cast<double>(data[1].real());
        std::printf("%-22s %8zu %14.2f\n", "Fft::forward", n, fwdUs - copyUs);
        std::printf("%-22s %8zu %14.2f\n", "Fft::inverse", n, invUs - copyUs);

        std::vector<float> real(n);
        for (std::size_t i = 0; i < n; ++i) real[i] = std::sin(0.02f * static_cast<float>(i));
        std::vector<std::complex<float>> spectrum(n / 2 + 1);
        rta::dsp::RealFft rfft(n);
        std::printf("%-22s %8zu %14.2f\n", "RealFft::forward", n,
                    medianMicroseconds([&] { rfft.forward(real, spectrum); }));
        std::printf("%-22s %8zu %14.2f\n", "RealFft::inverse", n,
                    medianMicroseconds([&] { rfft.inverse(spectrum, real); }));
        sink += static_cast<double>(real[1]);
    }
    std::printf("checksum %.6f\n", sink);
    return 0;
}
