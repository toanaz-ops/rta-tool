// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task I, the model half (plan I1-I6; decision record
// docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.6). Everything the G18
// surface KNOWS is here and JUCE-free, so it is proven with RTA_BUILD_APP=OFF
// on all three CI operating systems. The JUCE half is the dev-preview
// specimen, checked by the offscreen snapshot (I7, ALIGN-R8).

#include "AlignmentWizardFixture.h"
#include "view/CrossoverSurface.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::CrossoverFamily;
using rta::measure::ProcessorInversion;
using rta::measure::Topology;
using rta::test::codeLines;
using rta::test::kPi;
using rta::view::CrossoverSurface;
using rta::view::PhaseWindow;

namespace {

/// The crossing of risingDb()/fallingDb() sits at the middle bin.
constexpr double kWindowCentreHz = 12000.0;

rta::trace::VirtualTrace virtualOf(const rta::trace::Trace& trace) {
    auto result = rta::trace::VirtualTrace::fromTrace(trace);
    REQUIRE(result.has_value());
    return std::move(*result);
}

CrossoverSurface makeSurface(double highPhase = 0.0, double lowPhase = 0.0) {
    CrossoverSurface surface;
    surface.setAskedTopology({ CrossoverFamily::LinkwitzRiley, 4 }, ProcessorInversion::No);
    surface.setWindow({ kWindowCentreHz, 1.0 });
    surface.setSources(virtualOf(rta::test::makeCapture("hp", rta::test::risingDb(),
                                                        rta::test::constantPhase(highPhase))),
                       virtualOf(rta::test::makeCapture("lp", rta::test::fallingDb(),
                                                        rta::test::constantPhase(lowPhase))));
    return surface;
}

/// Identifiers a verdict would need. The scan below matches a DECLARATION --
/// the identifier must BE one of these words, not merely contain one -- which
/// is the correction the round-2 verifier made to the plan's own I3 pattern:
/// its leading wildcard let the word match INSIDE a longer identifier, so
/// `double lowPass = 1.0;` and the real `app/src/Main.cpp:119`
/// (`az::ui::AzLookAndFeel lookAndFeel;`) both hit.
const std::vector<std::string>& verdictWords() {
    static const std::vector<std::string> words{
        "pass",     "passed",  "passes",     "fail",      "failed",   "fails",
        "aligned",  "ok",      "ispassed",   "haspassed", "isaligned", "hasaligned",
        "isok",     "hasok",   "isfailed",   "hasfailed", "isfail",   "hasfail",
    };
    return words;
}

/// True when `line` declares or assigns something NAMED like a verdict: a
/// whole identifier from the list above, immediately followed by `(`, `;` or
/// `=`. Identifiers keep their underscores and digits, so `highPassSide_` is
/// one token and does not match `pass`.
bool declaresAVerdict(const std::string& line) {
    std::size_t i = 0;
    while (i < line.size()) {
        if (std::isalpha(static_cast<unsigned char>(line[i])) == 0 && line[i] != '_') {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < line.size()
               && (std::isalnum(static_cast<unsigned char>(line[i])) != 0 || line[i] == '_')) {
            ++i;
        }
        const std::string token = line.substr(start, i - start);
        std::size_t j = i;
        while (j < line.size() && line[j] == ' ') ++j;
        if (j >= line.size()) continue;
        const char next = line[j];
        if (next != '(' && next != ';' && next != '=') continue;
        if (std::find(verdictWords().begin(), verdictWords().end(), token)
            != verdictWords().end()) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST_CASE("I1: four traces, one asked line, and a relative phase that is EMPTY outside its "
          "window",
          "[crossover_surface]") {
    auto surface = makeSurface(kPi / 3.0, 0.0);

    const std::size_t points = rta::test::pointCount();
    CHECK(surface.pointCount() == points);
    CHECK(surface.highSideDb().size() == points);
    CHECK(surface.lowSideDb().size() == points);
    CHECK(surface.predictedSumDb().size() == points);
    CHECK(surface.ghostSumDb().size() == points);
    CHECK(surface.measuredSumDb().empty());  // nothing measured yet, and it SAYS so

    // The relative-phase series exists only inside the fit window. A
    // full-length array zero-filled outside would draw a flat line through the
    // middle of the plot and read as "in phase everywhere else".
    const double lowEdge = kWindowCentreHz / 2.0;
    const double highEdge = kWindowCentreHz * 2.0;
    REQUIRE_FALSE(surface.relativePhase().empty());
    CHECK(surface.relativePhase().size() < points);
    for (const auto& point : surface.relativePhase()) {
        CHECK(point.hz >= lowEdge);
        CHECK(point.hz <= highEdge);
        // arg(H_A conj H_B) = arg(H_A) - arg(H_B) = pi/3 at every bin here.
        CHECK_THAT(point.radians, WithinAbs(kPi / 3.0, 1e-6));
    }
}

TEST_CASE("I1b: the target line carries a SIGN, so a flipped odd-order row goes red",
          "[crossover_surface]") {
    // RULING FROM PR #3 (merged), docs/research/2026-09-15-l7-align-order4-
    // probe.md Sec.8. BW1 and BW3 differ by pi; LR2's two inversion answers
    // differ by pi. Negating what the model reads from expectedOffset moves
    // every one of these.
    const auto targetFor = [](Topology topology, ProcessorInversion inversion) {
        CrossoverSurface surface;
        surface.setAskedTopology(topology, inversion);
        return surface;
    };

    const auto bw1 = targetFor({ CrossoverFamily::Butterworth, 1 }, ProcessorInversion::No);
    const auto bw3 = targetFor({ CrossoverFamily::Butterworth, 3 }, ProcessorInversion::No);
    CHECK_THAT(bw1.targetRadians(), WithinAbs(+kPi / 2.0, 1e-12));
    CHECK_THAT(bw3.targetRadians(), WithinAbs(-kPi / 2.0, 1e-12));
    CHECK(std::abs(std::remainder(bw1.targetRadians() - bw3.targetRadians(), 2.0 * kPi))
          > 3.14);

    const auto lr2Yes =
        targetFor({ CrossoverFamily::LinkwitzRiley, 2 }, ProcessorInversion::Yes);
    const auto lr2No = targetFor({ CrossoverFamily::LinkwitzRiley, 2 }, ProcessorInversion::No);
    CHECK_THAT(lr2Yes.targetRadians(), WithinAbs(0.0, 1e-12));
    CHECK_THAT(std::abs(lr2No.targetRadians()), WithinAbs(kPi, 1e-12));
    CHECK_FALSE(lr2Yes.targetAmbiguous());

    // Question (c)'s Unknown branch: two lines, and nothing picks.
    const auto unknown =
        targetFor({ CrossoverFamily::LinkwitzRiley, 2 }, ProcessorInversion::Unknown);
    CHECK(unknown.targetAmbiguous());
    CHECK(std::abs(std::remainder(unknown.targetRadians() - unknown.alternativeTargetRadians(),
                                  2.0 * kPi))
          > 3.14);
}

TEST_CASE("I2: a pending op moves the prediction and leaves the ghost and the measured series "
          "bitwise alone",
          "[crossover_surface]") {
    auto surface = makeSurface(0.0, 0.0);

    std::vector<float> measured(rta::test::pointCount(), -1.25f);
    surface.setMeasuredSum(measured);

    const std::vector<float> ghostBefore = surface.ghostSumDb();
    const std::vector<float> measuredBefore = surface.measuredSumDb();
    const std::vector<float> predictedBefore = surface.predictedSumDb();

    // A polarity flip on the high-pass side: the largest change a single G11
    // op can make to a sum, so "the prediction moved" is not a rounding claim.
    surface.setPendingOps({ rta::trace::PolarityOp{} }, {});

    CHECK(surface.ghostSumDb() == ghostBefore);        // bitwise
    CHECK(surface.measuredSumDb() == measuredBefore);  // bitwise
    CHECK_FALSE(surface.predictedSumDb() == predictedBefore);

    // And the ghost is the PRE-alignment sum, not a copy of the prediction.
    CHECK(ghostBefore == predictedBefore);
}

TEST_CASE("I3: the marks are marks, and nothing here is named like a verdict",
          "[crossover_surface]") {
    // 20log10(2) and the topology's designed sum, exposed as reference marks.
    auto lr4 = makeSurface();
    CHECK_THAT(lr4.marks().coherentSumDb, WithinAbs(6.020599913279624, 1e-12));
    CHECK_THAT(lr4.marks().designedSumDb, WithinAbs(0.0, 1e-12));

    CrossoverSurface bw2;
    bw2.setAskedTopology({ CrossoverFamily::Butterworth, 2 }, ProcessorInversion::Yes);
    CHECK_THAT(bw2.marks().designedSumDb, WithinAbs(3.010299956639812, 1e-12));

    CrossoverSurface bw3;
    bw3.setAskedTopology({ CrossoverFamily::Butterworth, 3 }, ProcessorInversion::No);
    // The odd-order row sums flat in EITHER polarity, which is exactly what
    // makes "maximise the sum" a flat objective there (record Sec.6.2).
    CHECK_THAT(bw3.marks().designedSumDb, WithinAbs(0.0, 1e-12));

    // The declaration scan, with its positive controls first -- a scan that
    // stopped matching would otherwise pass on everything.
    CHECK(declaresAVerdict("    bool passed = false;"));
    CHECK(declaresAVerdict("    [[nodiscard]] bool isaligned() const noexcept;"));
    CHECK_FALSE(declaresAVerdict("    az::ui::azlookandfeel lookandfeel;"));
    CHECK_FALSE(declaresAVerdict("    double lowpass = 1.0;"));

    const std::filesystem::path root{ RTA_REPO_ROOT };
    const std::filesystem::path scanned[]{
        root / "app" / "src" / "view" / "CrossoverSurface.h",
        root / "app" / "src" / "view" / "CrossoverSurface.cpp",
    };
    for (const auto& file : scanned) {
        REQUIRE(std::filesystem::exists(file));
        for (const auto& line : codeLines(file)) {
            INFO(file.filename().string() << ": " << line);
            CHECK_FALSE(declaresAVerdict(line));
        }
    }
    // Negative controls on real files the earlier spelling falsely hit.
    for (const auto& file : { root / "app" / "src" / "view" / "BodeLayout.h",
                              root / "core" / "include" / "rta" / "ir" / "Polarity.h" }) {
        for (const auto& line : codeLines(file)) {
            INFO(file.filename().string() << ": " << line);
            CHECK_FALSE(declaresAVerdict(line));
        }
    }
}
