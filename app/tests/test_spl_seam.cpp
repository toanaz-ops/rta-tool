// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lane L6a task W0-E (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md; record
// §13 Q1, research §C2): the 3.0103 dB seam, closed form.
//
// THIS TASK CARRIES THE SCOPE DEFAULT. Record §13 Q1 asks whether the
// broadband SPL readout adopts the mean-square convention beside
// sine-referenced RTA bands, or whether the bands move. The default TAKEN is
// the record's own proposal: CONVERT ONCE AT THE METER SEAM, LABEL BOTH,
// CHANGE NOTHING THAT EXISTS. This file exists to make that default PROVABLE
// rather than merely intended -- it adds no source file, because the property
// is a consequence of arithmetic already shipped and the only thing missing
// was a test that says so.
//
// Flipping it (moving the bands) costs a schema bump plus a golden
// regeneration: kFullScaleSineOffsetDb goes to 0.0, every dBFS number on
// screen and in every stored Trace shifts by 3.0103 dB, and a session written
// under one convention and read under the other is silently 3 dB wrong unless
// SessionCodec.h's kSchemaVersion rises to 4 with a levelConvention key.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "CodeLines.h"

#include "measure/AnalysisPublish.h"
#include "measure/Levels.h"
#include "measure/SplConfig.h"
#include "measure/SplMeter.h"
#include "view/Readouts.h"

#include "rta/meter/Block.h"

#include <cmath>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::kFullScaleSineOffsetDb;
using rta::measure::SplConfig;
using rta::measure::SplMeter;

namespace {

constexpr double kFs = 48000.0;

/// A unit-amplitude 1 kHz sine, a whole number of periods in a 48 000-sample
/// block, so the block's mean square is 0.5 up to float32 quantisation.
std::vector<float> unitSine() {
    std::vector<float> x(static_cast<std::size_t>(kFs));
    for (std::size_t n = 0; n < x.size(); ++n) {
        const double t = static_cast<double>(n) / kFs;
        x[n] = static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * t));
    }
    return x;
}

/// The published SPL metric for one block of `x` at `offset`, through the Z
/// (flat) path, so nothing but the seam is in the arithmetic.
double publishedSplDb(const std::vector<float>& x, double offset) {
    SplConfig config;
    config.blockSeconds = 1.0;
    config.referenceOffsetDb = offset;
    SplMeter meter(config, rta::dsp::WeightingType::Z, kFs);
    meter.push(x);
    const auto block = meter.poll();
    REQUIRE(block.has_value());
    return SplMeter::blockLevelDb(*block, offset);
}

}  // namespace

// --- E1: the two conventions, stated ------------------------------------

