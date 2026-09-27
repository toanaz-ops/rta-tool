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
#include <iterator>
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

/// Source text as ONE lowercased string: comments removed, string and character
/// literals EMPTIED, every run of whitespace collapsed to a single space.
///
/// Added 2026-09-16 after PR #9's round-3 verifier. A LINE-based declaration scan
/// is defeated by pressing Return: the same namespace-scope lambda split across
/// two lines evaded a scan that caught it on one. Joining the text first means
/// the scanner never sees a line at all, so where the author put the newlines
/// cannot matter.
///
/// ## Why the literal state is not a nicety (round-4, W1)
///
/// The first version had none, and carried the premise "neither file this is
/// used on contains a string literal" as though that made it safe. It did not,
/// and the cost was larger than the premise implied. A `//` inside a string is
/// overwhelmingly a URL:
///
///     inline constexpr const char* kRecordUrl = "https://…/docs/dsp";
///     inline double bestDelayForLoudestSum(const CrossoverSurface&, double);
///
/// A stripper with no literal state eats from that `//` to the end of the line
/// -- taking the closing `";` with it -- so the NEXT declaration, at any
/// distance, merges into the `kRecordUrl =` unit, where an initialiser rule
/// looking for `=` before `(` silences it. An exported objective then sits in
/// the header with the whole suite green. Nothing enforced the premise; the only
/// thing standing between the tree and that was that nobody had added a URL yet,
/// which is a fact about the tree and not a property of the scan.
///
/// A literal's CONTENTS are dropped rather than kept, so a `;` or a `(` inside
/// one cannot shift the unit boundaries either. The delimiters stay, so
/// `k = ""` still reads as the initialiser it is.
///
/// Remaining limitation, and this one is a real boundary rather than a wish: raw
/// string literals (`R"(...)"`) are not handled. Neither is a preprocessor
/// conditional -- both arms are read. A scan is a scan, not a preprocessor.
///
/// One more normalisation (PR #43 fix round 4, items 11-12: the path guard
/// in test_main_component_session_path_guard.cpp anchors on literal text
/// like `.string(` and `.open(`, which only protects the EXACT spelling a
/// pin was written against, not a new site spelled differently): a space
/// directly before `(` is removed (after the whitespace-run collapse above
/// already reduced any RUN of spaces to one) -- `.open (` or `.string ()`
/// -- valid C++, and this codebase does not enforce one spelling --
/// otherwise reads as a different token sequence than `.open(`/`.string()`
/// and evades a literal anchor built on the no-space spelling.
///
/// `->` is deliberately NOT collapsed to `.` here (fix round 4 item 10 asked
/// for it in this shared function; lane-end LOW batch, 2026-09-27, found why
/// not): this function is shared with app/tests/test_spl_drain.cpp's D1,
/// which counts `->peek(`/`->discard(` specifically as POINTER calls on a
/// `rta::dsp::RingBuffer*` to prove the drain never opens a second ring
/// read -- collapsing every `->` to `.` silently zeroed that count (0 == 3,
/// caught by this repo's own ctest run, not by inspection) instead of
/// catching the new hazard it was meant to. A caller that wants `->`/`.`
/// treated alike for MEMBER ACCESS specifically -- the path guard's own
/// reason -- normalises its OWN copy of this function's output; see that
/// file's own `arrowsAsDots` helper. One shared reader, callers that need a
/// different equivalence class build it themselves rather than changing
/// what every caller sees (the same reasoning this file's own header
/// comment gives for being shared at all).
[[nodiscard]] inline std::string codeTextOf(const std::string& raw) {
    std::string stripped;
    stripped.reserve(raw.size());
    bool inString = false;
    bool inChar = false;

    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char ch = raw[i];
        if (inString || inChar) {
            if (ch == '\\') {
                ++i;  // an escaped character cannot close the literal
                continue;
            }
            if ((inString && ch == '"') || (inChar && ch == '\'')) {
                inString = false;
                inChar = false;
                stripped.push_back(ch);
            }
            continue;  // contents dropped
        }
        if (ch == '/' && i + 1 < raw.size() && raw[i + 1] == '/') {
            while (i < raw.size() && raw[i] != '\n') ++i;
            stripped.push_back(' ');
            continue;
        }
        if (ch == '/' && i + 1 < raw.size() && raw[i + 1] == '*') {
            i += 2;
            while (i + 1 < raw.size() && !(raw[i] == '*' && raw[i + 1] == '/')) ++i;
            ++i;
            stripped.push_back(' ');
            continue;
        }
        if (ch == '"' || ch == '\'') {
            inString = ch == '"';
            inChar = ch == '\'';
            stripped.push_back(ch);
            continue;
        }
        stripped.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    std::string collapsed;
    collapsed.reserve(stripped.size());
    bool inSpace = false;
    for (const char ch : stripped) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            inSpace = true;
            continue;
        }
        if (inSpace && !collapsed.empty()) collapsed.push_back(' ');
        inSpace = false;
        collapsed.push_back(ch);
    }

    // A single space directly before `(` is not load-bearing C++ syntax --
    // drop it so `.open (` and `.string ()` read identically to `.open(`/
    // `.string()` for a literal-anchor scan.
    std::string final;
    final.reserve(collapsed.size());
    for (std::size_t i = 0; i < collapsed.size(); ++i) {
        if (collapsed[i] == ' ' && i + 1 < collapsed.size() && collapsed[i + 1] == '(') continue;
        final.push_back(collapsed[i]);
    }
    return final;
}

/// `codeTextOf` over a whole file. Split so the literal handling above can be
/// driven directly from a test with a synthetic snippet, rather than only
/// end-to-end through a mutation of a real source file.
[[nodiscard]] inline std::string codeText(const std::filesystem::path& file) {
    std::ifstream stream(file);
    REQUIRE(stream.good());
    return codeTextOf(std::string((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>()));
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
