#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""diffmut.py -- differential delete-line mutation testing.

WHY. The L6a lane's review found that roughly 40% of its MEDIUM defects were
"a line no test can make red" -- found only by a human verifier mutating code
by hand and watching whether anything noticed. This script automates exactly
that check, but only over what a change actually TOUCHED: it reads
`git diff -U0 <base>...HEAD`, finds every line ADDED under the watched paths,
deletes each one in turn (one mutant at a time), rebuilds the test target
that covers it, and reports whether any test went red. A line that survives
its own deletion with every test still green is a line nothing is watching.

WHAT IS NOT MUTATED (the line classifier, `is_mutable_line`). Deleting these
either cannot change behaviour or almost certainly breaks the build in a way
unrelated to the statement's logic, which would make every such mutant
NO-BUILD noise rather than a signal:
  - blank lines
  - comment-only lines (`//...`, and a line that is entirely inside or closes
    a `/* ... */` block -- detected line-by-line, not by tracking nesting
    across the file, so a `/*` opened and NOT closed on the same line is
    treated as an ordinary line; this is a heuristic, not a C++ comment
    parser)
  - brace/paren/semicolon/comma "shape" lines with no other content
    (`{`, `}`, `};`, `);`, `});`, ...)
  - preprocessor directives (`#include`, `#pragma`, `#define`, ...)
  - `namespace ...` opening lines
  - access specifiers (`public:` / `private:` / `protected:`)
  - PURE DECLARATIONS: a line that is *only* `<return-type> name(args) [const]
    [override] [= 0];` with no `=` assignment and no `return` keyword. This is
    a heuristic, not a parser -- it is meant to catch a bare prototype (a
    declaration has no body to mutate), and it is deliberately narrow: a bare
    call statement like `doThing(x);` has no leading type token before the
    name, so it does NOT match and IS mutated (that call is a statement with
    an effect, and deleting it should be observable). The task naming this
    heuristic calls the general case "hard" and asks for something simple and
    documented rather than a real parse -- this is that: it undercounts (some
    prototypes may still slip through as statement-shaped) rather than
    overcounts (a real statement being skipped and never mutated at all).

