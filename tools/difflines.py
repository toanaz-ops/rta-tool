#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""difflines.py -- parse `git diff -U0` output into the lines it ADDED.

Split out of diffmut.py (process-tooling PR, fix round 1, item L3: diffmut.py
was 535 lines against the project's 400-line file cap) along the real seam
between "parse a diff" and "run a mutation campaign". orphan_check.py also
needs exactly this parse (fix round 1, item M1: a new member function added
to a PRE-EXISTING header is invisible to a `--diff-filter=A` added-files-only
scan), so sharing one implementation here is what keeps the two tools' diff
parsing from drifting apart -- reusing this function, not re-deriving it, is
the point.

`-U0` (zero context lines) means every hunk header's `+` side gives the exact
starting line number of the lines that follow, with no unchanged lines
interleaved -- see `parse_added_lines`'s own docstring for the format this
reads.
"""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
from pathlib import Path


@dataclass
class AddedLine:
    path: str  # repo-relative, forward slashes
    lineno: int  # 1-based, in the NEW (current) file
    text: str


_HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@")


def parse_added_lines(diff_text: str) -> list[AddedLine]:
    """Parse `git diff -U0` output into every ADDED line, with its 1-based
    line number in the new file. Deleted-only hunks (pure removals) contribute
    nothing, which is correct: there is no new line there to scan. A file
    diffed against `/dev/null` (freshly added) contributes every line of the
    new file, since the whole thing is "added" from git's point of view.
    """
    added: list[AddedLine] = []
    current_path: str | None = None
    new_lineno = 0
    for raw in diff_text.splitlines():
        if raw.startswith("+++ "):
            dest = raw[4:].strip()
            if dest == "/dev/null":
                current_path = None
            else:
                # "+++ b/path/to/file" -> "path/to/file"
                current_path = dest.split("/", 1)[1] if dest.startswith(("a/", "b/")) else dest
            continue
        if raw.startswith("--- "):
            continue
        hunk = _HUNK_RE.match(raw)
        if hunk:
            new_lineno = int(hunk.group(1))
            continue
        if current_path is None:
            continue
        if raw.startswith("+++") or raw.startswith("---"):
            continue
        if raw.startswith("+"):
            added.append(AddedLine(path=current_path, lineno=new_lineno, text=raw[1:]))
            new_lineno += 1
        elif raw.startswith("-"):
            pass  # removed line: does not advance new_lineno, nothing to add
        # context lines are absent under -U0; anything else (e.g. "\ No
        # newline at end of file") is ignored.
    return added


def git_diff_added_lines(
    repo_root: Path, base: str, at: str, *, paths: list[str] | None = None
) -> list[AddedLine]:
    """Run `git diff -U0 <base>...<at>` (optionally scoped to `paths`) and
    parse it. A thin wrapper so callers don't each re-invoke subprocess with
    slightly different flags.
    """
    cmd = ["git", "diff", "-U0", f"{base}...{at}"]
    if paths:
        cmd += ["--"] + paths
    result = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=repo_root,
        text=True,
        check=True,
    )
    return parse_added_lines(result.stdout)
