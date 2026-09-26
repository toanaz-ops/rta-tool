#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_check.py -- no component without a production caller.

WHY. One plan in the L6a lane built components nobody called: at commit
8c5d407 (PR #26, "l6a/wave2-app"), SplHistory, SplAlarms and SplLogWriter each
had a header, an implementation and a test suite, but nothing in app/src
outside their own pair ever named them -- the wiring into the app landed two
waves later (W2-E). A green test suite says a component works; it says
nothing about whether the product actually uses it. This script closes that
gap for NEW headers only, the same way diffmut.py closes the mutation gap for
changed LINES only: it looks at what a change added, not at the whole tree.

WHAT COUNTS AS "NEW". Every header (`.h`/`.hpp`) that `git diff --diff-filter=A
--name-only <base>...HEAD` reports as ADDED (not modified) under app/src/ or
core/include/. A header that already existed before the diff is not in scope
-- this is about a change introducing a component, not an audit of the whole
codebase (that audit already exists in miniature as the historical check this
script's own PR body runs at 8c5d407).

WHAT IS EXTRACTED (the symbol heuristic). A regex line-scan, not a C++ parser:
  - `class Foo` / `struct Foo` declaration lines -> the type name.
  - A free-function-shaped line: some prefix of type tokens, then a bare
    identifier, then `(args)`, then optionally `const`/`noexcept`, ending in
    `{` or `;`. This also catches a member function defined out-of-line
    (`Foo::bar(...)`), where the extracted "name" is `Foo::bar`; a reference
    check for that qualified name is stricter than for a bare one, which is
    the conservative direction (it undercounts hits rather than overcounts
    them, and an undercount here means "possibly still an orphan", not the
    reverse).
This is deliberately narrow, matching diffmut.py's own declared trade-off: a
symbol this heuristic MISSES (a macro-generated declaration, a lambda
assigned to a named variable, a `using Alias = ...`) is never flagged either
way, rather than risk a false orphan report on code the heuristic cannot read.

WHAT COUNTS AS A REFERENCE. Any whole-word occurrence of the symbol name (for
a qualified member function name, the qualified form) anywhere under app/src/,
EXCLUDING the new header itself and its paired implementation file (same
basename, `.cpp` alongside the new `.h`). Tests (anything under app/tests* or
core/tests, platform/tests*, ui/tests) do NOT count -- a component exercised
only by its own test suite is exactly the defect this script exists to catch,
not evidence against it.

KNOWN FALSE POSITIVES AND NEGATIVES, both from the same root cause (a text
search, not a compiler):
  - FALSE POSITIVE: a private helper class/function declared in the SAME
    header as its public wrapper and called only from that header's own
    .cpp (e.g. `SplAlarm`, used only inside SplAlarms.h/.cpp's own pair) has
    no reference OUTSIDE the pair by construction, so it reads as an orphan
    even though it is not an unwired component -- it is simply private. So
    does a result-struct type that is always consumed through `auto` at the
    call site (the caller never spells the type name), and a same-pair-only
    internal type is textually indistinguishable from one that truly has no
    caller anywhere.
  - FALSE NEGATIVE: the reference search does not strip comments, so a
    doc-comment that merely NAMES a component ("Declared here, DEFINED in
    SplLogWriter.cpp") counts as a "reference" even when nothing calls it.
Both are why this script's own PR body reports its 8c5d407 proof run's exact
output rather than a hand-picked subset -- the tool's real behaviour, not the
tidiest possible headline.

AT A HISTORICAL COMMIT. `--at <rev>` evaluates `<rev>`'s tree instead of the
working tree at HEAD, reading every file through `git show <rev>:<path>` and
enumerating app/src/ through `git ls-tree -r <rev>` -- no checkout needed.
This exists because this repo's sessions each run in their own git worktree
and cannot create a second one just to look at an old commit; it is also just
a more direct way to answer "what would this have printed at commit X" than
checking X out somewhere and running the working-tree code path.

Exit code: 1 if any new header declares a symbol with zero references, else 0.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

NEW_HEADER_DIRS = ("app/src/", "core/include/")
HEADER_SUFFIXES = (".h", ".hpp")
SEARCH_SUFFIXES = (".h", ".hpp", ".cpp")

_CLASS_RE = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_]\w*)\b")
# type-tokens, then NAME(args) [const] [noexcept] , ending in { or ;
# NAME may be qualified (Foo::bar) for an out-of-line member definition.
_FUNC_RE = re.compile(
    r"^\s*[A-Za-z_][\w:<>,\*&\s]*[\s\*&]"
    r"((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)"
    r"\s*\([^;{}]*\)\s*(const)?\s*(noexcept)?\s*(override)?\s*[;{]\s*$"
)
_CONTROL_KEYWORDS = {"if", "for", "while", "switch", "return", "catch", "sizeof", "static_assert"}
# A line that OPENS with one of these is a statement inside some function's
# body (a call, a return, a throw), never a declaration -- even though its
# tail can otherwise look exactly like one. Without this, `return
# std::put_time(&tm, fmt);` inside an inline header function is misread as a
# declaration of a function named `std::put_time` (a standard-library call,
# not a project symbol), which is a real false positive this check hit
# against SplSessionFolderName.h on its first run against the live repo.
_STATEMENT_PREFIX_RE = re.compile(r"^(return|throw|co_return)\b")


def extract_symbols(text: str) -> set[str]:
    """Best-effort class/struct/free-function names declared in `text`. See
    the module docstring's "WHAT IS EXTRACTED" section for the exact rule and
    its known blind spots.
    """
    names: set[str] = set()
    for raw in text.splitlines():
        line = raw.strip()
        if line.startswith("//"):
            continue
        if _STATEMENT_PREFIX_RE.match(line):
            continue
        m = _CLASS_RE.match(line)
        if m:
            names.add(m.group(1))
            continue
        m2 = _FUNC_RE.match(line)
        if m2:
            name = m2.group(1)
            # A header can reference std:: (or any other outside namespace)
            # from inside a statement that otherwise matches the declaration
            # shape (`out << std::put_time(...);` is the real example that
            # tripped this against SplSessionFolderName.h) -- it can never
            # DECLARE a symbol there, so this is a call, not a declaration.
            if name.startswith("std::"):
                continue
            if name.split("::")[-1] not in _CONTROL_KEYWORDS:
                names.add(name)
    return names


def new_headers(base: str, at: str = "HEAD") -> list[str]:
    result = subprocess.run(
        ["git", "diff", "--diff-filter=A", "--name-only", f"{base}...{at}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=REPO_ROOT,
        text=True,
        check=True,
    )
    headers = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        if not line.startswith(NEW_HEADER_DIRS):
            continue
        if not line.endswith(HEADER_SUFFIXES):
            continue
        headers.append(line)
    return headers


def _git_show(at: str, path: str) -> str | None:
    """`git show <at>:<path>`, or None if that path did not exist in <at>.
    Lets this script evaluate a HISTORICAL commit's tree without checking it
    out into a worktree (this repo's agents run each in their own worktree
    and cannot create a second one to inspect an old commit) -- see the
    module docstring's "AT A HISTORICAL COMMIT" section.
    """
    result = subprocess.run(
        ["git", "show", f"{at}:{path}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=REPO_ROOT,
        text=True,
    )
    if result.returncode != 0:
        return None
    return result.stdout


def _git_ls_tree(at: str, subdir: str) -> list[str]:
    result = subprocess.run(
        ["git", "ls-tree", "-r", "--name-only", at, "--", subdir],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=REPO_ROOT,
        text=True,
        check=True,
    )
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def _is_test_path(path: Path) -> bool:
    parts = path.as_posix()
    return (
        "/tests/" in parts
        or "/tests_juce/" in parts
        or parts.startswith("app/tests")
        or parts.startswith("core/tests")
        or parts.startswith("platform/tests")
        or parts.startswith("ui/tests")
    )


def app_src_files(exclude: set[str], at: str | None = None) -> list[str]:
    """Repo-relative paths (forward slashes) under app/src/, excluding
    `exclude`. Reads the working tree normally, or a historical commit's tree
    via `git ls-tree` when `at` is given.
    """
    if at is not None:
        paths = _git_ls_tree(at, "app/src")
        return [p for p in paths if Path(p).suffix in SEARCH_SUFFIXES and p not in exclude]
    root = REPO_ROOT / "app" / "src"
    if not root.is_dir():
        return []
    files = []
    for suffix in SEARCH_SUFFIXES:
        for p in root.rglob(f"*{suffix}"):
            rel = p.relative_to(REPO_ROOT).as_posix()
            if rel in exclude:
                continue
            files.append(rel)
    return files


def paired_files(header_repo_path: str, at: str | None = None) -> set[str]:
    """The header's own repo-relative path and its same-basename .cpp/.h
    sibling (if any), wherever it lives -- a new header's OWN pair is not
    evidence that something else calls it.
    """
    header = Path(header_repo_path)
    stem_dir = header.parent
    stem = header.stem
    pair = {header_repo_path}
    for suffix in (".cpp", ".h", ".hpp"):
        candidate = (stem_dir / f"{stem}{suffix}").as_posix()
        exists = (
            _git_show(at, candidate) is not None
            if at is not None
            else (REPO_ROOT / candidate).exists()
        )
        if exists:
            pair.add(candidate)
    return pair


def _read(path: str, at: str | None) -> str | None:
    if at is not None:
        return _git_show(at, path)
    full = REPO_ROOT / path
    if not full.is_file():
        return None
    return full.read_text(encoding="utf-8", errors="replace")


def find_orphans(base: str, at: str | None = None) -> dict[str, list[str]]:
    """header path -> list of symbol names with zero references. Only
    headers with at least one orphaned symbol appear in the result.

    `at` evaluates a historical commit's tree instead of the working tree
    (see `_git_show`'s docstring for why); the diff range is still
    `base...at`, so `at=None` (the default) means "the working tree at
    HEAD", matching every other use of this function.
    """
    at_ref = at if at is not None else "HEAD"
    orphans: dict[str, list[str]] = {}
    for header in new_headers(base, at_ref):
        content = _read(header, at)
        if content is None:
            continue  # deleted again later in the same diff range
        symbols = extract_symbols(content)
        if not symbols:
            continue
        exclude = paired_files(header, at)
        haystacks = [
            text
            for f in app_src_files(exclude, at)
            if not _is_test_path(Path(f))
            for text in [_read(f, at)]
            if text is not None
        ]
        combined = "\n".join(haystacks)
        missing = []
        for name in sorted(symbols):
            pattern = re.compile(r"\b" + re.escape(name) + r"\b")
            if not pattern.search(combined):
                missing.append(name)
        if missing:
            orphans[header] = missing
    return orphans


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="orphan_check.py",
        description="Flag new app/src or core/include headers with no production caller in app/src.",
    )
    parser.add_argument("--base", default="origin/main", help="diff base (default: origin/main)")
    parser.add_argument(
        "--at",
        default=None,
        help=(
            "evaluate a historical commit's tree via git plumbing (git show/ls-tree) "
            "instead of the working tree at HEAD -- no checkout needed. "
            "The diff range becomes <base>...<at>."
        ),
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    # --help exits above and runs nothing else (memory
    # a-gen-script-runs-the-moment-you-invoke-it.md).

    orphans = find_orphans(args.base, args.at)
    if not orphans:
        print("orphan_check: OK -- every new header's declared symbols are referenced in app/src")
        return 0

    print("orphan_check: found component(s) with no production caller in app/src:")
    for header, names in sorted(orphans.items()):
        print(f"  {header}")
        for name in names:
            print(f"    - {name}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