MAPPING A MUTATED PATH TO A TEST TARGET. core/, platform/types/, the rest of
platform/, and ui/ each map to exactly one ctest target by the project's own
layer convention (see CLAUDE.md "Module boundaries"). app/src/* is ambiguous
by directory alone -- e.g. app/src/export/ holds both SplLogWriter.cpp
(compiled into the JUCE-free rtatool_analysis_tests) and SplReport*.cpp
(exercised by the JUCE-only rtatool_export_tests) -- so for app/src/* this
script PARSES the real CMake source lists (app/tests/cmake/*.cmake for the
OFF target, app/tests_juce/CMakeLists.txt for the three ON targets) at run
time and matches by basename, rather than guessing from the subdirectory.
That means the mapping cannot silently drift from the CMakeLists it mirrors;
see build_app_src_target_map() below. A file that appears in neither list
falls back to a small subdirectory heuristic (with a WARNING printed) so the
script still does something useful for an app/src/ file the build does not
yet compile into any test target.

HOW A MUTANT IS APPLIED, TESTED AND UNDONE. For each mutant:
  1. The file's exact original bytes are read into memory.
  2. The chosen line is deleted (not merely commented out -- this is
     "delete-line" mutation, named for what it does).
  3. The test executable(s) for the mapped target are deleted FIRST, before
     the build runs (memory `mutation-testing-needs-the-exe-deleted-first.md`
     -- cmake --build can print "-> X.exe" without relinking a stale binary,
     which would silently PASS every mutant).
  4. For a header mutation, a sibling .cpp in the same directory is touched
     so an incremental build is forced to recompile something that includes
     it (CMake dependency scanning normally handles this, but the exe was
     just deleted out from under it, so this belt-and-braces touch costs
     nothing and removes one way this could go quiet).
  5. `cmake --build <dir> --config <cfg> --target <target>` runs. A non-zero
     exit is NO-BUILD -- the mutation broke compilation, which the compiler
     itself caught; reported separately from a real test verdict.
  6. On a successful build the target's exe is invoked DIRECTLY (never via
     `cmd //c` -- this is a Python script calling subprocess with an argument
     list, so a space in the build path is not a shell-quoting problem the
     way it is for a Git-Bash-invoked exe) and its exit code is the verdict:
     non-zero (a Catch2 assertion failed) is KILLED, zero is SURVIVED.
  7. The original bytes are restored in a `finally` block REGARDLESS of what
     happened above, and a sha256 of the restored file is compared against a
     sha256 taken before the mutation, so restoration is verified, not
     assumed. This script never uses `git checkout`/`git stash` to undo a
     mutation (memory
     `a-verifier-with-bash-can-git-checkout-your-uncommitted-fix.md`: either
     one can discard a real uncommitted fix sitting in the same file).

At the end, every mutated file is checked with `git diff --quiet` -- if the
restore-and-verify above worked, this reports clean; if it does not, that is
this script's own bug, not the mutation's, and it is surfaced loudly rather
than silently leaving a repo file altered.

Exit code: 1 if any mutant SURVIVED (so this can gate a pipeline), else 0.
NO-BUILD mutants do not affect the exit code -- they say something about the
mutation, not about test coverage.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# --- mutated-path -> ctest/exe target mapping -------------------------------
# Directory-prefix rules that are NOT ambiguous: each of these layers builds
# into exactly one test target by construction (see CLAUDE.md "Module
# boundaries"). Order matters -- platform/types/ must be checked before the
# more general platform/ prefix.
STATIC_PREFIX_TARGETS: list[tuple[str, str]] = [
    ("core/", "rta_core_tests"),
    ("platform/types/", "rta_platform_tests"),
    ("platform/", "rta_platform_juce_tests"),  # ON-only: needs JUCE
    ("ui/", "az_ui_tests"),  # ON-only: needs JUCE
]

# app/src/* is ambiguous by directory alone -- these are the real CMake
# source lists this script parses to resolve it. Paths are relative to
# REPO_ROOT.
APP_OFF_SOURCE_LISTS = [
    "app/tests/cmake/base_test_sources.cmake",
    "app/tests/cmake/api_test_sources.cmake",
    "app/tests/cmake/spl_test_sources.cmake",
    "app/tests/cmake/impl_test_sources.cmake",
]
APP_OFF_TARGET = "rtatool_analysis_tests"
APP_ON_CMAKELISTS = "app/tests_juce/CMakeLists.txt"
APP_ON_TARGETS = ("rtatool_view_tests", "rtatool_routing_live_tests", "rtatool_export_tests")

# Fallback ONLY for an app/src/ file this script's CMake parse did not find in
# any target's source list (e.g. a brand new file not yet wired into a test).
# Printed as a WARNING, never used silently.
APP_SRC_FALLBACK_PREFIXES: list[tuple[str, str]] = [
    ("app/src/measure/", APP_OFF_TARGET),
    ("app/src/api/", APP_OFF_TARGET),
    ("app/src/trace/", APP_OFF_TARGET),
    ("app/src/export/", APP_OFF_TARGET),
    ("app/src/view/", "rtatool_view_tests"),
    ("app/src/dev/", "rtatool_view_tests"),
]

MUTABLE_EXTENSIONS = {".h", ".hpp", ".cpp"}

# --- line classifier ---------------------------------------------------------

_BRACE_SHAPE_RE = re.compile(r"^[\{\}\(\)\;\,\s]*$")
_ACCESS_SPECIFIER_RE = re.compile(r"^(public|private|protected)\s*:\s*$")
_NAMESPACE_RE = re.compile(r"^namespace\b")
_PREPROCESSOR_RE = re.compile(r"^#")
_COMMENT_ONLY_RE = re.compile(r"^(//.*|/\*.*\*/|\*.*|/\*.*)$")
# See the module docstring's "PURE DECLARATIONS" section for what this is and
# is not meant to catch.
_PURE_DECLARATION_RE = re.compile(
    r"^[A-Za-z_][\w:<>,\*&\s]*[\s\*&][A-Za-z_~][\w]*\s*\([^;{}]*\)\s*"
    r"(const)?\s*(override)?\s*(noexcept)?\s*(=\s*0)?\s*;$"
)