TEST_CASE("E1 the two conventions differ by kFullScaleSineOffsetDb and nothing else",
          "[splseam]") {
    // A unit-amplitude sine has mean square 0.5.
    const double meanSquare = 0.5;

    // Through the APP's path: sine-referenced dBFS. Levels.h:44 adds
    // kFullScaleSineOffsetDb so a full-scale SINE reads 0.0 dBFS -- the
    // reference this whole app reads levels against, matching every analyser
    // on a rack.
    //
    // NOT ASSERTED BITWISE, and the reason is a measured CI failure rather
    // than a preference. An earlier revision of this line read
    // `CHECK(levelDbFs(meanSquare) == 0.0);  // EXACTLY, bitwise`, and that
    // equality is a coincidence of TWO ROUNDINGS, not an identity:
    // `log10(0.5)` is irrational, and it is rounding the product
    // `10 * log10(0.5)` to a double BEFORE the add that lands it on exactly
    // -kFullScaleSineOffsetDb. Fuse the multiply and the add into one `fma`
    // -- which Apple clang does on arm64, where FMA is in the baseline ISA --
    // and the product keeps its full width, so the sum reads 2^-53 =
    // 1.1102230246251565e-16 dB. Run 35306075307, macos-latest: this case and
    // E3 went red on exactly that, on arithmetic nobody had changed.
    // CMakeLists.txt now compiles with `-ffp-contract=off`, so the bitwise
    // form would pass again -- and it still must not be written that way,
    // because an assertion whose truth is a compiler flag records the flag,
    // not the arithmetic. PR #5 retired a `std::isinf` claim at Nyquist for
    // the same reason: "the exactness given up there was never real".
    //
    // THE BOUND IS DERIVED, not read off the failing run:
    //   * log10(0.5) = -0.30102999566398120 lies in [0.25, 0.5), so its ulp
    //     is 2^-54; libm is accurate to <= 1 ulp there, and the factor of 10
    //     carries that to 10 * 2^-54 = 5.6e-16 dB.
    //   * |10 * log10(0.5)| = 3.0103 lies in [2, 4), so its ulp is 2^-51 and
    //     rounding the product -- or NOT rounding it, under contraction --
    //     moves the result by at most 2^-52 = 2.2e-16 dB.
    //   * the final add is a subtraction of two nearly equal quantities, so
    //     it is exact (Sterbenz) and contributes nothing.
    // Total <= 7.8e-16 dB. 1e-15 is the tolerance the three sibling
    // assertions in this very case already use, it is 9x the largest residual
    // any of the three CI platforms has produced, and it is 1e-14 times the
    // 0.1 dB the readout can show (CLAUDE.md, "Reading out numbers").
    INFO("levelDbFs(0.5) residual from 0.0 = " << rta::measure::levelDbFs(meanSquare));
    CHECK_THAT(rta::measure::levelDbFs(meanSquare), WithinAbs(0.0, 1e-15));

    // The BITWISE half of the same claim, moved to the fixture where it is
    // exact BY ARITHMETIC rather than by luck. log10(1.0) is exactly +0.0, so
    // `10 * 0.0 + k` and `fma(10.0, 0.0, k)` are both exactly k: no rounding
    // happens anywhere, on any platform, under any contraction setting.
    //
    // WHAT IT DOES AND DOES NOT CATCH, measured by mutation rather than
    // assumed: editing kFullScaleSineOffsetDb leaves it GREEN, because both
    // sides move together -- the constant's VALUE is pinned against a literal
    // by the last assertion in this case, which is the one that went red when
    // the constant was moved to 3.0102999566398000 (residual -1.2e-14 dB).
    // What this pair catches is `levelDbFs` losing the offset, applying it
    // twice, or clamping a level it should have passed through. The log10
    // line is here so a libm returning a non-zero log10(1.0) would say which
    // of the two failed.
    CHECK(std::log10(1.0) == 0.0);
    CHECK(rta::measure::levelDbFs(1.0) == kFullScaleSineOffsetDb);

    // Through the METER's path with no offset: mean-square referenced, IEC
    // 61672-1 cl. 3.9's own definition, what rta::meter::Leq already
    // implements.
    const double meterDb = 10.0 * std::log10(meanSquare);
    INFO("levelDbFs(0.5)          = " << rta::measure::levelDbFs(meanSquare));
    INFO("10*log10(0.5)           = " << meterDb);
    INFO("kFullScaleSineOffsetDb  = " << kFullScaleSineOffsetDb);
    CHECK_THAT(meterDb, WithinAbs(-kFullScaleSineOffsetDb, 1e-15));

    // The difference is that ONE constant. Not a coincidence and not two
    // constants that happen to agree.
    CHECK_THAT(rta::measure::levelDbFs(meanSquare) - meterDb,
               WithinAbs(kFullScaleSineOffsetDb, 1e-15));
    CHECK_THAT(kFullScaleSineOffsetDb, WithinAbs(3.0102999566398120, 1e-15));
}

// --- E2: one conversion, at one seam ------------------------------------

TEST_CASE("E2 a full-scale sine reads the same dB through both paths once the offset is applied",
          "[splseam]") {
    // THIS IS THE ACCEPTANCE THE BRIEF ASKS FOR. It holds because the offset
    // ABSORBS the constant -- which is also why the mismatch bites only
    // UNCALIBRATED dBFS (research §C2): once a calibration offset exists it
    // has swallowed the 3.0103 along with everything else.
    const auto x = unitSine();
    const double published = publishedSplDb(x, kFullScaleSineOffsetDb);
    const double band = rta::measure::levelDbFs(0.5);

    INFO("published SPL metric = " << published);
    INFO("RTA band (levelDbFs) = " << band);
    INFO("residual             = " << (published - band));
    // TOLERANCE DEVIATION FROM THE PLAN, MEASURED NOT ARGUED: the row asks
    // for 1e-9. The sine's samples are rounded to FLOAT32 before the meter
    // ever sees them, so up to half a float ULP (2.98e-08) of relative
    // amplitude error gives up to ~2.6e-07 dB of mean-square error -- and the
    // measured residual is -8.27e-08 dB, 80x outside 1e-9 and nothing to do
    // with summation. 1e-6 is the float-derived bound. The BITWISE form of
    // the same identity is asserted below on an exactly representable
    // fixture, where it belongs.
    CHECK_THAT(published, WithinAbs(band, 1e-6));
    CHECK_THAT(published, WithinAbs(0.0, 1e-6));

    SECTION("the seam is exact when the fixture is exactly representable") {
        // Alternating +-1: every sample squares to 1.0 and 48 000 of them sum
        // exactly, so the mean square is exactly 1.0 -- which is the mean
        // square of a full-scale SQUARE wave, 3.0103 dB above the sine. The
        // meter reads 0.0 with no offset and the band reads +3.0103, and the
        // gap is the constant, BITWISE.
        std::vector<float> square(static_cast<std::size_t>(kFs));
        for (std::size_t n = 0; n < square.size(); ++n) {
            square[n] = (n % 2 == 0) ? 1.0f : -1.0f;
        }
        CHECK(publishedSplDb(square, 0.0) == 0.0);
        CHECK(rta::measure::levelDbFs(1.0) == kFullScaleSineOffsetDb);
        CHECK(rta::measure::levelDbFs(1.0) - publishedSplDb(square, 0.0)
              == kFullScaleSineOffsetDb);
    }
}

