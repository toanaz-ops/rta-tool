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

This is the run-mutate-restore driver and CLI; three sibling modules hold the
pieces the task's own review asked to split out along real seams (process-
tooling PR, fix round 1, item L3 -- this file alone was 535 lines against the
project's 400-line cap):
  - difflines.py -- parses `git diff -U0` into added lines. Shared with
    orphan_check.py, so the parse cannot drift between the two tools.
  - mutation_lines.py -- classifies which added lines are worth mutating
    (see its own docstring for the exclusion rules and the L5 bug they fix).
  - mutation_targets.py -- maps a mutated path to the ctest target that
    covers it (core/, platform/, ui/ by convention; app/src/* by parsing the
    real CMake source lists).
  - mutation_sidecar.py -- crash recovery for a hard-killed run (see item M3
    below and that module's own docstring).

HOW A MUTANT IS APPLIED, TESTED AND UNDONE. For each mutant:
  1. The file's exact original bytes are read into memory AND written to a
     sidecar file (mutation_sidecar.py) BEFORE anything is mutated. A
     `finally` block restores the file from memory and deletes the sidecar on
     every ordinary exit path, but a hard kill of this process during the
     build or test-run subprocess skips `finally` entirely -- see
     mutation_sidecar.py's docstring for the recovery path that leaves.
     `main()` refuses to start a new run while any sidecar exists.
  2. The chosen line is deleted (not merely commented out -- this is
     "delete-line" mutation, named for what it does).
  3. The test executable(s) for the mapped target are deleted, before the
     build runs (memory `mutation-testing-needs-the-exe-deleted-first.md` --
     cmake --build can print "-> X.exe" without relinking a stale binary,
     which would silently PASS every mutant).
  4. For a header mutation, a sibling .cpp in the same directory is touched
     so an incremental build is forced to recompile something that includes
     it.
  5. `cmake --build <dir> --config <cfg> --target <target>` runs. A non-zero
     exit is NO-BUILD -- the mutation broke compilation, which the compiler
     itself caught; reported separately from a real test verdict.
  6. On a successful build the target's exe is invoked DIRECTLY (never via
     `cmd //c`) and its exit code is the verdict: non-zero (a Catch2
     assertion failed) is KILLED, zero is SURVIVED.
  7. The original bytes are restored in `finally` REGARDLESS of what happened
     above, verified by sha256, and the sidecar removed only once that
     verification passes. This script never uses `git checkout`/`git stash`
     to undo a mutation (memory
     `a-verifier-with-bash-can-git-checkout-your-uncommitted-fix.md`).

At the end (fix round 1 item L2), every DISTINCT target that was actually
built during the run gets one final, ordinary rebuild against the restored
(unmutated) source, so the exe left on disk is not the last mutant's --
before this, a run that ended on a KILLED or NO-BUILD mutant left a broken or
mutated binary sitting in the build tree for whatever ran next to trip over.
Every mutated file is then checked with `git diff --quiet` -- if the
restore-and-verify above worked, this reports clean; if it does not, that is
this script's own bug, not the mutation's.

Exit code: 1 if any mutant SURVIVED (so this can gate a pipeline), else 0.
NO-BUILD mutants do not affect the exit code -- they say something about the
mutation, not about test coverage.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from difflines import git_diff_added_lines
from mutation_lines import MUTABLE_EXTENSIONS, is_mutable_line
from mutation_sidecar import find_stale_sidecars, restore_from_sidecars, sidecar_path
from mutation_targets import build_app_src_target_map, test_targets_for

REPO_ROOT = Path(__file__).resolve().parent.parent


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


def _run_build(build_dir: Path, config: str, target: str, log) -> int:
    return subprocess.run(
        ["cmake", "--build", str(build_dir), "--config", config, "--target", target],
        stdout=log,
        stderr=subprocess.STDOUT,
        cwd=REPO_ROOT,
    ).returncode


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
    sidecar = sidecar_path(repo_path)
    sidecar.write_bytes(original_bytes)  # fix round 1 M3: written BEFORE mutating
    try:
        text = original_bytes.decode("utf-8")
        lines = text.splitlines(keepends=True)
        if lineno < 1 or lineno > len(lines):
            return MutantResult(rel, lineno, "UNRESOLVED", target, detail="line out of range")
        deleted = lines.pop(lineno - 1)
        repo_path.write_bytes("".join(lines).encode("utf-8"))

        exe = find_exe(build_dir, config, target)
        if exe and exe.exists():
            exe.unlink()

        if repo_path.suffix in (".h", ".hpp"):
            touch_sibling_cpp(repo_path)

        with log_path.open("a", encoding="utf-8") as log:
            log.write(f"\n=== mutant {rel}:{lineno}: {deleted!r} (target {target}) ===\n")
            if _run_build(build_dir, config, target, log) != 0:
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
                f"(sha256 {restored_sha} != {original_sha}) -- the sidecar at "
                f"{sidecar} is left in place; run --restore before trusting "
                f"anything else this run reported"
            )
        sidecar.unlink()  # only once the restore is verified


# --- CLI ----------------------------------------------------------------


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="diffmut.py",
        description="Differential delete-line mutation testing over lines this branch added.",
    )
    parser.add_argument("--base", default="origin/main", help="diff base (default: origin/main)")
    parser.add_argument("--build-dir", help="CMake build directory to build in")
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
    parser.add_argument("--out", default=None, help="write the compact table to this file too")
    parser.add_argument(
        "--restore",
        action="store_true",
        help="restore any file(s) left mutated by a hard-killed previous run, then exit",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    # argparse's own --help exits before this point and runs nothing else --
    # see the module docstring's WHY and memory
    # a-gen-script-runs-the-moment-you-invoke-it.md for why that matters.

    if args.restore:
        restored = restore_from_sidecars(REPO_ROOT)
        if not restored:
            print("diffmut --restore: no stale sidecar found, nothing to do")
            return 0
        print(f"diffmut --restore: restored {len(restored)} file(s):")
        for path in restored:
            print(f"  {path}")
        return 0

    stale = find_stale_sidecars(REPO_ROOT)
    if stale:
        print(
            "diffmut: refusing to start -- a previous run left "
            f"{len(stale)} unrestored sidecar(s) (it was likely killed mid-mutant):",
            file=sys.stderr,
        )
        for s in stale:
            print(f"  {s.relative_to(REPO_ROOT).as_posix()}", file=sys.stderr)
        print("Run `python tools/diffmut.py --restore` first.", file=sys.stderr)
        return 2

    if not args.build_dir and not args.dry_run:
        parser.error("--build-dir is required unless --dry-run or --restore is given")

    added = git_diff_added_lines(REPO_ROOT, args.base, "HEAD")

    watched_prefixes = tuple(p.rstrip("/") + "/" for p in args.paths)
    candidates = [
        a
        for a in added
        if a.path.startswith(watched_prefixes) and Path(a.path).suffix in MUTABLE_EXTENSIONS
    ]

    app_src_map = build_app_src_target_map()
    mutants = []
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
    touched_targets: set[str] = set()
    for m in mutants:
        targets = args.target if args.target else test_targets_for(m.path, app_src_map)
        result = run_mutant(REPO_ROOT / m.path, m.lineno, targets, build_dir, args.config, log_path)
        results.append(result)
        if result.target != "<none>":
            touched_targets.add(result.target)
        print(f"{result.verdict:10s} {result.path}:{result.lineno}  ({result.target})")

    # fix round 1, L2: leave the build tree holding a normal (unmutated)
    # binary for every target this run touched -- otherwise the last
    # mutant's KILLED/NO-BUILD state (a broken or behaviour-altered exe) sits
    # in the build directory for whatever runs next.
    if touched_targets:
        with log_path.open("a", encoding="utf-8") as log:
            log.write("\n=== final rebuild of touched targets (restore exe to HEAD) ===\n")
            for target in sorted(touched_targets):
                _run_build(build_dir, args.config, target, log)

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
