// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of RTA Tool -- app/tests_juce. PR #43 fix round 2, MEDIUM R2-2: the
// round-1 guard (test_main_component_session_fixround.cpp's "toFsPath's
// implementation never round-trips through toStdString()") only read
// JuceFsPath.h itself. The round-2 verifier proved that is not enough by
// reverting MainComponentSession.cpp:103,129 back to
// `std::filesystem::path(folder.getFullPathName().toStdString())` (mutant
// "P1") and getting the WHOLE suite green -- nothing scanned the CALLER.
//
// SCOPE (memory: a-prescribed-mutation-is-not-proof-the-check-catches-it --
// a check must state exactly what it covers, not just what mutation it was
// written against). This guard reads every `.h`/`.cpp` directly under
// `app/src` and directly under `app/src/export` -- the two places this
// UTF-8/ACP bug class has actually been found (HIGH F1 round 1, HIGH R2-1
// round 2). It does NOT walk `app/src/trace`, `app/src/measure`,
// `app/src/view`, or any other subdirectory. `app/src/trace/SessionStore.cpp`
// in particular constructs `std::ofstream`/`std::ifstream` from bare local
// names (`path`, `tmpPath`) that ARE already `std::filesystem::path` (built
// from `root_`, itself a `std::filesystem::path` member) -- correct, but not
// something this text-only scanner can prove by reading that one call site
// in isolation, so that file is out of scope rather than a source of false
// positives this guard would otherwise have to special-case.
//
// ALLOW-LIST, not a blacklist of today's known-bad phrasing: every
// `std::filesystem::path`/`fs::path`/`std::ofstream`/`std::ifstream` DIRECT
// INITIALISATION this scan finds in scope (`Type name(arg, ...)` or the
// anonymous `Type(arg, ...)`) must have a first argument that is one of:
//   - a call through `toFsPath(` or `utf8Path(`, this project's two
//     ACP-safe constructors (JuceFsPath.h, SplLog.h);
//   - `toWideCharPointer(` or `std::u8string(`, the two TRUSTED PRIMITIVES
//     those constructors are themselves built from -- inherently ACP-safe
//     regardless of caller, by the standard's own contract;
//   - a string literal (still visible as a bare `"` after codeText() empties
//     literal CONTENTS -- the delimiters survive);
//   - a `std::filesystem::path` composed via `operator/` (contains `/`);
//   - a bare name this SAME FILE separately declares as a
//     `const fs::path&`/`const std::filesystem::path&` reference parameter
//     (SplReportPayloadBuilder.cpp's `readWholeFile(const fs::path& path)`
//     opening `std::ifstream in(path, ...)` is the one real case -- hand
//     reviewed, not inferred). This exception is scoped to the FILE, not the
//     function: a hazard only if some future bare name collided with an
//     UNSAFE variable of the same spelling elsewhere in the same file; none
//     currently do.
// A construction whose argument is a function's own parameter LIST (not a
// call at all -- e.g. `traceBlobPath(const std::filesystem::path& root, ...)`'s
// own declaration) is recognised by containing `&` (every reference
// parameter in this codebase's style) and is not a call this guard judges.
// Known limitation: an argument containing a literal `&` for some other
// reason (bitwise AND) would be skipped too -- none of the in-scope files do
// this.
//
// A bare, un-wrapped raw variable in scope -- exactly what the round-1 and
// round-2 bugs both were -- matches none of the allowed shapes and fails the
// build.
//
// WIDENED SCOPE (fix round 3, HIGH R3-1): the bug has a SECOND direction.
// Round 1/2 were all `narrow std::string -> std::filesystem::path`, which
// silently corrupts on Windows. `std::filesystem::path::string()`/
// `generic_string()` is the OPPOSITE conversion, native/wide back down to
// the ACP, and MSVC makes THAT one throw `std::system_error` on a character
// the code page cannot represent -- SplReportPayloadBuilder.cpp:85,100 hit
// exactly this once R2-1-C's fix let a Vietnamese `dir` resolve far enough
// to reach them. Two more checks, same ALLOW-LIST philosophy:
//   - no scanned file may contain `.string()` or `.generic_string()` at all.
//     Every legitimate need for a path's bytes goes through `utf8String()`
//     (SplLog.h) instead, which is `path::u8string()` -- never-throwing,
//     never ACP-dependent -- so requiring its ABSENCE is not a narrower
//     blacklist-of-today's-phrasing than the rest of this guard: there is no
//     allowed spelling of `.string()`/`.generic_string()` in scope at all,
//     the same way there is no allowed spelling of a bare narrow-string
//     `std::ofstream` construction.
//   - a `.open(` call (a stream member, not a free constructor -- the
//     `SplLogWriter.cpp:43` shape) must have a first argument matching the
//     SAME allow list as the constructions above (`toFsPath(`/`utf8Path(`
//     et al.), found the same way pathConstructionArgs() finds a
//     constructor's argument.
#include <catch2/catch_test_macros.hpp>

