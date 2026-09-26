# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/orphan_candidates.py -- core candidate detection
(single-line, multi-line, constructors, qualified out-of-line definitions,
local-variable and access-specifier exclusions) for orphan_check.py v2.

UNCHECKABLE classification (deleted/defaulted members, constexpr, anonymous
namespaces, templates, operators, friend, pure virtual, pure-data aggregates)
is tested separately in test_orphan_shapes.py -- split the same way the
production code is, to stay under this repo's 400-line cap.

Verified separately (PR body) against the REAL multi-line `enableSplLogging`
declaration in app/src/measure/AnalysisThread.h and the real
`AnalysisThread(...)` / `AnalysisThread(const AnalysisThread&) = delete;`
pair; these tests use small synthetic files so they need no repo checkout.
"""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from difflines import AddedLine  # noqa: E402
from orphan_candidates import find_candidates  # noqa: E402


def _reader(files: dict[str, str]):
    return lambda path: files[path]


def test_single_line_header_declaration():
    text = "namespace rta {\nclass Foo {\npublic:\n    void bar();\n};\n}\n"
    added = [AddedLine("f.h", 4, "    void bar();")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert uncheckable == []
    assert len(candidates) == 1
    assert candidates[0].name == "bar"
    assert candidates[0].enclosing == ["Foo", "rta"]
    assert candidates[0].is_constructor is False


def test_multiline_declaration_is_flattened_and_found():
    text = (
        "namespace rta {\n"
        "namespace measure {\n"
        "class AnalysisThread {\n"
        "public:\n"
        "    void enableSplLogging(const SplConfig& config, std::span<const int> channels,\n"
        "                          std::string logDirectory = {},\n"
        "                          std::optional<double> calibratorLevelDb = std::nullopt);\n"
        "};\n"
        "}\n"
        "}\n"
    )
    added = [
        AddedLine(
            "AnalysisThread.h",
            5,
            "    void enableSplLogging(const SplConfig& config, std::span<const int> channels,",
        )
    ]
    candidates, uncheckable = find_candidates(added, _reader({"AnalysisThread.h": text}))
    assert uncheckable == []
    assert len(candidates) == 1
    assert candidates[0].name == "enableSplLogging"
    assert candidates[0].enclosing == ["AnalysisThread", "measure", "rta"]


def test_constructor_is_detected_by_matching_the_enclosing_class():
    text = "class Foo {\npublic:\n    Foo(int x, int y);\n};\n"
    added = [AddedLine("f.h", 3, "    Foo(int x, int y);")]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert len(candidates) == 1
    assert candidates[0].name == "Foo"
    assert candidates[0].is_constructor is True


def test_out_of_line_cpp_definition_resolves_the_qualifier_into_enclosing():
    # The real bug this guards: MainComponent::calibrationStartClicked() is
    # DEFINED in a .cpp that never reopens `class MainComponent` -- the class
    # name is baked into the qualified NAME on the definition line, not into
    # any brace `cpp_scopes` can see. Treating the qualified name as an
    # opaque token (the first version of this module did) produced a
    # decorated-name fragment containing a literal "::", which can never
    # match a real MSVC decorated name -- every out-of-line .cpp definition
    # in the whole app was reported as an orphan the first time this ran
    # against the real repo.
    text = "void MainComponent::calibrationStartClicked() {\n    doThing();\n}\n"
    added = [AddedLine("f.cpp", 1, "void MainComponent::calibrationStartClicked() {")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert uncheckable == []
    assert len(candidates) == 1
    assert candidates[0].name == "calibrationStartClicked"
    assert candidates[0].enclosing == ["MainComponent"]
    assert candidates[0].is_constructor is False


def test_out_of_line_constructor_is_detected_as_a_constructor():
    text = "MainComponent::MainComponent(int x) : x_(x) {\n}\n"
    added = [AddedLine("f.cpp", 1, "MainComponent::MainComponent(int x) : x_(x) {")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert len(candidates) == 1
    assert candidates[0].name == "MainComponent"
    assert candidates[0].enclosing == ["MainComponent"]
    assert candidates[0].is_constructor is True


def test_out_of_line_definition_inside_a_wrapping_namespace_combines_both():
    # A qualifier baked into the name (the class) combines with an ACTUAL
    # namespace scope the .cpp itself reopens around the definition -- the
    # class must end up innermost, ahead of the wrapping namespace.
    text = (
        "namespace rta {\n"
        "namespace measure {\n"
        "void AnalysisThread::enableSplLogging(int x) {\n"
        "}\n"
        "}\n"
        "}\n"
    )
    added = [AddedLine("f.cpp", 3, "void AnalysisThread::enableSplLogging(int x) {")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert len(candidates) == 1
    assert candidates[0].name == "enableSplLogging"
    assert candidates[0].enclosing == ["AnalysisThread", "measure", "rta"]


def test_out_of_line_destructor_is_not_a_candidate():
    text = "MainComponent::~MainComponent() {\n}\n"
    added = [AddedLine("f.cpp", 1, "MainComponent::~MainComponent() {")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert candidates == []
    assert uncheckable == []


def test_local_variable_with_direct_initialisation_syntax_is_not_a_candidate():
    # The real second bug this guards: a local variable declared with
    # direct-initialisation syntax (`std::ofstream out(path, ...);`) is
    # byte-for-byte indistinguishable from a function declaration by name+
    # paren shape alone -- MainComponentSpl.cpp's real
    # `std::ofstream out(path, std::ios::out | ...);` was reported as an
    # orphaned function named `out` the first time this ran against the real
    # repo. The fix: a real declaration's own scope is always its innermost
    # NAMED enclosure (a class or namespace) with no unnamed block in
    # between; a local variable is one function body deeper.
    text = (
        "namespace rta {\n"
        "void writeLog(const std::string& path) {\n"
        "    std::ofstream out(path, std::ios::out | std::ios::trunc);\n"
        "}\n"
        "}\n"
    )
    added = [AddedLine("f.cpp", 3, "    std::ofstream out(path, std::ios::out | std::ios::trunc);")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert candidates == []
    assert uncheckable == []


def test_local_variable_directly_inside_a_lambda_body_is_not_a_candidate():
    text = (
        "namespace rta {\n"
        "void run() {\n"
        "    auto f = [](const std::string& path) {\n"
        "        std::span<const int> channels(buf, n);\n"
        "    };\n"
        "}\n"
        "}\n"
    )
    added = [AddedLine("f.cpp", 4, "        std::span<const int> channels(buf, n);")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert candidates == []


def test_added_access_specifier_does_not_fuse_into_the_next_declaration():
    # The real third bug this guards: an added `public:` line has no
    # terminator of its own, so flattening walked straight through it (and
    # through the blank lines a stripped doc comment leaves behind) into the
    # NEXT real declaration, producing a spurious extra candidate shaped
    # wrong (SplAlarm's own real constructor read as a non-constructor
    # "typed declaration" because "public:" plus the leftover "explicit"
    # keyword got absorbed into the bogus "return type" prefix) the first
    # time this ran against the real repo.
    text = (
        "namespace rta {\n"
        "namespace measure {\n"
        "class SplAlarm {\n"
        "public:\n"
        "    // a doc comment that used to sit here\n"
        "    explicit SplAlarm(int spec);\n"
        "};\n"
        "}\n"
        "}\n"
    )
    added = [
        AddedLine("f.h", 4, "public:"),
        AddedLine("f.h", 6, "    explicit SplAlarm(int spec);"),
    ]
    candidates, uncheckable = find_candidates(added, _reader({"f.h": text}))
    assert len(candidates) == 1
    assert candidates[0].name == "SplAlarm"
    assert candidates[0].is_constructor is True
    assert candidates[0].enclosing == ["SplAlarm", "measure", "rta"]


def test_member_access_call_is_not_a_candidate():
    text = "void useIt(Foo& f) {\n    f.bar(1, 2);\n}\n"
    added = [AddedLine("f.cpp", 2, "    f.bar(1, 2);")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert candidates == []
    assert uncheckable == []


def test_bare_call_to_an_existing_function_is_not_a_candidate():
    # No return-type prefix and the name does not match any enclosing class
    # -- this is an ordinary call statement, not a declaration.
    text = "void useIt() {\n    doSomething(1, 2);\n}\n"
    added = [AddedLine("f.cpp", 2, "    doSomething(1, 2);")]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert candidates == []


def test_control_flow_line_is_never_a_candidate():
    text = "void useIt(int x) {\n    if (x > 0) {\n        return;\n    }\n}\n"
    added = [
        AddedLine("f.cpp", 2, "    if (x > 0) {"),
        AddedLine("f.cpp", 3, "        return;"),
    ]
    candidates, uncheckable = find_candidates(added, _reader({"f.cpp": text}))
    assert candidates == []
    assert uncheckable == []


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
