# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for orphan_check.py v2's NOT IN TARGET and TEST HOOK reporting
(fix round 1 F8, fix round 2 F-A/F-I) -- against a scratch git repo and a
hand-written .map fixture, with `--skip-build`, split out of
test_orphan_check.py to stay under this repo's 400-line cap. CLI parsing,
basic exit codes and plain liveness reporting are tested in
test_orphan_check.py; the underlying `orphan_targets.py` functions have their
own direct tests where a full orphan_check.main() run is not needed.
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


def _write_fixture_map(build_dir: Path, config: str, decorated_names: list[str]) -> None:
    artefacts = build_dir / "app" / "rtatool_artefacts" / config
    artefacts.mkdir(parents=True)
    lines = [" rtatool\n\n  Address         Publics by Value              Rva+Base       Lib:Object\n\n"]
    addr = 0x1000
    for name in decorated_names:
        lines.append(f" 0001:{addr:08x}       {name} 000000014000{addr:04x} f   fixture.obj\n")
        addr += 0x10
    (artefacts / "RTA Tool.map").write_text("".join(lines), encoding="utf-8")


def _write_both_source_lists(repo: Path, *, rtatool: list[str], snapshot: list[str]) -> None:
    (repo / "app" / "cmake").mkdir(parents=True, exist_ok=True)
    (repo / "app" / "cmake" / "rtatool_sources.cmake").write_text(
        "set(RTATOOL_SOURCES\n" + "".join(f"    {p}\n" for p in rtatool) + ")\n", encoding="utf-8"
    )
    (repo / "app" / "cmake" / "rtatool_snapshot_sources.cmake").write_text(
        "set(RTATOOL_SNAPSHOT_SOURCES\n" + "".join(f"    {p}\n" for p in snapshot) + ")\n",
        encoding="utf-8",
    )


def test_excluded_directory_is_not_in_target_not_an_orphan(tmp_path, capsys):
    # fix round 1 (verifier), F8: a candidate under app/src/dev/preview/ (the
    # real SplPreview.cpp shape) is compiled only into rtatool_snapshot, so
    # the .map for rtatool never mentions it -- that must read as NOT IN
    # TARGET, never as "the linker discarded it".
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "dev" / "preview").mkdir(parents=True)
    (repo / "app" / "src" / "dev" / "preview" / "Existing.cpp").write_text(
        "int x = 0;\n", encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "dev" / "preview" / "SplPreview.h").write_text(
        "namespace rta {\nclass SplPreview {\npublic:\n    void render();\n};\n}\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add SplPreview::render")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 0
    assert "NOT IN TARGET" in out
    assert "render" in out
    assert "orphan_check: found component" not in out


def test_cpp_listed_only_in_snapshot_is_not_in_target(tmp_path, capsys):
    # A .cpp genuinely compiled into rtatool_snapshot (listed there) but
    # absent from rtatool's own list is a LEGITIMATE NOT IN TARGET, not an
    # orphan -- this is the SplPreview.cpp shape, generalised beyond the
    # dev/preview/ directory exclusion.
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _write_both_source_lists(
        repo, rtatool=["src/measure/Existing.cpp"], snapshot=["src/measure/Existing.cpp"]
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "measure" / "PreviewOnly.cpp").write_text(
        "namespace rta {\nvoid previewOnlyThing() {\n}\n}\n", encoding="utf-8"
    )
    _write_both_source_lists(
        repo,
        rtatool=["src/measure/Existing.cpp"],
        snapshot=["src/measure/Existing.cpp", "src/measure/PreviewOnly.cpp"],
    )
    _commit_all(repo, "add a .cpp compiled only into the snapshot target")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 0
    assert "NOT IN TARGET" in out
    assert "previewOnlyThing" in out


