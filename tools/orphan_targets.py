# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_targets.py -- two questions orphan_check.py's liveness check must
NOT try to answer itself, split out here (fix round 1, verifier, F8 and the
TEST HOOK category) to keep orphan_check.py under this repo's 400-line cap:

  1. Is this candidate's file even part of the `rtatool` target this tool
     built and read the .map for -- or of `rtatool_snapshot` instead, or of
     NEITHER? See `rtatool_target_sources`, `rtatool_snapshot_sources`,
     `not_in_target_reason` and `not_in_any_target_reason` --
     orphan_check.py's own module docstring has the full "NOT IN TARGET"
     rationale.
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

from cpp_text import strip_comments, strip_string_and_char_literals

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


def _cmake_source_list(cmake_path: Path) -> set[str] | None:
    """Every `app/src/...` path literally listed in a `set(..._SOURCES ...)`
    CMake file -- comment-stripped first (CMake's `#` line comment, not
    C++'s `//`, so cpp_text.strip_comments does not apply here). A bare
    regex over the whole file rather than a real CMake parse: these files
    are a plain source list each (see their own header comments), and every
    entry is a bare `src/...` path token -- nothing here needs conditionals,
    generator expressions, or variable expansion.

    Returns None, not {}, when the file does not exist -- true for every
    commit before 9a2862a (2026-09-26), which split rtatool's list (and
    introduced rtatool_snapshot's list the same way) out of the monolithic
    app/CMakeLists.txt. An acceptance run against an OLDER checkout (a git
    worktree at a historical SHA) legitimately has no such file to read;
    None tells the two `_reason` functions below "this run cannot answer
    that question" rather than have an empty set make every single .cpp
    file look absent.
    """
    if not cmake_path.is_file():
        return None
    text = cmake_path.read_text(encoding="utf-8")
    no_comments = re.sub(r"#.*", "", text)
    return {f"app/{m}" for m in _CMAKE_SOURCE_PATH_RE.findall(no_comments)}


def rtatool_target_sources(source_dir: Path) -> set[str] | None:
    """Every `app/src/...` path listed in app/cmake/rtatool_sources.cmake."""
    return _cmake_source_list(source_dir / "app" / "cmake" / "rtatool_sources.cmake")


def rtatool_snapshot_sources(source_dir: Path) -> set[str] | None:
    """Every `app/src/...` path listed in
    app/cmake/rtatool_snapshot_sources.cmake -- the OTHER target this tool
    does not build or check reachability for, but whose own source list is
    still ground truth for "is this file compiled anywhere at all". Both
    this file and rtatool_sources.cmake were introduced in the same commit
    (9a2862a), so in practice one exists iff the other does.
    """
    return _cmake_source_list(source_dir / "app" / "cmake" / "rtatool_snapshot_sources.cmake")


def not_in_target_reason(
    path: str, target_sources: set[str] | None, snapshot_sources: set[str] | None
) -> str | None:
    """None if `path` is part of the rtatool target this tool actually
    built and checked; otherwise, if it is a LEGITIMATE absence (compiled
    into some OTHER target instead), why. `target_sources`/
    `snapshot_sources` of None means this run's checkout predates the
    source-list split (see `_cmake_source_list`) -- the directory exclusion
    below still applies (it is a source fact about that directory, not
    about either cmake file), but the "listed in the other target instead"
    check is skipped rather than misjudging every .cpp file.

    fix round 2 (verifier), F-A: this function used to also cover "absent
    from rtatool_sources.cmake AND absent from every other list", which
    silently exempted a `.cpp` that is not compiled into ANYTHING at all --
    exactly the L6a shape (SplHistory.cpp/SplAlarms.cpp had no caller and so
    were never added to a source list either). That case is NOT a
    legitimate NOT IN TARGET any more; see `not_in_any_target_reason`, which
    reports it as a real, failing orphan instead.
    """
    if path.startswith(EXCLUDED_DIRECTORIES):
        return "compiled only into rtatool_snapshot, never rtatool (app/src/dev/preview/ exclusion)"
    if (
        target_sources is not None
        and snapshot_sources is not None
        and path.endswith(".cpp")
        and path not in target_sources
        and path in snapshot_sources
    ):
        return "compiled only into rtatool_snapshot, never rtatool (listed only in rtatool_snapshot_sources.cmake)"
    return None


def not_in_any_target_reason(
    path: str, target_sources: set[str] | None, snapshot_sources: set[str] | None
) -> str | None:
    """None unless `path` is a `.cpp` this tool can PROVE is compiled into
    NEITHER `rtatool` nor `rtatool_snapshot` -- i.e. never compiled at all,
    which means the linker never even got a chance to discard it. That is a
    stronger, more direct defect than an ordinary orphan (the function was
    written but nothing ever asked the compiler to build it), so it is
    reported as an orphan with its own explicit reason, never silently as
    NOT IN TARGET (fix round 2, F-A -- see `not_in_target_reason`'s own
    docstring for the bug this replaces).

    Requires BOTH source lists to be readable (not None) to fire: an
    unreadable list means this run cannot tell "not compiled anywhere" apart
    from "compiled via a list this tool cannot see", and the safe default on
    an unanswerable question is to let the candidate fall through to the
    ordinary liveness check rather than invent a claim either way.
    """
    if target_sources is None or snapshot_sources is None:
        return None
    if path.startswith(EXCLUDED_DIRECTORIES):
        return None  # a deliberate, legitimate exclusion -- not a defect
    if path.endswith(".cpp") and path not in target_sources and path not in snapshot_sources:
        return "not compiled into any target"
    return None


def test_hook_is_referenced(name: str, source_dir: Path) -> bool:
    """True if `name` (a whole word, so `feedSpl` never matches inside
    `feedSplForTest`'s own longer name and vice versa) appears, with
    comments AND string/char literals stripped, in at least one
    .cpp/.h/.hpp file under any `app/tests*` directory. String literals are
    stripped too (fix round 2, F-I) so a log message or display label that
    merely NAMES the hook (`"calling enableSplLoggingForTest"`) does not
    count as a real code reference the way a comment already does not. No
    free-text allow-list: this is the ONLY way a `*ForTest` candidate is
    spared an orphan report, and only for this run -- it is not cached or
    remembered across runs.
    """
    pattern = re.compile(r"\b" + re.escape(name) + r"\b")
    for tests_dir in sorted(source_dir.glob(_TESTS_DIR_GLOB)):
        if not tests_dir.is_dir():
            continue
        for file_path in sorted(tests_dir.rglob("*")):
            if not file_path.is_file() or file_path.suffix not in _TEST_FILE_SUFFIXES:
                continue
            text = file_path.read_text(encoding="utf-8", errors="replace")
            haystack = strip_string_and_char_literals(strip_comments(text))
            if pattern.search(haystack):
                return True
    return False