#include "CodeLines.h"

#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

using rta::test::codeText;

/// Given `text[openParen] == '('`, returns the FIRST top-level argument
/// (up to the first depth-0 comma, or the whole span if there is none) --
/// shared by `pathConstructionArgs` and `dotOpenArgs` below so the
/// paren/comma-balancing logic exists exactly once.
std::optional<std::string> firstArgumentAt(const std::string& text, std::size_t openParen) {
    std::size_t depth = 0;
    std::size_t j = openParen;
    for (; j < text.size(); ++j) {
        if (text[j] == '(') {
            ++depth;
        } else if (text[j] == ')') {
            --depth;
            if (depth == 0) break;
        }
    }
    if (j >= text.size()) return std::nullopt;  // unbalanced -- give up on this occurrence

    std::size_t commaDepth = 0;
    std::size_t commaPos = j;  // default: no top-level comma, the whole span is one argument
    for (std::size_t k = openParen + 1; k < j; ++k) {
        if (text[k] == '(') ++commaDepth;
        else if (text[k] == ')') --commaDepth;
        else if (text[k] == ',' && commaDepth == 0) { commaPos = k; break; }
    }
    return text.substr(openParen + 1, commaPos - (openParen + 1));
}

/// Every first-argument text of a `typeToken name(arg, ...)` or
/// `typeToken(arg, ...)` direct-initialisation found in `text` (already
/// lowercased and whitespace-collapsed by `codeText`, so `typeToken` must be
/// passed lowercase too).
std::vector<std::string> pathConstructionArgs(const std::string& text, const std::string& typeToken) {
    std::vector<std::string> args;
    std::size_t pos = 0;
    while (true) {
        const auto at = text.find(typeToken, pos);
        if (at == std::string::npos) break;
        std::size_t i = at + typeToken.size();
        while (i < text.size() && text[i] == ' ') ++i;
        while (i < text.size() &&
              (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
            ++i;
        }
        while (i < text.size() && text[i] == ' ') ++i;
        if (i >= text.size() || text[i] != '(') {
            pos = at + typeToken.size();
            continue;
        }
        const auto arg = firstArgumentAt(text, i);
        if (!arg.has_value()) { pos = i + 1; continue; }
        args.push_back(*arg);
        pos = i + arg->size() + 2;  // past the '(' and this argument's own text
    }
    return args;
}

/// Fix round 3 R3-1: every first-argument text of a `.open(arg, ...)` member
/// call -- the `SplLogWriter.cpp:43` `stream_.open(...)` shape, never a free
/// constructor, so `pathConstructionArgs`'s type-token anchor does not apply
/// (there is no type name immediately before `.open(`).
std::vector<std::string> dotOpenArgs(const std::string& text) {
    std::vector<std::string> args;
    std::size_t pos = 0;
    const std::string anchor = ".open(";
    while (true) {
        const auto at = text.find(anchor, pos);
        if (at == std::string::npos) break;
        const std::size_t openParen = at + anchor.size() - 1;
        const auto arg = firstArgumentAt(text, openParen);
        if (!arg.has_value()) { pos = openParen + 1; continue; }
        args.push_back(*arg);
        pos = openParen + arg->size() + 2;
    }
    return args;
}

/// A parameter LIST, not a call -- see this file's own header comment.
bool looksLikeParameterList(const std::string& arg) {
    return arg.find('&') != std::string::npos;
}

/// `text` with every `->` rewritten to `.` -- fix round 4 items 10-12: a
/// pointer's `->string()`/`->open(` is the exact same ACP hazard as a
/// value's `.string()`/`.open(`, and an anchor spelled only with `.` would
/// silently pass a new call site written through a pointer. This is a LOCAL
/// copy on top of `rta::test::codeText`'s shared output, not a change to
/// that shared function itself: `codeTextOf`'s own header comment explains
/// why -- `->`/`.` need to read as DIFFERENT tokens for
/// app/tests/test_spl_drain.cpp's D1, which counts them separately as
/// pointer-vs-value ring-buffer calls, so this equivalence is this file's
/// own to build, not something every caller of the shared reader should
/// have imposed on it.
std::string arrowsAsDots(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '-' && i + 1 < text.size() && text[i + 1] == '>') {
            result.push_back('.');
            ++i;
            continue;
        }
        result.push_back(text[i]);
    }
    return result;
}

