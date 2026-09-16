// SPDX-License-Identifier: AGPL-3.0-or-later
//
// L7-ALIGN task I, rows I4 and I5 (record docs/dsp/2026-09-06-l7-alignment-
// wizard.md Sec.6): NO OBJECTIVE EXISTS, no field could say "topology
// inferred", and predicted-versus-measured is a number rather than a
// re-derivation.
//
// Split out of test_crossover_surface.cpp on 2026-09-16: after three rounds of
// verification the declaration scan grew into its own subject, and the two
// files together were past the 400-line cap. "The surface SHOWS the sum" is the
// one claim this lane exists to keep, so it gets its own file.

#include "AlignmentWizardFixture.h"
#include "view/CrossoverSurface.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using rta::measure::CrossoverFamily;
using rta::measure::ProcessorInversion;
using rta::test::codeLines;
using rta::test::codeText;
using rta::view::CrossoverSurface;

namespace {

/// The identifier ending at `at`, skipping any spaces before it. Empty when
/// what precedes is not an identifier at all.
[[nodiscard]] std::string identifierEndingAt(const std::string& text, std::size_t at) {
    std::size_t end = at;
    while (end > 0 && text[end - 1] == ' ') --end;
    const std::size_t last = end;
    while (end > 0
           && (std::isalnum(static_cast<unsigned char>(text[end - 1])) != 0
               || text[end - 1] == '_')) {
        --end;
    }
    return text.substr(end, last - end);
}

/// The callable a single declaration UNIT introduces, or "".
///
/// Two forms, and the scan claims exactly these two:
///
///   1. `... name(`      -- a member, free, static or inline FUNCTION, where the
///                          identifier immediately precedes the parameter list.
///                          A `=` before that `(` means the unit is an
///                          initialiser instead: `const double k = 20.0 *
///                          std::log10(2.0)` must not read as declaring
///                          `log10`.
///   2. `... name = [`   -- a callable bound to an OBJECT: a lambda, `= +[`, or
///      `std::function`    any `std::function<...> name =`. Checked FIRST,
///                          because form 1's initialiser rule is exactly this
///                          shape and would otherwise silence it.
[[nodiscard]] std::string callableIn(const std::string& unit) {
    for (std::size_t i = 0; i + 1 < unit.size(); ++i) {
        if (unit[i] != '=') continue;
        if (unit[i + 1] == '=') continue;                                   // ==
        if (i > 0 && (unit[i - 1] == '!' || unit[i - 1] == '<' || unit[i - 1] == '>'
                      || unit[i - 1] == '=')) {
            continue;                                                        // != <= >= ==
        }
        std::size_t j = i + 1;
        while (j < unit.size() && unit[j] == ' ') ++j;
        if (j < unit.size() && unit[j] == '+') ++j;  // `= +[]{...}`
        const bool bindsLambda = j < unit.size() && unit[j] == '[';
        if (!bindsLambda && unit.find("std::function") == std::string::npos) continue;
        const std::string name = identifierEndingAt(unit, i);
        if (!name.empty()) return name;
    }

    const auto open = unit.find('(');
    if (open == std::string::npos) return {};
    if (unit.substr(0, open).find('=') != std::string::npos) return {};
    return identifierEndingAt(unit, open);
}

/// Every callable the file declares, read from the file as ONE string.
///
/// NOT line-based, and that is the whole point (PR #9 round-3 verifier, V1).
/// Every line-based version of this scan was defeated by pressing Return: the
/// same namespace-scope lambda, wrapped across two lines, walked through a scan
/// that caught it on one. `codeText` strips comments and collapses whitespace,
/// so the scanner never sees a line and where the author put the newlines
/// cannot matter.
///
/// Units are cut at `;` and `{`. When a unit that DID declare a callable is
/// followed by `{`, that brace opens a function BODY and everything to its
/// match is skipped -- which is what keeps the call sites inside
/// `recomputePrediction()` from being read as declarations. A brace after a
/// unit that declared nothing opens a namespace, a struct or a class, and the
/// scan carries on inside it.
[[nodiscard]] std::vector<std::string> declaredCallables(const std::string& text) {
    std::vector<std::string> names;
    std::string unit;
    int depth = 0;
    int skippingBodyAtDepth = -1;

    for (const char ch : text) {
        if (skippingBodyAtDepth >= 0) {
            if (ch == '{') ++depth;
            if (ch == '}') {
                --depth;
                if (depth == skippingBodyAtDepth) skippingBodyAtDepth = -1;
            }
            continue;
        }
        if (ch != ';' && ch != '{' && ch != '}') {
            unit.push_back(ch);
            continue;
        }

        const std::string name = callableIn(unit);
        if (!name.empty()) names.push_back(name);
        if (ch == '{') {
            if (!name.empty()) skippingBodyAtDepth = depth;
            ++depth;
        } else if (ch == '}') {
            --depth;
        }
        unit.clear();
    }
    return names;
}

}  // namespace