def test_cpp_absent_from_every_source_list_is_an_orphan(tmp_path, capsys):
    # fix round 2 (verifier), F-A -- MEDIUM, "NOT SOUND". The verifier's own
    # fixture shape: a new, entirely UNLISTED .cpp (absent from BOTH
    # rtatool_sources.cmake and rtatool_snapshot_sources.cmake) defining a
    # function nothing calls. Fix round 1's first cut silently reported this
    # as non-failing NOT IN TARGET and exited 0 -- exactly the L6a defect
    # this tool exists to catch (SplHistory.cpp/SplAlarms.cpp had no caller
    # and so were never added to a source list either). Must exit 1.
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _write_both_source_lists(
        repo, rtatool=["src/measure/Existing.cpp"], snapshot=["src/measure/Existing.cpp"]
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "measure" / "Old.cpp").write_text(
        "namespace rta {\nvoid neverCalled() {\n}\n}\n", encoding="utf-8"
    )
    _commit_all(repo, "add a .cpp nobody added to any source list")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 1
    assert "not compiled into any target" in out
    assert "neverCalled" in out
    assert "NOT IN TARGET" not in out


def test_missing_source_list_file_skips_the_not_listed_check(tmp_path, capsys):
    # A checkout that predates app/cmake/rtatool_sources.cmake's own split
    # (every commit before 9a2862a) has no such file to read at all --
    # rtatool_target_sources() must return None, not {}, or every single
    # .cpp file in the whole repo would misreport as NOT IN TARGET.
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "measure" / "Orphaned.cpp").write_text(
        "namespace rta {\nvoid neverWired() {\n}\n}\n", encoding="utf-8"
    )
    _commit_all(repo, "add a .cpp with no rtatool_sources.cmake in this repo at all")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert "NOT IN TARGET" not in out
    assert exit_code == 1
    assert "neverWired" in out


def test_referenced_test_hook_is_exempt_not_an_orphan(tmp_path, capsys):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "tests").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "measure" / "Thing.h").write_text(
        "namespace rta {\nclass Thing {\npublic:\n    void enableSplLoggingForTest();\n};\n}\n",
        encoding="utf-8",
    )
    (repo / "app" / "tests" / "ThingTest.cpp").write_text(
        "void run() {\n    rta::Thing t;\n    t.enableSplLoggingForTest();\n}\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add a test hook and its own test reference")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 0
    assert "TEST HOOK" in out
    assert "enableSplLoggingForTest" in out
    assert "orphan_check: found component" not in out


def test_unreferenced_test_hook_stays_an_orphan(tmp_path, capsys):
    # No free-text allow-list: a *ForTest name this run cannot find any real
    # reference to under app/tests* is left as an ordinary, failing orphan.
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "tests").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    (repo / "app" / "tests" / "Unrelated.cpp").write_text("void run() {}\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "measure" / "Thing.h").write_text(
        "namespace rta {\nclass Thing {\npublic:\n    void enableSplLoggingForTest();\n};\n}\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add a test hook nothing under app/tests references")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 1
    assert "TEST HOOK" not in out
    assert "enableSplLoggingForTest" in out


def test_test_hook_named_only_inside_a_string_literal_stays_an_orphan(tmp_path, capsys):
    # fix round 2 (verifier), F-I: a bare MENTION of the hook's name inside a
    # string literal (a log line, a display label) is not a real code
    # reference and must not exempt it -- only test_hook_is_referenced's own
    # comment-and-string-stripped search decides that, so a *ForTest name
    # that appears solely as text inside quotes stays a failing orphan.
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "tests").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    (repo / "app" / "tests" / "Unrelated.cpp").write_text(
        'void run() {\n    log("about to call enableSplLoggingForTest");\n}\n', encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "measure" / "Thing.h").write_text(
        "namespace rta {\nclass Thing {\npublic:\n    void enableSplLoggingForTest();\n};\n}\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add a test hook only NAMED inside a string literal under app/tests")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 1
    assert "TEST HOOK" not in out
    assert "enableSplLoggingForTest" in out


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
