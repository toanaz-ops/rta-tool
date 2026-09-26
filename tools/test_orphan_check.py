# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/orphan_check.py v2's own ORCHESTRATION (CLI parsing,
map-file discovery, exit codes, report formatting) -- against a scratch git
repo and a hand-written .map fixture, with `--skip-build` so no CMake/MSVC
build runs. The candidate-extraction and map-parsing MECHANISMS this
orchestrates are tested on their own in test_orphan_candidates.py,
test_map_symbols.py, test_msvc_decorate.py and test_cpp_scopes.py; the real,
full build-and-check acceptance runs (against ad4046b, 8c5d407 and current
main) are documented with their pasted output in the PR body, since they need
a real MSVC toolchain this test suite cannot assume.
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


def _make_repo_with_one_new_method(tmp_path: Path, *, wired: bool) -> tuple[Path, str]:
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    # `useIt` already exists at the base commit (an EMPTY body) -- only the
    # CALL inside it is added in the diff, so `useIt` itself is never a new
    # candidate; the one candidate under test is `enableSplLogging`.
    (repo / "app" / "src" / "measure" / "Thing.h").write_text(
        "namespace rta {\nnamespace measure {\nclass Thing {\n};\n}\n}\n"
        "void useIt(rta::measure::Thing& t);\n",
        encoding="utf-8",
    )
    (repo / "app" / "src" / "measure" / "UseIt.cpp").write_text(
        "#include \"Thing.h\"\nvoid useIt(rta::measure::Thing& t) {\n}\n", encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)

    (repo / "app" / "src" / "measure" / "Thing.h").write_text(
        "namespace rta {\nnamespace measure {\nclass Thing {\npublic:\n"
        "    void enableSplLogging(int x);\n};\n}\n}\n"
        "void useIt(rta::measure::Thing& t);\n",
        encoding="utf-8",
    )
    (repo / "app" / "src" / "measure" / "Thing.cpp").write_text(
        "namespace rta {\nnamespace measure {\n"
        "void Thing::enableSplLogging(int x) {}\n"
        "}\n}\n",
        encoding="utf-8",
    )
    call_body = "    t.enableSplLogging(1);\n" if wired else ""
    (repo / "app" / "src" / "measure" / "UseIt.cpp").write_text(
        "#include \"Thing.h\"\nvoid useIt(rta::measure::Thing& t) {\n" + call_body + "}\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add Thing::enableSplLogging")
    return repo, base_sha


def _write_fixture_map(build_dir: Path, config: str, decorated_names: list[str]) -> None:
    artefacts = build_dir / "app" / "rtatool_artefacts" / config
    artefacts.mkdir(parents=True)
    lines = [" rtatool\n\n  Address         Publics by Value              Rva+Base       Lib:Object\n\n"]
    addr = 0x1000
    for name in decorated_names:
        lines.append(f" 0001:{addr:08x}       {name} 000000014000{addr:04x} f   fixture.obj\n")
        addr += 0x10
    (artefacts / "RTA Tool.map").write_text("".join(lines), encoding="utf-8")


def test_reports_ok_and_exits_zero_when_the_candidate_is_live(tmp_path, capsys):
    repo, base_sha = _make_repo_with_one_new_method(tmp_path, wired=True)
    build_dir = tmp_path / "build"
    _write_fixture_map(
        build_dir, "Release", ["?enableSplLogging@Thing@measure@rta@@QEAAXH@Z"]
    )
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    assert exit_code == 0
    assert "OK" in capsys.readouterr().out


def test_reports_orphan_and_exits_one_when_the_candidate_is_absent(tmp_path, capsys):
    repo, base_sha = _make_repo_with_one_new_method(tmp_path, wired=False)
    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?someOtherLiveThing@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 1
    assert "enableSplLogging" in out
    assert "Thing.cpp" in out or "Thing.h" in out


def test_uncheckable_entries_never_cause_a_nonzero_exit_by_themselves(tmp_path, capsys):
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "measure" / "Thing.h").write_text(
        "namespace rta {\nclass Thing {\npublic:\n    Thing() = default;\n};\n}\n",
        encoding="utf-8",
    )
    _commit_all(repo, "add a defaulted member only")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 0
    assert "UNCHECKABLE" in out
    assert "= default" in out


def test_exit_code_two_when_no_map_file_is_found(tmp_path, capsys):
    repo = tmp_path / "repo"
    repo.mkdir()
    _init_repo(repo)
    (repo / "f.txt").write_text("x\n", encoding="utf-8")
    _commit_all(repo, "base")
    base_sha = _head(repo)

    build_dir = tmp_path / "build-empty"
    build_dir.mkdir()
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    assert exit_code == 2
    assert "no .map file found" in capsys.readouterr().err


def test_exit_code_two_on_a_bad_base_revision(tmp_path, capsys):
    repo = tmp_path / "repo"
    repo.mkdir()
    _init_repo(repo)
    (repo / "f.txt").write_text("x\n", encoding="utf-8")
    _commit_all(repo, "base")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?anything@@YAXXZ"])
    exit_code = oc.main(
        [
            "--base", "not-a-real-revision-at-all",
            "--build-dir", str(build_dir),
            "--source-dir", str(repo),
            "--skip-build",
        ]
    )
    assert exit_code == 2
    assert "git diff failed" in capsys.readouterr().err


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


def test_cpp_absent_from_source_list_is_not_in_target(tmp_path, capsys):
    # A .cpp this branch adds but never wires into
    # app/cmake/rtatool_sources.cmake's own list is not part of the rtatool
    # target at all -- reported as NOT IN TARGET, not as an orphan, even
    # though (correctly) absent from the .map.
    repo = tmp_path / "repo"
    (repo / "app" / "src" / "measure").mkdir(parents=True)
    (repo / "app" / "cmake").mkdir(parents=True)
    (repo / "app" / "src" / "measure" / "Existing.cpp").write_text("int x = 0;\n", encoding="utf-8")
    (repo / "app" / "cmake" / "rtatool_sources.cmake").write_text(
        "set(RTATOOL_SOURCES\n    src/measure/Existing.cpp\n)\n", encoding="utf-8"
    )
    _init_repo(repo)
    _commit_all(repo, "base")
    base_sha = _head(repo)
    (repo / "app" / "src" / "measure" / "Orphaned.cpp").write_text(
        "namespace rta {\nvoid neverWired() {\n}\n}\n", encoding="utf-8"
    )
    _commit_all(repo, "add a .cpp nobody added to the source list")

    build_dir = tmp_path / "build"
    _write_fixture_map(build_dir, "Release", ["?unrelated@@YAXXZ"])
    exit_code = oc.main(
        ["--base", base_sha, "--build-dir", str(build_dir), "--source-dir", str(repo), "--skip-build"]
    )
    out = capsys.readouterr().out
    assert exit_code == 0
    assert "NOT IN TARGET" in out
    assert "neverWired" in out


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


def test_help_runs_nothing(capsys):
    try:
        oc.main(["--help"])
    except SystemExit as exc:
        assert exc.code == 0
    else:
        raise AssertionError("--help must exit via argparse (SystemExit)")


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
