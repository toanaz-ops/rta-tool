// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Pinned against scipy.signal.csd/welch/coherence via tools/gen_mtw.py, one
// band at a time -- see that script's docstring for why each band's own
// TAIL slice (not the whole 19456-sample file) is what scipy analyses: the
// FIFO holds only the last 16 frames once a band has produced more than 16,
// and the scaled table (N0=512, K=3) spreads frame counts 16..149 across its
// four bands (task 4's "the generator matches the FIFO's window, not the
// whole file").

#include "rta/dsp/MtwEngine.h"
#include "rta/dsp/MtwLayout.h"
#include "rta/dsp/MtwResult.h"
#include "support/Golden.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace rta::dsp;

namespace {

// A REFERENCE to the function-local static, not a copy: findCase() below
// returns a reference INTO this vector, and binding that to a `const auto&`
// at the call site is only safe if the vector itself outlives the call --
// returning by value here would make every findCase() result a dangling
// reference to a temporary, destroyed at the end of the full expression that
// created it (found the hard way: case 1 above "worked" only because nothing
// reused the freed memory before it was read; case 2 does more allocation
// in between and crashed).
const std::vector<rta::test::GoldenCase>& mtwGolden() {
    static const auto cases = rta::test::loadGolden(std::string(RTA_GOLDEN_DIR) + "/mtw.txt");
    return cases;
}

// `name` is a by-value view, not `const std::string&`: gcc's -Wdangling-reference
// heuristic flags any reference-returning call that binds a temporary to a
// reference parameter, and the string built from a literal here is compared,
// never returned. A view has no reference for the heuristic to trip on and
// skips the allocation.
const rta::test::GoldenCase& findCase(const std::vector<rta::test::GoldenCase>& cases,
                                      std::string_view name) {
    for (const auto& c : cases) {
        if (c.name == name) return c;
    }
    throw std::runtime_error("golden case not found: " + std::string(name));
}

double peakAbs(const std::vector<double>& values) {
    double peak = 0.0;
    for (const double v : values) peak = std::max(peak, std::abs(v));
    return peak;
}

/// The two-term float32 tolerance (memory/float32-fft-precision.md), applied
/// per BAND: a 4096-point band's rounding floor and a 512-point band's are
/// different numbers, and a global peak would silently hand the tight band
/// the loose one's tolerance.
double tolerance(double expected, double bandPeak) {
    return std::abs(expected) * 1.0e-5 + bandPeak * 1.0e-6;
}

}  // namespace

TEST_CASE("the golden's layout block matches MtwLayout for the default table", "[mtw][golden]") {
    const auto& layoutCase = findCase(mtwGolden(), "mtw_layout_default");

    MtwConfig cfg;  // defaults: N0 = 1024, K = 6
    REQUIRE(mtwPointCount(cfg) == 1281);
    REQUIRE(static_cast<std::size_t>(layoutCase.row("point_count").front()) == 1281);

    const auto bands = mtwBands(cfg);
    const auto& firstIndex = layoutCase.row("first_index");
    const auto& fftSize = layoutCase.row("fft_size");
    const auto& firstBin = layoutCase.row("first_bin");
    const auto& lastBin = layoutCase.row("last_bin");
    const auto& integrationSeconds = layoutCase.row("integration_seconds");
    REQUIRE(bands.size() == firstIndex.size());
    for (std::size_t i = 0; i < bands.size(); ++i) {
        CAPTURE(i);
        REQUIRE(bands[i].firstIndex == static_cast<std::size_t>(firstIndex[i]));
        REQUIRE(bands[i].fftSize == static_cast<std::size_t>(fftSize[i]));
        REQUIRE(bands[i].firstBin == static_cast<std::size_t>(firstBin[i]));
        REQUIRE(bands[i].lastBin == static_cast<std::size_t>(lastBin[i]));
        REQUIRE(mtwIntegrationSeconds(cfg, i) ==
                Catch::Approx(integrationSeconds[i]).margin(1e-7));
    }

    const auto freq = mtwFrequencies(cfg);
    const auto& expectedFreq = layoutCase.row("freq");
    REQUIRE(freq.size() == expectedFreq.size());
    for (std::size_t i = 0; i < freq.size(); ++i) {
        CAPTURE(i);
        REQUIRE(freq[i] == Catch::Approx(expectedFreq[i]).epsilon(1e-9));
    }
}

