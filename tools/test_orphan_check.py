# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/orphan_check.py -- new/modified-header scoping (M1),
comment-stripped reference search (M2), and transitive orphan propagation
(L1), isolated from the real repo tree via scratch git repos.

Run: python -m pytest tools/test_orphan_check.py -v
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import orphan_check as oc  # noqa: E402


def _init_repo(root: Path) -> None:
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.email", "test@example.com"], cwd=root, check=True)
    subprocess.run(["git", "config", "user.name", "Test"], cwd=root, check=True)


def _commit_all(root: Path, message: str) -> None:
    subprocess.run(["git", "add", "-A"], cwd=root, check=True)
    subprocess.run(["git", "commit", "-q", "-m", message], cwd=root, check=True)


def _head(root: Path) -> str:
    return subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=root, capture_output=True, text=True, check=True
    ).stdout.strip()


# --- baseline behaviour (added header, wired vs unwired, tests don't count) --


def test_find_orphans_flags_uncalled_new_header(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text(
        "void alreadyWired() {}\n", encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

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
    base_sha = _head(repo)

    (repo / "app" / "src" / "measure" / "WiredThing.h").write_text(
        "struct WiredThing {\n    int value() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "WiredThing.cpp").write_text(
        "int WiredThing::value() const { return 1; }\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "Caller.cpp").write_text(
        "#include \"WiredThing.h\"\nvoid useIt() { WiredThing t; t.value(); }\n", encoding="utf-8"
    )
    _commit_all(repo, "add WiredThing and wire it in")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    assert oc.find_orphans(base_sha) == {}


def test_find_orphans_ignores_test_only_reference(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "tests").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

    (repo / "app" / "src" / "measure" / "TestOnlyThing.h").write_text(
        "struct TestOnlyThing {\n    int value() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "TestOnlyThing.cpp").write_text(
        "int TestOnlyThing::value() const { return 1; }\n", encoding="utf-8"
    )
    (repo / "app" / "tests" / "test_thing.cpp").write_text(
        "#include \"measure/TestOnlyThing.h\"\nTEST_CASE(\"x\") { TestOnlyThing t; }\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add TestOnlyThing, exercised only by its own test")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha)
    assert "app/src/measure/TestOnlyThing.h" in orphans


def test_find_orphans_at_historical_commit_via_git_plumbing(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text(
        "void alreadyWired() {}\n", encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

    (repo / "app" / "src" / "measure" / "OrphanThing.h").write_text(
        "struct OrphanThing {\n    int value() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "OrphanThing.cpp").write_text(
        "int OrphanThing::value() const { return 1; }\n", encoding="utf-8"
    )
    _commit_all(repo, "add OrphanThing")
    at_sha = _head(repo)

    (repo / "app" / "src" / "measure" / "Caller.cpp").write_text(
        "#include \"OrphanThing.h\"\nvoid useIt() { OrphanThing t; t.value(); }\n",
        encoding="utf-8",
    )
    _commit_all(repo, "wire OrphanThing in, later")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha, at=at_sha)
    assert "app/src/measure/OrphanThing.h" in orphans
    assert oc.find_orphans(base_sha, at=None) == {}


# --- fix round 1, item M1: a new member on a PRE-EXISTING header -----------


def test_find_orphans_flags_new_method_on_pre_existing_header(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    header = repo / "app" / "src" / "measure" / "AnalysisThread.h"
    header.write_text(
        "class AnalysisThread {\npublic:\n    void run();\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "AnalysisThread.cpp").write_text(
        "void AnalysisThread::run() {}\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "Caller.cpp").write_text(
        "#include \"AnalysisThread.h\"\nvoid useIt() { AnalysisThread t; t.run(); }\n",
        encoding="utf-8",
    )
    _init_repo(repo)
    _commit_all(repo, "base: AnalysisThread already exists and is wired")
    base_sha = _head(repo)

    assert oc.find_orphans.__wrapped__ if False else True  # sanity no-op
    # header is UNCHANGED in its class shape except one new, uncalled method.
    header.write_text(
        "class AnalysisThread {\npublic:\n    void run();\n"
        "    void enableSplLogging(int config);\n"
        "};\n",
        encoding="utf-8",
    )
    (repo / "app" / "src" / "measure" / "AnalysisThreadSpl.cpp").write_text(
        "#include \"AnalysisThread.h\"\nvoid AnalysisThread::enableSplLogging(int config) {}\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add enableSplLogging, no caller anywhere")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha)
    assert "app/src/measure/AnalysisThread.h" in orphans
    assert "enableSplLogging" in orphans["app/src/measure/AnalysisThread.h"]
    # `run` is pre-existing and already wired; M1 must not re-flag it just
    # because the file changed.
    assert "run" not in orphans["app/src/measure/AnalysisThread.h"]


def test_find_orphans_does_not_flag_a_wired_new_method_on_existing_header(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    header = repo / "app" / "src" / "measure" / "AnalysisThread.h"
    header.write_text("class AnalysisThread {\npublic:\n    void run();\n};\n", encoding="utf-8")
    (repo / "app" / "src" / "measure" / "AnalysisThread.cpp").write_text(
        "void AnalysisThread::run() {}\n", encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

    header.write_text(
        "class AnalysisThread {\npublic:\n    void run();\n    void enableSplLogging(int c);\n};\n",
        encoding="utf-8",
    )
    (repo / "app" / "src" / "measure" / "AnalysisThreadSpl.cpp").write_text(
        "#include \"AnalysisThread.h\"\nvoid AnalysisThread::enableSplLogging(int c) {}\n",
        encoding="utf-8",
    )
    (repo / "app" / "src" / "MainComponent.cpp").write_text(
        "#include \"measure/AnalysisThread.h\"\n"
        "void poll(AnalysisThread& t) { t.enableSplLogging(1); }\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add enableSplLogging AND wire it in, same commit")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    assert oc.find_orphans(base_sha) == {}


# --- fix round 1, item M2: a doc comment naming a symbol is not a caller ----


def test_find_orphans_ignores_a_doc_comment_naming_the_symbol(tmp_path, monkeypatch):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

    (repo / "app" / "src" / "measure" / "Widget.h").write_text(
        "struct Widget {\n    int value() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "Widget.cpp").write_text(
        "int Widget::value() const { return 1; }\n", encoding="utf-8"
    )
    # The ONLY mention of `Widget` outside its own pair is inside a comment --
    # this must not be read as a production caller (the exact Snapshot.h:230
    # regression M2 fixes).
    (repo / "app" / "src" / "measure" / "Sibling.h").write_text(
        "// `Widget` is the one producer of this snapshot.\nstruct Sibling {};\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add Widget, mentioned only in a doc comment elsewhere")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha)
    assert "app/src/measure/Widget.h" in orphans
    assert "Widget" in orphans["app/src/measure/Widget.h"]


# --- fix round 1, item L1: self-definition exclusion + transitivity --------


def test_find_orphans_excludes_a_classs_own_definition_in_a_differently_named_file(
    tmp_path, monkeypatch
):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "export").mkdir(parents=True)
    (repo / "app" / "src" / "export" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

    # Declared in Log.h, defined in LogWriter.cpp -- different basenames, so
    # the ordinary same-basename pairing does NOT exclude LogWriter.cpp.
    (repo / "app" / "src" / "export" / "Log.h").write_text(
        "class LogWriter {\npublic:\n    LogWriter(int x);\n    void write(int v);\n};\n",
        encoding="utf-8",
    )
    (repo / "app" / "src" / "export" / "LogWriter.cpp").write_text(
        "#include \"Log.h\"\n"
        "LogWriter::LogWriter(int x) {}\n"
        "void LogWriter::write(int v) {}\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add LogWriter, declared/defined across differently-named files")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha)
    assert "app/src/export/Log.h" in orphans
    assert "LogWriter" in orphans["app/src/export/Log.h"]


def test_find_orphans_propagates_through_a_file_whose_own_symbols_are_all_orphaned(
    tmp_path, monkeypatch
):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

    # History is used only by Alarms (a real parameter type, genuine code) --
    # and Alarms ITSELF has no caller anywhere. History must become orphan
    # too: a reference from a file that is not wired in is not a real
    # production caller.
    (repo / "app" / "src" / "measure" / "History.h").write_text(
        "struct History {\n    int size() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "History.cpp").write_text(
        "int History::size() const { return 0; }\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "Alarms.h").write_text(
        "#include \"History.h\"\n"
        "class Alarms {\npublic:\n    void update(History& h);\n};\n",
        encoding="utf-8",
    )
    (repo / "app" / "src" / "measure" / "Alarms.cpp").write_text(
        "#include \"Alarms.h\"\nvoid Alarms::update(History& h) {}\n", encoding="utf-8"
    )
    _commit_all(repo, "add History and Alarms; Alarms uses History but nothing calls Alarms")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    orphans = oc.find_orphans(base_sha)
    assert "app/src/measure/Alarms.h" in orphans and "Alarms" in orphans["app/src/measure/Alarms.h"]
    assert "app/src/measure/History.h" in orphans
    assert "History" in orphans["app/src/measure/History.h"]


def test_find_orphans_does_not_propagate_through_a_genuinely_wired_file(tmp_path, monkeypatch):
    # Same shape as above, but Alarms (the user of History) IS wired in from
    # a real caller -- transitivity must not still discard that reference.
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

    (repo / "app" / "src" / "measure" / "History.h").write_text(
        "struct History {\n    int size() const;\n};\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "History.cpp").write_text(
        "int History::size() const { return 0; }\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "measure" / "Alarms.h").write_text(
        "#include \"History.h\"\n"
        "class Alarms {\npublic:\n    void update(History& h);\n};\n",
        encoding="utf-8",
    )
    (repo / "app" / "src" / "measure" / "Alarms.cpp").write_text(
        "#include \"Alarms.h\"\nvoid Alarms::update(History& h) {}\n", encoding="utf-8"
    )
    (repo / "app" / "src" / "MainComponent.cpp").write_text(
        "#include \"measure/Alarms.h\"\n"
        "void poll(Alarms& a, History& h) { a.update(h); h.size(); }\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add History and Alarms; Alarms IS wired from MainComponent")

    monkeypatch.setattr(oc, "REPO_ROOT", repo)
    assert oc.find_orphans(base_sha) == {}


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