// --- E3: after calibration the question evaporates ----------------------

TEST_CASE("E3 with a calibration offset the metric reads 94 dB and the band still reads 0 dBFS",
          "[splseam]") {
    // Two different quantities, two correct numbers, and the LABEL is what
    // distinguishes them. This is why the record's proposal is "convert once,
    // label both" rather than "pick one".
    const double offset = rta::meter::calibrationOffsetDb(94.0, -kFullScaleSineOffsetDb);
    INFO("calibrationOffsetDb(94, -3.0103) = " << offset);
    CHECK_THAT(offset, WithinAbs(94.0 + kFullScaleSineOffsetDb, 1e-15));

    const auto x = unitSine();
    const double published = publishedSplDb(x, offset);
    INFO("published = " << published << ", residual = " << (published - 94.0));
    CHECK_THAT(published, WithinAbs(94.0, 1e-6));  // float-derived, see E2

    // The band is untouched: nothing in this lane moved kFullScaleSineOffsetDb
    // and nothing may. Within the derived bound rather than bitwise, and
    // bitwise on the fixture where log10 is exact -- E1 carries the
    // derivation and the FMA-contraction measurement behind both forms.
    INFO("levelDbFs(0.5) residual from 0.0 = " << rta::measure::levelDbFs(0.5));
    CHECK_THAT(rta::measure::levelDbFs(0.5), WithinAbs(0.0, 1e-15));
    CHECK(rta::measure::levelDbFs(1.0) == kFullScaleSineOffsetDb);
}

// --- E4: the label carries the convention, and no fourth formatter -------

TEST_CASE("E4 the SPL readout is composed from the shipped formatters, and no new one is written",
          "[splseam]") {
    // Record §9 and L-API §11 item 13: NO NEW FORMATTER MAY BE WRITTEN.
    // formatTrim and formatHz already ship, are already tested
    // (app/tests/test_readouts.cpp), are already in the guard's GLOBS and
    // already have a live caller (app/src/view/DevicePanel.cpp).
    //
    // The unit suffix is INSIDE the returned string, so an assertion that
    // omitted it would fail on the suffix rather than on the rounding.
    CHECK(rta::view::formatTrim(94.0) == "94.0 dB");
    CHECK(rta::view::formatHz(1000.0) == "1000 Hz");

    // The convention word is APPENDED by the readout, never baked into a new
    // formatter -- so the composition is a concatenation and that is the whole
    // of it. CLAUDE.md's readout rules hold on both halves: dB keeps one
    // decimal, hertz is a whole number.
    const auto splLabel = [](double valueDb, bool calibrated) {
        return rta::view::formatTrim(valueDb)
               + (calibrated ? " SPL" : " re FS (mean square)");
    };
    CHECK(splLabel(94.0, true) == "94.0 dB SPL");
    CHECK(splLabel(-30.25, false) == "-30.2 dB re FS (mean square)");
    CHECK(splLabel(0.0, false) == "0.0 dB re FS (mean square)");

    // Structural: app/src/view holds exactly the four formatters Readouts.h
    // already declared before this lane, and this lane added none.
    const std::string code =
        rta::test::codeText(std::filesystem::path(RTA_REPO_ROOT) / "app" / "src" / "view"
                            / "Readouts.h");
    int formatters = 0;
    std::size_t pos = 0;
    while ((pos = code.find("std::string format", pos)) != std::string::npos) {
        ++formatters;
        pos += 1;
    }
    INFO("formatters declared in Readouts.h = " << formatters);
    CHECK(formatters == 4);
}
