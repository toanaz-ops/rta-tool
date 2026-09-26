# SPDX-License-Identifier: AGPL-3.0-or-later
"""Self-test for tools/mutation_sidecar.py -- crash recovery for a
hard-killed diffmut.py run (fix round 1, item M3)."""

from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mutation_sidecar  # noqa: E402


def test_sidecar_path_appends_suffix(tmp_path):
    target = tmp_path / "Foo.cpp"
    assert mutation_sidecar.sidecar_path(target) == tmp_path / "Foo.cpp.diffmut-orig"


def test_find_stale_sidecars_empty_when_none(tmp_path):
    (tmp_path / "Foo.cpp").write_text("int x;\n", encoding="utf-8")
    assert mutation_sidecar.find_stale_sidecars(tmp_path) == []


def test_find_stale_sidecars_finds_a_left_over_one(tmp_path):
    target = tmp_path / "Foo.cpp"
    target.write_text("int x;\n", encoding="utf-8")
    sidecar = mutation_sidecar.sidecar_path(target)
    sidecar.write_bytes(b"int x;\nint y;\n")  # the pre-mutation original
    found = mutation_sidecar.find_stale_sidecars(tmp_path)
    assert found == [sidecar]


def test_restore_from_sidecars_writes_back_and_removes_sidecar(tmp_path):
    # Simulates exactly what M3 exists for: a hard kill left `target`
    # mutated (missing "int y;") with its sidecar still holding the ORIGINAL
    # two-line content.
    target = tmp_path / "Foo.cpp"
    original = b"int x;\nint y;\n"
    mutated = b"int x;\n"
    target.write_bytes(mutated)
    sidecar = mutation_sidecar.sidecar_path(target)
    sidecar.write_bytes(original)

    restored = mutation_sidecar.restore_from_sidecars(tmp_path)

    assert restored == ["Foo.cpp"]
    assert target.read_bytes() == original
    assert not sidecar.exists()


def test_restore_from_sidecars_handles_nested_paths(tmp_path):
    nested = tmp_path / "app" / "src" / "measure"
    nested.mkdir(parents=True)
    target = nested / "SplAlarms.cpp"
    original = b"// original\n"
    target.write_bytes(b"// mutated (line deleted)\n")
    mutation_sidecar.sidecar_path(target).write_bytes(original)

    restored = mutation_sidecar.restore_from_sidecars(tmp_path)

    assert restored == ["app/src/measure/SplAlarms.cpp"]
    assert target.read_bytes() == original


def test_restore_from_sidecars_is_a_noop_when_nothing_stale(tmp_path):
    (tmp_path / "Foo.cpp").write_text("int x;\n", encoding="utf-8")
    assert mutation_sidecar.restore_from_sidecars(tmp_path) == []


if __name__ == "__main__":
    import pytest

    sys.exit(pytest.main([__file__, "-v"]))
