// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task D (docs/plans/2026-09-15-L7-align-impl-plan.md; decision
// record docs/dsp/2026-09-06-l7-alignment-wizard.md Sec.3, Sec.10.2).
//
// D1-D4 are NOT re-derived here. PR #3 merged, so the five closed-form cases
// that prove the N*90-degree identity, LR's inheritance of it, the order-4
// in-phase sum and the order-2 null already live in
// core/tests/test_align_order4_identity.cpp and are folded in by reference,
// exactly as the plan's "Fold in PR #3's file" instructs. This file carries
// D5 (the shipped lookup) and D6 (core stays topology-free).

#include "measure/CrossoverTopology.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::CrossoverFamily;
using rta::measure::expectedOffset;
using rta::measure::ProcessorInversion;
using rta::measure::Topology;

namespace {

constexpr double kPi = std::numbers::pi;

/// Record Sec.3's table, transcribed as DATA rather than evaluated, so the
/// implementation's one closed form has something independent to be checked
/// against. `radians` is the RAW HP-LP offset with no processor inversion --
/// arg(H_HP) - arg(H_LP), the A = HP side contract (ALIGN-R14).
struct Row {
    CrossoverFamily family;
    int order;
    double rawRadians;
};

const std::vector<Row>& recordSection3() {
    // RULING FROM PR #3, merged: the N*90-degree identity holds exactly under
    // this repo's convention -- analog and digital, Butterworth and
    // Linkwitz-Riley, orders 1-8, at every frequency -- so record Sec.3's table
    // ships UNCHANGED. See docs/research/2026-09-15-l7-align-order4-probe.md
    // Sec.8. L4a's order-4 "wrong sign" is attributed there to L4a decision
    // 6b's UN-WHITENED correlation-peak-sign rule applied across two different
    // passbands, not to any convention -- and not to the shipped PHAT rule,
    // which reads the mirror of it on the same pair (probe Sec.5).
    //
    // The family does not appear in the arithmetic: an LR-N is the BW-(N/2)
    // cascaded with itself, so the same N gives the same offset. What the
    // family changes is the DESIGNED SUM, which is not this function's job.
    static const std::vector<Row> rows{
        { CrossoverFamily::Butterworth, 1, +kPi / 2.0 },  // N = 1 mod 4: +90
        { CrossoverFamily::Butterworth, 2, kPi },         // N = 2 mod 4: 180
        { CrossoverFamily::Butterworth, 3, -kPi / 2.0 },  // N = 3 mod 4: -90
        { CrossoverFamily::Butterworth, 4, 0.0 },         // N = 0 mod 4: 0
        { CrossoverFamily::Butterworth, 5, +kPi / 2.0 },
        { CrossoverFamily::Butterworth, 6, kPi },
        { CrossoverFamily::Butterworth, 7, -kPi / 2.0 },
        { CrossoverFamily::Butterworth, 8, 0.0 },
        { CrossoverFamily::LinkwitzRiley, 2, kPi },  // LR-N is defined for even N
        { CrossoverFamily::LinkwitzRiley, 4, 0.0 },
        { CrossoverFamily::LinkwitzRiley, 6, kPi },
        { CrossoverFamily::LinkwitzRiley, 8, 0.0 },
    };
    return rows;
}

double angleGap(double a, double b) noexcept { return std::abs(std::remainder(a - b, 2.0 * kPi)); }

/// Lines of a source file with the ones that are only a comment removed.
/// Record Sec.9's rule is that core must not KNOW what a crossover topology
/// is; a sentence in a doc comment saying which layer does is not knowledge,
/// it is a signpost. A word-grep cannot tell the two apart -- the plan's own
/// D6 spelling matches BandWeights.h's IEC conformance note and Decay.cpp's
/// include of ButterworthDesign.h, neither of which is a topology.
std::vector<std::string> codeLines(const std::filesystem::path& file) {
    std::vector<std::string> lines;
    std::ifstream stream(file);
    std::string line;
    while (std::getline(stream, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos) {
            // Written with substr rather than a char comparison against a NUL
            // escape: check_no_std_atomic_shared_ptr.cmake re-lexes these files
            // as CMake strings, where a backslash escape is a hard error. Same
            // trap commit 3d4eca1 fixed, one guard earlier.
            const std::string head = line.substr(first, 2);
            if (head.rfind("//", 0) == 0 || head.rfind("/*", 0) == 0
                || head.rfind("*", 0) == 0) {
                continue;
            }
        }
        std::transform(line.begin(), line.end(), line.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

TEST_CASE("the topology lookup reproduces record Sec.3's table, and question (c) flips it by pi",
          "[crossover_topology]") {
    // D5. Every row, every inversion answer. The table is transcribed above as
    // data; the implementation evaluates one closed form. If they agree at
    // twelve rows times three answers, the closed form IS the table.
    for (const auto& row : recordSection3()) {
        const Topology topology{ row.family, row.order };
        INFO("family " << (row.family == CrossoverFamily::Butterworth ? "BW" : "LR") << ", order "
                       << row.order);

        const auto notInverted = expectedOffset(topology, ProcessorInversion::No);
        CHECK_FALSE(notInverted.ambiguous);
        CHECK(angleGap(notInverted.radians, row.rawRadians) <= 1e-12);

        // The processor having already inverted one output adds exactly pi.
        // 180 degrees of wiring and 180 degrees of topology are the same thing
        // to a measurement, which is precisely why the wizard ASKS.
        const auto inverted = expectedOffset(topology, ProcessorInversion::Yes);
        CHECK_FALSE(inverted.ambiguous);
        CHECK(angleGap(inverted.radians, row.rawRadians + kPi) <= 1e-12);

        // "Unknown" is not a third value. It is TWO candidate lines, and the
        // operator picks (record Sec.13.3). Nothing here chooses for them.
        const auto unknown = expectedOffset(topology, ProcessorInversion::Unknown);
        CHECK(unknown.ambiguous);
        CHECK(angleGap(unknown.radians, row.rawRadians) <= 1e-12);
        CHECK(angleGap(unknown.alternativeRadians, row.rawRadians + kPi) <= 1e-12);
        CHECK(angleGap(unknown.radians, unknown.alternativeRadians) > 3.0);
    }
}

TEST_CASE("the odd-order rows carry a SIGN, so a transposed table goes red",
          "[crossover_topology]") {
    // D5's sign-distinguishing rows, called out separately because they are the
    // only ones a transposition can reach. 0 and 180 are symmetric under
    // swapping which side is the high-pass; +-90 is not, which is exactly why
    // the wizard asks which source is which (record Sec.3).
    const auto bw1 = expectedOffset({ CrossoverFamily::Butterworth, 1 }, ProcessorInversion::No);
    const auto bw3 = expectedOffset({ CrossoverFamily::Butterworth, 3 }, ProcessorInversion::No);
    CHECK_THAT(bw1.radians, WithinAbs(+kPi / 2.0, 1e-12));
    CHECK_THAT(bw3.radians, WithinAbs(-kPi / 2.0, 1e-12));
    CHECK(angleGap(bw1.radians, bw3.radians) > 3.14);  // they differ by pi

    // LR2 is the canonical-inversion row: the network ships with one output
    // inverted, so a processor that HAS inverted reads 0 and one that has not
    // reads 180.
    const auto lr2Yes = expectedOffset({ CrossoverFamily::LinkwitzRiley, 2 },
                                       ProcessorInversion::Yes);
    const auto lr2No = expectedOffset({ CrossoverFamily::LinkwitzRiley, 2 },
                                      ProcessorInversion::No);
    CHECK_THAT(lr2Yes.radians, WithinAbs(0.0, 1e-12));
    CHECK(angleGap(lr2No.radians, kPi) <= 1e-12);
}

TEST_CASE("core/ stays topology-free -- the table is an app lookup over an asked enum",
          "[crossover_topology]") {
    // D6, record Sec.9. A builder who puts `enum class CrossoverFamily` in
    // core/ has moved the ruling's boundary: core takes spans and returns
    // numbers, and "which crossover the designer built" is not a number any
    // measurement produces.
    //
    // The scan matches IDENTIFIERS in code, not words in prose -- the plan's
    // own word-grep spelling already hits BandWeights.h's IEC conformance note
    // and core/src/ir/Decay.cpp's include of ButterworthDesign.h on the
    // untouched tree, neither of which is a topology. `butterworth` is
    // deliberately NOT forbidden: ButterworthDesign is a filter DESIGNER, and
    // a polynomial is not a crossover topology.
    const std::filesystem::path root{ RTA_REPO_ROOT };
    const std::vector<std::string> forbidden{ "crossoverfamily", "linkwitz", "topolog" };

    std::vector<std::string> offenders;
    std::size_t scanned = 0;
    for (const auto& directory : { root / "core" / "include", root / "core" / "src" }) {
        REQUIRE(std::filesystem::exists(directory));
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            const auto extension = entry.path().extension().string();
            if (extension != ".h" && extension != ".cpp") continue;
            ++scanned;
            for (const auto& line : codeLines(entry.path())) {
                for (const auto& word : forbidden) {
                    if (line.find(word) != std::string::npos) {
                        offenders.push_back(entry.path().filename().string() + ": " + line);
                    }
                }
            }
        }
    }
    INFO("scanned " << scanned << " core files; offenders: " << offenders.size());
    for (const auto& offender : offenders) INFO(offender);
    CHECK(scanned > 50);  // the scan is watching something
    CHECK(offenders.empty());
}
