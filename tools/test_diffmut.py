# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/diffmut.py -- the run-mutate-restore driver and CLI,
runnable with no CMake build (the line classifier, diff parser, and target
mapping have their own test files: test_mutation_lines.py, test_difflines.py,
test_mutation_targets.py, test_mutation_sidecar.py).

Run: python -m pytest tools/test_diffmut.py -v
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import diffmut  # noqa: E402
import mutation_sidecar  # noqa: E402


# --- restore-byte-identity, on a temp file, no build involved ---------------


def test_run_mutant_restores_original_bytes_exactly(tmp_path):
    target_file = tmp_path / "Sample.cpp"
    original = (
        b"// SPDX-License-Identifier: AGPL-3.0-or-later\n"
        b"int add(int a, int b) {\n"
        b"    int result = a + b;\n"
        b"    return result;\n"
        b"}\n"
    )
    target_file.write_bytes(original)
    original_sha = hashlib.sha256(original).hexdigest()

    # No real build system for this fake file: force a build target that
    # cannot resolve to a real cmake target, and expect run_mutant to still
    # restore the file before propagating that outcome (a missing exe is
    # reported as NO-BUILD, not an exception, and never skips the restore).
    build_dir = tmp_path / "build-does-not-exist"
    build_dir.mkdir()
    log_path = tmp_path / "mutant.log"

    result = diffmut.run_mutant(
        target_file,
        lineno=3,  # "int result = a + b;"
        targets=["nonexistent_target"],
        build_dir=build_dir,
        config="Release",
        log_path=log_path,
    )

    # The mutant attempted a build against a target that will never link
    # (cmake itself will fail fast in this fake tree), which is NO-BUILD --
    # but the file on disk must be back to its ORIGINAL bytes regardless.
    assert result.verdict == "NO-BUILD"
    restored = target_file.read_bytes()
    assert restored == original
    assert hashlib.sha256(restored).hexdigest() == original_sha


# --- fix round 2, item F8: the mutant header must precede its own build log --


def test_run_mutant_writes_header_before_build_output(tmp_path):
    target_file = tmp_path / "Sample.cpp"
    target_file.write_bytes(b"int x;\n")
    build_dir = tmp_path / "build-does-not-exist"
    build_dir.mkdir()
    log_path = tmp_path / "mutant.log"

    diffmut.run_mutant(
        target_file,
        lineno=1,
        targets=["nonexistent_target"],
        build_dir=build_dir,
        config="Release",
        log_path=log_path,
    )

    content = log_path.read_text(encoding="utf-8", errors="replace")
    header_index = content.find("=== mutant")
    assert header_index != -1
    # The header must be the FIRST thing in this fresh log file. Before the
    # F8 fix, `log.write(header)` only reached Python's own text buffer;
    # `subprocess.run(..., stdout=log)` writes directly to the underlying
    # fd at its CURRENT OS-level position (which the buffered header had not
    # advanced yet), so the build's own output landed ahead of the header
    # that is supposed to introduce it, and the header only appeared once
    # the `with` block's close() finally flushed it -- after the build text.
    assert content[:header_index].strip() == ""


def test_run_mutant_restores_bytes_even_when_line_out_of_range(tmp_path):
    target_file = tmp_path / "Sample.h"
    original = b"#pragma once\nint x = 1;\n"
    target_file.write_bytes(original)

    build_dir = tmp_path / "build"
    build_dir.mkdir()
    result = diffmut.run_mutant(
        target_file,
        lineno=999,
        targets=["whatever"],
        build_dir=build_dir,
        config="Release",
        log_path=tmp_path / "mutant.log",
    )
    assert result.verdict == "UNRESOLVED"
    assert target_file.read_bytes() == original


# --- fix round 1, item M3: sidecar lifecycle --------------------------------


def test_run_mutant_removes_sidecar_on_ordinary_completion(tmp_path):
    target_file = tmp_path / "Sample.cpp"
    original = b"int add(int a, int b) {\n    return a + b;\n}\n"
    target_file.write_bytes(original)
    build_dir = tmp_path / "build"
    build_dir.mkdir()

    diffmut.run_mutant(
        target_file,
        lineno=2,
        targets=["nonexistent_target"],
        build_dir=build_dir,
        config="Release",
        log_path=tmp_path / "mutant.log",
    )

    # A normal (non-killed) run must leave no sidecar behind -- it is only
    # ever meant to survive a hard kill this process itself cannot catch.
    assert not mutation_sidecar.sidecar_path(target_file).exists()
    assert target_file.read_bytes() == original


def test_main_refuses_to_start_when_a_stale_sidecar_exists(tmp_path, monkeypatch, capsys):
    target = tmp_path / "Sample.cpp"
    original = b"int x;\nint y;\n"
    target.write_bytes(b"int x;\n")  # left mutated by a simulated hard kill
    mutation_sidecar.sidecar_path(target).write_bytes(original)

    monkeypatch.setattr(diffmut, "REPO_ROOT", tmp_path)
    exit_code = diffmut.main(["--dry-run"])

    assert exit_code == 2
    captured = capsys.readouterr()
    assert "refusing to start" in captured.err
    assert "--restore" in captured.err
    # Refusing to start must not itself touch the mutated file or sidecar.
    assert target.read_bytes() == b"int x;\n"


def test_main_restore_flag_fixes_a_stale_sidecar(tmp_path, monkeypatch, capsys):
    target = tmp_path / "Sample.cpp"
    original = b"int x;\nint y;\n"
    target.write_bytes(b"int x;\n")
    mutation_sidecar.sidecar_path(target).write_bytes(original)

    monkeypatch.setattr(diffmut, "REPO_ROOT", tmp_path)
    exit_code = diffmut.main(["--restore"])

    assert exit_code == 0
    assert target.read_bytes() == original
    assert not mutation_sidecar.sidecar_path(target).exists()

    # And a subsequent ordinary run no longer refuses.
    monkeypatch.setattr(
        diffmut, "git_diff_added_lines", lambda *a, **k: []  # nothing to mutate; just prove no refusal
    )
    exit_code2 = diffmut.main(["--dry-run"])
    assert exit_code2 == 0
    captured = capsys.readouterr()
    assert "refusing to start" not in captured.err


def test_main_restore_flag_is_a_noop_when_nothing_stale(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(diffmut, "REPO_ROOT", tmp_path)
    exit_code = diffmut.main(["--restore"])
    assert exit_code == 0
    captured = capsys.readouterr()
    assert "no stale sidecar" in captured.out


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