def is_mutable_line(line: str) -> bool:
    """True if `line` (no trailing newline) is a candidate for deletion.

    See the module docstring for the full rationale of each exclusion.
    """
    stripped = line.strip()
    if not stripped:
        return False
    if _COMMENT_ONLY_RE.match(stripped):
        return False
    if _BRACE_SHAPE_RE.match(stripped):
        return False
    if _PREPROCESSOR_RE.match(stripped):
        return False
    if _NAMESPACE_RE.match(stripped):
        return False
    if _ACCESS_SPECIFIER_RE.match(stripped):
        return False
    if _PURE_DECLARATION_RE.match(stripped) and not re.search(r"\breturn\b", stripped):
        return False
    return True


# --- unified diff parsing ----------------------------------------------------


@dataclass
class AddedLine:
    path: str  # repo-relative, forward slashes
    lineno: int  # 1-based, in the NEW (current HEAD) file
    text: str


_HUNK_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@")


def parse_added_lines(diff_text: str) -> list[AddedLine]:
    """Parse `git diff -U0` output into every ADDED line, with its 1-based
    line number in the new file. Deleted-only hunks (pure removals) contribute
    nothing, which is correct: there is no new line there to mutate.
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
            pass  # removed line: does not advance new_lineno, nothing to mutate
        # context lines are absent under -U0; anything else (e.g. "\ No
        # newline at end of file") is ignored.
    return added


# --- CMake source-list parsing (app/src/* target resolution) ----------------

_CMAKE_SOURCE_PATH_RE = re.compile(r"([\w./${}]*\.(?:cpp|h|hpp))\b")


def _basenames_from_cmake(text: str) -> set[str]:
    names: set[str] = set()
    for match in _CMAKE_SOURCE_PATH_RE.finditer(text):
        token = match.group(1)
        # Strip a leading CMake variable reference like
        # ${CMAKE_CURRENT_SOURCE_DIR}/../src/measure/Analyser.cpp
        names.add(Path(token).name)
    return names


def build_app_src_target_map() -> dict[str, str]:
    """basename -> target, for every .cpp/.h this script found listed in the
    app/ test CMakeLists. Later entries do not overwrite earlier ones: the OFF
    list is read first, so a name appearing in both an OFF and an ON list
    (should not happen, but silence here would be a bug hiding a bug) keeps
    the OFF verdict and a subsequent duplicate is simply redundant, not
    corrective.
    """
    mapping: dict[str, str] = {}
    for rel in APP_OFF_SOURCE_LISTS:
        p = REPO_ROOT / rel
        if not p.is_file():
            continue
        for name in _basenames_from_cmake(p.read_text(encoding="utf-8")):
            mapping.setdefault(name, APP_OFF_TARGET)

    on_path = REPO_ROOT / APP_ON_CMAKELISTS
    if on_path.is_file():
        text = on_path.read_text(encoding="utf-8")
        # Split into add_executable(...) blocks by target name so a file
        # listed under rtatool_view_tests is not confused with one listed
        # under rtatool_export_tests.
        for target in APP_ON_TARGETS:
            block_match = re.search(
                r"add_executable\(\s*" + re.escape(target) + r"\b(.*?)\)", text, re.DOTALL
            )
            if not block_match:
                continue
            for name in _basenames_from_cmake(block_match.group(1)):
                mapping.setdefault(name, target)
    return mapping


def test_targets_for(path: str, app_src_map: dict[str, str]) -> list[str]:
    """Resolve a repo-relative path (forward slashes) to the ctest target(s)
    that cover it. Returns [] if nothing maps -- the caller must treat that
    mutant as unresolved, not silently drop it.
    """
    for prefix, target in STATIC_PREFIX_TARGETS:
        if path.startswith(prefix):
            return [target]
    if path.startswith("app/src/"):
        name = Path(path).name
        if name in app_src_map:
            return [app_src_map[name]]
        for prefix, target in APP_SRC_FALLBACK_PREFIXES:
            if path.startswith(prefix):
                print(
                    f"WARNING: {path} not found in any app/ test CMakeLists; "
                    f"falling back to subdirectory heuristic -> {target}",
                    file=sys.stderr,
                )
                return [target]
    return []


# --- exe / build helpers -----------------------------------------------------


def find_exe(build_dir: Path, config: str, target: str) -> Path | None:
    candidates = sorted(build_dir.rglob(f"{target}.exe")) or sorted(build_dir.rglob(target))
    # Prefer a path that actually contains the requested config directory --
    # a multi-config generator (Visual Studio) lays targets out under
    # <...>/<Config>/<target>.exe, and a stale Debug binary from a previous
    # run must never be mistaken for the Release one we just asked to build.
    for c in candidates:
        if config in c.parts:
            return c
    return candidates[0] if candidates else None


def sha256_of(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


@dataclass
class MutantResult:
    path: str
    lineno: int
    verdict: str  # KILLED | SURVIVED | NO-BUILD | UNRESOLVED
    target: str
    detail: str = ""


def touch_sibling_cpp(header_path: Path) -> None:
    for sibling in sorted(header_path.parent.glob("*.cpp")):
        sibling.touch()
        return


def run_mutant(
    repo_path: Path,
    lineno: int,
    targets: list[str],
    build_dir: Path,
    config: str,
    log_path: Path,
) -> MutantResult:
    try:
        rel = repo_path.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        # Not under REPO_ROOT: only reachable from the self-test, which
        # exercises this function against a tmp_path fixture with no real
        # repo underneath it. Real invocations always pass a REPO_ROOT path.
        rel = repo_path.as_posix()
    if not targets:
        return MutantResult(rel, lineno, "UNRESOLVED", target="<none>")
    target = targets[0]

    original_bytes = repo_path.read_bytes()
    original_sha = hashlib.sha256(original_bytes).hexdigest()
    try:
        text = original_bytes.decode("utf-8")
        lines = text.splitlines(keepends=True)
        if lineno < 1 or lineno > len(lines):
            return MutantResult(rel, lineno, "UNRESOLVED", target, detail="line out of range")
        deleted = lines.pop(lineno - 1)
        repo_path.write_bytes("".join(lines).encode("utf-8"))

        # Step: delete the test exe FIRST (mutation-testing-needs-the-exe-
        # deleted-first.md) -- a stale binary can false-PASS a mutation.
        exe = find_exe(build_dir, config, target)
        if exe and exe.exists():
            exe.unlink()

        if repo_path.suffix in (".h", ".hpp"):
            touch_sibling_cpp(repo_path)

        with log_path.open("a", encoding="utf-8") as log:
            log.write(f"\n=== mutant {rel}:{lineno}: {deleted!r} (target {target}) ===\n")
            build = subprocess.run(
                [
                    "cmake",
                    "--build",
                    str(build_dir),
                    "--config",
                    config,
                    "--target",
                    target,
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                cwd=REPO_ROOT,
            )
            if build.returncode != 0:
                return MutantResult(rel, lineno, "NO-BUILD", target, detail=deleted.strip())

            exe = find_exe(build_dir, config, target)
            if exe is None or not exe.exists():
                return MutantResult(
                    rel, lineno, "NO-BUILD", target, detail="build succeeded but exe not found"
                )
            run = subprocess.run([str(exe)], stdout=log, stderr=subprocess.STDOUT, cwd=REPO_ROOT)
            verdict = "SURVIVED" if run.returncode == 0 else "KILLED"
            return MutantResult(rel, lineno, verdict, target, detail=deleted.strip())
    finally:
        repo_path.write_bytes(original_bytes)
        restored_sha = sha256_of(repo_path)
        if restored_sha != original_sha:
            raise RuntimeError(
                f"diffmut.py FAILED TO RESTORE {rel} byte-for-byte "
                f"(sha256 {restored_sha} != {original_sha}) -- fix this before trusting "
                f"anything else this run reported"
            )


# --- CLI ----------------------------------------------------------------


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="diffmut.py",
        description="Differential delete-line mutation testing over lines this branch added.",
    )
    parser.add_argument("--base", default="origin/main", help="diff base (default: origin/main)")
    parser.add_argument("--build-dir", required=True, help="CMake build directory to build in")
    parser.add_argument("--config", default="Release", help="build configuration (default: Release)")
    parser.add_argument(
        "--paths",
        nargs="+",
        default=["core/src", "core/include", "app/src"],
        help="repo-relative path prefixes to scan (default: core/src core/include app/src)",
    )
    parser.add_argument("--max", type=int, default=None, help="cap the number of mutants run")
    parser.add_argument("--dry-run", action="store_true", help="list mutants; run nothing")
    parser.add_argument("--target", action="append", default=[], help="override target(s) for every mutant")
    parser.add_argument("--ctest-regex", default=None, help="unused placeholder for a future ctest -R path")
    parser.add_argument("--out", default=None, help="write the compact table to this file too")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    # argparse's own --help exits before this point and runs nothing else --
    # see the module docstring's WHY and memory
    # a-gen-script-runs-the-moment-you-invoke-it.md for why that matters.

    diff = subprocess.run(
        ["git", "diff", "-U0", f"{args.base}...HEAD"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=REPO_ROOT,
        text=True,
        check=True,
    )
    added = parse_added_lines(diff.stdout)

    watched_prefixes = tuple(p.rstrip("/") + "/" for p in args.paths)
    candidates = [
        a
        for a in added
        if a.path.startswith(watched_prefixes) and Path(a.path).suffix in MUTABLE_EXTENSIONS
    ]

    app_src_map = build_app_src_target_map()
    mutants: list[AddedLine] = []
    for a in candidates:
        full_path = REPO_ROOT / a.path
        if not full_path.is_file():
            continue
        lines = full_path.read_text(encoding="utf-8").splitlines()
        if a.lineno < 1 or a.lineno > len(lines):
            continue
        if is_mutable_line(lines[a.lineno - 1]):
            mutants.append(a)

    if args.max is not None:
        mutants = mutants[: args.max]

    if args.dry_run:
        for m in mutants:
            print(f"{m.path}:{m.lineno}: {m.text}")
        print(f"\n{len(mutants)} mutant(s) (dry run, nothing built)")
        return 0

    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        build_dir = REPO_ROOT / build_dir
    log_path = Path(args.out).with_suffix(".log") if args.out else REPO_ROOT / "diffmut.log"

    results: list[MutantResult] = []
    for m in mutants:
        targets = args.target if args.target else test_targets_for(m.path, app_src_map)
        result = run_mutant(REPO_ROOT / m.path, m.lineno, targets, build_dir, args.config, log_path)
        results.append(result)
        print(f"{result.verdict:10s} {result.path}:{result.lineno}  ({result.target})")

    # Sentinel: verify no mutated file is left dirty. This should always be
    # true given run_mutant's own byte-identity check in `finally`, but a
    # second, independent check here (via git, not this script's own
    # bookkeeping) is what makes "restored" a measured fact rather than an
    # assumption baked into one function.
    mutated_paths = sorted({m.path for m in mutants})
    if mutated_paths:
        status = subprocess.run(
            ["git", "diff", "--quiet", "--"] + mutated_paths, cwd=REPO_ROOT
        )
        if status.returncode != 0:
            print(
                "FATAL: git diff is not clean for mutated files after restore -- "
                "this is diffmut.py's own bug, not the mutation's:",
                file=sys.stderr,
            )
            subprocess.run(["git", "diff", "--stat", "--"] + mutated_paths, cwd=REPO_ROOT)
            return 2

    counts = {"KILLED": 0, "SURVIVED": 0, "NO-BUILD": 0, "UNRESOLVED": 0}
    for r in results:
        counts[r.verdict] = counts.get(r.verdict, 0) + 1

    lines_out = [f"{r.verdict:10s} {r.path}:{r.lineno}  ({r.target})  {r.detail}" for r in results]
    lines_out.append("")
    lines_out.append(
        f"TOTAL {len(results)}  KILLED {counts['KILLED']}  SURVIVED {counts['SURVIVED']}  "
        f"NO-BUILD {counts['NO-BUILD']}  UNRESOLVED {counts['UNRESOLVED']}"
    )
    table = "\n".join(lines_out)
    print("\n" + table)
    if args.out:
        Path(args.out).write_text(table + "\n", encoding="utf-8")

    return 1 if counts["SURVIVED"] > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