bool isAllowListed(const std::string& arg, const std::string& wholeFileText) {
    if (arg.find("tofspath(") != std::string::npos) return true;
    if (arg.find("utf8path(") != std::string::npos) return true;
    if (arg.find("towidecharpointer(") != std::string::npos) return true;
    if (arg.find("u8string(") != std::string::npos) return true;
    if (arg.find('"') != std::string::npos) return true;  // a string literal survives as a bare quote
    if (arg.find('/') != std::string::npos) return true;  // path composed via operator/
    if (wholeFileText.find("fs::path& " + arg) != std::string::npos) return true;
    if (wholeFileText.find("filesystem::path& " + arg) != std::string::npos) return true;
    return false;
}

/// Runs the whole scan over one file, failing at the first disallowed
/// construction with the file, the type token, and the offending argument
/// all named in the failure message.
void checkFile(const std::filesystem::path& file) {
    const std::string text = codeText(file);
    for (const char* typeToken : {"std::filesystem::path", "fs::path", "std::ofstream", "std::ifstream"}) {
        for (const auto& arg : pathConstructionArgs(text, typeToken)) {
            if (looksLikeParameterList(arg)) continue;
            INFO("file: " << file.string());
            INFO("type: " << typeToken);
            INFO("arg: " << arg);
            CHECK(isAllowListed(arg, text));
        }
    }

    // R3-1: the throwing direction. No allowed spelling of `.string()`/
    // `.generic_string()` exists in scope at all -- utf8String() (SplLog.h)
    // is the only way to get a path's bytes as a std::string here. Scanned
    // through `arrowText` (fix round 4 items 10-12), so `ptr->string()` is
    // caught the same as `value.string()` -- see `arrowsAsDots`'s own
    // comment for why this is a LOCAL normalisation, not one `codeText`
    // itself applies.
    const std::string arrowText = arrowsAsDots(text);
    INFO("file: " << file.string());
    CHECK(arrowText.find(".string()") == std::string::npos);
    CHECK(arrowText.find(".generic_string()") == std::string::npos);

    // R3-1: a `.open(`/`->open(` member call must open through the same
    // allow list a free constructor does.
    for (const auto& arg : dotOpenArgs(arrowText)) {
        if (looksLikeParameterList(arg)) continue;
        INFO("file: " << file.string());
        INFO("type: .open(");
        INFO("arg: " << arg);
        CHECK(isAllowListed(arg, text));
    }
}

}  // namespace

TEST_CASE("Every path/stream construction in app/src and app/src/export is ACP-safe",
         "[main_component_session][path_guard]") {
    const auto srcDir = std::filesystem::path(__FILE__).parent_path() / ".." / "src";
    for (const auto& entry : std::filesystem::directory_iterator(srcDir)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext == ".h" || ext == ".hpp" || ext == ".cpp") checkFile(entry.path());
    }
    for (const auto& entry : std::filesystem::directory_iterator(srcDir / "export")) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext == ".h" || ext == ".hpp" || ext == ".cpp") checkFile(entry.path());
    }
}

