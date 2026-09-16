// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task E6 -- the structural half of Task E, split from
// test_relative_polarity.cpp when that file passed the 400-line cap. The
// numeric cases (E1-E5, E7-E9) stayed there; this one asserts that
// relativePolarity SHIPS NO VERDICT AND NO THRESHOLD, which record Sec.8
// forbids until two independent grids agree (task F).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

/// Declaration-shaped matches only. The word-grep the plan's own E6 prescribes
/// goes red against the header the plan itself specifies, because that header's
/// doc comment literally reads "NO VERDICT, NO THRESHOLD" (verifier defect 1).
/// Three things earn their place here and the controls below exercise each:
///   - comment lines are stripped, so prose ABOUT the prohibition is not the
///     prohibition being broken;
///   - the trailing character is one of ( ; = so a function-shaped declaration
///     is caught, not only a field;
///   - the search is case-insensitive over the whole identifier, so hasVerdict
///     and acceptMultiple both land.
std::size_t verdictShapedDeclarations(const std::filesystem::path& file,
                                      std::vector<std::string>& found) {
    static const std::vector<std::string> words{ "threshold", "verdict", "accept" };
    std::ifstream stream(file);
    std::string line;
    std::size_t lines = 0;
    while (std::getline(stream, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        const std::string head = line.substr(first, 2);
        if (head.rfind("//", 0) == 0 || head.rfind("/*", 0) == 0 || head.rfind("*", 0) == 0) {
            continue;
        }
        ++lines;
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const auto& word : words) {
            for (std::size_t at = lower.find(word); at != std::string::npos;
                 at = lower.find(word, at + 1)) {
                // Walk to the end of the identifier the word sits in, then see
                // whether a declaration character follows it.
                std::size_t end = at + word.size();
                while (end < lower.size()
                       && (std::isalnum(static_cast<unsigned char>(lower[end])) != 0
                           || lower[end] == '_')) {
                    ++end;
                }
                while (end < lower.size() && (lower[end] == ' ' || lower[end] == '\t')) ++end;
                if (end < lower.size()
                    && (lower[end] == '(' || lower[end] == ';' || lower[end] == '=')) {
                    found.push_back(line);
                }
            }
        }
    }
    return lines;
}

}  // namespace

TEST_CASE("relativePolarity ships NO verdict and NO threshold", "[relative_polarity]") {
    // E6, record Sec.8's last line: until two independent grids agree, rho is a
    // figure and not a gate. This is the case that keeps it that way -- a
    // session that adds `bool acceptable()` or a rhoThreshold constant to the
    // header lands here and reads why.
    const std::filesystem::path root{ RTA_REPO_ROOT };

    // POSITIVE CONTROL FIRST. A detector nobody has watched fire is a fixture
    // too well-behaved to fail
    // (memory/a-fixture-can-be-too-well-behaved-to-fail.md). DelayPolicy.h
    // carries both shapes already: a DelayVerdict field and an acceptMultiple.
    std::vector<std::string> control;
    const std::size_t controlLines = verdictShapedDeclarations(
        root / "core" / "include" / "rta" / "dsp" / "DelayPolicy.h", control);
    REQUIRE(controlLines > 20);
    for (const auto& hit : control) WARN("E6 positive control matched: " << hit);
    CHECK(control.size() >= 2);

    // NEGATIVE CONTROL. Polarity.h is full of prose about gates and thresholds
    // and declares neither, so a word-grep flags it and this must not.
    std::vector<std::string> polarity;
    verdictShapedDeclarations(root / "core" / "include" / "rta" / "ir" / "Polarity.h", polarity);
    CHECK(polarity.empty());

    // THE SUBJECT.
    std::vector<std::string> subject;
    const std::size_t subjectLines = verdictShapedDeclarations(
        root / "core" / "include" / "rta" / "ir" / "RelativePolarity.h", subject);
    REQUIRE(subjectLines > 10);  // the scan is reading a real file, not an empty one
    for (const auto& hit : subject) INFO("offender: " << hit);
    CHECK(subject.empty());
}
