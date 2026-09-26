#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""mutation_sidecar.py -- crash recovery for a killed diffmut.py run.

Fix round 1, item M3. diffmut.py restores a mutated file's original bytes in
a `finally` block, but a HARD kill of the process (TerminateProcess, a
Bash-tool timeout, `taskkill /F`) during the build or test-run subprocess it
is waiting on skips `finally` entirely, leaving the deleted line on disk with
no in-process recovery path.

The fix is a sidecar file (`<file>.diffmut-orig`) written NEXT TO the source
file, containing its exact original bytes, BEFORE the mutation is applied.
On an ordinary exit (success or any Python-catchable failure) the real
`finally` restores the file and this module's caller deletes the sidecar. On
a hard kill, the sidecar is the only thing that survives: `restore_from_
sidecars` writes it back over the real file, and diffmut.py's own `main()`
refuses to start a new run while any sidecar exists (a killed run must be
restored before its result -- or anything after it -- can be trusted).
"""

from __future__ import annotations

from pathlib import Path

SIDECAR_SUFFIX = ".diffmut-orig"


def sidecar_path(repo_path: Path) -> Path:
    return repo_path.with_name(repo_path.name + SIDECAR_SUFFIX)


def find_stale_sidecars(search_root: Path) -> list[Path]:
    """Every `*.diffmut-orig` sidecar under `search_root` -- non-empty means a
    previous run was hard-killed mid-mutation and never restored its file.
    """
    return sorted(search_root.rglob(f"*{SIDECAR_SUFFIX}"))


def restore_from_sidecars(search_root: Path) -> list[str]:
    """Write each stale sidecar's bytes back over its real file and remove
    the sidecar. Returns the repo-relative paths restored, for reporting.
    """
    restored = []
    for sidecar in find_stale_sidecars(search_root):
        original = sidecar.with_name(sidecar.name[: -len(SIDECAR_SUFFIX)])
        original.write_bytes(sidecar.read_bytes())
        sidecar.unlink()
        try:
            restored.append(original.relative_to(search_root).as_posix())
        except ValueError:
            restored.append(str(original))
    return restored
