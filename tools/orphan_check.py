#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""orphan_check.py -- no component without a production caller, decided by
the LINKER, not by grep.

WHY V2. v1 (see git history: process/tooling PR #38) answered "is this
symbol referenced" with a text-grep reference search, and after two rounds of
patching it still had four MEDIUM defects, all from one root: a regex has no
notion of REACHABILITY, only of a spelling appearing somewhere in a file.
It flagged calls it should have credited (a qualified static call, a
same-file private helper called from a sibling method) and credited
references it should have discarded (a doc comment merely naming a symbol, a
mutual reference cycle between two components neither one anything else
calls). Patching each hole traded it for another; see that PR's fix-round-2
body for the specific v1 false positives and negatives this replaces.

v2 asks a stronger, structurally different question: is this function
present in the FINAL LINKED IMAGE, once the linker has discarded everything
transitively unreachable from the app's own entry point? `app/CMakeLists.txt`
builds `rtatool` under `RTA_ORPHAN_LINKMAP=ON` with:
  /Ob0          no inlining -- a candidate must survive as its own
                out-of-line, separately addressable symbol.
  /Gy           function-level linking (COMDATs) -- what /OPT:REF discards.
  /OPT:REF      discard COMDATs nothing references, transitively from main.
  /OPT:NOICF    keep two byte-identical functions as two SEPARATE symbols in
                the map (ICF's default folding would erase one's name).
  /MAP          emit the map this tool reads.
This makes a cycle of two unwired components discard TOGETHER (neither is
"referenced" by anything the entry point reaches, so neither survives, unlike
v1's reference-counting which kept each alive via the other); a qualified
static call resolves exactly like any other reachable call (the linker does
not care how a caller spelled the callee); and a same-file private helper is
live the moment its caller is.

WHAT IS CHECKED. Every function, method and constructor DECLARED on a line
this branch ADDED under app/src/** (headers and .cpp -- see
orphan_candidates.py for exactly how "declared" is read, including a
signature that wraps across lines). A candidate whose own decorated-name
FRAGMENT (msvc_decorate.py) is not a prefix of any name in the map is an
orphan; a class is live iff at least one of its own members is. Some
candidates cannot be asked this question at all -- `= default`/`= delete`
members, `constexpr`/`consteval` functions, anonymous-namespace symbols
(internal linkage answers a different question), templates (instantiation
is not tracked), and a pure-data aggregate with no function to check
reachability through -- these are UNCHECKABLE, reported separately, and
never cause a non-zero exit by themselves.

EXIT CODES. 0 nothing orphaned. 1 orphan(s) found (UNCHECKABLE entries alone
never trigger this). 2 a tool or build error (bad --base, configure/build
failure, no .map file produced).

TWO LIVENESS CAVEATS (fix round 1, verifier, F7) -- both make the linker say
LIVE for a function that a human would call dead, never the other direction,
so they can only hide a real orphan, never invent a false one:
  - A never-called VIRTUAL OVERRIDE of an INSTANTIATED class reads live
    through the vtable. The vtable itself is a reference the linker sees (it
    is data, emitted whenever the class is instantiated), and every override
    slot in it is a target that reference keeps alive -- whether or not
    anything ever actually DISPATCHES to that particular override at
    runtime. This tool cannot distinguish "reachable because something calls
    it" from "reachable because it merely occupies a vtable slot".
  - The compiler can delete a call to a side-effect-free callee whose result
    is discarded (`someQuery();` with the return value unused, where the
    optimiser has proven `someQuery` has no observable side effects). If
    that was the candidate's ONLY call site, /OPT:REF then discards the
    candidate too -- a FALSE ORPHAN report for code that is, in the source,
    genuinely called.
Both are documented limitations of asking the OPTIMISED BINARY the question,
not bugs in this tool's own fragment matching -- see app/CMakeLists.txt's own
comment on the RTA_ORPHAN_LINKMAP option for the build-flag side of this.

NOT IN TARGET (fix round 1, verifier, F8; corrected in fix round 2, F-A). A
file this tool would otherwise scan is not necessarily compiled into
`rtatool` at all -- `app/src/dev/preview/**` is an explicit per-directory
exclusion (its own separate preview tooling, compiled only into
`rtatool_snapshot`; see app/cmake/rtatool_sources.cmake vs.
rtatool_snapshot_sources.cmake -- NOTE this is `dev/preview/`, not all of
`dev/`: `src/dev/SpecimenComponent.cpp` is genuinely in BOTH targets). A
`.cpp` genuinely compiled into `rtatool_snapshot` but absent from
`rtatool_sources.cmake` is ALSO reported NOT IN TARGET (a third,
non-failing category alongside orphan and UNCHECKABLE) -- but a `.cpp`
absent from BOTH source lists is NOT in this category: it was never
compiled into ANY target, which is the L6a defect this whole tool exists to
catch (SplHistory.cpp/SplAlarms.cpp had no caller and so were never added to
a source list either, at the commit before their wiring landed), so it is
reported as an ordinary, FAILING orphan with reason "not compiled into any
target" -- see orphan_targets.not_in_target_reason and
orphan_targets.not_in_any_target_reason for the exact checks. Fix round 1's
first cut treated any unlisted `.cpp` as non-failing NOT IN TARGET, which
silently defeated exactly the case this paragraph now calls out; the
verifier's `cpp_only_unlisted_false_clean` fixture is the regression test.
A checkout older than 2026-09-26 has no rtatool_sources.cmake to read at all
(it predates that file's split from app/CMakeLists.txt); both checks are
then skipped for that run (candidates fall through to the ordinary liveness
check instead), not treated as "nothing is in the target".

TEST HOOK (fix round 1, verifier). A candidate whose name ends in `ForTest`
is never orphaned outright the way an ordinary candidate is: if it is
otherwise unreachable from `rtatool`'s entry point AND this run finds at
least one comment-and-string-literal-stripped reference to that name under
`app/tests*`, it is reported as its own TEST HOOK category (non-failing)
instead of an orphan -- the fact that only a test calls it is expected, not
a defect, for a name whose whole purpose is to be a seam for a test. There
is no free-text allow-list: a `*ForTest` name this run cannot find any test
REFERENCING (fix round 2, F-I: a bare mention inside a string literal no
longer counts) is left as an ordinary, failing orphan. See
orphan_targets.test_hook_is_referenced.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from difflines import git_diff_added_lines
from map_symbols import extract_decorated_names, is_fragment_live
from msvc_decorate import decorated_fragment
from orphan_candidates import find_candidates
from orphan_targets import (
    TEST_HOOK_NAME_RE,
    not_in_any_target_reason,
    not_in_target_reason,
    rtatool_snapshot_sources,
    rtatool_target_sources,
    test_hook_is_referenced,
)

REPO_ROOT = Path(__file__).resolve().parent.parent


def _configure_and_build(
    source_dir: Path,
    build_dir: Path,
    config: str,
    generator: str | None,
    arch: str | None,
    juce_path: str | None,
) -> int:
    configure_cmd = [
        "cmake", "-S", str(source_dir), "-B", str(build_dir),
        "-DRTA_BUILD_APP=ON", "-DRTA_ORPHAN_LINKMAP=ON",
    ]
    if generator:
        configure_cmd += ["-G", generator]
    if arch:
        configure_cmd += ["-A", arch]
    if juce_path:
        configure_cmd.append(f"-DRTA_JUCE_PATH={juce_path}")
    result = subprocess.run(configure_cmd)
    if result.returncode != 0:
        return result.returncode
    build_cmd = [
        "cmake", "--build", str(build_dir), "--config", config,
        "--target", "rtatool", "--parallel",
    ]
    return subprocess.run(build_cmd).returncode


def _find_map_file(build_dir: Path, config: str) -> Path | None:
    matches = sorted(build_dir.glob(f"app/*_artefacts/{config}/*.map"))
    return matches[0] if matches else None


def _read_file_factory(source_dir: Path):
    def read(path: str) -> str:
        return (source_dir / path).read_text(encoding="utf-8")

    return read


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Flag app/src functions/methods/constructors, declared on lines this "
            "branch added, that the MSVC linker discards as unreachable from "
            "rtatool's entry point."
        )
    )
    parser.add_argument("--base", required=True, help="diff base")
    parser.add_argument(
        "--build-dir", required=True,
        help="CMake build directory for the RTA_ORPHAN_LINKMAP=ON build of rtatool",
    )
    parser.add_argument(
        "--source-dir", default=None,
        help="checked-out tree to diff/build from (default: this repo's own checkout). "
        "For a historical commit, `git worktree add <path> <sha>` it yourself and "
        "pass that path here; remove the worktree afterward.",
    )
    parser.add_argument("--config", default="Release")
    parser.add_argument(
        "--cmake-generator", default=None,
        help='e.g. "Visual Studio 18 2026" -- omit to let CMake auto-detect the way '
        "CI does; pass this explicitly on a machine where a non-MSVC generator "
        "(Ninja+MinGW) would otherwise be picked by default (see "
        "memory/a-misconfigured-build-goes-99-percent-of-the-way.md).",
    )
    parser.add_argument(
        "--cmake-arch", default=None, help="e.g. x64, passed as -A alongside --cmake-generator"
    )
    parser.add_argument(
        "--juce-path", default=None,
        help="existing JUCE 9.0.1 checkout to reuse (RTA_JUCE_PATH); omit to fetch",
    )
    parser.add_argument(
        "--skip-build", action="store_true",
        help="reuse an already-built .map in --build-dir instead of configuring/building "
        "(for testing this tool's own orchestration; never for a real acceptance run)",
    )
    args = parser.parse_args(argv)

    source_dir = Path(args.source_dir).resolve() if args.source_dir else REPO_ROOT
    build_dir = Path(args.build_dir)

    if not args.skip_build:
        rc = _configure_and_build(
            source_dir, build_dir, args.config, args.cmake_generator, args.cmake_arch, args.juce_path
        )
        if rc != 0:
            print(f"orphan_check: configure/build FAILED (exit {rc})", file=sys.stderr)
            return 2

    map_path = _find_map_file(build_dir, args.config)
    if map_path is None:
        print(
            f"orphan_check: no .map file found under {build_dir} -- "
            "did RTA_ORPHAN_LINKMAP=ON actually take effect for this build?",
            file=sys.stderr,
        )
        return 2

    try:
        added_lines = git_diff_added_lines(source_dir, args.base, "HEAD", paths=["app/src"])
    except subprocess.CalledProcessError as exc:
        print(f"orphan_check: git diff failed against base {args.base!r}: {exc}", file=sys.stderr)
        return 2

    decorated_names = extract_decorated_names(map_path.read_text(encoding="utf-8", errors="replace"))
    candidates, uncheckable = find_candidates(added_lines, _read_file_factory(source_dir))
    target_sources = rtatool_target_sources(source_dir)
    snapshot_sources = rtatool_snapshot_sources(source_dir)

    # fix round 1 (verifier), F8: a candidate outside the rtatool target is
    # never asked the liveness question at all -- reported separately below,
    # never as an orphan. Applied to UNCHECKABLE entries too, so a candidate
    # under app/src/dev/preview/ that also happens to be e.g. `= delete`
    # reads as NOT IN TARGET rather than as two different, both-technically-
    # true classifications.
    #
    # fix round 2 (verifier), F-A: a `.cpp` compiled into NEITHER target is
    # NOT a legitimate NOT IN TARGET -- it is an ordinary, failing orphan
    # with its own explicit reason, checked and appended BEFORE the
    # not-in-target partition below so it can never be shadowed by it.
    not_in_target: dict[str, list[str]] = {}
    orphans: dict[str, list[str]] = {}
    in_target_candidates = []
    for c in candidates:
        never_compiled = not_in_any_target_reason(c.path, target_sources, snapshot_sources)
        if never_compiled:
            orphans.setdefault(c.path, []).append(f"{c.name}  ({never_compiled})")
            continue
        reason = not_in_target_reason(c.path, target_sources, snapshot_sources)
        if reason:
            not_in_target.setdefault(c.path, []).append(f"{c.name}  ({reason})")
        else:
            in_target_candidates.append(c)

    in_target_uncheckable = []
    for u in uncheckable:
        reason = u.path and not_in_target_reason(u.path, target_sources, snapshot_sources)
        if reason:
            not_in_target.setdefault(u.path, []).append(f"{u.name}  ({reason})")
        else:
            in_target_uncheckable.append(u)
    uncheckable = in_target_uncheckable

    # Test hooks: an otherwise-unreachable `*ForTest` candidate is proven
    # live by a real reference under app/tests* on THIS run, not by its name
    # alone -- see orphan_targets.test_hook_is_referenced.
    test_hooks: dict[str, list[str]] = {}
    for c in in_target_candidates:
        fragment = decorated_fragment(c.name, c.enclosing, is_constructor=c.is_constructor)
        if is_fragment_live(fragment, decorated_names):
            continue
        if TEST_HOOK_NAME_RE.search(c.name) and test_hook_is_referenced(c.name, source_dir):
            test_hooks.setdefault(c.path, []).append(c.name)
        else:
            orphans.setdefault(c.path, []).append(c.name)

    if not_in_target:
        print("orphan_check: NOT IN TARGET (never fails; not asked the linker at all):")
        for path in sorted(not_in_target):
            print(f"  {path}")
            for entry in sorted(set(not_in_target[path])):
                print(f"    - {entry}")

    if uncheckable:
        print("orphan_check: UNCHECKABLE (never fails; a human call, not the linker's):")
        for u in sorted(uncheckable, key=lambda u: (u.path, u.lineno)):
            where = f"{u.path}:{u.lineno}" if u.path else f"line {u.lineno}"
            print(f"  {where}  {u.name}  ({u.reason})")

    if test_hooks:
        print("orphan_check: TEST HOOK (never fails; referenced under app/tests*):")
        for path in sorted(test_hooks):
            print(f"  {path}")
            for name in sorted(set(test_hooks[path])):
                print(f"    - {name}")

    if not orphans:
        print(
            "orphan_check: OK -- every new/changed function, method and constructor "
            "is reachable from rtatool's entry point"
        )
        return 0

    print("orphan_check: found component(s) the linker discarded as unreachable:")
    for path in sorted(orphans):
        print(f"  {path}")
        for name in sorted(set(orphans[path])):
            print(f"    - {name}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