TEST_CASE("I4/I5: no objective exists, no field could say 'topology inferred', and the gap is "
          "a number",
          "[crossover_surface]") {
    const std::filesystem::path root{ RTA_REPO_ROOT };
    const std::filesystem::path header = root / "app" / "src" / "view" / "CrossoverSurface.h";
    const std::filesystem::path source = root / "app" / "src" / "view" / "CrossoverSurface.cpp";
    REQUIRE(std::filesystem::exists(header));
    REQUIRE(std::filesystem::exists(source));

    // I4. The plan specifies a word-grep here and prescribes
    // `bestDelayForLoudestSum()` as the mutation that must break it. Measured:
    // it does not -- the identifier carries none of the listed words, and the
    // only lines that do are this header's own argument for WHY the move is
    // forbidden. A word list cannot separate naming an objective from arguing
    // against one, so the check is the other way round: every callable the two
    // files declare is ENUMERATED and the set must match exactly.
    //
    // THE SCAN'S SCOPE, stated here and nowhere wider. It reads the two forms
    // documented at `callableIn`, over the whole of CrossoverSurface.{h,cpp},
    // independent of line breaks. OUT of scope, by design: a macro, a callable
    // reached through a typedef'd function pointer, and anything in another
    // translation unit. What this case claims is exactly that: no NEW callable
    // can be declared in these two files, in either form, without joining the
    // list below -- and an objective has to be one of those to be reachable
    // from an includer.
    //
    // Three rounds of verification widened this: first from the class body to
    // the whole file, then from functions to lambda objects, then from lines to
    // the joined text. All three are in
    // memory/a-prescribed-mutation-is-not-proof-the-check-catches-it.md.
    const std::vector<std::string> allowed{
        // CrossoverSurface members
        "setsources", "setaskedtopology", "setwindow", "setpendingops", "setmeasuredsum",
        "highsidedb", "lowsidedb", "predictedsumdb", "ghostsumdb", "measuredsumdb",
        "relativephase", "targetradians", "targetambiguous", "alternativetargetradians",
        "marks", "gap", "pointcount", "binwidthhz", "recomputeprediction", "recomputegap",
        // file-local helper in CrossoverSurface.cpp's anonymous namespace
        "designedsumdbfor",
    };

    std::vector<std::string> declared = declaredCallables(codeText(header));
    for (const auto& name : declaredCallables(codeText(source))) {
        // A member DEFINITION in the .cpp repeats a name the header declared;
        // only a name new to the set is a new callable.
        if (std::find(declared.begin(), declared.end(), name) == declared.end()) {
            declared.push_back(name);
        }
    }

    std::string found;
    for (const auto& name : declared) found += " " + name;
    INFO("callables declared across CrossoverSurface.{h,cpp}:" << found);
    REQUIRE(declared.size() == allowed.size());
    for (const auto& name : declared) {
        INFO("CrossoverSurface declares " << name);
        CHECK(std::find(allowed.begin(), allowed.end(), name) != allowed.end());
    }

    // I5: and nothing in either file could hold a topology this model worked
    // out for itself. Here the WORDS are the failure mode, so a word scan is
    // the right instrument.
    const std::vector<std::string> forbidden{ "inferred", "detected", "guessed", "derivedtopo" };
    for (const auto& file : { header, source }) {
        for (const auto& line : codeLines(file)) {
            for (const auto& word : forbidden) {
                INFO(file.filename().string() << ": " << line);
                CHECK(line.find(word) == std::string::npos);
            }
        }
    }

    // I5: predicted versus measured is a per-bin dB difference and a summary.
    CrossoverSurface surface;
    surface.setAskedTopology({ CrossoverFamily::LinkwitzRiley, 4 }, ProcessorInversion::No);
    surface.setWindow({ 12000.0, 1.0 });
    auto high = rta::trace::VirtualTrace::fromTrace(
        rta::test::makeCapture("hp", rta::test::risingDb(), rta::test::constantPhase(0.0)));
    auto low = rta::trace::VirtualTrace::fromTrace(
        rta::test::makeCapture("lp", rta::test::fallingDb(), rta::test::constantPhase(0.0)));
    REQUIRE(high.has_value());
    REQUIRE(low.has_value());
    surface.setSources(std::move(*high), std::move(*low));

    CHECK_FALSE(surface.gap().has_value());  // absent, not a zeroed placeholder

    std::vector<float> measured = surface.predictedSumDb();
    for (auto& value : measured) value += 2.0f;
    surface.setMeasuredSum(measured);
    REQUIRE(surface.gap().has_value());
    CHECK(surface.gap()->bins == rta::test::pointCount());
    CHECK_THAT(surface.gap()->rmsDb, WithinAbs(2.0, 1e-5));
    CHECK_THAT(surface.gap()->maxAbsDb, WithinAbs(2.0, 1e-5));
    for (float value : surface.gap()->perBinDb) CHECK_THAT(value, WithinAbs(2.0, 1e-4));
}
