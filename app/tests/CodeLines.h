// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The one reader the structural scans in this target share.
//
// Three L7-ALIGN tests scan source files for DECLARATIONS -- the mechanism the
// plan's D6 amendment established after a plain word-grep was measured matching
// prose on the untouched tree. Each had its own copy of this function until
// PR #9's verifier pointed out that a duplicated reader is a second thing to
// keep correct: a fix to the comment-stripping in one copy silently leaves the
// other scanning a different language.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace rta::test {

/// A file's source lines, lowercased, with the COMMENT-ONLY ones removed.
///
/// Record Sec.9's rules are about what the code KNOWS. A sentence in a doc
/// comment saying which layer owns a table, or arguing against a forbidden
/// move, is not knowledge -- it is a signpost, and a word-grep cannot tell the
/// two apart. This strips whole-line comments and leaves everything else,
/// including trailing comments, because a scan that also dropped those would
/// stop seeing the code they sit beside.
[[nodiscard]] inline std::vector<std::string> codeLines(const std::filesystem::path& file) {
    std::vector<std::string> lines;
    std::ifstream stream(file);
    REQUIRE(stream.good());
    std::string line;
    while (std::getline(stream, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos) {
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

/// True when `line` ASSIGNS to `member` -- `x_ = v`, `x_= v`, `x_ =v` alike --
/// and false when it merely READS it, `x_ == v`.
///
/// The spacing tolerance is PR #9's verifier note: the first version searched
/// for the literal `member + " ="`, so `inversion_= x;` with no space would
/// have walked straight past a scan whose whole job is to catch that
/// assignment. A structural check that a formatter can defeat is not a check.
[[nodiscard]] inline bool assignsTo(const std::string& line, const std::string& member) {
    std::size_t from = 0;
    while (true) {
        const auto at = line.find(member, from);
        if (at == std::string::npos) return false;
        from = at + 1;

        // The name must BE the token, not the tail of a longer one.
        if (at > 0
            && (std::isalnum(static_cast<unsigned char>(line[at - 1])) != 0
                || line[at - 1] == '_')) {
            continue;
        }
        std::size_t i = at + member.size();
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || line[i] != '=') continue;
        if (i + 1 < line.size() && line[i + 1] == '=') continue;  // a comparison
        return true;
    }
}

}  // namespace rta::test
