# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_targets.py -- two questions orphan_check.py's liveness check must
NOT try to answer itself, split out here (fix round 1, verifier, F8 and the
TEST HOOK category) to keep orphan_check.py under this repo's 400-line cap:

  1. Is this candidate's file even part of the `rtatool` target this tool
     built and read the .map for? See `_rtatool_target_sources` and
     `not_in_target_reason` -- orphan_check.py's own module docstring has the
     full "NOT IN TARGET" rationale.
  2. Is an otherwise-unreachable `*ForTest` candidate actually a deliberate
     test seam, proven live by a real reference under app/tests* on THIS run?
     See `test_hook_is_referenced` -- the module docstring's own "TEST HOOK"
     section has the full rationale.

Neither question is about linker reachability -- both are read straight from
source text, which is why they live apart from map_symbols.py/msvc_decorate.py.
"""

from __future__ import annotations

import re
from pathlib import Path

from cpp_text import strip_comments

# fix round 1 (verifier), F8: app/src/dev/preview/ is real code, but compiled
# ONLY into rtatool_snapshot (app/cmake/rtatool_snapshot_sources.cmake), never
# into rtatool itself (app/cmake/rtatool_sources.cmake) -- SplPreview.cpp/.h
# is the concrete example. NOTE the exclusion is `dev/preview/`, not all of
# `dev/` -- `src/dev/SpecimenComponent.cpp` is listed in BOTH source lists
# (grep app/cmake/*_sources.cmake yourself: line 49 of rtatool_sources.cmake,
# line 19 of rtatool_snapshot_sources.cmake), so it genuinely IS part of the
# rtatool target; excluding the whole `dev/` directory would have made this
# fix invent exactly the false-NOT-IN-TARGET report it exists to prevent.
EXCLUDED_DIRECTORIES = ("app/src/dev/preview/",)
_CMAKE_SOURCE_PATH_RE = re.compile(r"\bsrc/[\w./-]+\.\w+")

# A name ending in ForTest is a deliberate test seam, e.g. `enableSplLoggingForTest`.
TEST_HOOK_NAME_RE = re.compile(r"ForTest$")

_TESTS_DIR_GLOB = "app/tests*"
_TEST_FILE_SUFFIXES = (".cpp", ".h", ".hpp")


def rtatool_target_sources(source_dir: Path) -> set[str] | None:
    """Every `app/src/...` path literally listed in
    app/cmake/rtatool_sources.cmake -- comment-stripped first (CMake's `#`
    line comment, not C++'s `//`, so cpp_text.strip_comments does not apply
    here). A bare regex over the whole file rather than a real CMake parse:
    this file is a plain `set(RTATOOL_SOURCES ...)` list (see its own header
    comment), and every entry is a bare `src/...` path token -- nothing here
    needs conditionals, generator expressions, or variable expansion.

    Returns None, not {}, when the file does not exist -- true for every
    commit before 9a2862a (2026-09-26), which split this list out of the
    monolithic app/CMakeLists.txt. An acceptance run against an OLDER
    checkout (a git worktree at a historical SHA) legitimately has no such
    file to read; None tells `not_in_target_reason` "this run cannot answer
    the rtatool_sources.cmake question" rather than have an empty set make
    every single .cpp file look absent from the target.
    """
    cmake_path = source_dir / "app" / "cmake" / "rtatool_sources.cmake"
    if not cmake_path.is_file():
        return None
    text = cmake_path.read_text(encoding="utf-8")
    no_comments = re.sub(r"#.*", "", text)
    return {f"app/{m}" for m in _CMAKE_SOURCE_PATH_RE.findall(no_comments)}


def not_in_target_reason(path: str, target_sources: set[str] | None) -> str | None:
    """None if `path` is part of the rtatool target this tool actually
    built and checked; otherwise why not. `target_sources=None` means this
    run's checkout predates rtatool_sources.cmake (see
    `rtatool_target_sources`) -- the directory exclusion below still applies
    (it is a source fact about that directory, not about the cmake file), but
    the "not listed in the source list" check is skipped rather than flagging
    every .cpp file.
    """
    if path.startswith(EXCLUDED_DIRECTORIES):
        return "compiled only into rtatool_snapshot, never rtatool (app/src/dev/preview/ exclusion)"
    if target_sources is not None and path.endswith(".cpp") and path not in target_sources:
        return "not listed in app/cmake/rtatool_sources.cmake"
    return None


def test_hook_is_referenced(name: str, source_dir: Path) -> bool:
    """True if `name` (a whole word, so `feedSpl` never matches inside
    `feedSplForTest`'s own longer name and vice versa) appears,
    comment-stripped, in at least one .cpp/.h/.hpp file under any
    `app/tests*` directory. No free-text allow-list: this is the ONLY way a
    `*ForTest` candidate is spared an orphan report, and only for this run --
    it is not cached or remembered across runs.
    """
    pattern = re.compile(r"\b" + re.escape(name) + r"\b")
    for tests_dir in sorted(source_dir.glob(_TESTS_DIR_GLOB)):
        if not tests_dir.is_dir():
            continue
        for file_path in sorted(tests_dir.rglob("*")):
            if not file_path.is_file() or file_path.suffix not in _TEST_FILE_SUFFIXES:
                continue
            text = file_path.read_text(encoding="utf-8", errors="replace")
            if pattern.search(strip_comments(text)):
                return True
    return False
