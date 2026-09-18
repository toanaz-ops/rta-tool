// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- core test support. No JUCE, no Qt, no audio-device API.
#pragma once

// Reading this repository's own source from a test, for the NAMING checks that
// L6a's negative claims rest on (test_alarm.cpp C4b, test_dose.cpp D3a/D3b).
//
// THE ONE THING THESE EXIST TO GET RIGHT. A naming check has to look at CODE,
// not at prose. Both of this lane's first attempts scanned the raw file and
// both went red against this project's own headers -- Alarm.h has to say
// "hysteresis" to record why there is none, and Dose.h has to say "OSHA" to
// cite 29 CFR 1910.95 App. A I(2). A check that forbids a decision from being
// documented is a check that trains the next author to delete the
// documentation, so `stripLineComments` runs first and the tests assert the
// word is absent from the code AND present in the prose.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

namespace rta::testing {

/// A repository file's whole text, by path relative to the repo root.
/// RTA_REPO_ROOT is defined for rta_core_tests in core/tests/CMakeLists.txt,
/// which is what lets these checks run in CI on three operating systems
/// instead of living in a grep somebody has to remember to type.
[[nodiscard]] inline std::string readRepoFile(const std::string& relative) {
    std::ifstream in(std::string(RTA_REPO_ROOT) + "/" + relative, std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Drops `//` comments, which is the whole of what these files use. Block
/// comments are deliberately NOT handled: no file this scans contains one, and
/// a half-working stripper would be worse than an absent one.
[[nodiscard]] inline std::string stripLineComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
        } else {
            out.push_back(text[i]);
            ++i;
        }
    }
    return out;
}

[[nodiscard]] inline std::string lowered(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

/// A file's code with comments and case removed, ready for a naming check.
[[nodiscard]] inline std::string repoFileCode(const std::string& relative) {
    return lowered(stripLineComments(readRepoFile(relative)));
}

}  // namespace rta::testing
