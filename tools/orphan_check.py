#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_check.py -- no component without a production caller.

WHY. One plan in the L6a lane built components nobody called: at commit
8c5d407 (PR #26, "l6a/wave2-app"), `SplHistory`, `SplAlarms` and `SplLogWriter`
each had a header, an implementation and a test suite, but nothing in
app/src outside their own pair ever named them; `enableSplLogging` (a member
added to the pre-existing `AnalysisThread.h`) had no caller either. The
wiring into the app landed waves later (W2-E1/E2a/E2b). A green test suite
says a component works; it says nothing about whether the product actually
uses it.

WHAT COUNTS AS "NEW" (fix round 1, item M1). Two cases, both scoped to
app/src/ or core/include/:
  1. A whole header ADDED in `base...at` (`git diff --diff-filter=A`):
     every symbol `orphan_symbols.extract_symbols` finds in its full content.
  2. A symbol newly declared inside a header that already EXISTED before the
     diff (`git diff --diff-filter=M`): only the symbols found in the ADDED
     HUNKS of that header (via `difflines.parse_added_lines`, the same parse
     diffmut.py uses to find mutation candidates -- shared rather than
     re-derived, so the two tools' notion of "what changed" cannot drift
     apart). Case 2 is what a `--diff-filter=A`-only scan structurally cannot
     see: a new member function on a header that itself is not new.
A header that changed with no symbol-shaped line in its added hunk (a comment
edit, a formatting pass) contributes nothing from case 2, which is correct --
nothing NEW was declared there.

WHAT IS EXTRACTED. See orphan_symbols.py's own docstring for the regex
heuristic and its documented blind spots.

WHAT COUNTS AS A REFERENCE. Any whole-word occurrence of the symbol name (for
a qualified member function name, the qualified form) anywhere under app/src/,
after two exclusions (fix round 1, items M2 and L1):
  - M2: `//` and `/* */` comments are stripped from both the header being
    scanned and every file searched for a reference, respecting string/char
    literals (cpp_text.strip_comments) -- a doc-comment that merely NAMES a
    component ("`SplAlarms` is the one producer of this...", Snapshot.h:230)
    no longer counts as a caller.
  - L1: a line that is the symbol's own out-of-line member DEFINITION
    (`orphan_symbols.is_self_definition_of`) does not count either, in ANY
    file -- not only inside the symbol's own same-basename pair.
    `SplLogWriter` is declared in `SplLog.h` and defined in
    `SplLogWriter.cpp` (different basenames, so the ordinary same-basename
    pairing below does not exclude that file); without this exclusion, its
    own constructor/method definitions there read as external callers of
    itself.
The new header's own same-basename `.cpp`/`.h` pair is still excluded
entirely, and tests (`app/tests*`, `core/tests`, `platform/tests*`,
`ui/tests`) still do not count.

TRANSITIVE ORPHANS (fix round 1, item L1). `SplHistory`'s only references
after the two exclusions above are from `SplAlarms.h`/`.cpp` -- genuine code
(a `SplHistory&` parameter), not a comment -- but `SplAlarms` (the class) is
ITSELF an orphan once its one remaining "reference" (the Snapshot.h comment)
is stripped by M2. A reference that comes only from a file whose own header's
symbols are ALL orphaned is not evidence of a production caller either --
that file is not wired in. `find_orphans` iterates this to a fixed point:
each round, any header whose every symbol is currently orphan makes its own
pair's files "orphaned files"; any symbol whose remaining references are
entirely inside orphaned files becomes orphan too; repeat until nothing
changes. A symbol with a genuine reference from a file that is not (even
transitively) orphaned stops the iteration from over-reaching -- it takes a
real, wired-in caller to clear a symbol, at any remove.

AT A HISTORICAL COMMIT. `--at <rev>` evaluates `<rev>`'s tree instead of the
working tree at HEAD, reading every file through `git show <rev>:<path>` and
enumerating app/src/ through `git ls-tree -r <rev>` -- no checkout needed
(this repo's sessions each run in their own git worktree and cannot create a
second one just to look at an old commit).

Exit code: 1 if any symbol in scope has zero (transitively) valid references,
else 0.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

from cpp_text import strip_comments
from difflines import git_diff_added_lines
from orphan_symbols import extract_symbols, is_self_definition_of

REPO_ROOT = Path(__file__).resolve().parent.parent

NEW_HEADER_DIRS = ("app/src/", "core/include/")
HEADER_SUFFIXES = (".h", ".hpp")
SEARCH_SUFFIXES = (".h", ".hpp", ".cpp")


def _changed_headers(base: str, at: str, diff_filter: str) -> list[str]:
    result = subprocess.run(
        ["git", "diff", f"--diff-filter={diff_filter}", "--name-only", f"{base}...{at}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=REPO_ROOT,
        text=True,
        check=True,
    )
    headers = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if line and line.startswith(NEW_HEADER_DIRS) and line.endswith(HEADER_SUFFIXES):
            headers.append(line)
    return headers


def new_headers(base: str, at: str = "HEAD") -> list[str]:
    return _changed_headers(base, at, "A")


def modified_headers(base: str, at: str = "HEAD") -> list[str]:
    return _changed_headers(base, at, "M")


def _git_show(at: str, path: str) -> str | None:
    """`git show <at>:<path>`, or None if that path did not exist in <at>.
    Lets this script evaluate a HISTORICAL commit's tree without checking it
    out into a worktree -- see the module docstring's "AT A HISTORICAL
    COMMIT" section.
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


def app_src_files(at: str | None = None) -> list[str]:
    """Repo-relative paths (forward slashes) under app/src/. Reads the
    working tree normally, or a historical commit's tree via `git ls-tree`
    when `at` is given.
    """
    if at is not None:
        paths = _git_ls_tree(at, "app/src")
        return [p for p in paths if Path(p).suffix in SEARCH_SUFFIXES]
    root = REPO_ROOT / "app" / "src"
    if not root.is_dir():
        return []
    files = []
    for suffix in SEARCH_SUFFIXES:
        for p in root.rglob(f"*{suffix}"):
            files.append(p.relative_to(REPO_ROOT).as_posix())
    return files


def paired_files(header_repo_path: str, at: str | None = None) -> set[str]:
    """The header's own repo-relative path and its same-basename .cpp/.h
    sibling (if any), wherever it lives -- a new header's OWN pair is not
    evidence that something else calls it. This does NOT catch a class
    defined in a differently-named .cpp (`SplLogWriter` in `SplLog.h` /
    `SplLogWriter.cpp`) -- `orphan_symbols.is_self_definition_of` is what
    excludes that case, from any file, not just a same-basename one.
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


def _symbols_from_added_header(header: str, at: str | None) -> set[str]:
    content = _read(header, at)
    if content is None:
        return set()
    return extract_symbols(strip_comments(content))


def _symbols_from_modified_header_hunk(header: str, base: str, at_ref: str) -> set[str]:
    """Fix round 1, item M1: only the symbols in this header's ADDED lines,
    not its whole (mostly pre-existing) content.
    """
    added = git_diff_added_lines(REPO_ROOT, base, at_ref, paths=[header])
    text = "\n".join(a.text for a in added)
    return extract_symbols(strip_comments(text))


def _collect_scoped_symbols(
    base: str, at: str | None
) -> tuple[dict[str, set[str]], dict[str, set[str]]]:
    """header -> its in-scope symbols, and header -> its paired files, for
    every header touched (added OR modified with a new declaration) in
    `base...at_ref`.
    """
    at_ref = at if at is not None else "HEAD"
    header_symbols: dict[str, set[str]] = {}
    header_pair: dict[str, set[str]] = {}

    for header in new_headers(base, at_ref):
        symbols = _symbols_from_added_header(header, at)
        if symbols:
            header_symbols[header] = symbols
            header_pair[header] = paired_files(header, at)

    for header in modified_headers(base, at_ref):
        symbols = _symbols_from_modified_header_hunk(header, base, at_ref)
        if symbols:
            header_symbols.setdefault(header, set()).update(symbols)
            header_pair.setdefault(header, paired_files(header, at))

    return header_symbols, header_pair


def find_orphans(base: str, at: str | None = None) -> dict[str, list[str]]:
    """header path -> list of symbol names with zero (transitively) valid
    references. Only headers with at least one orphaned symbol appear in the
    result. See the module docstring for what counts as "new", a reference,
    and the transitive fixed point.
    """
    header_symbols, header_pair = _collect_scoped_symbols(base, at)
    if not header_symbols:
        return {}

    content_cache: dict[str, str] = {}
    for f in app_src_files(at):
        if _is_test_path(Path(f)):
            continue
        text = _read(f, at)
        if text is not None:
            content_cache[f] = strip_comments(text)

    symbol_owner: dict[str, str] = {}
    symbol_refs: dict[str, set[str]] = {}
    for header, symbols in header_symbols.items():
        pair = header_pair[header]
        for name in symbols:
            symbol_owner[name] = header
            pattern = re.compile(r"\b" + re.escape(name) + r"\b")
            refs: set[str] = set()
            for f, text in content_cache.items():
                if f in pair:
                    continue
                filtered = "\n".join(
                    line for line in text.splitlines() if not is_self_definition_of(line, name)
                )
                if pattern.search(filtered):
                    refs.add(f)
            symbol_refs[name] = refs

    is_orphan = {name: (len(refs) == 0) for name, refs in symbol_refs.items()}

    changed = True
    while changed:
        changed = False
        fully_orphaned_headers = {
            header
            for header, syms in header_symbols.items()
            if syms and all(is_orphan[s] for s in syms)
        }
        orphaned_files: set[str] = set()
        for header in fully_orphaned_headers:
            orphaned_files |= header_pair[header]

        for name, refs in symbol_refs.items():
            if is_orphan[name]:
                continue
            if not (refs - orphaned_files):
                is_orphan[name] = True
                changed = True

    orphans: dict[str, list[str]] = {}
    for name, orphaned in is_orphan.items():
        if orphaned:
            orphans.setdefault(symbol_owner[name], []).append(name)
    for names in orphans.values():
        names.sort()
    return orphans


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="orphan_check.py",
        description="Flag a component under app/src or core/include with no production caller in app/src.",
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
        print("orphan_check: OK -- every new/changed symbol is referenced in app/src")
        return 0

    print("orphan_check: found component(s) with no production caller in app/src:")
    for header, names in sorted(orphans.items()):
        print(f"  {header}")
        for name in names:
            print(f"    - {name}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
