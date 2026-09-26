# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/orphan_check.py -- the symbol extractor and the
orphan-detection logic, isolated from git and from the real repo tree.

Run: python -m pytest tools/test_orphan_check.py -v
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import orphan_check as oc  # noqa: E402


# --- extract_symbols ----------------------------------------------------


def test_extracts_struct_name():
    assert oc.extract_symbols("struct SplHistory {\n    int x;\n};\n") == {"SplHistory"}


def test_extracts_class_name():
    assert oc.extract_symbols("class SplAlarms {\npublic:\n};\n") == {"SplAlarms"}


def test_extracts_free_function_declaration():
    text = "std::optional<double> slidingMaxLeqDb(std::span<const Block> b, double fs);\n"
    assert "slidingMaxLeqDb" in oc.extract_symbols(text)


def test_extracts_out_of_line_member_definition_qualified():
    text = "SplAlarmReading SplAlarm::report() const {\n    return out;\n}\n"
    assert "SplAlarm::report" in oc.extract_symbols(text)


def test_ignores_control_flow_lines():
    text = "if (windowFull) {\nfor (int i = 0; i < 3; ++i) {\nwhile (x) {\n"
    assert oc.extract_symbols(text) == set()


def test_ignores_comment_lines():
    text = "// struct Fake {\n// int foo();\n"
    assert oc.extract_symbols(text) == set()


def test_ignores_return_statement_calling_a_qualified_function():
    # Regression: `return std::put_time(&tm, fmt);` inside an inline header
    # function used to be misread as a DECLARATION of `std::put_time` -- a
    # standard-library call, not a project symbol -- because its tail matches
    # the same "name(args);" shape a real declaration has.
    text = (
        "inline std::string formatUtcTimestamp(const std::tm& tm) {\n"
        "    std::ostringstream oss;\n"
        "    return std::put_time(&tm, \"%Y%m%dT%H%M%SZ\");\n"
        "}\n"
    )
    symbols = oc.extract_symbols(text)
    assert "std::put_time" not in symbols
    assert "formatUtcTimestamp" in symbols


def test_multiple_symbols_in_one_header():
    text = (
        "struct SplHistory {\n"
        "    int size() const;\n"
        "};\n"
        "\n"
        "void writeSplLog(const SplHistory& h);\n"
    )
    symbols = oc.extract_symbols(text)
    assert "SplHistory" in symbols
    assert "writeSplLog" in symbols


# --- find_orphans, on a scratch git repo ------------------------------------


def _init_repo(root: Path) -> None:
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.name", "Test"], cwd=root, check=True)


def _commit_all(root: Path, message: str) -> None:
    subprocess.run(["git", "add", "-A"], cwd=root, check=True)
    subprocess.run(["git", "commit", "-q", "-m", message], cwd=root, check=True)


def test_find_orphans_flags_uncalled_new_header(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text(
        "void alreadyWired() {}\n", encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True
    ).stdout.strip()

    # A new header/impl pair that nothing else in app/src ever names.
    (repo / "app" / "src" / "measure" / "OrphanThing.h").write_text(
        "struct OrphanThing {\n    int value() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "OrphanThing.cpp").write_text(
        "int OrphanThing::value() const { return 1; }\n", encoding="utf-8"
    )
    _commit_all(repo, "add OrphanThing")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha)
    assert "app/src/measure/OrphanThing.h" in orphans
    assert "OrphanThing" in orphans["app/src/measure/OrphanThing.h"]


def test_find_orphans_clears_a_wired_header(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True
    ).stdout.strip()

    (repo / "app" / "src" / "measure" / "WiredThing.h").write_text(
        "struct WiredThing {\n    int value() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "WiredThing.cpp").write_text(
        "int WiredThing::value() const { return 1; }\n", encoding="utf-8"
    )
    # The actual production caller, in a DIFFERENT file -- this is what
    # should clear the orphan flag.
    (repo / "app" / "src" / "measure" / "Caller.cpp").write_text(
        "#include \"WiredThing.h\"\nvoid useIt() { WiredThing t; t.value(); }\n", encoding="utf-8"
    )
    _commit_all(repo, "add WiredThing and wire it in")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha)
    assert orphans == {}


def test_find_orphans_ignores_test_only_reference(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "tests").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True
    ).stdout.strip()

    (repo / "app" / "src" / "measure" / "TestOnlyThing.h").write_text(
        "struct TestOnlyThing {\n    int value() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "TestOnlyThing.cpp").write_text(
        "int TestOnlyThing::value() const { return 1; }\n", encoding="utf-8"
    )
    # A reference exists, but ONLY under app/tests -- must not count.
    (repo / "app" / "tests" / "test_thing.cpp").write_text(
        "#include \"measure/TestOnlyThing.h\"\nTEST_CASE(\"x\") { TestOnlyThing t; }\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add TestOnlyThing, exercised only by its own test")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha)
    assert "app/src/measure/TestOnlyThing.h" in orphans


def test_find_orphans_at_historical_commit_via_git_plumbing(tmp_path, monkeypatch):
    # Same scenario as test_find_orphans_flags_uncalled_new_header, but read
    # through --at's git-plumbing path (git show/ls-tree) instead of the
    # working tree, proving the historical-commit mode gives the same answer
    # without a checkout.
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text(
        "void alreadyWired() {}\n", encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True
    ).stdout.strip()

    (repo / "app" / "src" / "measure" / "OrphanThing.h").write_text(
        "struct OrphanThing {\n    int value() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "OrphanThing.cpp").write_text(
        "int OrphanThing::value() const { return 1; }\n", encoding="utf-8"
    )
    _commit_all(repo, "add OrphanThing")
    at_sha = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, capture_output=True, text=True, check=True
    ).stdout.strip()

    # A later commit that would clear the orphan -- but --at pins evaluation
    # to at_sha, so this must NOT be seen.
    (repo / "app" / "src" / "measure" / "Caller.cpp").write_text(
        "#include \"OrphanThing.h\"\nvoid useIt() { OrphanThing t; t.value(); }\n",
        encoding="utf-8",
    )
    _commit_all(repo, "wire OrphanThing in, later")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha, at=at_sha)
    assert "app/src/measure/OrphanThing.h" in orphans

    # And evaluating at HEAD (the wired commit) clears it, confirming the
    # difference is --at, not some other discrepancy.
    orphans_at_head = oc.find_orphans(base_sha, at=None)
    assert orphans_at_head == {}


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
