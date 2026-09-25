// SPDX-License-Identifier: AGPL-3.0-or-later
// Lane L6a Wave 2, task W2-D (docs/plans/2026-09-17-L6a-spl-pro-impl-plan.md;
// record docs/dsp/2026-09-16-spl-pro-l6a.md §4, §11, SPL-R11).
#include "view/SplStrip.h"
#include "view/PaneRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

using Catch::Matchers::WithinAbs;
using rta::view::SplStripRect;
using rta::meter::Block;

namespace {

Block blockAtLevel(std::uint64_t index, std::uint32_t samples, double levelDb) {
    Block b;
    b.blockIndex = index;
    b.blockSamples = samples;
    b.sumSquares = static_cast<double>(samples) * std::pow(10.0, levelDb / 10.0);
    return b;
}

std::string readWholeFile(const char* path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string stripComments(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            if (i < text.size()) out += '\n';
            continue;
        }
        out += text[i];
    }
    return out;
}

}  // namespace

// --- D1: the pane name falls back and says so -------------------------------

TEST_CASE("D1 resolvePaneView(\"spl\") resolves with no fallback", "[spl_strip]") {
    const auto resolved = rta::view::resolvePaneView("spl");
    CHECK(resolved.view == rta::view::PaneView::Spl);
    CHECK_FALSE(resolved.fellBack);
}

TEST_CASE("D1 an unrecognised pane name still falls back to Rta and says so", "[spl_strip]") {
    const auto resolved = rta::view::resolvePaneView("nonexistent");
    CHECK(resolved.view == rta::view::PaneView::Rta);
    CHECK(resolved.fellBack);
}

// --- D2: the strip model is numbers in, positions out -----------------------

TEST_CASE("D2 splStripGeometry maps the block-index span linearly across the rect", "[spl_strip]") {
    const SplStripRect content{ 0, 0, 1000, 200 };
    const auto geometry = rta::view::splStripGeometry(content, 100, 200, 140.0, 0.0);

    CHECK_THAT(geometry.xForBlockIndex(100), WithinAbs(0.0, 1e-6));
    CHECK_THAT(geometry.xForBlockIndex(200), WithinAbs(1000.0, 1e-6));
    CHECK_THAT(geometry.xForBlockIndex(150), WithinAbs(500.0, 1e-6));
}

TEST_CASE("D2 splStripGeometry's y axis is clamped and top-down", "[spl_strip]") {
    const SplStripRect content{ 0, 0, 1000, 200 };
    const auto geometry = rta::view::splStripGeometry(content, 0, 1, 140.0, 0.0);

    CHECK_THAT(geometry.yForDb(140.0), WithinAbs(0.0, 1e-6));    // top of the pane
    CHECK_THAT(geometry.yForDb(0.0), WithinAbs(200.0, 1e-6));    // bottom
    CHECK_THAT(geometry.yForDb(200.0), WithinAbs(0.0, 1e-6));    // clamped, not off-pane
    CHECK_THAT(geometry.yForDb(-50.0), WithinAbs(200.0, 1e-6));  // clamped
}

TEST_CASE("D2 splStripPoints returns one position per block, in order", "[spl_strip]") {
    const SplStripRect content{ 0, 0, 1000, 200 };
    const auto geometry = rta::view::splStripGeometry(content, 0, 9, 140.0, 0.0);

    std::vector<Block> blocks;
    for (std::uint64_t i = 0; i <= 9; ++i) blocks.push_back(blockAtLevel(i, 480, 80.0));

    const auto points = rta::view::splStripPoints(geometry, blocks, 0.0);
    REQUIRE(points.size() == 10);
    for (std::size_t i = 0; i < points.size(); ++i) CHECK(points[i].blockIndex == i);
    // The known level (80 dB, no offset) lands at a computable y.
    CHECK_THAT(points[0].y, WithinAbs(geometry.yForDb(80.0), 1e-4f));
}

TEST_CASE("D2 SplStrip.h holds no colour and no JUCE type", "[spl_strip]") {
    const auto header = stripComments(readWholeFile(RTA_REPO_ROOT "/app/src/view/SplStrip.h"));
    for (const char* needle : { "colour", "Colour", "color", "juce::", "Component", "Graphics" }) {
        INFO("needle " << needle);
        CHECK(header.find(needle) == std::string::npos);
    }
}

// --- D3: readouts obey CLAUDE.md, the three that exist ----------------------

TEST_CASE("D3 the strip's readouts are formatTrim and formatAgreement, bit for bit", "[spl_strip]") {
    CHECK(rta::view::splCurrentLevelLabel(94.27) == rta::view::formatTrim(94.27));
    CHECK(rta::view::splBufferFillLabel(0.6789) == rta::view::formatAgreement(0.6789));

    // dB keeps one decimal; the buffer fill keeps two -- CLAUDE.md's own
    // per-quantity rule, not unified for tidiness.
    CHECK(rta::view::splCurrentLevelLabel(94.27) == "94.3 dB");
    CHECK(rta::view::splBufferFillLabel(0.6789) == "0.68");
}

TEST_CASE("D3 SplStrip.h introduces no fourth formatter", "[spl_strip]") {
    // A structural scan: the only "std::string format*(" DEFINITIONS this
    // pane's headless half may contain are the two that forward straight
    // into Readouts.h's existing three -- never a new one, e.g. a hand-rolled
    // "formatDb" (PR #12's own defect E1: that name never existed).
    const auto header = stripComments(readWholeFile(RTA_REPO_ROOT "/app/src/view/SplStrip.h"));
    std::size_t formatDefinitions = 0;
    std::size_t pos = 0;
    while ((pos = header.find("std::string spl", pos)) != std::string::npos) {
        ++formatDefinitions;
        pos += 1;
    }
    CHECK(formatDefinitions == 2);  // splCurrentLevelLabel, splBufferFillLabel
    CHECK(header.find("formatDb") == std::string::npos);
}