TEST_CASE("The path/stream construction guard is not vacuous", "[main_component_session][path_guard]") {
    // Confirms the scan actually FINDS constructions and applies the
    // allow-list, rather than trivially passing because pathConstructionArgs()
    // found nothing at all in the files that matter most.
    const auto srcDir = std::filesystem::path(__FILE__).parent_path() / ".." / "src";

    const std::string juceFsPathText = codeText(srcDir / "JuceFsPath.h");
    const auto juceFsPathArgs = pathConstructionArgs(juceFsPathText, "std::filesystem::path");
    bool foundToWideCharPointer = false;
    for (const auto& arg : juceFsPathArgs) {
        if (arg.find("towidecharpointer(") != std::string::npos) foundToWideCharPointer = true;
    }
    CHECK(foundToWideCharPointer);

    const std::string splCalText = codeText(srcDir / "export" / "SplCalibrationRecord.h");
    const auto splCalArgs = pathConstructionArgs(splCalText, "std::ofstream");
    bool foundUtf8PathInCalRecord = false;
    for (const auto& arg : splCalArgs) {
        if (arg.find("utf8path(") != std::string::npos) foundUtf8PathInCalRecord = true;
    }
    CHECK(foundUtf8PathInCalRecord);

    const std::string splText = codeText(srcDir / "MainComponentSpl.cpp");
    const auto splArgs = pathConstructionArgs(splText, "std::ofstream");
    bool foundUtf8PathInSpl = false;
    for (const auto& arg : splArgs) {
        if (arg.find("utf8path(") != std::string::npos) foundUtf8PathInSpl = true;
    }
    CHECK(foundUtf8PathInSpl);

    const std::string payloadText = codeText(srcDir / "export" / "SplReportPayloadBuilder.cpp");
    const auto payloadFsPathArgs = pathConstructionArgs(payloadText, "fs::path");
    bool foundUtf8PathInPayload = false;
    for (const auto& arg : payloadFsPathArgs) {
        if (arg.find("utf8path(") != std::string::npos) foundUtf8PathInPayload = true;
    }
    CHECK(foundUtf8PathInPayload);
    // readWholeFile's `std::ifstream in(path, ...)` is the hand-reviewed
    // bare-name exception itself -- a DIFFERENT type token (std::ifstream,
    // not fs::path) from the construction checked just above.
    const auto payloadIfstreamArgs = pathConstructionArgs(payloadText, "std::ifstream");
    bool foundReadWholeFileException = false;
    for (const auto& arg : payloadIfstreamArgs) {
        if (arg == "path" && !looksLikeParameterList(arg)) foundReadWholeFileException = true;
    }
    CHECK(foundReadWholeFileException);
    // And the hand-reviewed bare-name exception is real, not a typo: this
    // file really does declare `path` as a `const fs::path&` parameter
    // somewhere, which is what makes that exception correct rather than a
    // hole.
    CHECK(payloadText.find("fs::path& path") != std::string::npos);

    // R3-1: utf8String() is actually used (not just declared) at both fixed
    // call sites, so the ".string()"-absence check above is not passing
    // because this file never touched a path's bytes at all.
    CHECK(payloadText.find("utf8string(") != std::string::npos);

    // R3-1: the `.open(` scan finds SplLogWriter.cpp's real call site and
    // allow-lists it -- confirms dotOpenArgs() is not an empty scan.
    const std::string writerText = codeText(srcDir / "export" / "SplLogWriter.cpp");
    const auto writerOpenArgs = dotOpenArgs(writerText);
    bool foundWriterUtf8PathOpen = false;
    for (const auto& arg : writerOpenArgs) {
        if (arg.find("utf8path(") != std::string::npos) foundWriterUtf8PathOpen = true;
    }
    CHECK(foundWriterUtf8PathOpen);
}