TEST_CASE("the MTW golden from scipy matches the stitched Sxx, Syy, Sxy and coherence",
          "[mtw][golden]") {
    const auto& stitched = findCase(mtwGolden(), "mtw_stitched_512_3");

    MtwConfig cfg;
    cfg.topFftSize = 512;
    cfg.octaveCount = 3;
    cfg.averaging = TransferAveraging::Fifo;
    cfg.fifoDepth = 16;  // the shipping default

    const auto x = stitched.floatRow("input_x");
    const auto y = stitched.floatRow("input_y");

    MtwEngine engine(cfg);
    engine.process(x, y);

    // The premise the generator's tail-slice matches: every band's FIFO
    // holds exactly its last 16 frames, all at Neff = 8.5866271.
    for (std::size_t i = 0; i < engine.bandCount(); ++i) {
        CAPTURE(i, engine.band(i).config().fftSize);
        REQUIRE(engine.band(i).frameCount() >= 16);
        REQUIRE(engine.band(i).effectiveAverages() == Catch::Approx(8.5866271).epsilon(1e-6));
    }

    const auto bands = mtwBands(cfg);
    const auto& expectedFreq = stitched.row("freq");
    const auto& expectedPxx = stitched.row("pxx");
    const auto& expectedPyy = stitched.row("pyy");
    const auto& expectedPxyReal = stitched.row("pxy_real");
    const auto& expectedPxyImag = stitched.row("pxy_imag");
    const auto& expectedCoherence = stitched.row("coherence");
    REQUIRE(expectedFreq.size() == mtwPointCount(cfg));

    for (std::size_t b = 0; b < bands.size(); ++b) {
        const auto& band = bands[b];
        const std::size_t count = band.lastBin - band.firstBin + 1;

        std::vector<double> pxxSlice(expectedPxx.begin() + static_cast<long>(band.firstIndex),
                                      expectedPxx.begin() +
                                          static_cast<long>(band.firstIndex + count));
        std::vector<double> pyySlice(expectedPyy.begin() + static_cast<long>(band.firstIndex),
                                      expectedPyy.begin() +
                                          static_cast<long>(band.firstIndex + count));
        std::vector<double> pxyMagnitude(count);
        for (std::size_t n = 0; n < count; ++n) {
            pxyMagnitude[n] = std::hypot(expectedPxyReal[band.firstIndex + n],
                                          expectedPxyImag[band.firstIndex + n]);
        }
        const double pxxPeak = peakAbs(pxxSlice);
        const double pyyPeak = peakAbs(pyySlice);
        const double pxyPeak = peakAbs(pxyMagnitude);

        const auto& fftEngine = engine.band(b);
        for (std::size_t bin = band.firstBin; bin <= band.lastBin; ++bin) {
            const std::size_t index = band.firstIndex + (bin - band.firstBin);
            CAPTURE(b, bin, index);
            CHECK_THAT(fftEngine.binFrequency(bin), WithinAbs(expectedFreq[index], 1.0e-6));
            CHECK_THAT(fftEngine.referencePsd()[bin],
                       WithinAbs(expectedPxx[index], tolerance(expectedPxx[index], pxxPeak)));
            CHECK_THAT(fftEngine.measurementPsd()[bin],
                       WithinAbs(expectedPyy[index], tolerance(expectedPyy[index], pyyPeak)));
            CHECK_THAT(fftEngine.crossPsd()[bin].real(),
                       WithinAbs(expectedPxyReal[index],
                                 tolerance(expectedPxyReal[index], pxyPeak)));
            CHECK_THAT(fftEngine.crossPsd()[bin].imag(),
                       WithinAbs(expectedPxyImag[index],
                                 tolerance(expectedPxyImag[index], pxyPeak)));

            const double coherence = magnitudeSquaredCoherence(
                fftEngine.crossPsd()[bin], fftEngine.referencePsd()[bin],
                fftEngine.measurementPsd()[bin]);
            CHECK_THAT(coherence, WithinAbs(expectedCoherence[index],
                                             tolerance(expectedCoherence[index], 1.0)));
        }
    }
}
